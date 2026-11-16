#ifndef IFIRM_BOARD_H
#define IFIRM_BOARD_H

#include <ifirm/types.h>

/* QEMU virt 的固定地址，作为 DTB 缺失时的兜底（§7.5） */
#define BOARD_UART0_PA   0x10000000u
#define BOARD_PLIC_PA    0x0c000000u
#define BOARD_RAM_BASE   0x80000000u

typedef struct {
    char  model[48];
    u64   ram_base;
    u64   ram_size;
    u32   timebase_hz;
    u64   uart_base;
    u64   plic_base;
    u64   dtb_pa;
    u64   boot_hartid;
    bool  from_fdt;
} board_info_t;

extern board_info_t g_board;

ifirm_status_t board_init(u64 hartid, u64 dtb_pa);
void board_halt(ifirm_status_t st, const char *reason);

#endif /* IFIRM_BOARD_H */
