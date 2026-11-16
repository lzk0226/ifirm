#ifndef IFIRM_ULIB_H
#define IFIRM_ULIB_H

/*
 * 用户态最小库：只依赖内核 ABI（附录 A.6），不含任何 libc。
 * 磁盘程序与内核内模板共用这一份头文件。
 */
typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef long long          i64;

#define SYS_IPC_SEND      1
#define SYS_IPC_RECV      2
#define SYS_IPC_CALL      3
#define SYS_IPC_REPLY     4
#define SYS_YIELD         5
#define SYS_EXIT          6
#define SYS_CLOCK         7
#define SYS_DEBUG_WRITE   8
#define SYS_DOMAIN_INFO   9
#define SYS_CONSOLE_WRITE 10
#define SYS_CAP_REQUEST   11
#define SYS_CAP_REVOKE    12
#define SYS_CAP_DELEGATE  13
#define SYS_CAP_AUDIT     14
#define SYS_SPAWN         15
#define SYS_WAIT          16
#define SYS_CONSOLE_READ  17
#define SYS_FS_OPEN       18
#define SYS_FS_READ       19
#define SYS_FS_WRITE      20
#define SYS_FS_CLOSE      21
#define SYS_FS_UNLINK     22
#define SYS_FS_READDIR    23
#define SYS_FS_STAT       24
#define SYS_PROC_LIST     25
#define SYS_FS_MKDIR      26
#define SYS_CAP_POLL      27

#define OBJ_TEMPLATE 1
#define OBJ_DOMAIN   2
#define OBJ_MEMORY   3
#define OBJ_CHANNEL  4
#define OBJ_DEVICE   5
#define OBJ_ANY      0xFF

#define CAP_READ    (1u << 0)
#define CAP_WRITE   (1u << 1)
#define CAP_EXEC    (1u << 2)
#define CAP_SEND    (1u << 3)
#define CAP_RECV    (1u << 4)
#define CAP_CALL    (1u << 5)
#define CAP_REPLY   (1u << 6)

#define FS_OPEN_READ   0x1u
#define FS_OPEN_CREATE 0x2u
#define FS_OPEN_TRUNC  0x4u

#define ICT_BASE 0x20000000ull

/* ---------- 系统调用 ---------- */
static inline i64 sys6(i64 n, i64 a0, i64 a1, i64 a2, i64 a3, i64 a4)
{
    register i64 r0 __asm__("a0") = a0;
    register i64 r1 __asm__("a1") = a1;
    register i64 r2 __asm__("a2") = a2;
    register i64 r3 __asm__("a3") = a3;
    register i64 r4 __asm__("a4") = a4;
    register i64 r7 __asm__("a7") = n;
    __asm__ volatile ("ecall"
                      : "+r"(r0), "+r"(r1)
                      : "r"(r2), "r"(r3), "r"(r4), "r"(r7)
                      : "memory");
    return r0;
}
#define sys5(n, a0, a1, a2, a3, a4) sys6((n), (a0), (a1), (a2), (a3), (a4))

static inline u64 slen(const char *s)
{
    u64 n = 0;
    while (s[n] != '\0') { n++; }
    return n;
}

static inline int scmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) { a++; b++; }
    return (int)(u8)*a - (int)(u8)*b;
}

static inline int sncmp(const char *a, const char *b, u32 n)
{
    for (u32 i = 0; i < n; i++) {
        if (a[i] != b[i]) { return (int)(u8)a[i] - (int)(u8)b[i]; }
        if (a[i] == '\0') { return 0; }
    }
    return 0;
}

static inline void scpy(char *d, const char *s, u32 cap)
{
    u32 i = 0;
    for (; i + 1u < cap && s[i] != '\0'; i++) { d[i] = s[i]; }
    for (; i < cap; i++) { d[i] = '\0'; }
}

static inline int atoi10(const char *s)
{
    int v = 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (int)(*s - '0'); s++; }
    return neg ? -v : v;
}

/* ---------- 输出 ---------- */
static inline void cwrite(const char *s, u64 n)
{
    (void)sys5(SYS_CONSOLE_WRITE, (i64)s, (i64)n, 0, 0, 0);
}

