#ifndef IFIRM_FS_H
#define IFIRM_FS_H

#include <ifirm/types.h>

/* 极简只读文件系统：4 KiB 头 + 连续文件区，LBA 512 字节
 * 头：magic[8] "IFIRMFS1" | version u32 | nfiles u32 | bytes u32 | reserved u32
 * 表项从偏移 32 开始，每项 24 字节：name[16] | lba u32 | size u32
 */
#define FS_MAGIC     "IFIRMFS1"
#define FS_MAX_FILES 32u
#define FS_NAME_LEN  16u

typedef struct {
    char name[FS_NAME_LEN];
    u32  lba;
    u32  size;
} fs_entry_t;

ifirm_status_t fs_init(void);
u32            fs_count(void);
u32            fs_bytes(void);
ifirm_status_t fs_lookup(const char *name, fs_entry_t *out);
ifirm_status_t fs_read(const char *name, void *dst, u32 cap, u32 *out_len);

#endif /* IFIRM_FS_H */
