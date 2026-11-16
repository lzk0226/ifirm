#include <ifirm/component.h>
#include <ifirm/types.h>
#include <ifirm/status.h>
#include <ifirm/console.h>
#include <ifirm/board.h>
#include <ifirm/pmm.h>
#include <ifirm/vmm.h>
#include <ifirm/cap.h>
#include <ifirm/ca.h>
#include <ifirm/ict.h>
#include <ifirm/ipc.h>
#include <ifirm/tplreg.h>
#include <ifirm/domain.h>
#include <ifirm/fs.h>
#include <ifirm/virtio.h>
#include <ifirm/spawn.h>
#include <ifirm/trap.h>
#include <ifirm/thread.h>
#include <ifirm/syscall.h>

typedef struct {
    u64 hartid;
    u64 dtb_pa;
    u64 kernel_end;
} kctx_t;

static kctx_t g_kctx;
static char  g_provided[KCOMP_SVC_MAX];
static kcomp_info_t g_info[KCOMP_MAX];
static u32   g_ready;

void kcomp_set_context(u64 hartid, u64 dtb_pa, u64 kernel_end)
{
    g_kctx.hartid = hartid;
    g_kctx.dtb_pa = dtb_pa;
    g_kctx.kernel_end = kernel_end;
}

/* ---------- 接口摘要：FNV-1a 32 ---------- */
static u32 fnv32(u32 h, const char *s)
{
    while (*s != '\0') {
        h ^= (u32)(u8)*s;
        h *= 16777619u;
        s++;
    }
    return h;
}

static bool token_in(const char *list, const char *tok)
{
    if (tok[0] == '\0') { return true; }
    u32 tl = 0;
    while (tok[tl] != '\0') { tl++; }
    const char *p = list;
    while (*p != '\0') {
        while (*p == ' ') { p++; }
        const char *q = p;
        while (*q != '\0' && *q != ' ') { q++; }
        u32 n = (u32)(q - p);
        if (n == tl) {
            bool same = true;
            for (u32 i = 0; i < n; i++) {
                if (p[i] != tok[i]) { same = false; break; }
            }
            if (same) { return true; }
        }
        p = q;
    }
    return false;
}

/* 逐个校验 requires 里的每个服务都已提供 */
static bool requires_ok(const char *reqs)
{
    const char *p = reqs;
    while (*p != '\0') {
        while (*p == ' ') { p++; }
        if (*p == '\0') { break; }
        const char *q = p;
        while (*q != '\0' && *q != ' ') { q++; }
        char tok[32];
        u32 n = (u32)(q - p);
        if (n >= sizeof(tok)) { return false; }
        for (u32 i = 0; i < n; i++) { tok[i] = p[i]; }
        tok[n] = '\0';
        if (!token_in(g_provided, tok)) {
            printk("IFIRM-COMP FAIL missing=%s requires=%s\n", tok, reqs);
            return false;
        }
        p = q;
    }
    return true;
}

bool kcomp_provided(const char *service) { return token_in(g_provided, service); }

static void add_provided(const char *list)
{
    u32 n = 0;
    while (g_provided[n] != '\0' && n + 1u < KCOMP_SVC_MAX) { n++; }
    if (n != 0u && n + 1u < KCOMP_SVC_MAX) { g_provided[n++] = ' '; }
    for (u32 i = 0; list[i] != '\0' && n + 1u < KCOMP_SVC_MAX; i++) {
        g_provided[n++] = list[i];
    }
    g_provided[n] = '\0';
}

/* ---------- 组件初始化函数 ---------- */
static ifirm_status_t comp_board(void)
{
    printk("IFIRM-BOOT stage=0\n");
    ifirm_status_t st = board_init(g_kctx.hartid, g_kctx.dtb_pa);
    if (st != IFIRM_OK) { return st; }
    printk("IFIRM-BOARD model=%s ram=%uMiB timebase=%u uart=0x%x plic=0x%x\n",
           g_board.model, (u32)(g_board.ram_size >> 20), (u32)g_board.timebase_hz,
           (u32)g_board.uart_base, (u32)g_board.plic_base);
    printk("IFIRM-BOOT stage=1\n");
    return IFIRM_OK;
}

static ifirm_status_t comp_pmm(void)
{
    pmm_init(g_board.ram_base, g_board.ram_size, g_kctx.kernel_end);
    u32 fp = pmm_frag_permille();
    if (fp > 999u) { fp = 999u; }
    printk("IFIRM-MEM total=%u free=%u used_kernel=%u frag=0.%u%u%u\n",
           (u32)pmm_total(), (u32)pmm_free_count(), (u32)pmm_used(),
           fp / 100u, (fp / 10u) % 10u, fp % 10u);
    return IFIRM_OK;
}

static ifirm_status_t comp_vmm(void)
{
    if (vmm_init() != IFIRM_OK) { return IFIRM_ERR_NO_MEMORY; }
    printk("IFIRM-VM on\n");
    return IFIRM_OK;
}

static ifirm_status_t comp_trap(void)
{
    trap_init();
    printk("IFIRM-TICK hz=100\n");
    return IFIRM_OK;
}

