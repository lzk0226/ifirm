#include "types.h"

void console_write(const char *s);

void kmain(u64 hartid, u64 dtb)
{
    (void)hartid;
    (void)dtb;

    console_write("IFIRM-BOOT stage=0\n");
    console_write("hello ifirm\n");

    for (;;) {
        __asm__ volatile("wfi");
    }
}
