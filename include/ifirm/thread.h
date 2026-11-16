#ifndef IFIRM_THREAD_H
#define IFIRM_THREAD_H

#include <ifirm/types.h>

/* x0..x31 + 四个 CSR，共 36 个 64 位字 = 288 字节（§12.3 冻结） */
typedef struct {
    u64 x[32];
    u64 sstatus;
    u64 sepc;
    u64 scause;
    u64 stval;
} trap_frame_t;

STATIC_ASSERT(sizeof(trap_frame_t) == 36u * 8u, "trap frame frozen");

#define REG_A0 10
#define REG_A1 11
#define REG_A2 12
#define REG_A3 13
#define REG_A4 14
#define REG_A5 15
#define REG_A6 16
#define REG_A7 17
#define REG_SP 2
#define REG_RA 1

enum {
    THREAD_FREE = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_DEAD
};

typedef struct {
    trap_frame_t tf;
    u64  wait_obj;      /* 阻塞原因：通道号或 0 */
    u64  wake_val;      /* 唤醒时写入 a1 */
    u64  blk_buf;       /* 阻塞中的接收缓冲（VA） */
    u32  blk_cap;
    u8   state;
    u8   dom;
    u16  id;
    u32  exit_code;
} thread_t;

/* wait_obj 的域退出标签：0x1_0000_0000 | 域号，区分通道句柄与域号 */
#define THREAD_WAIT_DOMAIN 0x100000000ull

#define MAX_THREADS 8

extern thread_t g_threads[MAX_THREADS];
extern thread_t *g_current;

thread_t *thread_create(u8 dom, u64 entry, u64 arg, u64 arg2, u64 arg3, u64 sp);
void      sched_start(void) __attribute__((noreturn));
trap_frame_t *sched_next(trap_frame_t *f);
void      thread_block(u64 obj);
void      thread_wake(thread_t *t, i64 a0, u64 a1);
void      thread_exit(i64 code);
thread_t *thread_of_domain(u8 dom);
thread_t *thread_waiting_on(u64 obj);
thread_t *thread_by_id(u16 id);
void      thread_wake_domain(u8 dom, i64 code);

#endif /* IFIRM_THREAD_H */
