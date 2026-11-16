#ifndef IFIRM_VMM_H
#define IFIRM_VMM_H

#include <ifirm/types.h>

/* Sv39 PTE 位（§10.3） */
#define PTE_V (1ull << 0)
#define PTE_R (1ull << 1)
#define PTE_W (1ull << 2)
#define PTE_X (1ull << 3)
#define PTE_U (1ull << 4)
#define PTE_G (1ull << 5)
#define PTE_A (1ull << 6)
#define PTE_D (1ull << 7)

#define SATP_MODE_SV39 (8ull << 60)
#define SATP_PPN_MASK  0x00000FFFFFFFFFFFull

extern u64 *g_kernel_root;

ifirm_status_t vmm_init(void);
u64           *vmm_new_as(void);
ifirm_status_t vmm_map(u64 *root, vaddr_t va, paddr_t pa, u64 perm);
ifirm_status_t vmm_switch(u64 *root);
void           vmm_flush(void);
u64            vmm_walk(u64 *root, vaddr_t va);
u64            vmm_walk_pte(u64 *root, vaddr_t va);

#endif /* IFIRM_VMM_H */
