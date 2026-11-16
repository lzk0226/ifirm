#include "ulib.h"

/* 交互 shell：本身也是一个普通用户态程序（从磁盘加载，不是内核的一部分）。
 * 内建命令 ps / ca / help / exit，其余都去 /bin 里找。 */

static char g_line[128];
static char g_args[160];
static char *g_argv[8];
static char g_path[32];

/* 与 include/ifirm/ca.h 的 ca_audit_t 逐字节一致（28 字节；result 必须是 i32，
 * 用 i64 会插入 4 字节填充，整个数组就错位了） */
typedef struct {
    u32 kind;
    u32 dom;
    u32 type;
    u32 rights;
    u32 object;
    int result;      /* i32：必须是 4 字节 */
    u32 t_ms;
} audit_t;

static const char *const k_kind[] = { "?", "req", "grant", "deny", "revoke", "deleg" };
static const char *const k_state[] = { "free", "ready", "running", "dead" };

static int read_line(void)
{
    int n = 0;
    for (;;) {
        int c = getchar_nb();
        if (c < 0) {
            (void)sys5(SYS_YIELD, 0, 0, 0, 0, 0);
            continue;
        }
        if (c == '\r') { continue; }
        if (c == '\n') { cwrite("\n", 1); g_line[n] = '\0'; return n; }
        if (c == 8 || c == 127) {
            if (n > 0) { n--; cwrite("\b \b", 3); }
            continue;
        }
        if (n < (int)sizeof(g_line) - 1) {
            char ch = (char)c;
            g_line[n++] = ch;
            cwrite(&ch, 1);
        }
    }
}

static int tokenize(void)
{
    int argc = 0;
    u32 i = 0;
    u32 k = 0;
    while (g_line[i] != '\0' && argc < 7) {
        while (g_line[i] == ' ') { i++; }
        if (g_line[i] == '\0') { break; }
        while (g_line[i] != '\0' && g_line[i] != ' ' && k < sizeof(g_args) - 1) {
            g_args[k++] = g_line[i++];
        }
        g_args[k++] = '\0';
        argc++;
    }
    /* 第二遍把 g_args 切成指针：上面已经顺序写入，这里重扫 */
    u32 p = 0;
    for (int j = 0; j < argc; j++) {
        g_argv[j] = &g_args[p];
        while (g_args[p] != '\0') { p++; }
        p++;
    }
    g_argv[argc] = 0;
    return argc;
}

static void cmd_ps(void)
{
    proc_info_t p;
    u32 i = 0;
    cprintf("PID STATE   KIND PAGES CAPS FAULTS NAME\n");
    while (p_list(i, &p) == 1) {
        const char *st = (p.state < 4u) ? k_state[p.state] : "?";
        cprintf("%d   %s", (i64)p.id, st);
        for (u32 k = slen(st); k < 8u; k++) { cputs(" "); }
        cprintf("%d     %d     %d      %d      %s\n",
                (i64)p.kind, (i64)p.pages, (i64)p.caps, (i64)p.faults, p.name);
        i++;
    }
    cprintf("domains=%d\n", (i64)i);
}

static void cmd_ca(void)
{
    audit_t e[16];
    i64 n = sys5(SYS_CAP_AUDIT, (i64)e, 16, 0, 0, 0);
    if (n < 0) { cprintf("ca: st=%d\n", n); return; }
    cprintf("CA audit (newest first, %d shown)\n", n);
    for (i64 i = 0; i < n; i++) {
        const char *k = (e[i].kind < 6u) ? k_kind[e[i].kind] : "?";
        cprintf("  %s dom=%d type=%d rights=%x obj=%d st=%d t=%dms\n",
                k, (i64)e[i].dom, (i64)e[i].type, (i64)e[i].rights,
                (i64)e[i].object, e[i].result, (i64)e[i].t_ms);
    }
}

static void cmd_help(void)
{
    cputs("builtin : help ps ca mkdir rm write exit\n");
    cputs("programs: ls cat echo hello ping crash (in /bin, loaded from disk)\n");
}

/* mkdir <路径> */
static void cmd_mkdir(int argc)
{
    if (argc < 2) { cputs("usage: mkdir <path>\n"); return; }
    i64 r = f_mkdir(g_argv[1]);
    if (r < 0) { cprintf("mkdir: %s: st=%d\n", g_argv[1], r); }
}

/* rm <路径> */
static void cmd_rm(int argc)
{
    if (argc < 2) { cputs("usage: rm <path>\n"); return; }
    i64 r = f_unlink(g_argv[1]);
    if (r < 0) { cprintf("rm: %s: st=%d\n", g_argv[1], r); }
}

/* write <路径> <文本...>：演示可写文件系统，数据真的落到 virtio 磁盘 */
static void cmd_write(int argc)
{
    if (argc < 3) { cputs("usage: write <path> <text...>\n"); return; }
    i64 ino = f_open(g_argv[1], FS_OPEN_CREATE | FS_OPEN_TRUNC);
    if (ino < 0) { cprintf("write: %s: st=%d\n", g_argv[1], ino); return; }
    char buf[160];
    u32 n = 0;
    for (int i = 2; i < argc; i++) {
        if (i > 2 && n < sizeof(buf) - 1) { buf[n++] = ' '; }
        for (u32 k = 0; g_argv[i][k] != '\0' && n < sizeof(buf) - 1; k++) {
            buf[n++] = g_argv[i][k];
        }
    }
    buf[n++] = '\n';
    i64 w = f_write(ino, buf, n, 0);
    if (w < 0) { cprintf("write: st=%d\n", w); return; }
    (void)sys5(SYS_FS_CLOSE, ino, 0, 0, 0, 0);
    cprintf("write: %s %d bytes\n", g_argv[1], w);
}

static int run_external(int argc)
{
    u32 n = 0;
    const char *pre = "/bin/";
    while (pre[n] != '\0' && n < sizeof(g_path) - 1) { g_path[n] = pre[n]; n++; }
    for (u32 i = 0; g_argv[0][i] != '\0' && n < sizeof(g_path) - 1; i++) {
        g_path[n++] = g_argv[0][i];
    }
    g_path[n] = '\0';

    i64 d = p_spawn(g_path, argc, g_argv);
    if (d < 0) {
        cprintf("sh: %s: st=%d\n", g_path, d);
        return -1;
    }
    i64 code = p_wait(d);
    if (code != 0) { cprintf("sh: %s exit=%d\n", g_argv[0], code); }
    return 0;
}

static int umain(int argc, char **argv)
{
    (void)argc; (void)argv;
    cputs("\nIfirm 1.0 minimal system\n");
    cputs("type 'help' for commands\n");

    for (;;) {
        cputs("ifirm> ");
        if (read_line() == 0) { continue; }
        int n = tokenize();
        if (n == 0) { continue; }

        if (scmp(g_argv[0], "exit") == 0) { break; }
        if (scmp(g_argv[0], "help") == 0) { cmd_help(); continue; }
        if (scmp(g_argv[0], "ps") == 0)   { cmd_ps(); continue; }
        if (scmp(g_argv[0], "ca") == 0)   { cmd_ca(); continue; }
        if (scmp(g_argv[0], "mkdir") == 0) { cmd_mkdir(n); continue; }
        if (scmp(g_argv[0], "rm") == 0)    { cmd_rm(n); continue; }
        if (scmp(g_argv[0], "write") == 0) { cmd_write(n); continue; }
        (void)run_external(n);
    }
    return 0;
}

USER_MAIN(umain)
