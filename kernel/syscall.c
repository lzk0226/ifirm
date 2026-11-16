#include <ifirm/types.h>
#include <ifirm/syscall.h>
#include <ifirm/console.h>
#include <ifirm/thread.h>
#include <ifirm/domain.h>
#include <ifirm/cap.h>
#include <ifirm/ipc.h>
#include <ifirm/vmm.h>
#include <ifirm/ca.h>
#include <ifirm/fs.h>
#include <ifirm/spawn.h>
#include <ifirm/elf.h>
#include <ifirm/status.h>

#define USER_PATH_MAX 64u
#define USER_ARG_MAX  8u
#define FS_IO_CHUNK   1024u

static u8 g_iobuf[FS_IO_CHUNK];

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
        u64 pa = vmm_walk(root, cur);       /* 已含页内偏移 */
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

/* 从用户空间读一个以 NUL 结尾的字符串（逐字节查页表，不信任长度） */
static i64 read_user_str(u64 *root, u64 va, char *buf, u32 cap)
{
    for (u32 i = 0; i + 1u < cap; i++) {
        u64 pte = vmm_walk_pte(root, va + (u64)i);
        if ((pte & PTE_V) == 0u || (pte & PTE_U) == 0u) { return (i64)IFIRM_ERR_INVALID_ARG; }
        char c = *(const char *)(void *)vmm_walk(root, va + (u64)i);
        buf[i] = c;
        if (c == '\0') { return (i64)i; }
    }
    buf[cap - 1u] = '\0';
    return (i64)(cap - 1u);
}

