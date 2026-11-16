#include <ifirm/types.h>
#include <ifirm/console.h>

#define UART0_BASE 0x10000000u
#define UART_THR 0x00u
#define UART_IER 0x01u
#define UART_FCR 0x02u
#define UART_LCR 0x03u
#define UART_LSR 0x05u
#define LSR_THRE 0x20u

static inline void wr8(u64 addr, u8 v) { *(volatile u8 *)addr = v; }
static inline u8 rd8(u64 addr) { return *(volatile u8 *)addr; }

void uart_init(void)
{
    wr8(UART0_BASE + UART_IER, 0x00u);
    wr8(UART0_BASE + UART_LCR, 0x03u);
    wr8(UART0_BASE + UART_FCR, 0x01u);
}

void uart_putc(char c)
{
    while ((rd8(UART0_BASE + UART_LSR) & LSR_THRE) == 0u) {
        /* 等发送保持寄存器空 */
    }
    wr8(UART0_BASE + UART_THR, (u8)c);
}

void puts_raw(const char *s)
{
    while (*s != '\0') {
        uart_putc(*s);
        s++;
    }
}
