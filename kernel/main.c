#include <ifirm/types.h>
#include <ifirm/console.h>
#include <ifirm/board.h>
#include <ifirm/pmm.h>
#include <ifirm/vmm.h>
#include <ifirm/cap.h>
#include <ifirm/domain.h>
#include <ifirm/thread.h>
#include <ifirm/trap.h>
#include <ifirm/syscall.h>
#include <ifirm/status.h>
#include <ifirm/fs.h>
#include <ifirm/spawn.h>
#include <ifirm/virtio.h>
#include <ifirm/elf.h>

extern char _image_end[];

/* 启动序列 §18.2 的 19 步，本版裁掉 KIP/journal/cache 三步 */
void kmain(u64 hartid, u64 dtb_pa)
{
    uart_init();
    printk("IFIRM-BOOT stage=0\n");

    if (board_init(hartid, dtb_pa) != IFIRM_OK) {
        board_halt(IFIRM_ERR_CORRUPT, "board-fdt");
    }
    printk("IFIRM-BOARD model=%s ram=%uMiB timebase=%u uart=0x%x plic=0x%x\n",
           g_board.model, (u32)(g_board.ram_size >> 20), (u32)g_board.timebase_hz,
           (u32)g_board.uart_base, (u32)g_board.plic_base);
    printk("IFIRM-BOOT stage=1\n");

    pmm_init(g_board.ram_base, g_board.ram_size, (u64)_image_end);
    {
        u32 fp = pmm_frag_permille();
        if (fp > 999u) { fp = 999u; }
        printk("IFIRM-MEM total=%u free=%u used_kernel=%u frag=0.%u%u%u\n",
               (u32)pmm_total(), (u32)pmm_free_count(), (u32)pmm_used(),
               fp / 100u, (fp / 10u) % 10u, fp % 10u);
    }

    if (vmm_init() != IFIRM_OK) { board_halt(IFIRM_ERR_NO_MEMORY, "vmm"); }
    printk("IFIRM-VM on\n");

    trap_init();
    printk("IFIRM-TICK hz=%u\n", 100u);

    cap_init();
    printk("IFIRM-CAPS mmu=1 timer=1 plic=1 uart=1 hw_watchdog=0 "
           "kernel_hang_recovery=false controller_restart=true cache=C1\n");

    ifirm_status_t cst = domain_compose_all();
    if (cst != IFIRM_OK) {
        const char *why = (cst == IFIRM_ERR_BAD_DIGEST) ? "tpl-digest"
                        : (cst == IFIRM_ERR_DENIED)    ? "charter-denied"
                        : (cst == IFIRM_ERR_CORRUPT)   ? "tpl-corrupt"
                        : "compose";
        board_halt(cst, why);
    }

    printk("IFIRM-READY domains=%u pages_used=%u t_ms=%u\n",
           (u32)domain_count_ready(), (u32)pmm_used(), (u32)now_ms());

    /* 从这里开始，内核不再依赖任何编进映像的用户代码：程序来自磁盘 */
    ifirm_status_t fst = fs_init();
    if (fst != IFIRM_OK) { board_halt(fst, "disk"); }
    printk("IFIRM-FS on files=%u bytes=%u blk=v%u\n",
           (u32)fs_count(), (u32)fs_bytes(), (u32)virtio_blk_version());

    static const char *const k_progs[] = { "hello", "ping" };
    for (u32 i = 0; i < 2u; i++) {
        elf_info_t ei;
        u16 sid = 0u;
        ifirm_status_t est = domain_spawn_file(k_progs[i], &sid, &ei);
        if (est != IFIRM_OK) { board_halt(est, "exec"); }
        printk("IFIRM-EXEC name=%s dom=%u entry=0x%x segs=%u pages=%u\n",
               k_progs[i], (u32)sid, (u32)ei.entry, (u32)ei.segs, (u32)ei.pages);
    }

    timer_enable();
    sched_start();
}