static ifirm_status_t comp_cap(void)
{
    cap_init();
    printk("IFIRM-CAPS mmu=1 timer=1 plic=1 uart=1 hw_watchdog=0 "
           "kernel_hang_recovery=false controller_restart=true cache=C1\n");
    return IFIRM_OK;
}

static ifirm_status_t comp_ca(void)  { return ca_init(); }
static ifirm_status_t comp_ict(void) { return IFIRM_OK; }
static ifirm_status_t comp_ipc(void) { return IFIRM_OK; }
static ifirm_status_t comp_tpl(void) { return tplreg_init(); }

static ifirm_status_t comp_domain(void)
{
    ifirm_status_t st = domain_compose_all();
    if (st != IFIRM_OK) { return st; }
    printk("IFIRM-READY domains=%u pages_used=%u t_ms=%u\n",
           (u32)domain_count_ready(), (u32)pmm_used(), (u32)now_ms());
    return IFIRM_OK;
}

static ifirm_status_t comp_fs(void)
{
    ifirm_status_t st = fs_init();
    if (st != IFIRM_OK) { return st; }
    printk("IFIRM-FS on files=%u bytes=%u blk=v%u rw=1 free=%u\n",
           (u32)fs_files(), (u32)fs_bytes(), (u32)virtio_blk_version(),
           (u32)fs_free_blocks());
    return IFIRM_OK;
}

static ifirm_status_t comp_spawn(void)
{
    /* 自检必须在域组合之后：CA 的 grant 要求目标域真实存在（cap.c:41） */
    return ca_queue_selftest();
}

/* 最后一个组件：把用户态的第一个程序（shell）拉起来 */
static ifirm_status_t comp_init(void)
{
    elf_info_t ei;
    u16 sid = 0u;
    ifirm_status_t st = domain_spawn_path("/bin/sh", 0u, NULL, &sid, &ei);
    if (st != IFIRM_OK) { return st; }
    printk("IFIRM-INIT name=sh dom=%u entry=0x%x pages=%u\n",
           (u32)sid, (u32)ei.entry, (u32)ei.pages);
    return IFIRM_OK;
}

static const kcomponent_t g_comp[] = {
    { "board", "board.console board.mem board.fdt", "", 1u, comp_board },
    { "pmm",   "mem.pages",                         "board.mem", 2u, comp_pmm },
    { "vmm",   "vm.map",                            "mem.pages", 3u, comp_vmm },
    { "trap",  "trap.timer",                        "mem.pages", 3u, comp_trap },
    { "cap",   "cap.table",                         "mem.pages", 4u, comp_cap },
    { "ca",    "ca.auth ca.audit",                  "cap.table", 5u, comp_ca },
    { "ict",   "ict.slots",                         "cap.table", 5u, comp_ict },
    { "ipc",   "ipc.chan",                          "mem.pages", 5u, comp_ipc },
    { "tpl",   "tpl.registry",                      "mem.pages", 6u, comp_tpl },
    { "domain", "domain.compose", "ca.auth ict.slots ipc.chan tpl.registry", 7u, comp_domain },
    { "fs",    "fs.rw",                             "mem.pages", 7u, comp_fs },
    { "spawn", "spawn.exec",                        "fs.rw domain.compose", 8u, comp_spawn },
    { "init",  "init.shell",                        "spawn.exec ca.auth", 9u, comp_init },
};

u32 kcomp_count(void) { return (u32)ARRAY_LEN(g_comp); }
const kcomp_info_t *kcomp_at(u32 i) { return (i < g_ready) ? &g_info[i] : NULL; }

u32 kcomp_composite_digest(void)
{
    u32 h = 2166136261u;
    for (u32 i = 0; i < g_ready; i++) {
        h ^= g_info[i].digest;
        h *= 16777619u;
    }
    return h;
}

ifirm_status_t kernel_compose(void)
{
    u8 last_level = 0u;
    for (u32 i = 0; i < ARRAY_LEN(g_comp); i++) {
        const kcomponent_t *c = &g_comp[i];
        if (c->level < last_level) { return IFIRM_ERR_INVALID_STATE; }
        last_level = c->level;

        if (!requires_ok(c->requires)) { return IFIRM_ERR_NOT_READY; }

        ifirm_status_t st = c->init();
        if (st != IFIRM_OK) {
            printk("IFIRM-COMP FAIL name=%s st=%d\n", c->name, (i64)st);
            return st;
        }

        u32 h = 2166136261u;
        h = fnv32(h, c->name);
        h = fnv32(h, "|");
        h = fnv32(h, c->provides);
        h = fnv32(h, "|");
        h = fnv32(h, c->requires);
        g_info[g_ready].name = c->name;
        g_info[g_ready].provides = c->provides;
        g_info[g_ready].digest = h;
        g_ready++;
        add_provided(c->provides);

        printk("IFIRM-COMP n=%u name=%s lvl=%u digest=0x%x\n",
               i + 1u, c->name, (u32)c->level, (u32)h);
    }
    printk("IFIRM-COMPOSE n=%u ok digest=0x%x\n",
           (u32)g_ready, (u32)kcomp_composite_digest());
    return IFIRM_OK;
}
