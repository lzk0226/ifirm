#include <ifirm/spawn.h>
#include <ifirm/domain.h>
#include <ifirm/fs.h>
#include <ifirm/elf.h>
#include <ifirm/pmm.h>
#include <ifirm/vmm.h>
#include <ifirm/ict.h>
#include <ifirm/cap.h>
#include <ifirm/ca.h>
#include <ifirm/thread.h>
#include <ifirm/console.h>
#include <ifirm/status.h>

#define SPAWN_MAX_ELF  (64u * 1024u)
#define SPAWN_FIRST_ID 6u
#define SPAWN_ARGS_MAX 8u
#define SPAWN_ARG_MAX  64u

static u8   g_elf[SPAWN_MAX_ELF];
static char g_names[MAX_DOMAINS][FS_NAME_LEN];

static void zero_page(u64 pa)
{
    u8 *p = (u8 *)pa;
    for (u32 i = 0; i < PAGE_SIZE; i++) { p[i] = 0u; }
}

static ifirm_status_t map_zero(u64 *root, vaddr_t va, u64 perm, u64 *out_pa)
{
    u64 pa = pmm_alloc();
    if (pa == 0u) { return IFIRM_ERR_NO_MEMORY; }
    zero_page(pa);
    ifirm_status_t st = vmm_map(root, va, pa, perm);
    if (st != IFIRM_OK) { pmm_free(pa); return st; }
    if (out_pa != NULL) { *out_pa = pa; }
    return IFIRM_OK;
}

/* 往新域的地址空间写字节：页表已建好，逐页取物理地址后直接写 */
static ifirm_status_t put_user(u64 *root, vaddr_t va, const void *src, u64 len)
{
    const u8 *s = (const u8 *)src;
    u64 done = 0u;
    while (done < len) {
        vaddr_t cur = va + done;
        u64 pte = vmm_walk_pte(root, cur);
        if ((pte & PTE_V) == 0u || (pte & PTE_U) == 0u) { return IFIRM_ERR_INVALID_ARG; }
        u64 pa = vmm_walk(root, cur);          /* 已含页内偏移 */
        u64 chunk = PAGE_SIZE - (cur & 0xFFFull);
        if (chunk > len - done) { chunk = len - done; }
        u8 *k = (u8 *)pa;
        for (u64 i = 0; i < chunk; i++) { k[i] = s[done + i]; }
        done += chunk;
    }
    return IFIRM_OK;
}

/*
 * 把一个域槽彻底恢复成 FREE：地址空间、能力表、ICT、计数、名字一起清。
 * spawn 失败与域回收必须走同一条路径——只把 state 改回 FREE 而留下
 * caps/ict，域号复用时会继承上一代的句柄和槽（ca/cap 都以为还占着）。
 */
static void spawn_reset(u16 id)
{
    domain_t *d = &g_domains[id];
    if (d->root != NULL) { vmm_release(d->root); d->root = NULL; }

    for (u32 i = 1u; i < 256u; i++) {
        cap_entry_t *e = &d->caps[i];
        e->flags = 0u;
        e->gen_owner = 0u;
        e->rights = 0u;
        e->object_id = 0u;
        e->object_type = 0u;
        e->derived_from = 0u;
    }
    ict_reset((u8)id);

    d->state = DOMAIN_FREE;
    d->thread_count = 0u;
    d->mem_pages = 0u;
    d->mem_used = 0u;
    d->cap_used = 0u;
    d->faults = 0u;
    d->tpl_id = 0u;
    d->name = NULL;
}

void domain_reap(void)
{
    for (u16 i = 0; i < MAX_DOMAINS; i++) {
        if (g_domains[i].state != DOMAIN_DEAD) { continue; }
        thread_t *t = thread_of_domain((u8)i);
        if (t != NULL) { t->state = THREAD_FREE; t->wait_obj = 0u; }
        spawn_reset(i);
    }
}

/*
 * 把 argv 拷到新用户栈顶上：字符串从高地址往低放，再放指针数组，
 * 最后把 sp 指到数组底部（§26.4 参数传递约定）。
 */
static ifirm_status_t push_args(u64 *root, u64 stack_top, u32 argc,
                                const char *const *argv, u64 *out_sp,
                                u64 *out_argc, u64 *out_argvp)
{
    u64 sp = stack_top;
    u64 ptrs[SPAWN_ARGS_MAX];
    for (u32 i = 0; i < argc; i++) {
        u32 n = 0;
        while (argv[i][n] != '\0' && n + 1u < SPAWN_ARG_MAX) { n++; }
        n++;                                  /* 含结尾 NUL */
        sp -= (u64)n;
        ifirm_status_t st = put_user(root, sp, argv[i], n);
        if (st != IFIRM_OK) { return st; }
        ptrs[i] = sp;
    }
    u64 arr = (sp - 8ull * (u64)(argc + 1u)) & ~7ull;
    ptrs[argc] = 0u;
    ifirm_status_t st = put_user(root, arr, ptrs, 8ull * (u64)(argc + 1u));
    if (st != IFIRM_OK) { return st; }

    *out_sp = arr;
    *out_argc = (u64)argc;
    *out_argvp = arr;
    return IFIRM_OK;
}

