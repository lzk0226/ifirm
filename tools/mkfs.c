/* mkfs：生成 IFIRMFS2 磁盘映像（宿主工具，不进内核）
 * usage: mkfs <out.img> [path=hostfile ...]
 * 目录由文件路径隐式创建：/bin/sh=build/sh.elf 会先建 /bin。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define BLOCK        512u
#define TOTAL_BLOCKS 8192u          /* 4 MiB */
#define MAX_INODES   64u
#define INODE_SIZE   64u
#define NAME_LEN     16u
#define DIRECT       7u
#define PTRS         128u
#define MAX_FILE     ((uint32_t)DIRECT * BLOCK + (uint32_t)PTRS * BLOCK)
#define BITMAP_LBA   1u
#define INODE_LBA    5u
#define DATA_LBA     13u

typedef struct {
    uint8_t  type, used;
    uint16_t nlink;
    uint32_t size;
    uint32_t parent;
    uint32_t blocks[DIRECT];
    uint32_t indirect;
    char     name[NAME_LEN];
    uint32_t mtime;
} inode_t;

static inode_t inodes[MAX_INODES];
static uint8_t bitmap[TOTAL_BLOCKS / 8u];
static FILE   *img;
static uint32_t nfiles, nbytes;

static void mark(uint32_t b, int used)
{
    uint8_t bit = (uint8_t)(1u << (b % 8u));
    if (used) { bitmap[b / 8u] |= bit; } else { bitmap[b / 8u] &= (uint8_t)~bit; }
}

static int used(uint32_t b) { return (bitmap[b / 8u] & (1u << (b % 8u))) != 0u; }

static void put_at(uint32_t off, const void *p, size_t n)
{
    fseek(img, (long)off, SEEK_SET);
    if (n > 0 && fwrite(p, 1, n, img) != n) { perror("write"); exit(1); }
}

static uint32_t alloc_block(void)
{
    for (uint32_t b = DATA_LBA; b < TOTAL_BLOCKS; b++) {
        if (!used(b)) { mark(b, 1); return b; }
    }
    fprintf(stderr, "mkfs: disk full\n");
    exit(1);
}

static inode_t *find(uint32_t parent, const char *name)
{
    for (uint32_t i = 1; i < MAX_INODES; i++) {
        if (inodes[i].type == 0 || inodes[i].parent != parent) { continue; }
        if (!strncmp(inodes[i].name, name, NAME_LEN)) { return &inodes[i]; }
    }
    return NULL;
}

static uint32_t ino_of(inode_t *e) { return (uint32_t)(e - inodes); }

static inode_t *mknode(uint32_t parent, const char *name, uint8_t type)
{
    for (uint32_t i = 1; i < MAX_INODES; i++) {
        if (inodes[i].type != 0) { continue; }
        memset(&inodes[i], 0, sizeof inodes[i]);
        inodes[i].type = type;
        inodes[i].used = 1;
        inodes[i].nlink = 1;
        inodes[i].parent = parent;
        strncpy(inodes[i].name, name, NAME_LEN - 1);   /* 定长名字，超长截断 */
        return &inodes[i];
    }
    fprintf(stderr, "mkfs: out of inodes\n");
    exit(1);
}

/* 逐段创建路径，返回最后一级 inode；type 只在创建时用 */
static inode_t *ensure(const char *path, uint8_t type, int create_last)
{
    char buf[128];
    snprintf(buf, sizeof buf, "%s", path);
    uint32_t cur = 1;               /* root */
    char *p = buf;
    while (*p == '/') { p++; }
    if (*p == '\0') { return &inodes[1]; }

    for (;;) {
        char *slash = strchr(p, '/');
        if (slash != NULL) { *slash = '\0'; }
        inode_t *e = find(cur, p);
        if (e == NULL) {
            if (slash == NULL && !create_last) { return NULL; }
            e = mknode(cur, p, (slash == NULL) ? type : 2u);
        }
        cur = ino_of(e);
        if (slash == NULL) { return e; }
        if (e->type != 2u) { return NULL; }
        p = slash + 1;
        while (*p == '/') { p++; }
        if (*p == '\0') { return e; }
    }
}

