#ifndef IFIRM_SYSCALL_H
#define IFIRM_SYSCALL_H

#include <ifirm/types.h>
#include <ifirm/thread.h>

/* a7 号表（附录 A.6 冻结；13 起为本版扩展，只增不改 REQ-API-021） */
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
#define SYS_CAP_REQUEST   11u   /* a0=对象类型 a1=权限 a2=对象号 → a1=句柄 */
#define SYS_CAP_REVOKE    12u   /* a0=句柄：撤销并传播 */
#define SYS_CAP_DELEGATE  13u   /* a0=句柄 a1=目标域 a2=权限掩码 → a1=新句柄 a2=对象号 */
#define SYS_CAP_AUDIT     14u   /* a0=缓冲 a1=最多条数 → a1=条数 */
#define SYS_SPAWN         15u   /* a0=路径 a1=路径长 a2=argc a3=argv → a1=域号 */
#define SYS_WAIT          16u   /* a0=域号 → a1=退出码 */
#define SYS_CONSOLE_READ  17u   /* a0=缓冲 a1=长度 → a1=读到的字节数（非阻塞） */
#define SYS_FS_OPEN       18u   /* a0=路径 a1=路径长 a2=标志 → a1=inode */
#define SYS_FS_READ       19u   /* a0=ino a1=缓冲 a2=长度 a3=偏移 → a1=读到的字节数 */
#define SYS_FS_WRITE      20u   /* 同上，返回写入字节数 */
#define SYS_FS_CLOSE      21u   /* a0=ino（本版无状态，仅对齐接口） */
#define SYS_FS_UNLINK     22u   /* a0=路径 a1=长度 */
#define SYS_FS_READDIR    23u   /* a0=目录 ino a1=序号 a2=缓冲 → a1=1 继续 0 结束 */
#define SYS_FS_STAT       24u   /* a0=路径 a1=长度 a2=缓冲 */
#define SYS_PROC_LIST     25u   /* a0=序号 a1=缓冲 → a1=1 有 0 结束 */
#define SYS_FS_MKDIR      26u   /* a0=路径 a1=长度 */

#define SYS_ICT           0u   /* a7=0 表示"经 ICT 槽调用"，a6=槽号 */

/* 进程表项 32 字节（用户态 ps 用） */
typedef struct {
    u16  id;
    u8   state;
    u8   kind;        /* 0=内核组合域 1=磁盘程序域 */
    u32  pages;
    u32  caps;
    u32  faults;
    char name[16];
} proc_info_t;

STATIC_ASSERT(sizeof(proc_info_t) == 32u, "proc_info frozen");

trap_frame_t *syscall_dispatch(trap_frame_t *f);
i64 syscall_execute(u32 nr, trap_frame_t *f);
u64 now_ns(void);
u64 now_ms(void);

#endif /* IFIRM_SYSCALL_H */
