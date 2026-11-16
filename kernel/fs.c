#include <ifirm/fs.h>
#include <ifirm/virtio.h>
#include <ifirm/syscall.h>
#include <ifirm/console.h>
#include <ifirm/status.h>

#define FS_SB_LBA        0u
#define FS_BITMAP_LBA    1u
#define FS_BITMAP_SECT   2u
#define FS_INODE_LBA     5u
#define FS_INODE_SECT    8u
#define FS_DATA_LBA      13u
#define FS_TOTAL_BLOCKS  8192u

static u8  g_sb[FS_BLOCK];
static u8  g_bitmap[FS_BITMAP_BYTES];
static u8  g_inodes[FS_MAX_INODES * FS_INODE_SIZE];
static u32 g_nfiles;
static u32 g_bytes;

typedef struct {
    u8   type;
    u8   used;
    u16  nlink;
    u32  size;
    u32  parent;
    u32  blocks[FS_DIRECT];
    u32  indirect;
    char name[FS_NAME_LEN];
    u32  mtime;
} inode_t;

STATIC_ASSERT(sizeof(inode_t) == FS_INODE_SIZE, "inode frozen");

static inode_t *ino_at(u32 i)
{
    return (inode_t *)(void *)(g_inodes + (u64)i * FS_INODE_SIZE);
}

static void zero(void *p, u32 n)
{
    u8 *b = (u8 *)p;
    for (u32 i = 0; i < n; i++) { b[i] = 0u; }
}

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void wr32(u8 *p, u32 v)
{
    p[0] = (u8)(v & 0xFFu); p[1] = (u8)((v >> 8) & 0xFFu);
    p[2] = (u8)((v >> 16) & 0xFFu); p[3] = (u8)((v >> 24) & 0xFFu);
}

static bool name_eq(const char *a, const char *b)
{
    for (u32 i = 0; i < FS_NAME_LEN; i++) {
        if (a[i] != b[i]) { return false; }
        if (a[i] == '\0') { return true; }
    }
    return true;
}

static void name_set(char *dst, const char *src)
{
    u32 i = 0;
    for (; i + 1u < FS_NAME_LEN && src[i] != '\0'; i++) { dst[i] = src[i]; }
    for (; i < FS_NAME_LEN; i++) { dst[i] = '\0'; }
}

/* ---------- 落盘 ---------- */
static ifirm_status_t sb_flush(void)
{
    wr32(g_sb + 44u, g_nfiles);
    wr32(g_sb + 48u, g_bytes);
    wr32(g_sb + 52u, fs_free_blocks());
    return virtio_blk_write(FS_SB_LBA, 1u, g_sb);
}

static ifirm_status_t bitmap_flush(void)
{
    return virtio_blk_write(FS_BITMAP_LBA, FS_BITMAP_SECT, g_bitmap);
}

static ifirm_status_t inode_flush(u32 ino)
{
    u32 off = ino * FS_INODE_SIZE;
    u32 sect = off / FS_BLOCK;
    return virtio_blk_write(FS_INODE_LBA + sect, 1u, g_inodes + (u64)sect * FS_BLOCK);
}

static bool block_used(u32 b)
{
    return (g_bitmap[b / 8u] & (u8)(1u << (b % 8u))) != 0u;
}

static void block_mark(u32 b, bool used)
{
    u8 bit = (u8)(1u << (b % 8u));
    if (used) { g_bitmap[b / 8u] |= bit; }
    else      { g_bitmap[b / 8u] &= (u8)~bit; }
}

u32 fs_free_blocks(void)
{
    u32 n = 0;
    for (u32 b = FS_DATA_LBA; b < FS_TOTAL_BLOCKS; b++) {
        if (!block_used(b)) { n++; }
    }
    return n;
}

