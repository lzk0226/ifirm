#include "ulib.h"

struct req { u64 op; u64 seq; u64 x; };

void _start(u64 chan, u64 con, u64 inst)
{
    line_t l;
    l.n = 0;

    /* 经 ICT 槽 0 进内核：本实例的槽带 console 能力（安装期检查过）
       整行含换行一次发出，避免被时钟片切成两半 */
    l_tag(&l, "producer", inst);
    l_str(&l, "ict=ok");
    l_nl(&l);
    i64 n = ict_write(l.b, l.n);
    l.n = 0;
    if (n < 0) {
        l_tag(&l, "producer", inst);
        l_str(&l, "ict 失败 st=");
        l_i64(&l, n);
        l_flush(&l);
    }

    if (con != 0u) {
        /* 运行期向 CA 申请能力：只有 charter 里 CA_RUNTIME 的规则能过 */
        i64 h2 = 0;
        i64 st = sys5_ret1(SYS_CAP_REQUEST, OBJ_DEVICE, CAP_WRITE, 1, (i64)(u64)&h2);
        l_tag(&l, "producer", inst);
        l_str(&l, "ca grant st="); l_i64(&l, st);
        l_str(&l, " cap=");        l_u64(&l, (u64)h2);
        l_flush(&l);

        /* 撤销 console 能力：ICT 槽必须同步失效 */
        i64 rv = sys5(SYS_CAP_REVOKE, (i64)con, 0, 0, 0, 0);
        l_tag(&l, "producer", inst);
        l_str(&l, "revoke con="); l_i64(&l, rv);
        l_flush(&l);

        i64 again = ict_write("producer: ict after revoke\n", 26);
        l_tag(&l, "producer", inst);
        l_str(&l, "ict after revoke="); l_i64(&l, again);
        l_flush(&l);
    }

    for (u64 i = 0u; i < 3u; i++) {
        struct req q;
        q.op = 1u; q.seq = i; q.x = i * 7u + 1u;
        u64 reply = 0u;
        i64 st = sys5(SYS_IPC_CALL, (i64)chan, (i64)&q, 24, (i64)&reply, 8);
        if (st != 0) {
            l_tag(&l, "producer", inst);
            l_str(&l, "call 失败 st="); l_i64(&l, st);
            l_flush(&l);
            break;
        }
        l_tag(&l, "producer", inst);
        l_str(&l, "seq=");   l_u64(&l, i);
        l_str(&l, " x=");    l_u64(&l, q.x);
        l_str(&l, " reply="); l_u64(&l, reply);
        l_flush(&l);
    }

    if (con != 0u) {
        /* 撤销通道能力，再调用一次：旧的句柄必须被代次挡住 */
        i64 rv2 = sys5(SYS_CAP_REVOKE, (i64)chan, 0, 0, 0, 0);
        struct req q;
        q.op = 1u; q.seq = 9u; q.x = 0u;
        u64 rep = 0u;
        i64 st = sys5(SYS_IPC_CALL, (i64)chan, (i64)&q, 24, (i64)&rep, 8);
        l_tag(&l, "producer", inst);
        l_str(&l, "revoke chan="); l_i64(&l, rv2);
        l_flush(&l);

        l_tag(&l, "producer", inst);
        l_str(&l, "call after revoke="); l_i64(&l, st);
        l_flush(&l);
    }

    l_tag(&l, "producer", inst);
    l_str(&l, "exit");
    l_flush(&l);
    (void)sys5(SYS_EXIT, 0, 0, 0, 0, 0);
    for (;;) { }
}
