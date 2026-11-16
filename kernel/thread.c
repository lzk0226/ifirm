#include <ifirm/types.h>
#include <ifirm/thread.h>
#include <ifirm/domain.h>
#include <ifirm/vmm.h>
#include <ifirm/console.h>
#include <ifirm/trap.h>
#include <ifirm/sbi.h>

#define SSTATUS_SPIE (1ull << 5)

thread_t g_threads[MAX_THREADS];
thread_t *g_current;
domain_t *g_current_domain;

static u32 g_rr;
static bool g_halted;

thread_t *thread_create(u8 dom, u64 entry, u64 arg, u64 sp)
{
    for (u32 i = 0; i < MAX_THREADS; i++) {
        thread_t *t = &g_threads[i];
        if (t->state != THREAD_FREE) { continue; }
        for (u32 k = 0; k < 32u; k++) { t->tf.x[k] = 0u; }
        t->tf.sstatus = SSTATUS_SPIE;
        t->tf.sepc = entry;
        t->tf.x[REG_SP] = sp;
        t->tf.x[REG_A0] = arg;
        t->wait_obj = 0u;
        t->wake_val = 0u;
        t->blk_buf = 0u;
        t->blk_cap = 0u;
        t->state = THREAD_READY;
        t->dom = dom;
        t->id = (u16)i;
        t->exit_code = 0u;
        return t;
    }
    return NULL;
}

thread_t *thread_of_domain(u8 dom)
{
    for (u32 i = 0; i < MAX_THREADS; i++) {
        if (g_threads[i].state != THREAD_FREE && g_threads[i].dom == dom) { return &g_threads[i]; }
    }
    return NULL;
}

thread_t *thread_waiting_on(u64 obj)
{
    for (u32 i = 0; i < MAX_THREADS; i++) {
        if (g_threads[i].state == THREAD_BLOCKED && g_threads[i].wait_obj == obj) {
            return &g_threads[i];
        }
    }
    return NULL;
}

void thread_block(u64 obj)
{
    if (g_current == NULL) { return; }
    g_current->state = THREAD_BLOCKED;
    g_current->wait_obj = obj;
}

void thread_wake(thread_t *t, i64 a0, u64 a1)
{
    if (t == NULL || t->state != THREAD_BLOCKED) { return; }
    t->state = THREAD_READY;
    t->wait_obj = 0u;
    t->tf.x[REG_A0] = (u64)a0;
    t->tf.x[REG_A1] = a1;
}

void thread_exit(i64 code)
{
    if (g_current == NULL) { return; }
    g_current->state = THREAD_DEAD;
    g_current->exit_code = (u32)code;
}

static bool any_blocked(void)
{
    for (u32 i = 0; i < MAX_THREADS; i++) {
        if (g_threads[i].state == THREAD_BLOCKED) { return true; }
    }
    return false;
}

trap_frame_t *sched_next(trap_frame_t *f)
{
    (void)f;
    if (g_current != NULL && g_current->state == THREAD_RUNNING) {
        g_current->state = THREAD_READY;
    }

    thread_t *next = NULL;
    for (u32 k = 0; k < MAX_THREADS; k++) {
        u32 i = (g_rr + k) % MAX_THREADS;
        if (g_threads[i].state == THREAD_READY) { next = &g_threads[i]; g_rr = i + 1u; break; }
    }

    if (next == NULL) {
        if (!g_halted) {
            g_halted = true;
            printk("IFIRM-HALT all-threads-exited blocked=%u t_ms=%u\n",
                   any_blocked() ? 1u : 0u, (u32)(trap_ticks() * 10ull));
        }
        sbi_shutdown();
        for (;;) { __asm__ volatile ("wfi"); }
    }

    g_current = next;
    next->state = THREAD_RUNNING;
    g_current_domain = domain_get(next->dom);
    if (g_current_domain != NULL) { (void)vmm_switch(g_current_domain->root); }
    return &next->tf;
}

void sched_start(void)
{
    trap_frame_t *f = sched_next(NULL);
    trap_return(f);
    for (;;) { __asm__ volatile ("wfi"); }
}
