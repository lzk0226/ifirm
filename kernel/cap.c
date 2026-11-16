#include <ifirm/types.h>
#include <ifirm/cap.h>
#include <ifirm/domain.h>
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
    if ((e->flags & CAP_FLAG_VALID) == 0u) { return IFIRM_ERR_BAD_HANDLE; }
    if ((e->gen_owner & 0xFFFu) != (u32)d->id) { return IFIRM_ERR_BAD_HANDLE; }
    if (type != OBJ_ANY && e->object_type != type) { return IFIRM_ERR_BAD_HANDLE; }
    if ((rights & ~CAP_RIGHTS_ALL) != 0u) { return IFIRM_ERR_DENIED; }
    if ((e->rights & rights) != rights) { return IFIRM_ERR_DENIED; }
    if (((e->gen_owner >> 12) & 0xFFFFFu) != ((((u32)h) & CAP_GEN_MASK) >> 12)) {
        return IFIRM_ERR_REVOKED;
    }
    if (out != NULL) { *out = e; }
    return IFIRM_OK;
}

ifirm_status_t cap_grant(u16 dom_id, u8 type, u32 object_id, u16 rights, cap_t *out)
{
    domain_t *d = domain_get(dom_id);
    if (d == NULL || d->state == DOMAIN_FREE) { return IFIRM_ERR_INVALID_ARG; }
    if ((rights & ~CAP_RIGHTS_ALL) != 0u) { return IFIRM_ERR_DENIED; }

    for (u32 idx = 1; idx < 256u; idx++) {
        if ((d->caps[idx].flags & CAP_FLAG_VALID) != 0u) { continue; }
        u32 gen = g_gen[dom_id]++;
        d->caps[idx].object_id = object_id;
        d->caps[idx].gen_owner = (gen << 12) | (u32)dom_id;
        d->caps[idx].rights = rights;
        d->caps[idx].object_type = type;
        d->caps[idx].flags = CAP_FLAG_VALID | CAP_FLAG_DELEGABLE;
        d->caps[idx].derived_from = 0u;
        d->cap_used++;
        if (out != NULL) { *out = cap_make((u16)idx, gen); }
        return IFIRM_OK;
    }
    return IFIRM_ERR_NO_SPACE;
}

ifirm_status_t cap_revoke(u16 dom_id, u16 idx)
{
    domain_t *d = domain_get(dom_id);
    if (d == NULL || idx == 0u || idx >= 256u) { return IFIRM_ERR_INVALID_ARG; }
    cap_entry_t *e = &d->caps[idx];
    if ((e->flags & CAP_FLAG_VALID) == 0u) { return IFIRM_ERR_BAD_HANDLE; }
    e->flags = 0u;
    e->gen_owner = (g_gen[dom_id]++ << 12) | (u32)dom_id;
    d->cap_used--;
    return IFIRM_OK;
}

u16 cap_count(u16 dom_id)
{
    domain_t *d = domain_get(dom_id);
    return (d != NULL) ? d->cap_used : 0u;
}
