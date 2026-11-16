#ifndef IFIRM_SYSCALL_H
#define IFIRM_SYSCALL_H

#include <ifirm/types.h>
#include <ifirm/thread.h>

/* a7 号表（附录 A.6 冻结） */
#define SYS_IPC_SEND      1u
#define SYS_IPC_RECV      2u
#define SYS_IPC_CALL      3u
#define SYS_IPC_REPLY     4u
#define SYS_YIELD         5u
#define SYS_EXIT          6u
#define SYS_CLOCK         7u
#define SYS_DEBUG_WRITE   8u
#define SYS_DOMAIN_INFO   9u
#define SYS_CONSOLE_WRITE 10u

#define SYS_ICT           0u   /* a7=0 表示"经 ICT 槽调用"，a6=槽号 */

trap_frame_t *syscall_dispatch(trap_frame_t *f);
i64 syscall_execute(u32 nr, trap_frame_t *f);
u64 now_ns(void);
u64 now_ms(void);

#endif /* IFIRM_SYSCALL_H */
