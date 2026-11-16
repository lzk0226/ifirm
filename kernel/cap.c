#include <ifirm/types.h>
#include <ifirm/cap.h>
#include <ifirm/domain.h>
#include <ifirm/ict.h>
#include <ifirm/console.h>

static u32 g_gen[MAX_DOMAINS];

void cap_init(void)
{
    for (u32 i = 0; i < MAX_DOMAINS; i++) { g_gen[i] = 1u; }
}

/* 检查流水线，顺序冻结（§13.5） */
ifirm_status_t cap_check(cap_t h, u8 type, u16 rights, cap_entry_t **out)
{
    domain_t *d = g_current_domain;
    if (d == NULL) { return IFIRM_ERR_BAD_HANDLE; }

    u32 idx = (u32)h & CAP_IDX_MASK;
    if (idx == 0u || idx >= 256u) { return IFIRM_ERR_BAD_HANDLE; }

    cap_entry_t *e = &d->caps[idx];
    /* 代次先比：撤销会顶掉代次，旧句柄必须报 REVOKED 而不是"坏句柄"（§13.5） */
    if (((e->gen_owner >> 12) & 0xFFFFFu) != ((((u32)h) & CAP_GEN_MASK) >> 12)) {
        return IFIRM_ERR_REVOKED;
    }
    if ((e->flags & CAP_FLAG_VALID) == 0u) { return IFIRM_ERR_BAD_HANDLE; }
    if ((e->gen_owner & 0xFFFu) != (u32)d->id) { return IFIRM_ERR_BAD_HANDLE; }
    if (type != OBJ_ANY && e->object_type != type) { return IFIRM_ERR_BAD_HANDLE; }
    if ((rights & ~CAP_RIGHTS_ALL) != 0u) { return IFIRM_ERR_DENIED; }
    if ((e->rights & rights) != rights) { return IFIRM_ERR_DENIED; }
    if (out != NULL) { *out = e; }
    return IFIRM_OK;
}

static ifirm_status_t grant_at(u16 dom_id, u8 type, u32 object_id, u16 rights,
                               u32 derived_from, cap_t *out)
{
    domain_t *d = domain_get(dom_id);
    if (d == NULL || d->state == DOMAIN_FREE) { return IFIRM_ERR_INVALID_ARG; }
    if ((rights & ~CAP_RIGHTS_ALL) != 0u || rights == 0u) { return IFIRM_ERR_DENIED; }

    for (u32 idx = 1; idx < 256u; idx++) {
        if ((d->caps[idx].flags & CAP_FLAG_VALID) != 0u) { continue; }
        u32 gen = g_gen[dom_id]++;
        d->caps[idx].object_id = object_id;
        d->caps[idx].gen_owner = (gen << 12) | (u32)dom_id;
        d->caps[idx].rights = rights;
        d->caps[idx].object_type = type;
        d->caps[idx].flags = CAP_FLAG_VALID | CAP_FLAG_DELEGABLE;
        d->caps[idx].derived_from = derived_from;
        d->cap_used++;
        if (out != NULL) { *out = cap_make((u16)idx, gen); }
        return IFIRM_OK;
    }
    return IFIRM_ERR_NO_SPACE;
}

ifirm_status_t cap_grant(u16 dom_id, u8 type, u32 object_id, u16 rights, cap_t *out)
{
    return grant_at(dom_id, type, object_id, rights, 0u, out);
}

static u32 derive_depth(u16 dom_id, u16 idx, u32 depth)
{
    domain_t *d = domain_get(dom_id);
    if (d == NULL || depth > CAP_DERIVE_DEPTH) { return depth; }
    u32 tag = d->caps[idx].derived_from;
    if (tag == 0u) { return depth; }
    return derive_depth((u16)(tag >> 16), (u16)(tag & 0xFFFFu), depth + 1u);
}

ifirm_status_t cap_delegate(u16 src_dom, u16 src_idx, u16 dst_dom, u16 rights_mask, cap_t *out)
{
    domain_t *s = domain_get(src_dom);
    if (s == NULL || src_idx == 0u || src_idx >= 256u) { return IFIRM_ERR_INVALID_ARG; }
    cap_entry_t *e = &s->caps[src_idx];
    if ((e->flags & CAP_FLAG_VALID) == 0u) { return IFIRM_ERR_BAD_HANDLE; }
    if ((e->rights & CAP_RIGHT_DELEGATE) == 0u) { return IFIRM_ERR_DENIED; }
    if ((e->flags & CAP_FLAG_DELEGABLE) == 0u) { return IFIRM_ERR_DENIED; }
    if (derive_depth(src_dom, src_idx, 0u) >= CAP_DERIVE_DEPTH) { return IFIRM_ERR_NO_SPACE; }

    u16 rights = (u16)(e->rights & rights_mask & ~CAP_RIGHT_DELEGATE);
    if (rights == 0u) { return IFIRM_ERR_INVALID_ARG; }
    return grant_at(dst_dom, e->object_type, e->object_id, rights,
                    ((u32)src_dom << 16) | (u32)src_idx, out);
}

ifirm_status_t cap_revoke(u16 dom_id, u16 idx)
{
    domain_t *d = domain_get(dom_id);
    if (d == NULL || idx == 0u || idx >= 256u) { return IFIRM_ERR_INVALID_ARG; }
    cap_entry_t *e = &d->caps[idx];
    if ((e->flags & CAP_FLAG_VALID) == 0u) { return IFIRM_ERR_BAD_HANDLE; }
    e->flags = 0u;
    e->derived_from = 0u;
    e->gen_owner = (g_gen[dom_id]++ << 12) | (u32)dom_id;
    if (d->cap_used > 0u) { d->cap_used--; }
    return IFIRM_OK;
}

/* 撤销 + 沿派生链反向传播（§14.5 的反向引用表的极简形态：线性扫描 16×256） */
static u32 revoke_walk(u16 dom_id, u16 idx, u32 depth)
{
    if (cap_revoke(dom_id, idx) != IFIRM_OK) { return 0u; }
    u32 n = 1u;
    (void)ict_revoke_cap((u8)dom_id, idx);       /* 引用该句柄的 ICT 槽同步失效 */
    if (depth >= CAP_DERIVE_DEPTH) { return n; }

    u32 tag = ((u32)dom_id << 16) | (u32)idx;
    for (u32 dd = 0; dd < MAX_DOMAINS; dd++) {
        domain_t *t = &g_domains[dd];
        for (u32 i = 1u; i < 256u; i++) {
            if ((t->caps[i].flags & CAP_FLAG_VALID) == 0u) { continue; }
            if (t->caps[i].derived_from != tag) { continue; }
            n += revoke_walk((u16)dd, (u16)i, depth + 1u);
        }
    }
    return n;
}

u32 cap_revoke_tree(u16 dom_id, u16 idx) { return revoke_walk(dom_id, idx, 0u); }

u16 cap_count(u16 dom_id)
{
    domain_t *d = domain_get(dom_id);
    return (d != NULL) ? d->cap_used : 0u;
}

u32 cap_total(void)
{
    u32 n = 0;
    for (u32 i = 0; i < MAX_DOMAINS; i++) { n += g_domains[i].cap_used; }
    return n;
}
