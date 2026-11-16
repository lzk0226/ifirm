#include <ifirm/types.h>
#include <ifirm/thread.h>
#include <ifirm/domain.h>
#include <ifirm/console.h>
#include <ifirm/sbi.h>
#include <ifirm/syscall.h>
#include <ifirm/trap.h>

#define SSTATUS_SPP  (1ull << 8)
#define SCAUSE_INT   (1ull << 63)
#define IRQ_S_TIMER  5ull
#define TICK_CYCLES  100000ull        /* 10 MHz timebase 下的 10 ms */

static volatile u64 g_ticks;

extern void trap_entry(void);

void trap_init(void)
{
    __asm__ volatile ("csrw stvec, %0" :: "r"((u64)trap_entry));
    /* U 态允许 rdtime（§25.3） */
    __asm__ volatile ("csrs scounteren, %0" :: "r"(1ull << 1));
}

u64 trap_ticks(void) { return g_ticks; }

void timer_arm(void)
{
    u64 now;
    __asm__ volatile ("rdtime %0" : "=r"(now));
    sbi_set_timer(now + TICK_CYCLES);
}

void timer_enable(void)
{
    __asm__ volatile ("csrs sie, %0" :: "r"(1ull << 5));   /* STIE */
    __asm__ volatile ("csrs sstatus, %0" :: "r"(1ull << 1)); /* SIE */
    timer_arm();
}

void panic(const char *why)
{
    printk("\nIFIRM-PANIC %s\n", why);
    sbi_shutdown();
}

trap_frame_t *trap_dispatch(trap_frame_t *f)
{
    thread_t *t = g_current;

    if ((f->sstatus & SSTATUS_SPP) != 0u) {
        printk("\nIFIRM-PANIC trap-in-kernel scause=0x%x sepc=0x%x\n",
               (u32)f->scause, (u32)f->sepc);
        sbi_shutdown();
    }

    t->tf = *f;

    if ((f->scause & SCAUSE_INT) != 0u) {
        if ((f->scause & 0xFFull) == IRQ_S_TIMER) {
            g_ticks++;
            timer_arm();
        }
    } else if (f->scause == 8u) {
        syscall_dispatch(&t->tf);
    } else {
        domain_fault(t->dom, f->scause, f->sepc, f->stval);
    }

    return sched_next(&t->tf);
}
