#include "ulib.h"

/* ping [次数]：用 CLOCK 计时，演示"程序自己决定跑多久" */
static int umain(int argc, char **argv)
{
    int n = (argc > 1) ? atoi10(argv[1]) : 3;
    if (n <= 0 || n > 20) { n = 3; }
    u64 t0 = now_ms();
    for (int i = 0; i < n; i++) {
        u64 t = now_ms() - t0;
        cprintf("ping: seq=%d t=%dms\n", (i64)i, (i64)t);
        u64 until = now_ms() + 20u;
        while (now_ms() < until) { (void)sys5(SYS_YIELD, 0, 0, 0, 0, 0); }
    }
    cprintf("ping: %d packets, %dms total\n", (i64)n, (i64)(now_ms() - t0));
    return 0;
}

USER_MAIN(umain)
