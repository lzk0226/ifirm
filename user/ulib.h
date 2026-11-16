#ifndef IFIRM_ULIB_H
#define IFIRM_ULIB_H

typedef unsigned long long u64;
typedef long long          i64;
typedef unsigned int       u32;

#define SYS_IPC_RECV     2
#define SYS_IPC_CALL     3
#define SYS_IPC_REPLY    4
#define SYS_EXIT         6
#define SYS_CLOCK        7
#define SYS_DEBUG_WRITE  8
#define SYS_CAP_REQUEST  11
#define SYS_CAP_REVOKE   12

#define OBJ_CHANNEL 4
#define OBJ_DEVICE  5   /* console 能力 = UART 设备 */
#define CAP_WRITE   (1u << 1)

#define ICT_BASE 0x20000000ull

static inline i64 sys5(i64 n, i64 a0, i64 a1, i64 a2, i64 a3, i64 a4)
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

/* 需要同时拿回 a1（CA 授权返回的句柄） */
static inline i64 sys5_ret1(i64 n, i64 a0, i64 a1, i64 a2, i64 *out1)
{
    register i64 r0 __asm__("a0") = a0;
    register i64 r1 __asm__("a1") = a1;
    register i64 r2 __asm__("a2") = a2;
    register i64 r7 __asm__("a7") = n;
    __asm__ volatile ("ecall" : "+r"(r0), "+r"(r1) : "r"(r2), "r"(r7) : "memory");
    *out1 = r1;
    return r0;
}

static inline u64 slen(const char *s)
{
    u64 n = 0;
    while (s[n] != '\0') { n++; }
    return n;
}

/* 行缓冲：整行只发一次 debug_write，避免与其他域的输出交错 */
typedef struct { char b[192]; int n; } line_t;

static inline void l_str(line_t *l, const char *s)
{
    while (*s != '\0' && l->n < 180) { l->b[l->n++] = *s++; }
}

static inline void l_u64(line_t *l, u64 v)
{
    char t[24];
    int  k = 0;
    if (v == 0u) { t[k++] = '0'; }
    while (v != 0u && k < 24) { t[k++] = (char)('0' + (int)(v % 10u)); v /= 10u; }
    while (k > 0 && l->n < 180) { l->b[l->n++] = t[--k]; }
}

static inline void l_i64(line_t *l, i64 v)
{
    if (v < 0) { l_str(l, "-"); l_u64(l, (u64)(-v)); }
    else       { l_u64(l, (u64)v); }
}

static inline void l_tag(line_t *l, const char *name, u64 inst)
{
    l_str(l, name); l_str(l, "["); l_u64(l, inst); l_str(l, "]: ");
}

static inline void l_nl(line_t *l) { l->b[l->n++] = '\n'; }

static inline void l_flush(line_t *l)
{
    if (l->n < (int)sizeof(l->b) - 1) { l->b[l->n++] = '\n'; }
    (void)sys5(SYS_DEBUG_WRITE, (i64)l->b, (i64)l->n, 0, 0, 0);
    l->n = 0;
}

static inline void dbg(const char *s)
{
    (void)sys5(SYS_DEBUG_WRITE, (i64)s, (i64)slen(s), 0, 0, 0);
}

/* 经 ICT 槽 0 调用（槽内 function 8） */
static inline i64 ict_write(const char *s, i64 n)
{
    i64 (*fn)(const char *, i64) = (i64 (*)(const char *, i64))ICT_BASE;
    return fn(s, n);
}

#endif /* IFIRM_ULIB_H */
