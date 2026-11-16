#include "ulib.h"

/* 这个程序不编进内核：它单独链接成 ELF，由 mkfs 打进磁盘映像，
 * 内核在运行时从 virtio-blk 读出来，装进一个新域执行。 */
static int umain(int argc, char **argv)
{
    cprintf("hello: loaded from disk (elf), argc=%d\n", (i64)argc);
    for (int i = 0; i < argc; i++) { cprintf("hello: argv[%d]=%s\n", (i64)i, argv[i]); }
    cprintf("hello: t=%dms\n", (i64)now_ms());
    return 0;
}

USER_MAIN(umain)
