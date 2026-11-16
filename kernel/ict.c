#include <ifirm/types.h>
#include <ifirm/ict.h>
#include <ifirm/cap.h>
#include <ifirm/domain.h>
#include <ifirm/thread.h>
#include <ifirm/syscall.h>
#include <ifirm/console.h>
#include <ifirm/status.h>

/* function_id → 所需权限（§21.5 表） */
static u16 rights_for_function(u32 fid)
{
    switch (fid) {
    case 1u: return CAP_RIGHT_SEND;
    case 2u: return CAP_RIGHT_RECV;
    case 3u: return CAP_RIGHT_CALL;
    case 4u: return CAP_RIGHT_REPLY;
    case 7u: case 9u: return CAP_RIGHT_READ;
    case 8u: case 10u: return CAP_RIGHT_WRITE;
    default: return 0u;
    }
}

/* 影子表不进用户可见页：槽的 16 字节布局保持冻结 */
static u16 g_slot_cap[MAX_DOMAINS][ICT_MAX_ENTRIES];
static u8  g_slot_dead[MAX_DOMAINS][ICT_MAX_ENTRIES];

ifirm_status_t ict_install(u8 dom, u32 function_id, u16 cap_slot, u16 flags, u32 arg_limit)
{
    domain_t *d = domain_get(dom);
    if (d == NULL || function_id == 0u) { return IFIRM_ERR_INVALID_ARG; }

    u32 slot = ICT_MAX_ENTRIES;
    for (u32 i = 0; i < ICT_MAX_ENTRIES; i++) {
        if (d->ict[i].function_id == 0u) { slot = i; break; }
    }
    if (slot == ICT_MAX_ENTRIES) { return IFIRM_ERR_NO_SPACE; }

    if ((flags & ICT_F_NEEDS_CAP) != 0u) {
        if (cap_slot == 0u || cap_slot >= 256u) { return IFIRM_ERR_BAD_HANDLE; }
        cap_entry_t *ce = &d->caps[cap_slot];
        if ((ce->flags & CAP_FLAG_VALID) == 0u) { return IFIRM_ERR_BAD_HANDLE; }
        if (!rights_for_function(function_id)) { return IFIRM_ERR_DENIED; }
        if ((ce->rights & rights_for_function(function_id)) != rights_for_function(function_id)) {
            return IFIRM_ERR_DENIED;
        }
    }

    /* 写入顺序：function_id 最后写，读者只会看到完整槽（§21.5） */
    d->ict[slot].target_off = slot * 16u;
    d->ict[slot].flags = flags;
    d->ict[slot].cap_slot = cap_slot;
    d->ict[slot].arg_limit = arg_limit;
    g_slot_cap[dom][slot] = ((flags & ICT_F_NEEDS_CAP) != 0u) ? cap_slot : 0u;
    g_slot_dead[dom][slot] = 0u;
    d->ict[slot].function_id = function_id;
    return IFIRM_OK;
}

u32 ict_revoke_cap(u8 dom, u16 cap_slot)
{
    u32 n = 0;
    if (dom >= MAX_DOMAINS || cap_slot == 0u) { return 0u; }
    for (u32 i = 0; i < ICT_MAX_ENTRIES; i++) {
        if (g_slot_cap[dom][i] != cap_slot) { continue; }
        if (g_slot_dead[dom][i] != 0u) { continue; }
        g_slot_dead[dom][i] = 1u;
        n++;
    }
    return n;
}

u32 ict_count(u8 dom)
{
    domain_t *d = domain_get(dom);
    u32 n = 0;
    if (d == NULL) { return 0u; }
    for (u32 i = 0; i < ICT_MAX_ENTRIES; i++) {
        if (d->ict[i].function_id != 0u) { n++; }
    }
    return n;
}

i64 ict_dispatch(trap_frame_t *f, u32 slot)
{
    domain_t *d = g_current_domain;
    if (d == NULL) { return (i64)IFIRM_ERR_BAD_HANDLE; }
    if (slot >= ICT_MAX_ENTRIES) { return (i64)IFIRM_ERR_BAD_HANDLE; }

    ict_entry_t *e = &d->ict[slot];
    if (e->function_id == 0u) { return (i64)IFIRM_ERR_NOT_FOUND; }   /* 空槽：安装期检查的运行时回声 */

    if ((e->flags & ICT_F_NEEDS_CAP) != 0u) {
        if (e->cap_slot == 0u || e->cap_slot >= 256u) { return (i64)IFIRM_ERR_BAD_HANDLE; }
        if (g_slot_dead[d->id][slot] != 0u) { return (i64)IFIRM_ERR_REVOKED; }  /* 撤销已传播到此槽 */
        cap_entry_t *ce = &d->caps[e->cap_slot];
        if ((ce->flags & CAP_FLAG_VALID) == 0u) { return (i64)IFIRM_ERR_BAD_HANDLE; }
        u16 need = rights_for_function(e->function_id);
        if (need != 0u && (ce->rights & need) != need) { return (i64)IFIRM_ERR_DENIED; }
    }
    if (e->arg_limit != 0u && f->x[REG_A2] > (u64)e->arg_limit) { return (i64)IFIRM_ERR_OVERFLOW; }
    return syscall_execute(e->function_id, f);
}

void ict_build_page(u8 dom, u8 *page)
{
    domain_t *d = domain_get(dom);
    for (u32 slot = 0; slot < ICT_ENTRIES_PER_PAGE; slot++) {
        u8 *p = page + slot * 16u;
        bool used = (d != NULL) && (d->ict[slot].function_id != 0u);
        u32 insn[4];
        insn[0] = 0x00000813u | ((slot & 0xFFFu) << 20);   /* addi a6, x0, slot */
        insn[1] = 0x00000893u;                             /* addi a7, x0, 0 */
        insn[2] = used ? 0x00000073u : 0x00000013u;        /* ecall / nop */
        insn[3] = 0x00008067u;                             /* ret */
        for (u32 k = 0; k < 4u; k++) {
            p[k * 4u + 0u] = (u8)(insn[k] & 0xFFu);
            p[k * 4u + 1u] = (u8)((insn[k] >> 8) & 0xFFu);
            p[k * 4u + 2u] = (u8)((insn[k] >> 16) & 0xFFu);
            p[k * 4u + 3u] = (u8)((insn[k] >> 24) & 0xFFu);
        }
    }
}
