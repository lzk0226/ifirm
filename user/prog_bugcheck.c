#include "ulib.h"

/* 回归测试程序：只依赖公开 ABI，故意用坏输入打内核。
 * 期望（修复后）：坏 ELF 返回 -14 且不 panic；失败不泄漏域槽；
 * 根目录不可删；跨页用户指针返回 -2。
 * 做法：把 /bin/hello 分块复制成 /tmp/bad*，再按偏移改几个字节，
 * 不把整个 ELF 读进用户态缓冲。 */

#define CHUNK 512u
#define STACK_TOP 0x20014000ull

static u8 g_buf[CHUNK];

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u64 rd64(const u8 *p)
{
    return (u64)rd32(p) | ((u64)rd32(p + 4) << 32);
}

static void wr64(u8 *p, u64 v)
{
    for (u32 i = 0; i < 8u; i++) { p[i] = (u8)(v >> (i * 8u)); }
}

/* 从 src 拷到 dst（512 字节一块），返回 0 或负错误码 */
static i64 copy_file(i64 src, i64 dst)
{
    u64 off = 0;
    for (;;) {
        i64 n = f_read(src, g_buf, CHUNK, off);
        if (n < 0) { return n; }
        if (n == 0) { break; }
        i64 w = f_write(dst, g_buf, (u64)n, off);
        if (w != n) { return -1; }
        off += (u64)n;
    }
    return (i64)off;
}

/* 复制 /bin/hello 到 path，并按 mode 改坏：0=ELF 头偏移溢出，1=段偏移越界 */
static i64 make_bad(const char *path, u32 mode)
{
    i64 src = f_open("/bin/hello", FS_OPEN_READ);
    if (src < 0) { return src; }
    i64 dst = f_open(path, FS_OPEN_CREATE | FS_OPEN_TRUNC);
    if (dst < 0) { return dst; }
    i64 n = copy_file(src, dst);
    if (n < 0) { return n; }

    if (mode == 0u) {
        /* e_phoff = -16：旧代码 phoff + phnum*56 回绕，随后读野指针 */
        u8 v[8];
        wr64(v, 0xFFFFFFFFFFFFFFF0ull);
        return (f_write(dst, v, 8, 32) == 8) ? 0 : -1;
    }

    /* 读 ELF 头与程序头，找第一个 PT_LOAD（52 字节头 + phoff 在 32） */
    if (f_read(dst, g_buf, 64, 0) != 64) { return -1; }
    u64 phoff = rd64(g_buf + 32);
    u32 phnum = (u32)(g_buf[56] | ((u32)g_buf[57] << 8));
    for (u32 i = 0; i < phnum; i++) {
        u64 po = phoff + (u64)i * 56u;
        if (f_read(dst, g_buf, 56, po) != 56) { return -1; }
        if (rd32(g_buf) == 1u) {
            u8 v[8];
            wr64(v, 0x10000000ull);   /* 文件偏移远大于长度 */
            return (f_write(dst, v, 8, po + 8u) == 8) ? 0 : -1;
        }
    }
    return -1;
}

static int umain(int argc, char **argv)
{
    (void)argc; (void)argv;
    i64 st;

    /* 1) ELF 头溢出：修复前会读越界（可能 panic） */
    st = make_bad("/tmp/badph", 0u);
    cprintf("bugcheck: make ph st=%d\n", st);
    st = p_spawn("/tmp/badph", 0, 0);
    cprintf("bugcheck: badph st=%d\n", st);

    /* 2) 装载失败 12 次：必须每次回收域槽，之后好程序还能起来 */
    st = make_bad("/tmp/badseg", 1u);
    cprintf("bugcheck: make seg st=%d\n", st);
    for (int i = 0; i < 12; i++) { st = p_spawn("/tmp/badseg", 0, 0); }
    cprintf("bugcheck: badseg last=%d\n", st);

    st = p_spawn("/bin/hello", 0, 0);
    if (st > 0) {
        /* 先等 hello 打完，否则两个域的输出会交错，测试无法按行断言 */
        (void)p_wait(st);
        cprintf("bugcheck: good id=%d\n", st);
    } else {
        cprintf("bugcheck: good st=%d\n", st);
    }

    /* 3) 根目录不可删：返回 -2，而不是把根 inode 清零 */
    st = f_unlink("/");
    cprintf("bugcheck: unlink-root st=%d\n", st);

    i64 ino = f_open("/etc/motd", FS_OPEN_READ);
    if (ino > 0) {
        char b[16];
        i64 n = f_read(ino, b, sizeof(b), 0);
        cprintf("bugcheck: fs n=%d\n", n);
    } else {
        cprintf("bugcheck: fs st=%d\n", ino);
    }

    /* 4) 栈顶前一字节 + 长度 2：第二字节落在未映射页，必须 -2 */
    st = sys5(SYS_CONSOLE_WRITE, (i64)(STACK_TOP - 1ull), 2, 0, 0, 0);
    cprintf("bugcheck: badptr st=%d\n", st);

    cprintf("bugcheck: DONE\n");
    return 0;
}

USER_MAIN(umain)