static inline void cputs(const char *s) { cwrite(s, slen(s)); }

/* ---------- 行缓冲：内核内模板先拼整行再一次写出，避免被时钟片切开 ---------- */
typedef struct { char b[128]; u64 n; } line_t;

static inline void l_ch(line_t *l, char c)
{
    if (l->n + 1u < (u64)sizeof(l->b)) { l->b[l->n++] = c; }
}

static inline void l_str(line_t *l, const char *s)
{
    while (*s != '\0') { l_ch(l, *s); s++; }
}

static inline void l_u64(line_t *l, u64 v)
{
    char t[24];
    int k = 0;
    if (v == 0u) { t[k++] = '0'; }
    while (v != 0u && k < 24) { t[k++] = (char)('0' + (int)(v % 10u)); v /= 10u; }
    while (k > 0) { l_ch(l, t[--k]); }
}

static inline void l_i64(line_t *l, i64 v)
{
    if (v < 0) { l_ch(l, '-'); l_u64(l, (u64)(-v)); }
    else { l_u64(l, (u64)v); }
}

static inline void l_nl(line_t *l) { l_ch(l, '\n'); }

static inline void l_tag(line_t *l, const char *tag, u64 inst)
{
    l_str(l, tag); l_ch(l, '['); l_u64(l, inst); l_str(l, "]: ");
}

static inline void l_flush(line_t *l) { cwrite(l->b, l->n); l->n = 0u; }

/* 经 ICT 槽 0 进内核：a7=0 表示走 ICT，a6=槽号（§21.4） */
static inline i64 ict_write(const char *s, u64 n)
{
    register i64 r0 __asm__("a0") = (i64)s;
    register i64 r1 __asm__("a1") = (i64)n;
    register i64 r6 __asm__("a6") = 0;
    register i64 r7 __asm__("a7") = 0;
    __asm__ volatile ("ecall" : "+r"(r0) : "r"(r1), "r"(r6), "r"(r7) : "memory");
    return r0;
}

#define sys5_ret1(n, a0, a1, a2, a3) sys6((n), (a0), (a1), (a2), (a3), 0)

/* 异步能力申请：a3 的 bit0 置 1 表示只入队；成功时返回票号（a1） */
static inline i64 cap_req_async(u64 type, u64 rights, u64 obj)
{
    register i64 r0 __asm__("a0") = (i64)type;
    register i64 r1 __asm__("a1") = (i64)rights;
    register i64 r2 __asm__("a2") = (i64)obj;
    register i64 r3 __asm__("a3") = 1;
    register i64 r7 __asm__("a7") = SYS_CAP_REQUEST;
    __asm__ volatile ("ecall" : "+r"(r0), "+r"(r1) : "r"(r2), "r"(r3), "r"(r7) : "memory");
    if (r0 != 0) { return r0; }
    return r1;
}

/* 取异步结果：0 完成（句柄写进 out）、-11 还没轮到、其它为拒绝码 */
static inline i64 cap_poll(i64 ticket, u64 *out)
{
    register i64 r0 __asm__("a0") = ticket;
    register i64 r1 __asm__("a1") = (i64)out;
    register i64 r7 __asm__("a7") = SYS_CAP_POLL;
    __asm__ volatile ("ecall" : "+r"(r0), "+r"(r1) : "r"(r7) : "memory");
    return r0;
}

static inline void dbg(const char *s)
{
    (void)sys5(SYS_DEBUG_WRITE, (i64)s, (i64)slen(s), 0, 0, 0);
}

static inline void cputu(u64 v)
{
    char t[24];
    int k = 0;
    if (v == 0u) { t[k++] = '0'; }
    while (v != 0u) { t[k++] = (char)('0' + (int)(v % 10u)); v /= 10u; }
    char o[24];
    int j = 0;
    while (k > 0) { o[j++] = t[--k]; }
    cwrite(o, (u64)j);
}

static inline void cputi(i64 v)
{
    if (v < 0) { cputs("-"); cputu((u64)(-v)); }
    else       { cputu((u64)v); }
}

