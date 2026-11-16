#include <ifirm/types.h>
#include <ifirm/syscall.h>
#include <ifirm/console.h>
#include <ifirm/thread.h>
#include <ifirm/domain.h>
#include <ifirm/cap.h>
#include <ifirm/ipc.h>
#include <ifirm/vmm.h>
#include <ifirm/ca.h>
#include <ifirm/status.h>

u64 now_ns(void)
{
    u64 t;
    __asm__ volatile ("rdtime %0" : "=r"(t));
    return t * 100ull;                 /* 10 MHz timebase → 100 ns/tick */
}

u64 now_ms(void) { return now_ns() / 1000000ull; }

static bool user_range_ok(u64 *root, vaddr_t va, u64 len)
{
    if (len == 0u) { return true; }
    if (va + len < va) { return false; }
    for (u64 off = 0; off < len; off += PAGE_SIZE) {
        u64 e = vmm_walk_pte(root, va + off);
        if ((e & PTE_V) == 0u || (e & PTE_U) == 0u) { return false; }
    }
    return true;
}

/* 跨地址空间拷贝：先查页表拿物理地址，再从内核恒等映射读写（§10.7） */
static ifirm_status_t xcopy(u64 *root, vaddr_t va, void *buf, u64 len, bool to_user)
{
    if (!user_range_ok(root, va, len)) { return IFIRM_ERR_INVALID_ARG; }
    u64 done = 0;
    while (done < len) {
        vaddr_t cur = va + done;
        u64 pa = vmm_walk(root, cur);
        u64 chunk = PAGE_SIZE - (cur & 0xFFFull);
        if (chunk > len - done) { chunk = len - done; }
        u8 *k = (u8 *)pa;
        for (u64 i = 0; i < chunk; i++) {
            if (to_user) { k[i] = ((const u8 *)buf)[done + i]; }
            else         { ((u8 *)buf)[done + i] = k[i]; }
        }
        done += chunk;
    }
    return IFIRM_OK;
}

static ifirm_status_t get_channel(trap_frame_t *f, u16 rights, channel_t **out)
{
    cap_entry_t *e = NULL;
    ifirm_status_t st = cap_check((cap_t)f->x[REG_A0], OBJ_CHANNEL, rights, &e);
    if (st != IFIRM_OK) { return st; }
    channel_t *ch = ipc_channel_get(e->object_id);
    if (ch == NULL) { return IFIRM_ERR_BAD_HANDLE; }
    *out = ch;
    return IFIRM_OK;
}

/* 唤醒某通道上阻塞的接收者，把队首消息直接投递到它的缓冲 */
static void try_deliver(channel_t *ch)
{
    thread_t *t = thread_waiting_on((u64)ch);
    if (t == NULL) { return; }
    u8 msg[IPC_MSG_SIZE];
    u32 len = 0;
    if (ipc_dequeue(ch, msg, &len) != IFIRM_OK) { return; }
    domain_t *d = domain_get(t->dom);
    if (len > t->blk_cap) {
        thread_wake(t, (i64)IFIRM_ERR_OVERFLOW, 0u);
        return;
    }
    ifirm_status_t st = IFIRM_ERR_BAD_HANDLE;
    if (d != NULL) { st = xcopy(d->root, t->blk_buf, msg, len, true); }
    thread_wake(t, (st == IFIRM_OK) ? 0 : (i64)st, len);
}

static i64 do_debug_write(trap_frame_t *f)
{
    u64 len = f->x[REG_A1];
    if (len > 128u) { return (i64)IFIRM_ERR_OVERFLOW; }
    char buf[129];
    ifirm_status_t st = xcopy(g_current_domain->root, (vaddr_t)f->x[REG_A0], buf, len, false);
    if (st != IFIRM_OK) { return (i64)st; }
    buf[len] = '\0';
    puts_raw(buf);
    return (i64)len;
}

static i64 do_ipc_call(trap_frame_t *f)
{
    channel_t *ch = NULL;
    ifirm_status_t st = get_channel(f, CAP_RIGHT_CALL, &ch);
    if (st != IFIRM_OK) { return (i64)st; }

    u64 len = f->x[REG_A2];
    if (len > IPC_MSG_SIZE) { return (i64)IFIRM_ERR_INVALID_ARG; }
    if (ch->call_pending != 0u) { return (i64)IFIRM_ERR_BUSY; }

    u8 msg[IPC_MSG_SIZE];
    st = xcopy(g_current_domain->root, (vaddr_t)f->x[REG_A1], msg, len, false);
    if (st != IFIRM_OK) { return (i64)st; }
    st = ipc_enqueue(ch, msg, (u32)len);
    if (st != IFIRM_OK) { return (i64)st; }

    ch->call_pending = 1u;
    ch->caller_dom = g_current_domain->id;
    ch->caller_tid = g_current->id;
    ch->caller_va = f->x[REG_A3];
    ch->caller_cap = (u32)f->x[REG_A4];

    try_deliver(ch);                 /* 接收方可能已经在等 */
    thread_block((u64)ch);
    return 0;
}

