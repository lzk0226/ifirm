#include <ifirm/types.h>
#include <ifirm/ca.h>
#include <ifirm/cap.h>
#include <ifirm/domain.h>
#include <ifirm/ict.h>
#include <ifirm/ipc.h>
#include <ifirm/console.h>
#include <ifirm/status.h>
#include <ifirm/syscall.h>

/*
 * charter：唯一授权者的全部依据都在表里，不在 C 代码的分支里（§14.3）。
 * phase=CA_BOOT 的规则只在组合期可用；运行期申请一律拒绝。
 */
typedef struct {
    u8  dom;
    u8  type;
    u8  phase;
    u16 rights;
    u32 object_id;
} ca_rule_t;

static const ca_rule_t g_rules[] = {
    { 1u, OBJ_CHANNEL, CA_BOOT, CAP_RIGHT_SEND | CAP_RIGHT_CALL,    1u },
    { 2u, OBJ_CHANNEL, CA_BOOT, CAP_RIGHT_RECV | CAP_RIGHT_REPLY,   1u },
    { 4u, OBJ_CHANNEL, CA_BOOT, CAP_RIGHT_SEND | CAP_RIGHT_CALL,    2u },
    { 5u, OBJ_CHANNEL, CA_BOOT, CAP_RIGHT_RECV | CAP_RIGHT_REPLY,   2u },
    { 1u, OBJ_DEVICE, CA_BOOT, CAP_RIGHT_WRITE,                    1u },
    { 1u, OBJ_DEVICE, CA_RUNTIME, CAP_RIGHT_WRITE,                 1u },
    { 4u, OBJ_DEVICE, CA_RUNTIME, CAP_RIGHT_WRITE,                 1u },
    /* dom=0 是"派生域类"通配：运行期由 spawn 装配出的域（id>=6）都落在这一条上 */
    { 0u, OBJ_DEVICE, CA_BOOT,    CAP_RIGHT_WRITE,                 1u },
};

static u64 g_requests;
static u64 g_grants;
static u64 g_denied;
static u64 g_revokes;

u64 ca_requests_total(void) { return g_requests; }
u64 ca_grants_total(void)   { return g_grants; }
u64 ca_denied_total(void)   { return g_denied; }
u64 ca_revokes_total(void)  { return g_revokes; }

ifirm_status_t ca_authorize(u8 dom, u8 type, u16 rights, u32 object_id, u8 phase, cap_t *out)
{
    for (u32 i = 0; i < ARRAY_LEN(g_rules); i++) {
        const ca_rule_t *r = &g_rules[i];
        u8 want = (r->dom == 0u) ? dom : r->dom;
        if (want != dom || r->type != type || r->object_id != object_id) { continue; }
        if (r->dom == 0u && dom < 6u) { continue; }
        if ((r->rights & rights) != rights) { continue; }
        if (phase == CA_RUNTIME && r->phase != CA_RUNTIME) { continue; }

        /* 对象必须真实存在，charter 不凭空造对象 */
        if (type == OBJ_CHANNEL && ipc_channel_get(object_id) == NULL) { break; }
        if (type == OBJ_DEVICE && object_id != 1u) { break; }

        ifirm_status_t st = cap_grant(dom, type, object_id, rights, out);
        if (st == IFIRM_OK) { g_grants++; }
        return st;
    }
    g_denied++;
    return IFIRM_ERR_DENIED;
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
        return (i64)IFIRM_ERR_INVALID_ARG;
    }

    cap_t h = 0u;
    ifirm_status_t st = ca_authorize(g_current_domain->id, type, rights, obj, CA_RUNTIME, &h);
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

    st = cap_revoke(g_current_domain->id, idx);
    if (st != IFIRM_OK) { return (i64)st; }

    u32 slots = ict_revoke_cap(g_current_domain->id, idx);
    g_revokes++;
    printk("IFIRM-EVT id=8 dom=%u a=%u b=%u c=0 t=%u\n",
           (u32)g_current_domain->id, slots, (u32)idx, (u32)now_ms());
    return 0;
}
