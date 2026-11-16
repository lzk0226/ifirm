#include <ifirm/types.h>
#include <ifirm/domain.h>
#include <ifirm/cap.h>
#include <ifirm/ict.h>
#include <ifirm/pmm.h>
#include <ifirm/vmm.h>
#include <ifirm/thread.h>
#include <ifirm/ipc.h>
#include <ifirm/console.h>
#include <ifirm/syscall.h>
#include <ifirm/status.h>

#define CHARTER_MAGIC 0x494649524d434852ull    /* "IFIRMCHR" */

typedef struct {
    u64 magic;
    u16 version;
    u16 n_grants;
    u32 reserved;
} charter_t;

/* 本版唯一的组合计划：三个域，一条通道（§19.4 的静态形态） */
static const charter_t g_charter = {
    .magic = CHARTER_MAGIC,
    .version = 1u,
    .n_grants = 2u,
    .reserved = 0u,
};

extern const u8 _binary_tpl_producer_text_bin_start[];
extern const u8 _binary_tpl_producer_text_bin_end[];
extern const u8 _binary_tpl_consumer_text_bin_start[];
extern const u8 _binary_tpl_consumer_text_bin_end[];
extern const u8 _binary_tpl_faulty_text_bin_start[];
extern const u8 _binary_tpl_faulty_text_bin_end[];

typedef struct {
    u8 id;
    const char *name;
    const u8 *text;
    const u8 *text_end;
} plan_t;

static const plan_t g_plan[3] = {
    { 1u, "producer", _binary_tpl_producer_text_bin_start, _binary_tpl_producer_text_bin_end },
    { 2u, "consumer", _binary_tpl_consumer_text_bin_start, _binary_tpl_consumer_text_bin_end },
    { 3u, "faulty",   _binary_tpl_faulty_text_bin_start,   _binary_tpl_faulty_text_bin_end   },
};

domain_t g_domains[MAX_DOMAINS];

static void zero_page(u64 pa)
{
    u8 *p = (u8 *)pa;
    for (u32 i = 0; i < PAGE_SIZE; i++) { p[i] = 0u; }
}

static ifirm_status_t map_new(u64 *root, vaddr_t va, u64 perm, u64 *out_pa)
{
    u64 pa = pmm_alloc();
    if (pa == 0u) { return IFIRM_ERR_NO_MEMORY; }
    zero_page(pa);
    ifirm_status_t st = vmm_map(root, va, pa, perm);
    if (st != IFIRM_OK) { pmm_free(pa); return st; }
    *out_pa = pa;
    return IFIRM_OK;
}

