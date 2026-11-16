#include "ulib.h"

struct req { u64 op; u64 seq; u64 x; };

void _start(u64 chan)
{
    line_t l;
    l.n = 0;

    for (;;) {
        struct req q;
        i64 st = sys5(SYS_IPC_RECV, (i64)chan, (i64)&q, 24, 0, 0);
        if (st != 0) { dbg("consumer: recv 失败\n"); break; }

        u64 res = q.x * 2u;
        l_str(&l, "consumer: seq="); l_u64(&l, q.seq);
        l_str(&l, " x=");            l_u64(&l, q.x);
        l_str(&l, " -> ");           l_u64(&l, res);
        l_flush(&l);

        u64 reply = res;
        (void)sys5(SYS_IPC_REPLY, (i64)chan, (i64)&reply, 8, 0, 0);
        if (q.op == 0u) { break; }
    }

    dbg("consumer: exit\n");
    (void)sys5(SYS_EXIT, 0, 0, 0, 0, 0);
    for (;;) { }
}
