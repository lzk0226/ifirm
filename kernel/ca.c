#include <ifirm/types.h>
#include <ifirm/ca.h>
#include <ifirm/cap.h>
#include <ifirm/domain.h>
#include <ifirm/ict.h>
#include <ifirm/ipc.h>
#include <ifirm/console.h>
#include <ifirm/status.h>
#include <ifirm/syscall.h>

/* charter 二进制里的一条规则 */
typedef struct {
    u8  dom;         /* 0 = 派生域类通配（只匹配 id>=6） */
    u8  type;
    u8  phase;
    u8  reserved;
    u16 rights;
    u16 reserved2;
    u32 object_id;
} ca_bin_rule_t;

STATIC_ASSERT(sizeof(ca_bin_rule_t) == 12u, "charter rule frozen");

extern const u8 _binary_charter_bin_start[];
extern const u8 _binary_charter_bin_end[];

static ca_bin_rule_t g_rules[CHARTER_MAX_RULES];
static u32 g_nrules;
static u64 g_charter_digest;

static u64 g_requests;
static u64 g_grants;
static u64 g_denied;
static u64 g_revokes;

static ca_audit_t g_log[CA_LOG_MAX];
static u32 g_log_n;          /* 累计写入条数（环形，取模） */

/* 队列槽位：PENDING 待处理、DONE 已出结果等申请者取走、FREE 可复用 */
#define CA_Q_FREE    0u
#define CA_Q_PENDING 1u
#define CA_Q_DONE    2u

typedef struct {
    u32 ticket;        /* (代次 << 8) | 槽位 */
    u8  dom;
    u8  type;
    u8  phase;
    u8  state;
    u16 rights;
    u32 object_id;
    i32 result;
    cap_t cap;
} ca_qent_t;

static u64 fnv64(const u8 *p, u64 n)
{
    u64 h = 1469598103934665603ull;
    for (u64 i = 0; i < n; i++) { h ^= (u64)p[i]; h *= 1099511628211ull; }
    return h;
}

static void ca_log(u32 kind, u8 dom, u8 type, u16 rights, u32 object, i32 result)
{
    ca_audit_t *a = &g_log[g_log_n % CA_LOG_MAX];
    a->kind = kind;
    a->dom = dom;
    a->type = type;
    a->rights = rights;
    a->object = object;
    a->result = result;
    a->t_ms = (u32)now_ms();
    g_log_n++;
}

u32 ca_audit_count(void) { return (g_log_n < CA_LOG_MAX) ? g_log_n : CA_LOG_MAX; }

bool ca_audit_at(u32 i, ca_audit_t *out)
{
    u32 n = ca_audit_count();
    if (i >= n || out == NULL) { return false; }
    /* i=0 最新 */
    u32 idx = (g_log_n - 1u - i) % CA_LOG_MAX;
    *out = g_log[idx];
    return true;
}

u64 ca_requests_total(void) { return g_requests; }
u64 ca_grants_total(void)   { return g_grants; }
u64 ca_denied_total(void)   { return g_denied; }
u64 ca_revokes_total(void)  { return g_revokes; }
u32 ca_rule_count(void)     { return g_nrules; }
u64 ca_charter_digest(void) { return g_charter_digest; }

