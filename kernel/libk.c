#include <ifirm/types.h>

/* 编译器会为结构体赋值/清零生成这三个调用（-ffreestanding 下没有 libc） */
void *memcpy(void *dst, const void *src, usize n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    for (usize i = 0; i < n; i++) { d[i] = s[i]; }
    return dst;
}

void *memmove(void *dst, const void *src, usize n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    if (d < s) {
        for (usize i = 0; i < n; i++) { d[i] = s[i]; }
    } else {
        for (usize i = n; i > 0u; i--) { d[i - 1u] = s[i - 1u]; }
    }
    return dst;
}

void *memset(void *dst, int c, usize n)
{
    u8 *d = (u8 *)dst;
    for (usize i = 0; i < n; i++) { d[i] = (u8)c; }
    return dst;
}

int memcmp(const void *a, const void *b, usize n)
{
    const u8 *x = (const u8 *)a;
    const u8 *y = (const u8 *)b;
    for (usize i = 0; i < n; i++) {
        if (x[i] != y[i]) { return (int)x[i] - (int)y[i]; }
    }
    return 0;
}
