#include "ulib.h"

/* cat <文件>...：从文件系统读出来打到控制台 */
static char g_buf[512];

static int umain(int argc, char **argv)
{
    if (argc < 2) { cputs("usage: cat <file>\n"); return 1; }
    int rc = 0;
    for (int a = 1; a < argc; a++) {
        i64 ino = f_open(argv[a], FS_OPEN_READ);
        if (ino < 0) { cprintf("cat: %s: st=%d\n", argv[a], ino); rc = 1; continue; }
        u64 off = 0;
        for (;;) {
            i64 n = f_read(ino, g_buf, sizeof(g_buf), off);
            if (n < 0) { cprintf("cat: read st=%d\n", n); rc = 1; break; }
            if (n == 0) { break; }
            cwrite(g_buf, (u64)n);
            off += (u64)n;
        }
        (void)sys5(SYS_FS_CLOSE, ino, 0, 0, 0, 0);
    }
    return rc;
}

USER_MAIN(umain)