ifirm_status_t domain_spawn_path(const char *path, u32 argc, const char *const *argv,
                                 u16 *out_id, elf_info_t *info)
{
    if (argc > SPAWN_ARGS_MAX) { return IFIRM_ERR_INVALID_ARG; }

    u32 len = 0u;
    ifirm_status_t st = fs_read_file(path, g_elf, SPAWN_MAX_ELF, &len);
    if (st != IFIRM_OK) { return st; }

    st = elf_check(g_elf, len);
    if (st != IFIRM_OK) { return st; }

    u16 id = 0u;
    for (u16 i = SPAWN_FIRST_ID; i < MAX_DOMAINS; i++) {
        if (g_domains[i].root == NULL && g_domains[i].state == DOMAIN_FREE) { id = i; break; }
    }
    if (id == 0u) { return IFIRM_ERR_NO_SPACE; }

    domain_t *d = &g_domains[id];
    d->id = id;
    d->state = DOMAIN_READY;
    d->trust_level = 1u;
    d->mem_pages = 0u;
    d->faults = 0u;
    d->tpl_id = 0u;
    d->cap_used = 0u;

    /* 名字拷到静态区：ELF 缓冲下一轮会被覆盖，路径缓冲在调用者栈上 */
    const char *leaf = path;
    for (const char *p = path; *p != '\0'; p++) { if (*p == '/') { leaf = p + 1; } }
    for (u32 i = 0; i < FS_NAME_LEN; i++) { g_names[id][i] = '\0'; }
    for (u32 i = 0; i + 1u < FS_NAME_LEN && leaf[i] != '\0'; i++) { g_names[id][i] = leaf[i]; }
    d->name = g_names[id];

    d->root = vmm_new_as();
    if (d->root == NULL) { spawn_reset(id); return IFIRM_ERR_NO_MEMORY; }

    st = elf_load(d->root, g_elf, len, info);
    if (st != IFIRM_OK) { spawn_reset(id); return st; }
    d->mem_pages = (u16)info->pages;

    /* console 能力只能由 CA 发；charter 的 dom=0 规则覆盖全部派生域 */
    /* 一个句柄同时带 READ|WRITE：ICT 槽要 WRITE，读键盘要 READ */
    cap_t con = 0u;
    st = ca_authorize(id, OBJ_DEVICE, (u16)(CAP_RIGHT_READ | CAP_RIGHT_WRITE), 1u, CA_BOOT, &con);
    if (st != IFIRM_OK) { spawn_reset(id); return st; }

    st = ict_install(id, 8u, (u16)((u32)con & CAP_IDX_MASK), ICT_F_NEEDS_CAP, 0u);
    if (st != IFIRM_OK) { spawn_reset(id); return st; }

    u64 ict_pa = 0u;
    st = map_zero(d->root, DOMAIN_ICT_VA, PTE_R | PTE_X | PTE_U, &ict_pa);
    if (st != IFIRM_OK) { spawn_reset(id); return st; }
    ict_build_page(id, (u8 *)ict_pa);
    d->mem_pages++;

    for (u32 i = 0; i < 4u; i++) {
        st = map_zero(d->root, DOMAIN_USTACK_BASE + (u64)i * PAGE_SIZE,
                      PTE_R | PTE_W | PTE_U, NULL);
        if (st != IFIRM_OK) { spawn_reset(id); return st; }
        d->mem_pages++;
    }

    u64 stack_top = DOMAIN_USTACK_BASE + DOMAIN_USTACK_SIZE;
    u64 sp = stack_top;
    u64 a0 = 0u;
    u64 a1 = 0u;
    if (argc > 0u && argv != NULL) {
        st = push_args(d->root, stack_top, argc, argv, &sp, &a0, &a1);
        if (st != IFIRM_OK) { spawn_reset(id); return st; }
    }

    thread_t *t = thread_create((u8)id, info->entry, a0, a1, (u64)con, sp);
    if (t == NULL) { spawn_reset(id); return IFIRM_ERR_NO_SPACE; }
    d->thread_count = 1u;

    if (out_id != NULL) { *out_id = id; }
    return IFIRM_OK;
}

/* 启动期自检：回收一个 DEAD 域后，它的 cap 表与 ICT 槽必须一起消失 */
i64 spawn_reap_selftest(void)
{
    u16 id = (u16)(MAX_DOMAINS - 1u);
    domain_t *d = &g_domains[id];
    if (d->state != DOMAIN_FREE || d->root != NULL || d->cap_used != 0u) {
        return IFIRM_ERR_INVALID_STATE;
    }

    d->id = id;
    d->state = DOMAIN_READY;
    cap_t h = 0u;
    ifirm_status_t st = cap_grant(id, OBJ_DEVICE, 1u, CAP_RIGHT_WRITE, &h);
    if (st != IFIRM_OK) { d->state = DOMAIN_FREE; return st; }
    u32 idx = (u32)h & CAP_IDX_MASK;
    st = ict_install((u8)id, 8u, (u16)idx, ICT_F_NEEDS_CAP, 0u);
    if (st != IFIRM_OK) { d->state = DOMAIN_FREE; return st; }

    d->state = DOMAIN_DEAD;
    domain_reap();

    bool ok = (d->state == DOMAIN_FREE) && (d->root == NULL) &&
              (d->cap_used == 0u) && (ict_count((u8)id) == 0u) &&
              ((d->caps[idx].flags & CAP_FLAG_VALID) == 0u);
    printk("IFIRM-REAP selftest=%s caps_used=%u ict=%u\n",
           ok ? "ok" : "fail", (u32)d->cap_used, (u32)ict_count((u8)id));
    return ok ? IFIRM_OK : IFIRM_ERR_INVALID_STATE;
}