/* 解析并校验 charter 镜像：magic / version / 条数 / 长度 / 摘要，一条不对就拒绝启动 */
ifirm_status_t ca_init(void)
{
    const u8 *p = _binary_charter_bin_start;
    u64 len = (u64)(_binary_charter_bin_end - _binary_charter_bin_start);
    if (len < CHARTER_HDR_SIZE) { return IFIRM_ERR_CORRUPT; }

    static const char magic[8] = { 'I', 'F', 'I', 'R', 'M', 'C', 'H', 'A' };
    for (u32 i = 0; i < 8u; i++) {
        if (p[i] != (u8)magic[i]) { return IFIRM_ERR_CORRUPT; }
    }

    u32 ver = (u32)p[8] | ((u32)p[9] << 8) | ((u32)p[10] << 16) | ((u32)p[11] << 24);
    u32 nr  = (u32)p[12] | ((u32)p[13] << 8) | ((u32)p[14] << 16) | ((u32)p[15] << 24);
    if (ver != 1u || nr == 0u || nr > CHARTER_MAX_RULES) { return IFIRM_ERR_CORRUPT; }
    if (len != CHARTER_HDR_SIZE + (u64)nr * sizeof(ca_bin_rule_t)) { return IFIRM_ERR_CORRUPT; }

    u64 want = (u64)p[16] | ((u64)p[17] << 8) | ((u64)p[18] << 16) | ((u64)p[19] << 24) |
               ((u64)p[20] << 32) | ((u64)p[21] << 40) | ((u64)p[22] << 48) | ((u64)p[23] << 56);
    u64 got = fnv64(p + CHARTER_HDR_SIZE, len - CHARTER_HDR_SIZE);
    if (want != got) { return IFIRM_ERR_BAD_DIGEST; }
    g_charter_digest = got;

    const u8 *r = p + CHARTER_HDR_SIZE;
    for (u32 i = 0; i < nr; i++) {
        const ca_bin_rule_t *src = (const ca_bin_rule_t *)(const void *)(r + (u64)i * sizeof(ca_bin_rule_t));
        g_rules[i].dom = src->dom;
        g_rules[i].type = src->type;
        g_rules[i].phase = src->phase;
        g_rules[i].reserved = 0u;
        g_rules[i].rights = src->rights;
        g_rules[i].reserved2 = 0u;
        g_rules[i].object_id = src->object_id;
    }
    g_nrules = nr;

    printk("IFIRM-CA charter=0x%x rules=%u digest=0x%x\n",
           (u32)(g_charter_digest >> 32), (u32)g_nrules, (u32)(g_charter_digest & 0xFFFFFFFFu));
    return IFIRM_OK;
}

static bool rule_matches(const ca_bin_rule_t *r, u8 dom, u8 type, u16 rights,
                         u32 object_id, u8 phase)
{
    if (r->dom == 0u) {
        if (dom < 6u) { return false; }          /* 派生域类通配 */
    } else if (r->dom != dom) {
        return false;
    }
    if (r->type != type || r->object_id != object_id) { return false; }
    if ((r->rights & rights) != rights) { return false; }
    if (phase == CA_RUNTIME && r->phase != CA_RUNTIME) { return false; }
    return true;
}

ifirm_status_t ca_authorize(u8 dom, u8 type, u16 rights, u32 object_id, u8 phase, cap_t *out)
{
    for (u32 i = 0; i < g_nrules; i++) {
        if (!rule_matches(&g_rules[i], dom, type, rights, object_id, phase)) { continue; }

        /* 对象必须真实存在，charter 不凭空造对象 */
        if (type == OBJ_CHANNEL && ipc_channel_get(object_id) == NULL) { break; }
        if (type == OBJ_DEVICE && object_id != 1u) { break; }

        ifirm_status_t st = cap_grant(dom, type, object_id, rights, out);
        if (st == IFIRM_OK) {
            g_grants++;
            ca_log(CA_LOG_GRANT, dom, type, rights, object_id, 0);
        }
        return st;
    }
    g_denied++;
    ca_log(CA_LOG_DENY, dom, type, rights, object_id, (i32)IFIRM_ERR_DENIED);
    return IFIRM_ERR_DENIED;
}

/* ---------- 授权请求队列 ----------
 * 域用 SYS_CAP_REQUEST + (a3 & CA_REQ_ASYNC) 入队，内核在时钟中断里批量处理，
 * 用户态用 SYS_CAP_POLL 取结果。票号 = (代次 << 8) | 槽位：槽位复用时代次递增，
 * 旧票号因此永远取不到新请求的结果（没有 ABA）。
 */
static ca_qent_t g_q[CA_QUEUE_MAX];
static u32 g_q_gen;

u32 ca_queue_depth(void)
{
    u32 n = 0u;
    for (u32 i = 0; i < CA_QUEUE_MAX; i++) {
        if (g_q[i].state != CA_Q_FREE) { n++; }
    }
    return n;
}

