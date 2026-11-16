#include "ulib.h"

void _start(u64 chan, u64 con, u64 inst)
{
    (void)chan; (void)con; (void)inst;
    line_t l;
    l.n = 0;

    /* 向 CA 申请一个 charter 里根本没有的对象：必须被拒 */
    i64 st = sys5(SYS_CAP_REQUEST, 5, (1u << 10), 1, 0, 0);
    l_str(&l, "faulty: ca denied=");
    l_i64(&l, st);
    l_flush(&l);

    dbg("faulty: 即将写地址 0\n");
    *(volatile u64 *)0 = 1u;
    dbg("faulty: 这一行不应出现\n");
    for (;;) { }
}
