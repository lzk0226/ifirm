#include "ulib.h"

void _start(u64 chan)
{
    (void)chan;
    dbg("faulty: 即将写地址 0\n");
    *(volatile u64 *)0 = 1u;
    dbg("faulty: 这一行不应出现\n");
    for (;;) { }
}
