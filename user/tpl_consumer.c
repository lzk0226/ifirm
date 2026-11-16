#include "ulib.h"

struct req { u64 op; u64 seq; u64 x; };

void _start(u64 chan, u64 con, u64 inst)
{
    (void)con;
    line_t l;
    l.n = 0;

    for (u64 i = 0u; i < 3u; i++) {
        struct req q;
        i64 st = sys5(SYS_IPC_RECV, (i64)chan, (i64)&q, 24, 0, 0);
        if (st != 0) {
            l_tag(&l, "consumer", inst);
            l_str(&l, "recv 失败 st="); l_i64(&l, st);
            l_flush(&l);
            break;
        }

        u64 res = q.x * 2u;
        l_tag(&l, "consumer", inst);
        l_str(&l, "seq="); l_u64(&l, q.seq);
        l_str(&l, " x=");  l_u64(&l, q.x);
        l_str(&l, " -> "); l_u64(&l, res);
        l_flush(&l);

        u64 reply = res;
        (void)sys5(SYS_IPC_REPLY, (i64)chan, (i64)&reply, 8, 0, 0);
    }

    l_tag(&l, "consumer", inst);
    l_str(&l, "exit");
    l_flush(&l);
    (void)sys5(SYS_EXIT, 0, 0, 0, 0, 0);
    for (;;) { }
}
