#include <ifirm/types.h>
#include <ifirm/board.h>
#include <ifirm/status.h>
#include <ifirm/console.h>
#include <ifirm/sbi.h>

#define FDT_MAGIC 0xd00dfeedu
#define FDT_BEGIN_NODE 1u
#define FDT_END_NODE   2u
#define FDT_PROP       3u
#define FDT_END        9u

typedef struct {
    u32 magic, totalsize, off_dt_struct, off_dt_strings;
    u32 off_mem_rsvmap, version, last_comp_version, boot_cpuid_phys;
    u32 size_dt_strings, size_dt_struct;
} fdt_header_t;

board_info_t g_board;

static u32 be32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static bool name_eq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) { return false; }
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

static bool name_prefix(const char *s, const char *prefix)
{
    while (*prefix != '\0') {
        if (*s++ != *prefix++) { return false; }
    }
    return true;
}

/* 只解析三样东西：model、memory 的 reg、cpus 的 timebase-frequency（§7.5） */
static void fdt_parse(u64 dtb_pa)
{
    const u8 *base = (const u8 *)dtb_pa;
    const fdt_header_t *h = (const fdt_header_t *)base;
    const u8 *str = base + be32((const u8 *)&h->off_dt_strings);
    const u8 *p   = base + be32((const u8 *)&h->off_dt_struct);
    const u8 *end = p + be32((const u8 *)&h->size_dt_struct);
    char node[64];
    u32 depth = 0;

    node[0] = '\0';
    while (p + 4u <= end) {
        u32 tok = be32(p);
        p += 4u;
        if (tok == FDT_BEGIN_NODE) {
            u32 i = 0;
            while (p + i < end && p[i] != 0u && i < sizeof(node) - 1u) { node[i] = (char)p[i]; i++; }
            node[i] = '\0';
            depth++;
            p += (i + 1u + 3u) & ~3u;
        } else if (tok == FDT_END_NODE) {
            depth--;
            if (depth == 0u) { node[0] = '\0'; }
        } else if (tok == FDT_PROP) {
            u32 len = be32(p);
            u32 nameoff = be32(p + 4u);
            const u8 *val = p + 8u;
            const char *pname = (const char *)(str + nameoff);
            if (name_eq(pname, "model")) {
                u32 i = 0;
                while (i < len && i < sizeof(g_board.model) - 1u && val[i] != 0u) {
                    g_board.model[i] = (char)val[i]; i++;
                }
                g_board.model[i] = '\0';
            } else if (name_eq(pname, "reg") && name_prefix(node, "memory")) {
                if (len >= 16u) {
                    g_board.ram_base = ((u64)be32(val) << 32) | (u64)be32(val + 4u);
                    g_board.ram_size = ((u64)be32(val + 8u) << 32) | (u64)be32(val + 12u);
                }
            } else if (name_eq(pname, "timebase-frequency")) {
                g_board.timebase_hz = be32(val);
            }
            p += 8u + len;
            p = (const u8 *)(((u64)p + 3u) & ~3ull);
        } else if (tok == FDT_END) {
            break;
        } else {
            break;   /* 未知 token：停止解析，用兜底值 */
        }
    }
}

ifirm_status_t board_init(u64 hartid, u64 dtb_pa)
{
    g_board.boot_hartid = hartid;
    g_board.dtb_pa = dtb_pa;
    g_board.uart_base = BOARD_UART0_PA;
    g_board.plic_base = BOARD_PLIC_PA;
    g_board.ram_base = BOARD_RAM_BASE;
    g_board.ram_size = 128ull * 1024ull * 1024ull;
    g_board.timebase_hz = 10000000u;

    const fdt_header_t *h = (const fdt_header_t *)dtb_pa;
    if (dtb_pa < 0x80000000ull || be32((const u8 *)&h->magic) != FDT_MAGIC) {
        return IFIRM_ERR_CORRUPT;      /* IFIRM-BOOT FAIL board-fdt-magic */
    }
    fdt_parse(dtb_pa);
    g_board.from_fdt = true;

    if (g_board.ram_size < 64ull * 1024ull * 1024ull) { return IFIRM_ERR_INVALID_ARG; }
    if (g_board.timebase_hz == 0u) { g_board.timebase_hz = 10000000u; }
    return IFIRM_OK;
}

void board_halt(ifirm_status_t st, const char *reason)
{
    if (reason != NULL) {
        printk("IFIRM-BOOT FAIL %s (%d)\n", reason, (i32)st);
    }
    sbi_shutdown();
}