static u32 block_alloc(void)
{
    for (u32 b = FS_DATA_LBA; b < FS_TOTAL_BLOCKS; b++) {
        if (!block_used(b)) {
            block_mark(b, true);
            (void)bitmap_flush();
            u8 z[FS_BLOCK];
            zero(z, FS_BLOCK);
            (void)virtio_blk_write(b, 1u, z);
            return b;
        }
    }
    return 0u;
}

static void block_free(u32 b)
{
    if (b < FS_DATA_LBA || b >= FS_TOTAL_BLOCKS) { return; }
    block_mark(b, false);
    (void)bitmap_flush();
}

static ifirm_status_t block_read(u32 b, void *dst)
{
    return virtio_blk_read(b, 1u, dst);
}

static ifirm_status_t block_write(u32 b, const void *src)
{
    return virtio_blk_write(b, 1u, src);
}

/* 把文件内第 bi 块映射到磁盘块号；bi 超过 7 时走一级间接块。
   alloc=false 且该块尚未分配时返回 0。 */
static ifirm_status_t inode_block(inode_t *e, u32 bi, bool alloc, u32 *out)
{
    *out = 0u;
    if (bi < FS_DIRECT) {
        if (e->blocks[bi] == 0u) {
            if (!alloc) { return IFIRM_OK; }
            u32 b = block_alloc();
            if (b == 0u) { return IFIRM_ERR_NO_SPACE; }
            e->blocks[bi] = b;
        }
        *out = e->blocks[bi];
        return IFIRM_OK;
    }

    u32 k = bi - FS_DIRECT;
    if (k >= FS_PTRS) { return IFIRM_ERR_NO_SPACE; }
    if (e->indirect == 0u) {
        if (!alloc) { return IFIRM_OK; }
        u32 b = block_alloc();
        if (b == 0u) { return IFIRM_ERR_NO_SPACE; }
        e->indirect = b;
    }

    u8 ib[FS_BLOCK];
    ifirm_status_t st = block_read(e->indirect, ib);
    if (st != IFIRM_OK) { return st; }
    u32 b = rd32(ib + k * 4u);
    if (b == 0u) {
        if (!alloc) { return IFIRM_OK; }
        b = block_alloc();
        if (b == 0u) { return IFIRM_ERR_NO_SPACE; }
        wr32(ib + k * 4u, b);
        st = block_write(e->indirect, ib);
        if (st != IFIRM_OK) { return st; }
    }
    *out = b;
    return IFIRM_OK;
}

/* 释放一个文件占用的全部块（直接块 + 间接块里的块 + 间接块自身） */
static void inode_free_blocks(inode_t *e)
{
    for (u32 i = 0; i < FS_DIRECT; i++) {
        if (e->blocks[i] != 0u) { block_free(e->blocks[i]); e->blocks[i] = 0u; }
    }
    if (e->indirect != 0u) {
        u8 ib[FS_BLOCK];
        if (block_read(e->indirect, ib) == IFIRM_OK) {
            for (u32 k = 0; k < FS_PTRS; k++) {
                u32 b = rd32(ib + k * 4u);
                if (b != 0u) { block_free(b); }
            }
        }
        block_free(e->indirect);
        e->indirect = 0u;
    }
}

/* ---------- 初始化 ---------- */
ifirm_status_t fs_init(void)
{
    ifirm_status_t st = virtio_blk_init();
    if (st != IFIRM_OK) { return st; }

    st = virtio_blk_read(FS_SB_LBA, 1u, g_sb);
    if (st != IFIRM_OK) { return st; }

    static const char magic[8] = { 'I', 'F', 'I', 'R', 'M', 'F', 'S', '2' };
    for (u32 i = 0; i < 8u; i++) {
        if (g_sb[i] != (u8)magic[i]) { return IFIRM_ERR_BAD_DIGEST; }
    }
    if (rd32(g_sb + 8u) != FS_VERSION) { return IFIRM_ERR_UNSUPPORTED; }
    if (rd32(g_sb + 12u) != FS_MAX_INODES) { return IFIRM_ERR_CORRUPT; }

    st = virtio_blk_read(FS_BITMAP_LBA, FS_BITMAP_SECT, g_bitmap);
    if (st != IFIRM_OK) { return st; }
    st = virtio_blk_read(FS_INODE_LBA, FS_INODE_SECT, g_inodes);
    if (st != IFIRM_OK) { return st; }

    g_nfiles = rd32(g_sb + 44u);
    g_bytes = rd32(g_sb + 48u);
    if (g_nfiles > FS_MAX_INODES) { return IFIRM_ERR_CORRUPT; }
    return IFIRM_OK;
}