static ifirm_status_t get_path(trap_frame_t *f, u8 reg, u8 len_reg, char *buf)
{
    u32 len = (u32)f->x[len_reg];
    if (len == 0u || len >= USER_PATH_MAX) { return IFIRM_ERR_INVALID_ARG; }
    ifirm_status_t st = xcopy(g_current_domain->root, (vaddr_t)f->x[reg], buf, len, false);
    if (st != IFIRM_OK) { return st; }
    buf[len] = '\0';
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

/* ---------- 进程 ---------- */

static i64 do_spawn(trap_frame_t *f)
{
    char path[USER_PATH_MAX];
    u32 plen = (u32)f->x[REG_A1];
    if (plen == 0u || plen >= USER_PATH_MAX) { return (i64)IFIRM_ERR_INVALID_ARG; }
    ifirm_status_t st = xcopy(g_current_domain->root, (vaddr_t)f->x[REG_A0], path, plen, false);
    if (st != IFIRM_OK) { return (i64)st; }
    path[plen] = '\0';

    u32 argc = (u32)f->x[REG_A2];
    if (argc > USER_ARG_MAX) { return (i64)IFIRM_ERR_INVALID_ARG; }

    char store[USER_ARG_MAX][USER_PATH_MAX];
    const char *argv[USER_ARG_MAX];
    if (argc > 0u) {
        u64 ptrs[USER_ARG_MAX];
        st = xcopy(g_current_domain->root, (vaddr_t)f->x[REG_A3], ptrs,
                   (u64)argc * 8ull, false);
        if (st != IFIRM_OK) { return (i64)st; }
        for (u32 i = 0; i < argc; i++) {
            i64 n = read_user_str(g_current_domain->root, ptrs[i], store[i], USER_PATH_MAX);
            if (n < 0) { return n; }
            argv[i] = store[i];
        }
    }

    domain_reap();                   /* 先回收已退出的域，避免 16 个槽耗尽 */
    u16 id = 0u;
    elf_info_t ei;
    st = domain_spawn_path(path, argc, (argc > 0u) ? argv : NULL, &id, &ei);
    if (st != IFIRM_OK) { return (i64)st; }

    /* 只回写 a1：a2/a3 属于调用者现场（用户编译器不认为它们会被破坏） */
    (void)ei;
    f->x[REG_A1] = (u64)id;
    return 0;
}

static i64 do_wait(trap_frame_t *f)
{
    u16 dom = (u16)f->x[REG_A0];
    thread_t *t = thread_of_domain((u8)dom);
    if (t == NULL) { return (i64)IFIRM_ERR_NOT_FOUND; }
    if (t->state == THREAD_DEAD) {
        f->x[REG_A1] = (u64)t->exit_code;
        return 0;
    }
    thread_block(THREAD_WAIT_DOMAIN | (u64)dom);
    return 0;                        /* 唤醒时 a1 = 退出码 */
}

static i64 do_proc_list(trap_frame_t *f)
{
    u32 want = (u32)f->x[REG_A0];
    u32 seen = 0u;
    for (u16 i = 0; i < MAX_DOMAINS; i++) {
        domain_t *d = &g_domains[i];
        /* 死域不列：槽位要留给新的 spawn，列出来只会让人误以为它还活着 */
        if (d->state == DOMAIN_FREE || d->state == DOMAIN_DEAD) { continue; }
        if (seen++ != want) { continue; }
        proc_info_t p;
        for (u32 k = 0; k < sizeof(p); k++) { ((u8 *)&p)[k] = 0u; }
        p.id = i;
        p.state = d->state;
        p.kind = (i < 6u) ? 0u : 1u;
        p.pages = d->mem_pages;
        p.caps = d->cap_used;
        p.faults = d->faults;
        if (d->name != NULL) {
            for (u32 k = 0; k + 1u < sizeof(p.name) && d->name[k] != '\0'; k++) {
                p.name[k] = d->name[k];
            }
        }
        ifirm_status_t st = xcopy(g_current_domain->root, (vaddr_t)f->x[REG_A1], &p,
                                  sizeof(p), true);
        if (st != IFIRM_OK) { return (i64)st; }
        return 1;
    }
    return 0;
}

static i64 do_cap_audit(trap_frame_t *f)
{
    u32 max = (u32)f->x[REG_A1];
    u32 n = ca_audit_count();
    if (n > max) { n = max; }
    for (u32 i = 0; i < n; i++) {
        ca_audit_t e;
        if (!ca_audit_at(i, &e)) { n = i; break; }
        ifirm_status_t st = xcopy(g_current_domain->root,
                                  (vaddr_t)(f->x[REG_A0] + (u64)i * sizeof(ca_audit_t)),
                                  &e, sizeof(e), true);
        if (st != IFIRM_OK) { return (i64)st; }
    }
    return (i64)n;
}

static i64 do_console_read(trap_frame_t *f)
{
    u32 cap = (u32)f->x[REG_A1];
    if (cap > 256u) { cap = 256u; }
    char buf[256];
    u32 n = 0u;
    while (n < cap && uart_rx_ready()) { buf[n++] = uart_getc(); }
    if (n > 0u) {
        ifirm_status_t st = xcopy(g_current_domain->root, (vaddr_t)f->x[REG_A0], buf, n, true);
        if (st != IFIRM_OK) { return (i64)st; }
    }
    /* 与 read(2) 不同：这里直接把字节数放返回值，用户态一句 ecall 就能拿到 */
    return (i64)n;
}

/* ---------- 文件系统 ---------- */

static i64 do_fs_open(trap_frame_t *f)
{
    char path[USER_PATH_MAX];
    ifirm_status_t st = get_path(f, REG_A0, REG_A1, path);
    if (st != IFIRM_OK) { return (i64)st; }
    u32 flags = (u32)f->x[REG_A2];

    i64 ino = fs_lookup(path);
    if (ino < 0 && (flags & FS_OPEN_CREATE) != 0u) {
        ino = fs_creat(path, FS_TYPE_FILE);
    }
    if (ino < 0) { return ino; }
    if ((flags & FS_OPEN_TRUNC) != 0u) {
        i64 r = fs_trunc((u32)ino);
        if (r < 0) { return r; }
    }
    f->x[REG_A1] = (u64)ino;
    return 0;
}

static i64 do_fs_read(trap_frame_t *f)
{
    u32 ino = (u32)f->x[REG_A0];
    u64 buf_va = f->x[REG_A1];
    u64 len = f->x[REG_A2];
    u32 off = (u32)f->x[REG_A3];
    if (len > 64ull * 1024ull) { return (i64)IFIRM_ERR_INVALID_ARG; }

    u64 done = 0u;
    while (done < len) {
        u32 want = (u32)((len - done > FS_IO_CHUNK) ? FS_IO_CHUNK : (len - done));
        i64 n = fs_read(ino, off + (u32)done, g_iobuf, want);
        if (n < 0) { return n; }
        if (n == 0) { break; }
        ifirm_status_t st = xcopy(g_current_domain->root, buf_va + done, g_iobuf, (u64)n, true);
        if (st != IFIRM_OK) { return (i64)st; }
        done += (u64)n;
        if ((u32)n < want) { break; }
    }
    f->x[REG_A1] = done;
    return 0;
}

static i64 do_fs_write(trap_frame_t *f)
{
    u32 ino = (u32)f->x[REG_A0];
    u64 buf_va = f->x[REG_A1];
    u64 len = f->x[REG_A2];
    u32 off = (u32)f->x[REG_A3];
    if (len > 64ull * 1024ull) { return (i64)IFIRM_ERR_INVALID_ARG; }

    u64 done = 0u;
    while (done < len) {
        u32 want = (u32)((len - done > FS_IO_CHUNK) ? FS_IO_CHUNK : (len - done));
        ifirm_status_t st = xcopy(g_current_domain->root, buf_va + done, g_iobuf, want, false);
        if (st != IFIRM_OK) { return (i64)st; }
        i64 n = fs_write(ino, off + (u32)done, g_iobuf, want);
        if (n < 0) { return n; }
        done += (u64)n;
        if ((u32)n < want) { break; }
    }
    f->x[REG_A1] = done;
    return 0;
}

static i64 do_fs_unlink(trap_frame_t *f)
{
    char path[USER_PATH_MAX];
    ifirm_status_t st = get_path(f, REG_A0, REG_A1, path);
    if (st != IFIRM_OK) { return (i64)st; }
    return fs_unlink(path);
}

static i64 do_fs_mkdir(trap_frame_t *f)
{
    char path[USER_PATH_MAX];
    ifirm_status_t st = get_path(f, REG_A0, REG_A1, path);
    if (st != IFIRM_OK) { return (i64)st; }
    return fs_mkdir(path);
}

static i64 do_fs_readdir(trap_frame_t *f)
{
    fs_dirent_t e;
    i64 r = fs_readdir((u32)f->x[REG_A0], (u32)f->x[REG_A1], &e);
    if (r < 0) { return 0; }         /* 目录读完：返回 0 = 结束 */
    ifirm_status_t st = xcopy(g_current_domain->root, (vaddr_t)f->x[REG_A2], &e,
                              sizeof(e), true);
    if (st != IFIRM_OK) { return (i64)st; }
    return 1;
}

static i64 do_fs_stat(trap_frame_t *f)
{
    char path[USER_PATH_MAX];
    ifirm_status_t st = get_path(f, REG_A0, REG_A1, path);
    if (st != IFIRM_OK) { return (i64)st; }
    fs_stat_t s;
    i64 r = fs_stat(path, &s);
    if (r < 0) { return r; }
    st = xcopy(g_current_domain->root, (vaddr_t)f->x[REG_A2], &s, sizeof(s), true);
    if (st != IFIRM_OK) { return (i64)st; }
    return 0;
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
    case SYS_YIELD:         return 0;
    case SYS_EXIT:          thread_exit((i64)f->x[REG_A0]); return 0;
    case SYS_IPC_CALL:      return do_ipc_call(f);
    case SYS_IPC_RECV:      return do_ipc_recv(f);
    case SYS_IPC_REPLY:     return do_ipc_reply(f);
    case SYS_DOMAIN_INFO:   return do_domain_info(f);
    case SYS_CAP_REQUEST:   return ca_request(f);
    case SYS_CAP_REVOKE:    return ca_revoke_own(f);
    case SYS_CAP_DELEGATE:  return ca_delegate(f);
    case SYS_CAP_AUDIT:     return do_cap_audit(f);
    case SYS_SPAWN:         return do_spawn(f);
    case SYS_WAIT:          return do_wait(f);
    case SYS_CONSOLE_READ:  return do_console_read(f);
    case SYS_FS_OPEN:       return do_fs_open(f);
    case SYS_FS_READ:       return do_fs_read(f);
    case SYS_FS_WRITE:      return do_fs_write(f);
    case SYS_FS_CLOSE:      return 0;
    case SYS_FS_UNLINK:     return do_fs_unlink(f);
    case SYS_FS_READDIR:    return do_fs_readdir(f);
    case SYS_FS_STAT:       return do_fs_stat(f);
    case SYS_FS_MKDIR:      return do_fs_mkdir(f);
    case SYS_PROC_LIST:     return do_proc_list(f);
    default:                return (i64)IFIRM_ERR_UNSUPPORTED;
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
