#ifndef IFIRM_TEMPLATE_H
#define IFIRM_TEMPLATE_H

#include <ifirm/types.h>
#include <ifirm/status.h>

/*
 * 原子模板（atomic component）v2：64 字节头 + .text 载荷 + .data 载荷。
 * 头里带名字、服务声明、ABI、栈页数与摘要；组合器只认头里的声明，
 * 不认镜像在内存里的位置（模板不是域，同一模板可实例化多次）。
 */
#define TPL_VERSION   2u
#define TPL_HDR_SIZE  64u
#define TPL_NAME_LEN  16u
#define TPL_ABI       1u
#define TPL_MAX_TEXT  0x10000u
#define TPL_MAX_DATA  0x10000u

/* 模板声明的服务（组合期用来对 charter 提要求） */
#define TPL_SVC_CONSOLE 0x01u
#define TPL_SVC_CHANNEL 0x02u
#define TPL_SVC_REPLY   0x04u
#define TPL_SVC_FAULT   0x08u

typedef struct {
    u8   magic[8];               /*  0: "IFIRMTPL" */
    u16  version;                /*  8 */
    u16  hdr_size;               /* 10 */
    u32  text_len;               /* 12 */
    u32  data_len;               /* 16 */
    u32  name_hash;              /* 20: FNV-1a32(name) */
    u64  digest;                 /* 24: FNV-1a64(text || data) */
    char name[TPL_NAME_LEN];     /* 32 */
    u16  abi;                    /* 48 */
    u16  stack_pages;            /* 50 */
    u32  services;               /* 52 */
    u32  flags;                  /* 56 */
    u32  reserved;               /* 60 */
} tpl_hdr_t;

STATIC_ASSERT(sizeof(tpl_hdr_t) == TPL_HDR_SIZE, "tpl header frozen");

u32            tpl_name_hash(const char *name);
u64            tpl_digest(const u8 *p, u64 n);
/* 校验：magic / version / hdr_size / abi / 长度上限 / 载荷摘要 */
ifirm_status_t tpl_verify(const u8 *img, u64 avail, const tpl_hdr_t **out);

#endif /* IFIRM_TEMPLATE_H */
