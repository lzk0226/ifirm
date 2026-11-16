#ifndef IFIRM_PMM_H
#define IFIRM_PMM_H

#include <ifirm/types.h>

#define PMM_MAX_PAGES 65536u            /* 256 MiB / 4 KiB */

void pmm_init(u64 ram_base, u64 ram_size, u64 kernel_end);
u64  pmm_alloc(void);                   /* 失败返回 0 */
void pmm_free(u64 pa);
u32  pmm_total(void);
u32  pmm_free_count(void);
u32  pmm_used(void);
u32  pmm_frag_permille(void);           /* 最大连续空闲段占比，0..1000 */

#endif /* IFIRM_PMM_H */
