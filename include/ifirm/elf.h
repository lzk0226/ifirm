#ifndef IFIRM_ELF_H
#define IFIRM_ELF_H

#include <ifirm/types.h>

typedef struct {
    u64 entry;
    u32 segs;
    u32 pages;
} elf_info_t;

ifirm_status_t elf_check(const u8 *img, u64 len);
ifirm_status_t elf_load(u64 *root, const u8 *img, u64 len, elf_info_t *info);

#endif /* IFIRM_ELF_H */