u32 fs_files(void) { return g_nfiles; }
u32 fs_bytes(void) { return g_bytes; }

/* ---------- 路径 ---------- */
/* 返回 (父目录 ino, 叶子名)；叶子名写进 leaf[FS_NAME_LEN] */
static ifirm_status_t split_path(const char *path, u32 *parent, char *leaf)
{
    if (path == NULL || path[0] != '/') { return IFIRM_ERR_INVALID_ARG; }
    u32 cur = rd32(g_sb + 40u);      /* root */
    const char *p = path;
    char comp[FS_NAME_LEN];

    while (*p == '/') { p++; }
    if (*p == '\0') { return IFIRM_ERR_INVALID_ARG; }

    for (;;) {
        u32 n = 0;
        while (*p != '\0' && *p != '/') {
            if (n + 1u >= FS_NAME_LEN) { return IFIRM_ERR_INVALID_ARG; }
            comp[n++] = *p++;
        }
        comp[n] = '\0';
        while (*p == '/') { p++; }

        if (*p == '\0') {           /* 最后一段：叶子 */
            *parent = cur;
            name_set(leaf, comp);
            return IFIRM_OK;
        }

        /* 中间段必须是已存在的目录 */
        if (comp[0] == '.' && comp[1] == '\0') { continue; }
        if (comp[0] == '.' && comp[1] == '.' && comp[2] == '\0') {
            inode_t *d = ino_at(cur);
            cur = (d->parent != 0u) ? d->parent : cur;
            continue;
        }
        u32 nxt = 0;
        for (u32 i = 1u; i < FS_MAX_INODES; i++) {
            inode_t *e = ino_at(i);
            if (e->type == FS_TYPE_FREE || e->parent != cur) { continue; }
            if (name_eq(e->name, comp)) { nxt = i; break; }
        }
        if (nxt == 0u) { return IFIRM_ERR_NOT_FOUND; }
        if (ino_at(nxt)->type != FS_TYPE_DIR) { return IFIRM_ERR_INVALID_ARG; }
        cur = nxt;
    }
}

static i64 lookup_ino(const char *path)
{
    u32 root = rd32(g_sb + 40u);
    if (path != NULL && path[0] == '/' && path[1] == '\0') { return (i64)root; }

    u32 parent = 0u;
    char leaf[FS_NAME_LEN];
    ifirm_status_t st = split_path(path, &parent, leaf);
    if (st != IFIRM_OK) { return (i64)st; }

    for (u32 i = 1u; i < FS_MAX_INODES; i++) {
        inode_t *e = ino_at(i);
        if (e->type == FS_TYPE_FREE || e->parent != parent) { continue; }
        if (name_eq(e->name, leaf)) { return (i64)i; }
    }
    return (i64)IFIRM_ERR_NOT_FOUND;
}

i64 fs_lookup(const char *path) { return lookup_ino(path); }

static i64 alloc_inode(u8 type, u32 parent, const char *name)
{
    for (u32 i = 1u; i < FS_MAX_INODES; i++) {
        inode_t *e = ino_at(i);
        if (e->type != FS_TYPE_FREE) { continue; }
        zero(e, FS_INODE_SIZE);
        e->type = type;
        e->used = 1u;
        e->nlink = 1u;
        e->parent = parent;
        name_set(e->name, name);
        e->mtime = (u32)now_ms();
        ifirm_status_t st = inode_flush(i);
        if (st != IFIRM_OK) { return (i64)st; }
        if (type == FS_TYPE_FILE) {
            g_nfiles++;
            (void)sb_flush();
        }
        return (i64)i;
    }
    return (i64)IFIRM_ERR_NO_SPACE;
}

