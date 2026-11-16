#ifndef IFIRM_DOMAIN_H
#define IFIRM_DOMAIN_H

#include <ifirm/types.h>
#include <ifirm/cap.h>
#include <ifirm/ict.h>

#define MAX_DOMAINS 16

/* §20.4 域地址空间布局（冻结） */
#define DOMAIN_GUARD_VA     0x00000000u
#define DOMAIN_KIP_VA       0x00001000u
#define DOMAIN_SLOT_BASE    0x00010000u
#define DOMAIN_TEXT_OFF     0x00000000u
#define DOMAIN_DATA_OFF     0x00010000u
#define DOMAIN_ICT_VA       0x20000000u
#define DOMAIN_USTACK_BASE  0x20010000u
#define DOMAIN_USTACK_STRIDE 0x00005000u
#define DOMAIN_USTACK_SIZE  0x00004000u

enum {
    DOMAIN_FREE = 0,
    DOMAIN_READY,
    DOMAIN_RUNNING,
    DOMAIN_DEAD
};

typedef struct {
    u16  id;
    u8   state;
    u8   trust_level;
    u16  thread_count;
    u16  mem_pages;
    u16  mem_used;
    u16  cap_used;
    u64 *root;
    cap_entry_t caps[256];
    ict_entry_t ict[ICT_MAX_ENTRIES];
    u32  faults;
    u32  tpl_id;         /* 模板身份（FNV-1a 32 of 模板名），组合期验摘要后填入 */
    u64  ready_ms;
    const char *name;
} domain_t;

extern domain_t g_domains[MAX_DOMAINS];
extern domain_t *g_current_domain;

static inline domain_t *domain_get(u16 id)
{
    return (id < MAX_DOMAINS) ? &g_domains[id] : NULL;
}

ifirm_status_t domain_compose_all(void);
void           domain_fault(u8 dom, u64 scause, u64 sepc, u64 stval);
u8             domain_count_ready(void);

#endif /* IFIRM_DOMAIN_H */
