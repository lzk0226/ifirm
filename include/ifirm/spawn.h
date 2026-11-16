#ifndef IFIRM_SPAWN_H
#define IFIRM_SPAWN_H

#include <ifirm/types.h>
#include <ifirm/elf.h>

/* 从磁盘映像里的 ELF 建一个新域：地址空间、ICT 页、用户栈、线程都由内核装配 */
ifirm_status_t domain_spawn_file(const char *name, u16 *out_id, elf_info_t *info);

#endif /* IFIRM_SPAWN_H */
