#ifndef IFIRM_FS_H
#define IFIRM_FS_H

#include <ifirm/types.h>

/*
 * IFIRMFS2：可读写的极简文件系统（512 字节块，块位图 + 定长 inode 表 + 直接块）。
 *
 *   LBA 0        超级块
 *   LBA 1..2     块位图（8192 个块）
 *   LBA 5..12    inode 表（64 个 inode × 64 字节）
 *   LBA 13..     数据块
 *
 * inode 64 字节：type u8 | used u8 | nlink u16 | size u32 | parent u32 |
 *                blocks[7] u32 | indirect u32 | name[16] | mtime u32
 * 目录就是「parent 指向自己」的一组 inode，寻址靠线性扫描（最小系统够用）。
 * 单个文件最大 7 个直接块 + 128 个间接块 = 67.5 KiB（够放一个 ELF）。
 */
#define FS_MAGIC        "IFIRMFS2"
#define FS_VERSION      2u
#define FS_BLOCK        512u
#define FS_MAX_INODES   64u
#define FS_INODE_SIZE   64u
#define FS_NAME_LEN     16u
#define FS_DIRECT       7u      /* 前 7 个块号直接放在 inode 里 */
#define FS_PTRS         128u    /* 一级间接块：128 个 32 位块号 */
#define FS_MAX_FILE     ((FS_DIRECT * FS_BLOCK) + (FS_PTRS * FS_BLOCK))
#define FS_BITMAP_BYTES 1024u
#define FS_MAX_BLOCKS   (FS_BITMAP_BYTES * 8u)

#define FS_TYPE_FREE 0u
#define FS_TYPE_FILE 1u
#define FS_TYPE_DIR  2u

#define FS_OPEN_READ   0x1u
#define FS_OPEN_CREATE 0x2u
#define FS_OPEN_TRUNC  0x4u

typedef struct {
    u32  ino;
    u32  size;
    u8   type;
    char name[FS_NAME_LEN];
} fs_dirent_t;

STATIC_ASSERT(sizeof(fs_dirent_t) == 28u, "dirent frozen");

typedef struct {
    u32 ino;
    u32 size;
    u32 parent;
    u32 mtime;
    u8  type;
} fs_stat_t;

ifirm_status_t fs_init(void);

u32  fs_files(void);
u32  fs_bytes(void);
u32  fs_free_blocks(void);

i64  fs_lookup(const char *path);
i64  fs_read_file(const char *path, void *dst, u32 cap, u32 *out_len);
i64  fs_creat(const char *path, u8 type);
i64  fs_mkdir(const char *path);
i64  fs_unlink(const char *path);
i64  fs_read(u32 ino, u32 off, void *buf, u32 len);
i64  fs_write(u32 ino, u32 off, const void *buf, u32 len);
i64  fs_trunc(u32 ino);
i64  fs_readdir(u32 dir_ino, u32 index, fs_dirent_t *out);
i64  fs_stat(const char *path, fs_stat_t *out);

#endif /* IFIRM_FS_H */
