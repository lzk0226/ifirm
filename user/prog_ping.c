#include "ulib.h"

/* 第二个磁盘程序：只证明内核按名字从文件系统里取程序，没有给 hello 写死。 */
void _start(u64 chan, u64 con, u64 inst)
{
    (void)chan; (void)con; (void)inst;
    line_t l;
    l.n = 0;
    l_tag(&l, "ping", 7);
    l_str(&l, "alive from disk (elf)");
    l_flush(&l);

    l_tag(&l, "ping", 7);
    l_str(&l, "sum 1..10=");
    l_i64(&l, (i64)(1+2+3+4+5+6+7+8+9+10));
    l_flush(&l);

    (void)sys5(SYS_EXIT, 0, 0, 0, 0, 0);
    for (;;) { }
}
