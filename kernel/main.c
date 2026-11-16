#include <ifirm/types.h>
#include <ifirm/console.h>
#include <ifirm/board.h>
#include <ifirm/trap.h>
#include <ifirm/status.h>
#include <ifirm/component.h>
#include <ifirm/thread.h>

extern char _image_end[];

/*
 * kmain 只剩三件事：把启动参数交给组合器、组合内核、开始调度。
 * 板级初始化、内存、页表、能力、CA、模板、域、FS、init 全部是
 * 组合器按 g_components 的声明顺序调起来的（§22）。
 */
void kmain(u64 hartid, u64 dtb_pa)
{
    uart_init();
    kcomp_set_context(hartid, dtb_pa, (u64)_image_end);

    ifirm_status_t st = kernel_compose();
    if (st != IFIRM_OK) { board_halt(st, "compose"); }   /* 具体哪个组件由 IFIRM-COMP FAIL 给出 */

    printk("IFIRM-BOOT stage=2\n");
    timer_enable();
    sched_start();
}
