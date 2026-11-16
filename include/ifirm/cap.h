#ifndef IFIRM_CAP_H
#define IFIRM_CAP_H

#include <ifirm/types.h>
#include <ifirm/status.h>

/* 句柄 = [31:12] 代次 | [11:0] 索引（§13.2）；索引 0 永远无效 */
#define CAP_IDX_BITS   12u
#define CAP_IDX_MASK   0x00000FFFu
#define CAP_GEN_SHIFT  12u
#define CAP_GEN_MASK   0xFFFFF000u

#define CAP_RIGHT_READ        (1u << 0)
#define CAP_RIGHT_WRITE       (1u << 1)
#define CAP_RIGHT_EXEC        (1u << 2)
#define CAP_RIGHT_SEND        (1u << 3)
#define CAP_RIGHT_RECV        (1u << 4)
#define CAP_RIGHT_CALL        (1u << 5)
#define CAP_RIGHT_REPLY       (1u << 6)
#define CAP_RIGHT_MAP         (1u << 7)
#define CAP_RIGHT_UNMAP       (1u << 8)
#define CAP_RIGHT_IRQ         (1u << 9)
#define CAP_RIGHT_MMIO        (1u << 10)
#define CAP_RIGHT_DELEGATE    (1u << 11)
#define CAP_RIGHT_REVOKE      (1u << 12)
#define CAP_RIGHT_INSTANTIATE (1u << 13)
#define CAP_RIGHT_ADMIN       (1u << 14)
#define CAP_RIGHTS_ALL        (0x7FFFu)

#define OBJ_TEMPLATE 1u
#define OBJ_DOMAIN   2u
#define OBJ_MEMORY   3u
#define OBJ_CHANNEL  4u
#define OBJ_DEVICE   5u
#define OBJ_ANY      0xFFu

#define CAP_FLAG_VALID         0x01u
#define CAP_FLAG_DELEGABLE     0x02u
#define CAP_FLAG_KERNEL_PRIVATE 0x04u

/* 派生链的最大深度：超过就拒绝，避免组合爆炸与环 */
#define CAP_DERIVE_DEPTH 4u

typedef struct {
    u32 object_id;      /*  0 */
    u32 gen_owner;      /*  4: [31:12] 代次 [11:0] 属主域号 */
    u16 rights;         /*  8 */
    u8  object_type;    /* 10 */
    u8  flags;          /* 11 */
    u32 derived_from;   /* 12: (源域 << 16) | 源索引，0 表示 CA 直接授予 */
} cap_entry_t;

STATIC_ASSERT(sizeof(cap_entry_t) == 16, "cap entry frozen");

static inline cap_t cap_make(u16 idx, u32 gen)
{
    return (cap_t)(((gen << CAP_GEN_SHIFT) & CAP_GEN_MASK) | (u32)idx);
}

void           cap_init(void);
ifirm_status_t cap_check(cap_t h, u8 type, u16 rights, cap_entry_t **out);
ifirm_status_t cap_grant(u16 dom_id, u8 type, u32 object_id, u16 rights, cap_t *out);
/* 派生：从源域某个句柄派生一个子句柄到目标域，derived_from 记下源 */
ifirm_status_t cap_delegate(u16 src_dom, u16 src_idx, u16 dst_dom, u16 rights_mask, cap_t *out);
ifirm_status_t cap_revoke(u16 dom_id, u16 idx);
/* 撤销并沿派生链传播：返回被失效的句柄总数（含自身） */
u32            cap_revoke_tree(u16 dom_id, u16 idx);
u16            cap_count(u16 dom_id);
u32            cap_total(void);

#endif /* IFIRM_CAP_H */