i64 ca_enqueue(u8 dom, u8 type, u16 rights, u32 object_id, u8 phase)
{
    for (u32 i = 0; i < CA_QUEUE_MAX; i++) {
        if (g_q[i].state != CA_Q_FREE) { continue; }
        g_q_gen++;
        g_q[i].ticket = (g_q_gen << 8) | i;
        g_q[i].dom = dom;
        g_q[i].type = type;
        g_q[i].phase = phase;
        g_q[i].rights = rights;
        g_q[i].object_id = object_id;
        g_q[i].result = 0;
        g_q[i].cap = 0u;
        g_q[i].state = CA_Q_PENDING;
        ca_log(CA_LOG_ENQUEUE, dom, type, rights, object_id, 0);
        return (i64)g_q[i].ticket;
    }
    /* 队列满：拒绝入队，调用者稍后重试（背压，不是策略拒绝） */
    ca_log(CA_LOG_ENQUEUE, dom, type, rights, object_id, (i32)IFIRM_ERR_AGAIN);
    return (i64)IFIRM_ERR_AGAIN;
}

u32 ca_drain(void)
{
    u32 done = 0u;
    for (;;) {
        ca_qent_t *pick = NULL;
        /* 票号最小的先处理：入队顺序即授权顺序（FIFO） */
        for (u32 i = 0; i < CA_QUEUE_MAX; i++) {
            if (g_q[i].state != CA_Q_PENDING) { continue; }
            if (pick == NULL || (i32)(g_q[i].ticket - pick->ticket) < 0) { pick = &g_q[i]; }
        }
        if (pick == NULL) { break; }
        cap_t h = 0u;
        pick->result = (i32)ca_authorize(pick->dom, pick->type, pick->rights,
                                         pick->object_id, pick->phase, &h);
        pick->cap = h;
        pick->state = CA_Q_DONE;
        done++;
    }
    if (done != 0u) {
        printk("IFIRM-CA drain n=%u depth=%u t=%u\n",
               (u32)done, (u32)ca_queue_depth(), (u32)now_ms());
    }
    return done;
}

i64 ca_queue_poll(u32 ticket, cap_t *out)
{
    u32 slot = ticket & 0xFFu;
    u32 gen  = ticket >> 8;
    if (slot >= CA_QUEUE_MAX || gen == 0u) { return (i64)IFIRM_ERR_INVALID_ARG; }
    ca_qent_t *e = &g_q[slot];
    if (e->state == CA_Q_FREE || (e->ticket >> 8) != gen) {
        return (i64)IFIRM_ERR_NOT_FOUND;
    }
    if (e->state == CA_Q_PENDING) { return (i64)IFIRM_ERR_AGAIN; }
    i32 r = e->result;
    if (r == 0 && out != NULL) { *out = e->cap; }
    e->state = CA_Q_FREE;
    e->cap = 0u;
    return (i64)r;
}

