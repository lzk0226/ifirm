#include "ulib.h"

/* 这个程序不编进内核：它单独链接成 ELF，由 tools/mkfs 打进磁盘映像，
 * 内核在运行时从 virtio-blk 读出来装进一个新域。 */
void _start(u64 chan, u64 con, u64 inst)
{
    (void)chan; (void)con; (void)inst;
    line_t l;
    l.n = 0;

    l_tag(&l, "hello", 1);
    l_str(&l, "loaded from disk (elf)");
    l_flush(&l);

    l_tag(&l, "hello", 1);
    l_str(&l, "tick=");
    u64 ns = 0u;
    l_i64(&l, sys5(SYS_CLOCK, (u64)&ns, 0, 0, 0, 0));
    l_flush(&l);

    l_tag(&l, "hello", 1);
    l_str(&l, "bye");
    l_flush(&l);

    (void)sys5(SYS_EXIT, 0, 0, 0, 0, 0);
    for (;;) { }
}
