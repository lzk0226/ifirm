#include <ifirm/types.h>
#include <ifirm/vmm.h>
#include <ifirm/pmm.h>
#include <ifirm/status.h>
#include <ifirm/console.h>

#define PTE_PPN(e)  (((e) >> 10) << 12)
#define PTE_LEAF    (PTE_R | PTE_W | PTE_X)
#define IDENT_FLAGS (PTE_V | PTE_LEAF | PTE_G | PTE_A | PTE_D)

u64 *g_kernel_root;

static u64 *alloc_page(void)
{
    u64 pa = pmm_alloc();
    if (pa == 0u) { return NULL; }
    u64 *p = (u64 *)pa;
    for (u32 i = 0; i < 512u; i++) { p[i] = 0u; }
    return p;
}

/* 内核恒等映射：0..1 GiB 用二级表（留给用户映射），2..3 GiB 用 1 GiB 大页（RAM） */
u64 *vmm_new_as(void)
{
    u64 *root = alloc_page();
    if (root == NULL) { return NULL; }
    u64 *l1 = alloc_page();
    if (l1 == NULL) { pmm_free((u64)root); return NULL; }

    for (u32 i = 0; i < 512u; i++) {
        l1[i] = ((u64)i * 0x80000ull) | IDENT_FLAGS;      /* (i*2MiB)>>12<<10 */
    }
    root[0] = (((u64)l1 >> 12) << 10) | PTE_V;
    root[2] = 0x20000000ull | IDENT_FLAGS;      /* 1 GiB 大页：0x8000_0000>>12<<10 */
    return root;
}

ifirm_status_t vmm_map(u64 *root, vaddr_t va, paddr_t pa, u64 perm)
{
    if (root == NULL || (va & 0xFFFull) != 0u || (pa & 0xFFFull) != 0u) {
        return IFIRM_ERR_INVALID_ARG;
    }
    if ((perm & PTE_W) != 0u && (perm & PTE_X) != 0u) {
        return IFIRM_ERR_DENIED;                /* W^X */
    }
    if ((va >> 30) != 0u) { return IFIRM_ERR_INVALID_ARG; }

    u64 *l1 = (u64 *)PTE_PPN(root[0]);
    u32 i1 = (u32)((va >> 21) & 0x1FFu);
    u32 i0 = (u32)((va >> 12) & 0x1FFu);
    u64 *l0;

    if ((l1[i1] & PTE_V) != 0u && (l1[i1] & PTE_LEAF) == 0u) {
        l0 = (u64 *)PTE_PPN(l1[i1]);
    } else {
        l0 = alloc_page();
        if (l0 == NULL) { return IFIRM_ERR_NO_MEMORY; }
        if ((l1[i1] & PTE_V) != 0u) {
            /* 拆分 2 MiB 恒等大页：先铺满 512 个 4 KiB 恒等项，再覆盖目标项 */
            u64 base = PTE_PPN(l1[i1]);
            for (u32 i = 0; i < 512u; i++) {
                l0[i] = (base + (u64)i * PAGE_SIZE) | IDENT_FLAGS;
            }
        }
        l1[i1] = (((u64)l0 >> 12) << 10) | PTE_V;
    }

    if ((l0[i0] & PTE_V) != 0u && (l0[i0] & PTE_U) != 0u) {
        return IFIRM_ERR_INVALID_STATE;         /* 已映射：不做静默覆盖（§10.6） */
    }
    l0[i0] = ((pa >> 12) << 10) | (perm & 0x3FFull) | PTE_V | PTE_A | PTE_D;
    return IFIRM_OK;
}

ifirm_status_t vmm_switch(u64 *root)
{
    if (root == NULL) { return IFIRM_ERR_INVALID_ARG; }
    u64 satp = SATP_MODE_SV39 | (((u64)root >> 12) & SATP_PPN_MASK);
    __asm__ volatile ("csrw satp, %0; sfence.vma" :: "r"(satp) : "memory");
    return IFIRM_OK;
}

void vmm_flush(void) { __asm__ volatile ("sfence.vma" ::: "memory"); }

u64 vmm_walk(u64 *root, vaddr_t va)
{
    if (root == NULL || (va >> 30) != 0u) { return 0u; }
    u64 e1 = root[0];
    if ((e1 & PTE_V) == 0u || (e1 & PTE_LEAF) != 0u) { return 0u; }
    u64 *l1 = (u64 *)PTE_PPN(e1);
    u32 i1 = (u32)((va >> 21) & 0x1FFu);
    u32 i0 = (u32)((va >> 12) & 0x1FFu);
    u64 e = l1[i1];
    if ((e & PTE_V) == 0u) { return 0u; }
    if ((e & PTE_LEAF) != 0u) {
        return (PTE_PPN(e) & ~0x1FFFFFull) | (va & 0x1FFFFFull);
    }
    u64 *l0 = (u64 *)PTE_PPN(e);
    u64 e0 = l0[i0];
    if ((e0 & PTE_V) == 0u) { return 0u; }
    return (PTE_PPN(e0) & PAGE_MASK) | (va & 0xFFFull);
}

u64 vmm_walk_pte(u64 *root, vaddr_t va)
{
    if (root == NULL || (va >> 30) != 0u) { return 0u; }
    u64 e1 = root[0];
    if ((e1 & PTE_V) == 0u || (e1 & PTE_LEAF) != 0u) { return 0u; }
    u64 *l1 = (u64 *)PTE_PPN(e1);
    u64 e = l1[(va >> 21) & 0x1FFu];
    if ((e & PTE_V) == 0u) { return 0u; }
    if ((e & PTE_LEAF) != 0u) { return e; }
    u64 *l0 = (u64 *)PTE_PPN(e);
    u64 e0 = l0[(va >> 12) & 0x1FFu];
    if ((e0 & PTE_V) == 0u) { return 0u; }
    return e0;
}

/* 组合失败时的回滚：释放该地址空间拥有的每一个页（不碰内核恒等映射） */
void vmm_release(u64 *root)
{
    if (root == NULL) { return; }
    u64 e1 = root[0];
    if ((e1 & PTE_V) != 0u && (e1 & PTE_LEAF) == 0u) {
        u64 *l1 = (u64 *)PTE_PPN(e1);
        for (u32 i = 0; i < 512u; i++) {
            u64 e = l1[i];
            if ((e & PTE_V) == 0u || (e & PTE_LEAF) != 0u) { continue; }
            u64 *l0 = (u64 *)PTE_PPN(e);
            for (u32 k = 0; k < 512u; k++) {
                u64 e0 = l0[k];
                if ((e0 & PTE_V) == 0u || (e0 & PTE_U) == 0u) { continue; }
                pmm_free(PTE_PPN(e0) & PAGE_MASK);
            }
            pmm_free((u64)l0);
        }
        pmm_free((u64)l1);
    }
    pmm_free((u64)root);
}

ifirm_status_t vmm_init(void)
{
    g_kernel_root = vmm_new_as();
    if (g_kernel_root == NULL) { return IFIRM_ERR_NO_MEMORY; }
    return vmm_switch(g_kernel_root);
}