static inline void cputhex(u64 v)
{
    const char *d = "0123456789abcdef";
    char tmp[16];
    int k = 0;
    cputs("0x");
    if (v == 0u) { cwrite("0", 1); return; }
    while (v != 0u) { tmp[k++] = d[v & 0xFu]; v >>= 4; }
    while (k > 0) { cwrite(&tmp[--k], 1); }
}

/* 行缓冲 printf：整行拼好再一次写出。逐字符 cwrite 会让两个域的 */
/* 输出在串口上交错成乱码，而且一行要几十次系统调用。 */
typedef struct { char b[160]; u64 n; } cbuf_t;

static inline void cb_ch(cbuf_t *c, char ch)
{
    if (c->n == (u64)sizeof(c->b)) { cwrite(c->b, c->n); c->n = 0u; }
    c->b[c->n++] = ch;
}

static inline void cb_str(cbuf_t *c, const char *s)
{
    while (*s != '\0') { cb_ch(c, *s); s++; }
}

static inline void cb_u64(cbuf_t *c, u64 v)
{
    char t[24];
    int k = 0;
    if (v == 0u) { t[k++] = '0'; }
    while (v != 0u && k < 24) { t[k++] = (char)('0' + (int)(v % 10u)); v /= 10u; }
    while (k > 0) { cb_ch(c, t[--k]); }
}

static inline void cb_hex(cbuf_t *c, u64 v)
{
    const char *d = "0123456789abcdef";
    char t[16];
    int k = 0;
    cb_str(c, "0x");
    if (v == 0u) { cb_ch(c, '0'); return; }
    while (v != 0u && k < 16) { t[k++] = d[v & 0xFu]; v >>= 4; }
    while (k > 0) { cb_ch(c, t[--k]); }
}

/* 极简 printf：%s %d %u %x %c %% */
static inline void cprintf(const char *fmt, ...)
{
    cbuf_t c;
    c.n = 0u;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    for (const char *p = fmt; *p != '\0'; p++) {
        if (*p != '%') { cb_ch(&c, *p); continue; }
        p++;
        switch (*p) {
        case 's': cb_str(&c, __builtin_va_arg(ap, const char *)); break;
        case 'd': {
            i64 v = __builtin_va_arg(ap, i64);
            if (v < 0) { cb_ch(&c, '-'); cb_u64(&c, (u64)(-v)); }
            else { cb_u64(&c, (u64)v); }
            break;
        }
        case 'u': cb_u64(&c, __builtin_va_arg(ap, u64)); break;
        case 'x': cb_hex(&c, __builtin_va_arg(ap, u64)); break;
        case 'c': { char ch = (char)__builtin_va_arg(ap, int); cb_ch(&c, ch); } break;
        case '%': cb_ch(&c, '%'); break;
        default:  cb_ch(&c, '%'); cb_ch(&c, *p); break;
        }
    }
    __builtin_va_end(ap);
    if (c.n > 0u) { cwrite(c.b, c.n); }
}

/* ---------- 控制台输入（非阻塞 + 让出） ---------- */
static inline int cread(char *buf, u32 cap)
{
    i64 n = sys5(SYS_CONSOLE_READ, (i64)buf, (i64)cap, 0, 0, 0);
    if (n < 0) { return (int)n; }
    return (int)n;
}

static inline int getchar_nb(void)
{
    char c;
    if (cread(&c, 1) == 1) { return (int)(u8)c; }
    return -1;
}

static inline u64 now_ms(void)
{
    u64 t = 0;
    (void)sys5(SYS_CLOCK, (i64)&t, 0, 0, 0, 0);
    return t / 1000000ull;
}

/* ---------- 文件 ---------- */
static inline i64 f_open(const char *path, u32 flags)
{
    register i64 r0 __asm__("a0") = (i64)path;
    register i64 r1 __asm__("a1") = (i64)slen(path);
    register i64 r2 __asm__("a2") = (i64)flags;
    register i64 r3 __asm__("a3") = 0;
    register i64 r7 __asm__("a7") = SYS_FS_OPEN;
    i64 ino = 0;
    __asm__ volatile ("ecall" : "+r"(r0), "+r"(r1)
                      : "r"(r2), "r"(r3), "r"(r7) : "memory");
    if (r0 < 0) { return r0; }
    ino = r1;
    return (r0 == 0) ? ino : r0;
}

