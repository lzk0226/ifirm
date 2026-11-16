#include "ulib.h"

struct req { u64 op; u64 seq; u64 x; };

void _start(u64 chan)
{
    line_t l;
    l.n = 0;

    i64 n = ict_write("producer: ict=ok\n", 18);
    if (n != 18) { dbg("producer: ict 失败\n"); }

    for (u64 i = 0u; i < 3u; i++) {
        struct req q;
        q.op = 1u; q.seq = i; q.x = i * 7u + 1u;
        u64 reply = 0u;
        i64 st = sys5(SYS_IPC_CALL, (i64)chan, (i64)&q, 24, (i64)&reply, 8);
        if (st != 0) {
            l_str(&l, "producer: call 失败 st=");
            l_u64(&l, (u64)(st < 0 ? -st : st));
            l_flush(&l);
            break;
        }
        l_str(&l, "producer: seq="); l_u64(&l, i);
        l_str(&l, " x=");            l_u64(&l, q.x);
        l_str(&l, " reply=");        l_u64(&l, reply);
        l_flush(&l);
    }

    struct req stop;
    stop.op = 0u; stop.seq = 3u; stop.x = 0u;
    u64 r = 0u;
    (void)sys5(SYS_IPC_CALL, (i64)chan, (i64)&stop, 24, (i64)&r, 8);

    dbg("producer: exit\n");
    (void)sys5(SYS_EXIT, 0, 0, 0, 0, 0);
    for (;;) { }
}
