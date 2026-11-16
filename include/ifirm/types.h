#ifndef IFIRM_TYPES_H
#define IFIRM_TYPES_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;

typedef signed char        i8;
typedef signed short       i16;
typedef signed int         i32;
typedef signed long long   i64;

typedef u64 usize;          /* 与指针同宽，但语义是"大小" */
typedef u64 paddr_t;        /* 物理地址 */
typedef u64 vaddr_t;        /* 虚拟地址 */
typedef u32 cap_t;          /* Capability 句柄 */
typedef i32 ifirm_status_t; /* 0 成功，负数错误（§8.2） */

typedef int bool;
#define true  1
#define false 0

#ifndef NULL
#define NULL ((void *)0)
#endif

#define PAGE_SIZE   4096u
#define PAGE_SHIFT  12
#define PAGE_MASK   (~(u64)(PAGE_SIZE - 1u))
#define IFIRM_ABI_VERSION 1u

#define STATIC_ASSERT(c, m) _Static_assert((c), m)
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static inline u64 align_up(u64 x, u64 a)   { return (x + a - 1u) & ~(a - 1u); }
static inline u64 align_down(u64 x, u64 a) { return x & ~(a - 1u); }

#endif /* IFIRM_TYPES_H */
