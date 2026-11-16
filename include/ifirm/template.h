#ifndef IFIRM_TEMPLATE_H
#define IFIRM_TEMPLATE_H

#include <ifirm/types.h>
#include <ifirm/status.h>

/*
 * 模板镜像头（§20.3 的原子模板）：40 字节，紧接 .text 载荷，再紧接 .data 载荷。
 * 组合期必须先验摘要，摘要覆盖 text||data，不符则拒绝引导（REQ-TPL-003）。
 */
#define TPL_MAGIC0 0x49u   /* 'I' */
#define TPL_VERSION 1u
#define TPL_HDR_SIZE 40u
#define TPL_MAX_TEXT 0x10000u  /* 模板槽里留给 .text 的窗口（64 KiB） */
#define TPL_MAX_DATA 0x10000u

typedef struct {
    u8  magic[8];      /*  0: "IFIRMTPL" */
    u16 version;       /*  8 */
    u16 hdr_size;      /* 10 */
    u32 text_len;      /* 12 */
    u32 data_len;      /* 16 */
    u32 name_hash;     /* 20: 模板名 FNV-1a 32 */
    u64 digest;        /* 24: 载荷 FNV-1a 64（text||data） */
    u64 reserved;      /* 32 */
} tpl_hdr_t;

STATIC_ASSERT(sizeof(tpl_hdr_t) == TPL_HDR_SIZE, "tpl header frozen");

ifirm_status_t tpl_verify(const u8 *img, u64 avail, u32 *text_len, u32 *data_len, u32 *name_hash);
u64            tpl_digest(const u8 *p, u64 n);

#endif /* IFIRM_TEMPLATE_H */
