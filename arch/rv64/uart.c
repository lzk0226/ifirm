#include "types.h"

#define UART0        0x10000000UL
#define UART_THR     0x00
#define UART_LSR     0x05
#define UART_LSR_THRE 0x20

void arch_console_putc(char c)
{
    volatile u8 *lsr = (volatile u8 *)(UART0 + UART_LSR);
    volatile u8 *thr = (volatile u8 *)(UART0 + UART_THR);

    while ((*lsr & UART_LSR_THRE) == 0) { }
    *thr = (u8)c;
}

void console_write(const char *s)
{
    while (*s != '\0') {
        arch_console_putc(*s++);
    }
}