static inline i64 f_read(i64 ino, void *buf, u64 len, u64 off)
{
    register i64 r0 __asm__("a0") = ino;
    register i64 r1 __asm__("a1") = (i64)buf;
    register i64 r2 __asm__("a2") = (i64)len;
    register i64 r3 __asm__("a3") = (i64)off;
    register i64 r7 __asm__("a7") = SYS_FS_READ;
    i64 n = 0;
    __asm__ volatile ("ecall" : "+r"(r0), "+r"(r1)
                      : "r"(r2), "r"(r3), "r"(r7) : "memory");
    if (r0 < 0) { return r0; }
    n = r1;
    return n;
}

static inline i64 f_write(i64 ino, const void *buf, u64 len, u64 off)
{
    register i64 r0 __asm__("a0") = ino;
    register i64 r1 __asm__("a1") = (i64)buf;
    register i64 r2 __asm__("a2") = (i64)len;
    register i64 r3 __asm__("a3") = (i64)off;
    register i64 r7 __asm__("a7") = SYS_FS_WRITE;
    i64 n = 0;
    __asm__ volatile ("ecall" : "+r"(r0), "+r"(r1)
                      : "r"(r2), "r"(r3), "r"(r7) : "memory");
    if (r0 < 0) { return r0; }
    n = r1;
    return n;
}

static inline i64 f_unlink(const char *path)
{
    return sys5(SYS_FS_UNLINK, (i64)path, (i64)slen(path), 0, 0, 0);
}

static inline i64 f_mkdir(const char *path)
{
    return sys5(SYS_FS_MKDIR, (i64)path, (i64)slen(path), 0, 0, 0);
}

typedef struct {
    u32  ino;
    u32  size;
    u8   type;
    char name[16];
} dirent_t;

static inline int f_readdir(i64 dir, u32 index, dirent_t *out)
{
    return (int)sys5(SYS_FS_READDIR, dir, (i64)index, (i64)out, 0, 0);
}

/* ---------- 进程 ---------- */
static inline i64 p_spawn(const char *path, int argc, char *const *argv)
{
    i64 dom = 0;
    register i64 r0 __asm__("a0") = (i64)path;
    register i64 r1 __asm__("a1") = (i64)slen(path);
    register i64 r2 __asm__("a2") = (i64)argc;
    register i64 r3 __asm__("a3") = (i64)argv;
    register i64 r7 __asm__("a7") = SYS_SPAWN;
    __asm__ volatile ("ecall" : "+r"(r0), "+r"(r1)
                      : "r"(r2), "r"(r3), "r"(r7) : "memory");
    if (r0 != 0) { return r0; }
    dom = r1;
    return dom;
}

static inline i64 p_wait(i64 dom)
{
    register i64 r0 __asm__("a0") = dom;
    register i64 r1 __asm__("a1") = 0;
    register i64 r7 __asm__("a7") = SYS_WAIT;
    __asm__ volatile ("ecall" : "+r"(r0), "+r"(r1) : "r"(r7) : "memory");
    if (r0 != 0) { return r0; }
    return r1;
}

static inline i64 p_run(const char *path, int argc, char *const *argv)
{
    i64 d = p_spawn(path, argc, argv);
    if (d < 0) { return d; }
    return p_wait(d);
}

typedef struct {
    u16  id;
    u8   state;
    u8   kind;
    u32  pages;
    u32  caps;
    u32  faults;
    char name[16];
} proc_info_t;

static inline int p_list(u32 index, proc_info_t *out)
{
    return (int)sys5(SYS_PROC_LIST, (i64)index, (i64)out, 0, 0, 0);
}

/* ---------- 程序入口 ---------- */
/* 内核按 ABI 传 argc/argv/con：程序只写 umain，入口由这个宏生成 */
#define USER_MAIN(fn)                                                     \
    void _start(u64 argc, u64 argv, u64 con)                              \
    {                                                                     \
        (void)con;                                                        \
        int rc = fn((int)argc, (char **)argv);                            \
        (void)sys5(SYS_EXIT, rc, 0, 0, 0, 0);                             \
        for (;;) { }                                                      \
    }

#endif /* IFIRM_ULIB_H */