/* 把文件内第 bi 块映射到磁盘块号，必要时分配（与内核 fs.c 的 inode_block 同构） */
static uint32_t map(inode_t *e, uint32_t bi)
{
    if (bi < DIRECT) {
        if (e->blocks[bi] == 0u) { e->blocks[bi] = alloc_block(); }
        return e->blocks[bi];
    }
    uint32_t k = bi - DIRECT;
    if (k >= PTRS) { fprintf(stderr, "mkfs: file too big\n"); exit(1); }
    if (e->indirect == 0u) { e->indirect = alloc_block(); }
    uint32_t ptrs[PTRS];
    memset(ptrs, 0, sizeof ptrs);
    fseek(img, (long)e->indirect * BLOCK, SEEK_SET);
    if (fread(ptrs, 1, sizeof ptrs, img) != sizeof ptrs) { memset(ptrs, 0, sizeof ptrs); }
    if (ptrs[k] == 0u) {
        ptrs[k] = alloc_block();
        put_at(e->indirect * BLOCK, ptrs, sizeof ptrs);
    }
    return ptrs[k];
}

static void write_data(inode_t *e, const uint8_t *data, uint32_t n)
{
    uint32_t blocks = (n + BLOCK - 1u) / BLOCK;
    for (uint32_t i = 0; i < blocks; i++) {
        uint32_t b = map(e, i);
        uint32_t len = n - i * BLOCK;
        if (len > BLOCK) { len = BLOCK; }
        put_at(b * BLOCK, data + i * BLOCK, len);
    }
    e->size = n;
    nfiles++;
    nbytes += n;
}

static void add_file(const char *path, const char *host)
{
    FILE *f = fopen(host, "rb");
    if (f == NULL) { perror(host); exit(1); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || (size_t)n > MAX_FILE) {
        fprintf(stderr, "mkfs: %s too big (%ld, max %u)\n", path, n, MAX_FILE);
        exit(1);
    }
    inode_t *e = ensure(path, 1u, 1);
    if (e == NULL || e->type != 1u) { fprintf(stderr, "mkfs: bad path %s\n", path); exit(1); }

    uint8_t *buf = calloc(1, (size_t)n + 1u);
    if (buf == NULL) { perror("calloc"); exit(1); }
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) { perror(host); exit(1); }
    fclose(f);

    write_data(e, buf, (uint32_t)n);
    free(buf);
}

static void add_text(const char *path, const char *text)
{
    inode_t *e = ensure(path, 1u, 1);
    write_data(e, (const uint8_t *)text, (uint32_t)strlen(text));
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: mkfs <out.img> [path=hostfile ...]\n"); return 2; }

    memset(inodes, 0, sizeof inodes);
    memset(bitmap, 0, sizeof bitmap);
    for (uint32_t b = 0; b < DATA_LBA; b++) { mark(b, 1); }

    inodes[1].type = 2u; inodes[1].used = 1; inodes[1].nlink = 1;
    inodes[1].parent = 0u; snprintf(inodes[1].name, NAME_LEN, "/");

    img = fopen(argv[1], "wb+");
    if (img == NULL) { perror(argv[1]); return 1; }

    /* 清成 0，再写元数据 */
    uint8_t zero[BLOCK];
    memset(zero, 0, sizeof zero);
    for (uint32_t b = 0; b < TOTAL_BLOCKS; b++) { put_at(b * BLOCK, zero, BLOCK); }

    /* 预置目录 */
    ensure("/bin", 2u, 1);
    ensure("/tmp", 2u, 1);
    ensure("/etc", 2u, 1);
    ensure("/dev", 2u, 1);

    add_text("/etc/motd",
             "Ifirm 1.0 minimal system: CA + composite kernel + atomic templates\n"
             "type 'help' for commands, 'ls /bin' for programs\n");

    for (int i = 2; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq == NULL) { fprintf(stderr, "mkfs: bad arg %s\n", argv[i]); return 2; }
        *eq = '\0';
        add_file(argv[i], eq + 1);
    }

    uint8_t sb[BLOCK];
    memset(sb, 0, sizeof sb);
    memcpy(sb, "IFIRMFS2", 8);
#define W32(off, v) do { uint32_t _v = (uint32_t)(v); memcpy(sb + (off), &_v, 4); } while (0)
    W32(8, 2u);
    W32(12, MAX_INODES);
    W32(16, BITMAP_LBA);
    W32(20, 2u);
    W32(24, INODE_LBA);
    W32(28, 8u);
    W32(32, DATA_LBA);
    W32(36, TOTAL_BLOCKS);
    W32(40, 1u);
    W32(44, nfiles);
    W32(48, nbytes);
    uint32_t freeb = 0;
    for (uint32_t b = DATA_LBA; b < TOTAL_BLOCKS; b++) { if (!used(b)) { freeb++; } }
    W32(52, freeb);
    put_at(0, sb, BLOCK);
    put_at(BITMAP_LBA * BLOCK, bitmap, sizeof bitmap);
    put_at(INODE_LBA * BLOCK, inodes, sizeof inodes);
    fclose(img);

    printf("mkfs: %s files=%u bytes=%u free_blocks=%u\n", argv[1], nfiles, nbytes, freeb);
    return 0;
}