static i64 do_ipc_recv(trap_frame_t *f)
{
    channel_t *ch = NULL;
    ifirm_status_t st = get_channel(f, CAP_RIGHT_RECV, &ch);
    if (st != IFIRM_OK) { return (i64)st; }

    u32 cap = (u32)f->x[REG_A2];
    if (cap > IPC_MSG_SIZE) { cap = IPC_MSG_SIZE; }
    u8 msg[IPC_MSG_SIZE];
    u32 len = 0;
    st = ipc_recv(ch, g_current_domain->id, msg, sizeof(msg), &len);
    if (st == IFIRM_ERR_AGAIN) {
        if (((u32)f->x[REG_A3] & 1u) != 0u) { return (i64)IFIRM_ERR_AGAIN; }
        g_current->blk_buf = f->x[REG_A1];
        g_current->blk_cap = cap;
        thread_block((u64)ch);
        return 0;                    /* 唤醒时 a0/a1 被投递方覆盖 */
    }
    if (st != IFIRM_OK) { return (i64)st; }
    if (len > cap) { return (i64)IFIRM_ERR_OVERFLOW; }
    st = xcopy(g_current_domain->root, (vaddr_t)f->x[REG_A1], msg, len, true);
    if (st != IFIRM_OK) { return (i64)st; }
    f->x[REG_A1] = len;
    return 0;
}

static i64 do_ipc_reply(trap_frame_t *f)
{
    channel_t *ch = NULL;
    ifirm_status_t st = get_channel(f, CAP_RIGHT_REPLY, &ch);
    if (st != IFIRM_OK) { return (i64)st; }
    if (ch->call_pending == 0u) { return (i64)IFIRM_ERR_INVALID_STATE; }

    u64 len = f->x[REG_A2];
    if (len > (u64)ch->caller_cap || len > IPC_MSG_SIZE) { return (i64)IFIRM_ERR_INVALID_ARG; }
    u8 msg[IPC_MSG_SIZE];
    st = xcopy(g_current_domain->root, (vaddr_t)f->x[REG_A1], msg, len, false);
    if (st != IFIRM_OK) { return (i64)st; }

    domain_t *cd = domain_get(ch->caller_dom);
    if (cd != NULL) {
        st = xcopy(cd->root, (vaddr_t)ch->caller_va, msg, len, true);
        if (st != IFIRM_OK) { ch->call_pending = 0u; return (i64)st; }
    }
    ch->call_pending = 0u;
    thread_wake(&g_threads[ch->caller_tid], 0, len);
    return 0;
}

static i64 do_domain_info(trap_frame_t *f)
{
    u64 out[4];
    out[0] = g_current_domain->id;
    out[1] = g_current_domain->mem_used;
    out[2] = g_current_domain->cap_used;
    out[3] = g_current_domain->faults;
    ifirm_status_t st = xcopy(g_current_domain->root, (vaddr_t)f->x[REG_A0], out, sizeof(out), true);
    if (st != IFIRM_OK) { return (i64)st; }
    return (i64)sizeof(out);
}

i64 syscall_execute(u32 nr, trap_frame_t *f)
{
    switch (nr) {
    case SYS_DEBUG_WRITE:   return do_debug_write(f);
    case SYS_CONSOLE_WRITE: return do_debug_write(f);
    case SYS_CLOCK: {
        u64 t = now_ns();
        ifirm_status_t st = xcopy(g_current_domain->root, (vaddr_t)f->x[REG_A0], &t, 8u, true);
        return (st == IFIRM_OK) ? (i64)t : (i64)st;
    }
    case SYS_YIELD:       return 0;
    case SYS_EXIT:        thread_exit((i64)f->x[REG_A0]); return 0;
    case SYS_IPC_CALL:    return do_ipc_call(f);
    case SYS_IPC_RECV:    return do_ipc_recv(f);
    case SYS_IPC_REPLY:   return do_ipc_reply(f);
    case SYS_DOMAIN_INFO: return do_domain_info(f);
    case SYS_CAP_REQUEST: return ca_request(f);
    case SYS_CAP_REVOKE:  return ca_revoke_own(f);
    default:              return (i64)IFIRM_ERR_UNSUPPORTED;
    }
}

trap_frame_t *syscall_dispatch(trap_frame_t *f)
{
    f->sepc += 4u;
    u32 nr = (u32)f->x[REG_A7];
    i64 ret;
    if (nr == SYS_ICT) {
        ret = ict_dispatch(f, (u32)f->x[REG_A6]);
    } else {
        ret = syscall_execute(nr, f);
    }
    f->x[REG_A0] = (u64)ret;
    return f;
}
