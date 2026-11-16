#include <ifirm/types.h>
#include <ifirm/domain.h>
#include <ifirm/cap.h>
#include <ifirm/ca.h>
#include <ifirm/ict.h>
#include <ifirm/pmm.h>
#include <ifirm/vmm.h>
#include <ifirm/thread.h>
#include <ifirm/ipc.h>
#include <ifirm/console.h>
#include <ifirm/syscall.h>
#include <ifirm/status.h>
#include <ifirm/template.h>
#include <ifirm/tplreg.h>

/*
 * 组合计划：域的名字、用哪个原子模板、要哪些能力、实例号，全部是构建期数据。
 * 组合器只做三件事：按名字取模板（核验摘要已由模板注册组件完成）、
 * 向 CA 申请能力、把能力映射进 ICT 与该域的地址空间。
 */
typedef struct {
    u8  id;
    const char *name;        /* 域实例名 */
    const char *tpl;         /* 原子模板名 */
    u16 chan;                /* 0 = 无通道 */
    u16 chan_rights;
    u8  has_console;
    u8  instance;
} plan_t;

/* 同一模板（producer/consumer）各实例化两次：模板不等于域（§20.2） */
static const plan_t g_plan[] = {
    { 1u, "producer",   "producer", 1u, CAP_RIGHT_SEND | CAP_RIGHT_CALL,  1u, 1u },
    { 2u, "consumer",   "consumer", 1u, CAP_RIGHT_RECV | CAP_RIGHT_REPLY, 0u, 1u },
    { 3u, "faulty",     "faulty",   0u, 0u,                               0u, 1u },
    { 4u, "producer#2", "producer", 2u, CAP_RIGHT_SEND | CAP_RIGHT_CALL,  0u, 2u },
    { 5u, "consumer#2", "consumer", 2u, CAP_RIGHT_RECV | CAP_RIGHT_REPLY, 0u, 2u },
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

/*
 * 组合一个域：取模板 → 建地址空间 → 装模板 → CA 授权 → 装 ICT → 建线程。
 * 任何一步失败都返回错误码，由 domain_compose_all 统一回滚已建好的域。
 */
static ifirm_status_t compose_one(const plan_t *p)
{
    const tpl_ent_t *tpl = tplreg_find(p->tpl);
    if (tpl == NULL) { return IFIRM_ERR_NOT_FOUND; }

    /* 组合期能力检查：计划要的能力，模板必须声明自己需要（否则是计划写错） */
    if (p->chan != 0u && (tpl->hdr->services & TPL_SVC_CHANNEL) == 0u) { return IFIRM_ERR_DENIED; }
    if (p->has_console != 0u && (tpl->hdr->services & TPL_SVC_CONSOLE) == 0u) {
        return IFIRM_ERR_DENIED;
    }

    domain_t *d = &g_domains[p->id];
    d->id = p->id;
    d->name = p->name;
    d->tpl_id = tpl->hdr->name_hash;
    d->root = vmm_new_as();
    if (d->root == NULL) { return IFIRM_ERR_NO_MEMORY; }

    u32 text_len = tpl->hdr->text_len;
    u32 data_len = tpl->hdr->data_len;
    const u8 *text = tpl->img + TPL_HDR_SIZE;
    const u8 *data = text + text_len;

    u32 text_pages = (text_len + PAGE_SIZE - 1u) / PAGE_SIZE;
    for (u32 pg = 0; pg < text_pages; pg++) {
        u64 pa = 0;
        ifirm_status_t st = map_new(d->root, DOMAIN_SLOT_BASE + DOMAIN_TEXT_OFF + (u64)pg * PAGE_SIZE,
                                    PTE_R | PTE_X | PTE_U, &pa);
        if (st != IFIRM_OK) { return st; }
        for (u32 i = 0; i < PAGE_SIZE; i++) {
            u32 off = pg * PAGE_SIZE + i;
            ((u8 *)pa)[i] = (off < text_len) ? text[off] : 0u;
        }
        d->mem_pages++;
    }

    u32 data_pages = (data_len + PAGE_SIZE - 1u) / PAGE_SIZE;
    if (data_pages == 0u) { data_pages = 1u; }
    for (u32 pg = 0; pg < data_pages; pg++) {
        u64 pa = 0;
        ifirm_status_t st = map_new(d->root, DOMAIN_SLOT_BASE + DOMAIN_DATA_OFF + (u64)pg * PAGE_SIZE,
                                    PTE_R | PTE_W | PTE_U, &pa);
        if (st != IFIRM_OK) { return st; }
        for (u32 i = 0; i < PAGE_SIZE; i++) {
            u32 off = pg * PAGE_SIZE + i;
            ((u8 *)pa)[i] = (off < data_len) ? data[off] : 0u;
        }
        d->mem_pages++;
    }

    /* 能力只来自 CA：先授权，再让 ICT 槽引用它 */
    cap_t chan = 0u;
    if (p->chan != 0u) {
        ifirm_status_t st = ca_authorize(p->id, OBJ_CHANNEL, p->chan_rights, p->chan, CA_BOOT, &chan);
        if (st != IFIRM_OK) { return st; }
    }
    cap_t con = 0u;
    if (p->has_console != 0u) {
        ifirm_status_t st = ca_authorize(p->id, OBJ_DEVICE, CAP_RIGHT_WRITE, 1u, CA_BOOT, &con);
        if (st != IFIRM_OK) { return st; }
    }

    ifirm_status_t st;
    if (p->has_console != 0u) {
        st = ict_install(p->id, 8u, (u16)((u32)con & CAP_IDX_MASK), ICT_F_NEEDS_CAP, 0u);
    } else {
        st = ict_install(p->id, 8u, 0u, ICT_F_LOCAL, 0u);
    }
    if (st != IFIRM_OK) { return st; }

    u64 ict_pa = 0;
    st = map_new(d->root, DOMAIN_ICT_VA, PTE_R | PTE_X | PTE_U, &ict_pa);
    if (st != IFIRM_OK) { return st; }
    ict_build_page(p->id, (u8 *)ict_pa);
    d->mem_pages++;

    u32 stack_pages = tpl->hdr->stack_pages;
    if (stack_pages > 4u) { stack_pages = 4u; }   /* 布局冻结 16 KiB 用户栈 */
    for (u32 i = 0; i < stack_pages; i++) {
        u64 sp_pa = 0;
        st = map_new(d->root, DOMAIN_USTACK_BASE + (u64)i * PAGE_SIZE,
                     PTE_R | PTE_W | PTE_U, &sp_pa);
        if (st != IFIRM_OK) { return st; }
        d->mem_pages++;
    }

    u64 stack_top = DOMAIN_USTACK_BASE + (u64)stack_pages * PAGE_SIZE;
    thread_t *t = thread_create(p->id, DOMAIN_SLOT_BASE + DOMAIN_TEXT_OFF,
                                (u64)chan, (u64)con, (u64)p->instance, stack_top);
    if (t == NULL) { return IFIRM_ERR_NO_SPACE; }
    d->thread_count = 1u;

    printk("IFIRM-DOMAIN id=%u state=READY name=%s tpl=0x%x tplname=%s ict=%u pages=%u\n",
           (u32)p->id, p->name, (u32)(d->tpl_id & 0xFFFFFFFFu), tpl->name,
           (u32)ict_count(p->id), (u32)d->mem_pages);
    return IFIRM_OK;
}

/* 回滚：把已经建了一半的域整套释放，保证失败后没有泄漏的页 */
static void compose_rollback(void)
{
    for (u32 i = 0; i < ARRAY_LEN(g_plan); i++) {
        domain_t *d = &g_domains[g_plan[i].id];
        if (d->root == NULL) { continue; }
        vmm_release(d->root);
        d->root = NULL;
        d->state = DOMAIN_FREE;
        d->thread_count = 0u;
        d->mem_pages = 0u;
    }
}

ifirm_status_t domain_compose_all(void)
{
    for (u32 i = 0; i < ARRAY_LEN(g_plan); i++) {
        g_domains[g_plan[i].id].state = DOMAIN_READY;
        g_domains[g_plan[i].id].trust_level = 1u;
    }

    ifirm_status_t st = ipc_channel_create(1u, 2u);
    if (st == IFIRM_OK) { st = ipc_channel_create(4u, 5u); }
    if (st != IFIRM_OK) { return st; }

    for (u32 i = 0; i < ARRAY_LEN(g_plan); i++) {
        st = compose_one(&g_plan[i]);
        if (st != IFIRM_OK) {
            compose_rollback();
            return st;
        }
    }
    return IFIRM_OK;
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
    if (t != NULL) { t->state = THREAD_DEAD; t->exit_code = (u32)IFIRM_ERR_INVALID_STATE; }
    thread_wake_domain(dom, (i64)IFIRM_ERR_INVALID_STATE);
}

u8 domain_count_ready(void)
{
    u8 n = 0;
    for (u32 i = 0; i < MAX_DOMAINS; i++) {
        if (g_domains[i].state == DOMAIN_READY || g_domains[i].state == DOMAIN_RUNNING) { n++; }
    }
    return n;
}