i64 fs_creat(const char *path, u8 type)
{
    u32 parent = 0u;
    char leaf[FS_NAME_LEN];
    ifirm_status_t st = split_path(path, &parent, leaf);
    if (st != IFIRM_OK) { return (i64)st; }
    if (ino_at(parent)->type != FS_TYPE_DIR) { return (i64)IFIRM_ERR_INVALID_ARG; }

    for (u32 i = 1u; i < FS_MAX_INODES; i++) {
        inode_t *e = ino_at(i);
        if (e->type == FS_TYPE_FREE || e->parent != parent) { continue; }
        if (name_eq(e->name, leaf)) { return (i64)i; }   /* 已存在：直接返回 */
    }
    return alloc_inode(type, parent, leaf);
}

i64 fs_mkdir(const char *path) { return fs_creat(path, FS_TYPE_DIR); }

i64 fs_unlink(const char *path)
{
    i64 r = lookup_ino(path);
    if (r < 0) { return r; }
    u32 ino = (u32)r;
    if (ino == rd32(g_sb + 40u)) { return (i64)IFIRM_ERR_INVALID_ARG; }  /* 根目录不可删除 */
    inode_t *e = ino_at(ino);
    if (e->type == FS_TYPE_DIR) {
        for (u32 i = 1u; i < FS_MAX_INODES; i++) {
            if (ino_at(i)->type != FS_TYPE_FREE && ino_at(i)->parent == ino) {
                return (i64)IFIRM_ERR_BUSY;      /* 目录非空 */
            }
        }
    }
    inode_free_blocks(e);
    if (e->type == FS_TYPE_FILE && g_nfiles > 0u) { g_nfiles--; }
    g_bytes = (g_bytes > e->size) ? g_bytes - e->size : 0u;
    zero(e, FS_INODE_SIZE);
    ifirm_status_t st = inode_flush(ino);
    if (st != IFIRM_OK) { return (i64)st; }
    st = sb_flush();
    return (st == IFIRM_OK) ? 0 : (i64)st;
}

i64 fs_read(u32 ino, u32 off, void *buf, u32 len)
{
    if (ino == 0u || ino >= FS_MAX_INODES) { return (i64)IFIRM_ERR_INVALID_ARG; }
    inode_t *e = ino_at(ino);
    if (e->type != FS_TYPE_FILE) { return (i64)IFIRM_ERR_INVALID_ARG; }
    if (off >= e->size) { return 0; }
    if (off + len > e->size) { len = e->size - off; }

    u8 *dst = (u8 *)buf;
    u32 done = 0;
    while (done < len) {
        u32 cur = off + done;
        u32 bi = cur / FS_BLOCK;
        u32 bo = cur % FS_BLOCK;
        u32 b = 0u;
        ifirm_status_t st = inode_block(e, bi, false, &b);
        if (st != IFIRM_OK) { return (i64)st; }
        u8 blk[FS_BLOCK];
        if (b == 0u) { zero(blk, FS_BLOCK); }
        else {
            st = block_read(b, blk);
            if (st != IFIRM_OK) { return (i64)st; }
        }
        u32 n = FS_BLOCK - bo;
        if (n > len - done) { n = len - done; }
        for (u32 i = 0; i < n; i++) { dst[done + i] = blk[bo + i]; }
        done += n;
    }
    return (i64)done;
}