/* 启动期自检：必须能在域组合之后跑（cap.c 的 grant 要求目标域存在） */
i64 ca_queue_selftest(void)
{
    i64 tk[CA_QUEUE_MAX];
    u32 want_ok = 0u;
    u32 want_deny = 0u;
    for (u32 i = 0; i < CA_QUEUE_MAX; i++) {
        if (i < 5u) {
            tk[i] = ca_enqueue(1u, OBJ_DEVICE, CAP_RIGHT_WRITE, 1u, CA_BOOT);
            want_ok++;
        } else {
            /* 域 3 在 charter 里没有任何规则：CA 必须拒绝 */
            tk[i] = ca_enqueue(3u, OBJ_DEVICE, CAP_RIGHT_WRITE, 1u, CA_BOOT);
            want_deny++;
        }
    }
    i64 full = ca_enqueue(1u, OBJ_DEVICE, CAP_RIGHT_WRITE, 1u, CA_BOOT);
    u32 depth_full = ca_queue_depth();
    u32 drained = ca_drain();

    u32 ok = 0u;
    u32 deny = 0u;
    u32 bad = 0u;
    for (u32 i = 0; i < CA_QUEUE_MAX; i++) {
        if (tk[i] <= 0) { bad++; continue; }
        cap_t h = 0u;
        i64 st = ca_queue_poll((u32)tk[i], &h);
        if (st == 0) {
            ok++;
            (void)cap_revoke(1u, (u16)((u32)h & CAP_IDX_MASK));
        } else if (st == (i64)IFIRM_ERR_DENIED) {
            deny++;
        } else {
            bad++;
        }
    }

    bool pass = (depth_full == CA_QUEUE_MAX) && (full == (i64)IFIRM_ERR_AGAIN) &&
                (drained == CA_QUEUE_MAX) && (ok == want_ok) && (deny == want_deny) &&
                (bad == 0u) && (ca_queue_depth() == 0u);
    printk("IFIRM-CA queue depth=%u full=%d drained=%u ok=%u deny=%u test=%s\n",
           (u32)depth_full, (i32)full, (u32)drained, (u32)ok, (u32)deny,
           pass ? "ok" : "fail");
    return pass ? IFIRM_OK : IFIRM_ERR_INVALID_STATE;
}
i64 ca_request(trap_frame_t *f)
{
    if (g_current_domain == NULL) { return (i64)IFIRM_ERR_INVALID_STATE; }
    g_requests++;

    u8  type   = (u8)f->x[REG_A0];
    u16 rights = (u16)f->x[REG_A1];
    u32 obj    = (u32)f->x[REG_A2];

    if (rights == 0u || (rights & ~CAP_RIGHTS_ALL) != 0u) {
        g_denied++;
        ca_log(CA_LOG_DENY, (u8)g_current_domain->id, type, rights, obj, (i32)IFIRM_ERR_INVALID_ARG);
        return (i64)IFIRM_ERR_INVALID_ARG;
    }

    if ((f->x[REG_A3] & CA_REQ_ASYNC) != 0u) {
        /* 异步申请：只入队，票号放在 a1；内核在下一个时钟片里处理 */
        i64 tk = ca_enqueue((u8)g_current_domain->id, type, rights, obj, CA_RUNTIME);
        if (tk < 0) { return tk; }
        f->x[REG_A1] = (u64)tk;
        return 0;
    }

    ca_log(CA_LOG_REQUEST, (u8)g_current_domain->id, type, rights, obj, 0);
    cap_t h = 0u;
    ifirm_status_t st = ca_authorize((u8)g_current_domain->id, type, rights, obj, CA_RUNTIME, &h);
    if (st != IFIRM_OK) { return (i64)st; }
    f->x[REG_A1] = (u64)h;
    return 0;
}

i64 ca_revoke_own(trap_frame_t *f)
{
    if (g_current_domain == NULL) { return (i64)IFIRM_ERR_INVALID_STATE; }
    cap_t h = (cap_t)f->x[REG_A0];

    cap_entry_t *e = NULL;
    ifirm_status_t st = cap_check(h, OBJ_ANY, 0u, &e);
    if (st != IFIRM_OK) { return (i64)st; }
    u16 idx = (u16)((u32)h & CAP_IDX_MASK);

    u32 n = cap_revoke_tree((u16)g_current_domain->id, idx);
    g_revokes++;
    ca_log(CA_LOG_REVOKE, (u8)g_current_domain->id, e->object_type, 0u, e->object_id, (i32)n);
    printk("IFIRM-EVT id=8 dom=%u a=%u b=%u c=0 t=%u\n",
           (u32)g_current_domain->id, n, (u32)idx, (u32)now_ms());
    return 0;
}

i64 ca_delegate(trap_frame_t *f)
{
    if (g_current_domain == NULL) { return (i64)IFIRM_ERR_INVALID_STATE; }
    cap_t h = (cap_t)f->x[REG_A0];
    u16 dst = (u16)f->x[REG_A1];
    u16 mask = (u16)f->x[REG_A2];

    cap_entry_t *e = NULL;
    ifirm_status_t st = cap_check(h, OBJ_ANY, 0u, &e);
    if (st != IFIRM_OK) { return (i64)st; }
    u16 idx = (u16)((u32)h & CAP_IDX_MASK);

    cap_t out = 0u;
    st = cap_delegate((u16)g_current_domain->id, idx, dst, mask, &out);
    ca_log(CA_LOG_DELEGATE, (u8)g_current_domain->id, e->object_type, mask, e->object_id, (i32)st);
    if (st != IFIRM_OK) { return (i64)st; }
    f->x[REG_A1] = (u64)out;
    f->x[REG_A2] = (u64)dst;
    return 0;
}
