#include <ifirm/types.h>
#include <ifirm/console.h>
#include <ifirm/status.h>

static void put_u64(u64 v, u32 base, bool pad)
{
    char tmp[24];
    u32 n = 0;
    if (v == 0u) { tmp[n++] = '0'; }
    while (v != 0u) {
        u32 d = (u32)(v % base);
        tmp[n++] = (char)((d < 10u) ? ('0' + (char)d) : ('a' + (char)(d - 10u)));
        v /= base;
    }
    if (pad) {
        for (u32 i = n; i < 16u; i++) { tmp[n++] = '0'; }
    }
    while (n > 0u) { uart_putc(tmp[--n]); }
}

void put_hex(u64 v) { puts_raw("0x"); put_u64(v, 16u, false); }
void put_dec(i64 v)
{
    if (v < 0) { uart_putc('-'); put_u64((u64)(-v), 10u, false); }
    else       { put_u64((u64)v, 10u, false); }
}

void printk(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    while (*fmt != '\0') {
        if (*fmt != '%') { uart_putc(*fmt++); continue; }
        fmt++;
        switch (*fmt) {
        case 's': { const char *s = __builtin_va_arg(ap, const char *); puts_raw((s != NULL) ? s : "(null)"); break; }
        case 'd': put_dec(__builtin_va_arg(ap, i64)); break;
        case 'u': put_u64(__builtin_va_arg(ap, u64), 10u, false); break;
        case 'x': put_u64(__builtin_va_arg(ap, u64), 16u, false); break;
        case 'p': put_u64(__builtin_va_arg(ap, u64), 16u, false); break;
        case 'c': uart_putc((char)__builtin_va_arg(ap, i64)); break;
        case '%': uart_putc('%'); break;
        default: uart_putc('%'); uart_putc(*fmt); break;
        }
        fmt++;
    }
    __builtin_va_end(ap);
}

const char *status_str(ifirm_status_t st)
{
    switch (st) {
    case IFIRM_OK: return "OK";
    case IFIRM_ERR_NOT_FOUND: return "NOT_FOUND";
    case IFIRM_ERR_INVALID_ARG: return "INVALID_ARG";
    case IFIRM_ERR_NO_MEMORY: return "NO_MEMORY";
    case IFIRM_ERR_NO_SPACE: return "NO_SPACE";
    case IFIRM_ERR_DENIED: return "DENIED";
    case IFIRM_ERR_BAD_HANDLE: return "BAD_HANDLE";
    case IFIRM_ERR_INVALID_STATE: return "INVALID_STATE";
    case IFIRM_ERR_BUSY: return "BUSY";
    case IFIRM_ERR_RETRY: return "RETRY";
    case IFIRM_ERR_TIMEOUT: return "TIMEOUT";
    case IFIRM_ERR_AGAIN: return "AGAIN";
    case IFIRM_ERR_CANCELED: return "CANCELED";
    case IFIRM_ERR_UNSUPPORTED: return "UNSUPPORTED";
    case IFIRM_ERR_CORRUPT: return "CORRUPT";
    case IFIRM_ERR_REVOKED: return "REVOKED";
    case IFIRM_ERR_STALE_EPOCH: return "STALE_EPOCH";
    case IFIRM_ERR_OVERFLOW: return "OVERFLOW";
    case IFIRM_ERR_BAD_DIGEST: return "BAD_DIGEST";
    case IFIRM_ERR_NOT_READY: return "NOT_READY";
    default: return "UNKNOWN";
    }
}