i64 fs_write(u32 ino, u32 off, const void *buf, u32 len)
{
    if (ino == 0u || ino >= FS_MAX_INODES) { return (i64)IFIRM_ERR_INVALID_ARG; }
    inode_t *e = ino_at(ino);
    if (e->type != FS_TYPE_FILE) { return (i64)IFIRM_ERR_INVALID_ARG; }
    if ((u64)off + (u64)len > (u64)FS_MAX_FILE) { return (i64)IFIRM_ERR_NO_SPACE; }

    const u8 *src = (const u8 *)buf;
    u32 done = 0;
    while (done < len) {
        u32 cur = off + done;
        u32 bi = cur / FS_BLOCK;
        u32 bo = cur % FS_BLOCK;
        u32 b = 0u;
        ifirm_status_t st = inode_block(e, bi, true, &b);
        if (st != IFIRM_OK) { return (done > 0u) ? (i64)done : (i64)st; }
        u8 blk[FS_BLOCK];
        st = block_read(b, blk);
        if (st != IFIRM_OK) { return (i64)st; }
        u32 n = FS_BLOCK - bo;
        if (n > len - done) { n = len - done; }
        for (u32 i = 0; i < n; i++) { blk[bo + i] = src[done + i]; }
        /* 读出整块再写回：可能跨越块内已有内容 */
        st = block_write(b, blk);
        if (st != IFIRM_OK) { return (i64)st; }
        done += n;
    }
    if (off + done > e->size) {
        u32 old_size = e->size;
        e->size = off + done;
        g_bytes = g_bytes + (e->size - old_size);
        (void)inode_flush(ino);
        (void)sb_flush();
    }
    return (i64)done;
}

i64 fs_trunc(u32 ino)
{
    if (ino == 0u || ino >= FS_MAX_INODES) { return (i64)IFIRM_ERR_INVALID_ARG; }
    inode_t *e = ino_at(ino);
    if (e->type != FS_TYPE_FILE) { return (i64)IFIRM_ERR_INVALID_ARG; }
    inode_free_blocks(e);
    g_bytes = (g_bytes > e->size) ? g_bytes - e->size : 0u;
    e->size = 0u;
    ifirm_status_t st = inode_flush(ino);
    if (st != IFIRM_OK) { return (i64)st; }
    st = sb_flush();
    return (st == IFIRM_OK) ? 0 : (i64)st;
}

i64 fs_readdir(u32 dir_ino, u32 index, fs_dirent_t *out)
{
    if (dir_ino == 0u || dir_ino >= FS_MAX_INODES || out == NULL) {
        return (i64)IFIRM_ERR_INVALID_ARG;
    }
    if (ino_at(dir_ino)->type != FS_TYPE_DIR) { return (i64)IFIRM_ERR_INVALID_ARG; }
    u32 seen = 0;
    for (u32 i = 1u; i < FS_MAX_INODES; i++) {
        inode_t *e = ino_at(i);
        if (e->type == FS_TYPE_FREE || e->parent != dir_ino) { continue; }
        if (seen++ != index) { continue; }
        out->ino = i;
        out->size = e->size;
        out->type = e->type;
        name_set(out->name, e->name);
        return 0;
    }
    return (i64)IFIRM_ERR_NOT_FOUND;
}

i64 fs_stat(const char *path, fs_stat_t *out)
{
    i64 r = lookup_ino(path);
    if (r < 0 || out == NULL) { return (r < 0) ? r : (i64)IFIRM_ERR_INVALID_ARG; }
    inode_t *e = ino_at((u32)r);
    out->ino = (u32)r;
    out->size = e->size;
    out->parent = e->parent;
    out->mtime = e->mtime;
    out->type = e->type;
    return 0;
}

i64 fs_read_file(const char *path, void *dst, u32 cap, u32 *out_len)
{
    i64 r = lookup_ino(path);
    if (r < 0) { return r; }
    inode_t *e = ino_at((u32)r);
    if (e->type != FS_TYPE_FILE) { return (i64)IFIRM_ERR_INVALID_ARG; }
    if (e->size > cap) { return (i64)IFIRM_ERR_NO_SPACE; }
    i64 n = fs_read((u32)r, 0u, dst, e->size);
    if (n < 0) { return n; }
    if (out_len != NULL) { *out_len = (u32)n; }
    return 0;
}
