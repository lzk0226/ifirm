#include <ifirm/types.h>
#include <ifirm/pmm.h>
#include <ifirm/status.h>

static u64 g_bits[PMM_MAX_PAGES / 64u];
static u64 g_ram_base;
static u32 g_npages;
static u32 g_free;
static u32 g_next;

static inline bool bit_test(u32 i) { return (g_bits[i / 64u] & (1ull << (i % 64u))) != 0u; }
static inline void bit_set(u32 i)  { g_bits[i / 64u] |=  (1ull << (i % 64u)); }
static inline void bit_clr(u32 i)  { g_bits[i / 64u] &= ~(1ull << (i % 64u)); }

void pmm_init(u64 ram_base, u64 ram_size, u64 kernel_end)
{
    g_ram_base = ram_base;
    g_npages = (u32)(ram_size / PAGE_SIZE);
    if (g_npages > PMM_MAX_PAGES) { g_npages = PMM_MAX_PAGES; }
    g_free = g_npages;

    for (u32 i = 0; i < g_npages; i++) { bit_clr(i); }

    /* 保留：低 2 MiB（OpenSBI）、内核映像、DTB 与栈 */
    u64 res_end = kernel_end;
    if (g_ram_base + 0x200000ull > res_end) { res_end = g_ram_base + 0x200000ull; }
    u64 res_pages = (res_end - g_ram_base + PAGE_SIZE - 1u) / PAGE_SIZE;
    for (u32 i = 0; i < (u32)res_pages && i < g_npages; i++) {
        bit_set(i);
        g_free--;
    }
    g_next = (u32)res_pages;
}

u64 pmm_alloc(void)
{
    for (u32 scanned = 0; scanned < g_npages; scanned++) {
        u32 i = (g_next + scanned) % g_npages;
        if (!bit_test(i)) {
            bit_set(i);
            g_free--;
            g_next = (i + 1u) % g_npages;
            return g_ram_base + (u64)i * PAGE_SIZE;
        }
    }
    return 0u;
}

void pmm_free(u64 pa)
{
    if (pa < g_ram_base) { return; }
    u64 idx = (pa - g_ram_base) / PAGE_SIZE;
    if (idx >= (u64)g_npages) { return; }
    u32 i = (u32)idx;
    if (bit_test(i)) {
        bit_clr(i);
        g_free++;
        if (i < g_next) { g_next = i; }
    }
}

u32 pmm_total(void) { return g_npages; }
u32 pmm_free_count(void) { return g_free; }
u32 pmm_used(void) { return g_npages - g_free; }

u32 pmm_frag_permille(void)
{
    u32 best = 0;
    u32 run = 0;
    for (u32 i = 0; i < g_npages; i++) {
        if (!bit_test(i)) {
            run++;
            if (run > best) { best = run; }
        } else {
            run = 0;
        }
    }
    return (g_free == 0u) ? 0u : (u32)(((u64)best * 1000ull) / (u64)g_free);
}
