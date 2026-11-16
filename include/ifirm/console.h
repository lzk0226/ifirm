#ifndef IFIRM_CONSOLE_H
#define IFIRM_CONSOLE_H

#include <ifirm/types.h>

void uart_init(void);
void uart_putc(char c);
void puts_raw(const char *s);
void printk(const char *fmt, ...);
void put_hex(u64 v);
void put_dec(i64 v);

#endif /* IFIRM_CONSOLE_H */
