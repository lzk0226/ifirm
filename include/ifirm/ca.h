#ifndef IFIRM_CA_H
#define IFIRM_CA_H

#include <ifirm/types.h>
#include <ifirm/status.h>
#include <ifirm/cap.h>
#include <ifirm/thread.h>

/* 授权阶段：启动期组合用的规则，运行期申请走 CA_RUNTIME（§14.2 的静态形态） */
#define CA_BOOT    0u
#define CA_RUNTIME 1u

/*
 * 中央授权控制器（CA）的唯一策略来源是 charter 镜像（构建期由 tools/mkcharter 生成，
 * 启动期解析并校验摘要）。CA 自己不保存分支逻辑：规则表决定一切。
 */
#define CHARTER_MAGIC 0x494649524d434852ull    /* "IFIRMCHR" */
#define CHARTER_MAX_RULES 32u
#define CHARTER_HDR_SIZE  32u

/* 审计记录：CA 的每一次决定都留痕，用户态可用 SYS_CAP_AUDIT 读走 */
#define CA_LOG_MAX 256u
#define CA_LOG_REQUEST  1u
#define CA_LOG_GRANT    2u
#define CA_LOG_DENY     3u
#define CA_LOG_REVOKE   4u
#define CA_LOG_DELEGATE 5u
#define CA_LOG_ENQUEUE  6u   /* 入队（异步申请） */
#define CA_LOG_DRAIN    7u

typedef struct {
    u32 kind;
    u32 dom;
    u32 type;
    u32 rights;
    u32 object;
    i32 result;
    u32 t_ms;
} ca_audit_t;

STATIC_ASSERT(sizeof(ca_audit_t) == 28u, "ca audit record frozen");

ifirm_status_t ca_init(void);

/* 授权请求队列（§14.4 的异步形态）：固定槽位，满时 -AGAIN 形成背压 */
#define CA_QUEUE_MAX 8u
#define CA_REQ_ASYNC 1u        /* SYS_CAP_REQUEST 的 a3 bit0：只入队，不等待 */

u64 ca_requests_total(void);
u64 ca_grants_total(void);
u64 ca_denied_total(void);
u64 ca_revokes_total(void);
u32 ca_rule_count(void);
u64 ca_charter_digest(void);

/* 唯一授权入口：先查 charter 允许表，再落句柄 */
ifirm_status_t ca_authorize(u8 dom, u8 type, u16 rights, u32 object_id, u8 phase, cap_t *out);

i64 ca_request(trap_frame_t *f);     /* syscall 11：域在运行期申请能力 */
i64 ca_revoke_own(trap_frame_t *f);  /* syscall 12：撤销自身句柄并沿派生链传播 */
i64 ca_delegate(trap_frame_t *f);    /* syscall 13：把句柄派生给另一个域 */

/* 队列：入队 → 时钟中断里批量处理 → 申请者轮询取结果 */
i64 ca_enqueue(u8 dom, u8 type, u16 rights, u32 object_id, u8 phase);
i64 ca_queue_poll(u8 dom, u32 ticket, cap_t *out); /* 0 完成 / -AGAIN 未处理 / 票号不属于该域 -DENIED */
u32 ca_drain(void);                          /* 处理所有待决项，返回条数 */
u32 ca_queue_depth(void);
i64 ca_queue_selftest(void);                 /* 启动期自检：满、背压、批量 */

u32  ca_audit_count(void);
bool ca_audit_at(u32 i, ca_audit_t *out);   /* i=0 是最新一条 */

#endif /* IFIRM_CA_H */