static ifirm_status_t compose_one(const plan_t *p, cap_t chan_cap)
{
    domain_t *d = &g_domains[p->id];
    d->id = p->id;
    d->name = p->name;
    d->root = vmm_new_as();
    if (d->root == NULL) { return IFIRM_ERR_NO_MEMORY; }

    /* 模板槽：0x10000 代码（RX），0x20000 数据（RW，本版为空）+（§20.4） */
    u64 text_pa = 0;
    ifirm_status_t st = map_new(d->root, DOMAIN_SLOT_BASE + DOMAIN_TEXT_OFF,
                                PTE_R | PTE_X | PTE_U, &text_pa);
    if (st != IFIRM_OK) { return st; }
    u64 len = (u64)(p->text_end - p->text);
    if (len > PAGE_SIZE) { return IFIRM_ERR_NO_SPACE; }
    for (u32 i = 0; i < (u32)len; i++) { ((u8 *)text_pa)[i] = p->text[i]; }
    d->mem_pages++;
    d->mem_used++;

    u64 data_pa = 0;
    st = map_new(d->root, DOMAIN_SLOT_BASE + DOMAIN_DATA_OFF, PTE_R | PTE_W | PTE_U, &data_pa);
    if (st != IFIRM_OK) { return st; }
    d->mem_pages++;
    d->mem_used++;

    /* ICT：一页桩 + 一条 function 8 槽（§21.5） */
    st = ict_install(p->id, 8u, 0u, ICT_F_LOCAL, 0u);
    if (st != IFIRM_OK) { return st; }
    u64 ict_pa = 0;
    st = map_new(d->root, DOMAIN_ICT_VA, PTE_R | PTE_X | PTE_U, &ict_pa);
    if (st != IFIRM_OK) { return st; }
    ict_build_page(p->id, (u8 *)ict_pa);
    d->mem_pages++;
    d->mem_used++;

    /* 用户栈：4 页（下面 0x2000F000 留作守卫页，不映射） */
    for (u32 i = 0; i < 4u; i++) {
        u64 sp_pa = 0;
        st = map_new(d->root, DOMAIN_USTACK_BASE + (u64)i * PAGE_SIZE,
                     PTE_R | PTE_W | PTE_U, &sp_pa);
        if (st != IFIRM_OK) { return st; }
        d->mem_pages++;
        d->mem_used++;
    }

    u64 stack_top = DOMAIN_USTACK_BASE + DOMAIN_USTACK_SIZE;
    thread_t *t = thread_create(p->id, DOMAIN_SLOT_BASE + DOMAIN_TEXT_OFF, (u64)chan_cap, stack_top);
    if (t == NULL) { return IFIRM_ERR_NO_SPACE; }
    d->thread_count = 1u;
    printk("IFIRM-DOMAIN id=%u state=READY name=%s ict=%u pages=%u\n",
           (u32)p->id, p->name, ict_count(p->id), (u32)d->mem_pages);
    return IFIRM_OK;
}

ifirm_status_t domain_compose_all(void)
{
    if (g_charter.magic != CHARTER_MAGIC || g_charter.version != 1u) {
        return IFIRM_ERR_BAD_DIGEST;
    }

    for (u32 i = 0; i < ARRAY_LEN(g_plan); i++) {
        domain_t *d = &g_domains[g_plan[i].id];
        d->id = g_plan[i].id;
        d->state = DOMAIN_READY;
        d->name = g_plan[i].name;
        d->trust_level = 1u;
    }

    ifirm_status_t st = ipc_channel_create(1u, 2u);
    if (st != IFIRM_OK) { return st; }

    cap_t h1 = 0u;
    cap_t h2 = 0u;
    st = cap_grant(1u, OBJ_CHANNEL, 1u, CAP_RIGHT_SEND | CAP_RIGHT_CALL, &h1);
    if (st != IFIRM_OK) { return st; }
    st = cap_grant(2u, OBJ_CHANNEL, 1u, CAP_RIGHT_RECV | CAP_RIGHT_REPLY, &h2);
    if (st != IFIRM_OK) { return st; }

    st = compose_one(&g_plan[0], h1);
    if (st != IFIRM_OK) { return st; }
    st = compose_one(&g_plan[1], h2);
    if (st != IFIRM_OK) { return st; }
    return compose_one(&g_plan[2], 0u);
}

void domain_fault(u8 dom, u64 scause, u64 sepc, u64 stval)
{
    domain_t *d = domain_get(dom);
    printk("IFIRM-EVT id=7 dom=%u a=%u b=0x%x c=0 stval=0x%x t=%u\n",
           (u32)dom, (u32)scause, sepc, stval, (u32)now_ms());
    if (d != NULL) {
        d->faults++;
        d->state = DOMAIN_DEAD;
    }
    thread_t *t = thread_of_domain(dom);
    if (t != NULL) { t->state = THREAD_DEAD; }
}

u8 domain_count_ready(void)
{
    u8 n = 0;
    for (u32 i = 0; i < MAX_DOMAINS; i++) {
        if (g_domains[i].state == DOMAIN_READY || g_domains[i].state == DOMAIN_RUNNING) { n++; }
    }
    return n;
}
