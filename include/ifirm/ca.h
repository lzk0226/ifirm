#ifndef IFIRM_CA_H
#define IFIRM_CA_H

#include <ifirm/types.h>
#include <ifirm/status.h>
#include <ifirm/cap.h>
#include <ifirm/thread.h>

/* 授权阶段：启动期组合用的规则，运行期申请不可用（§14.2 的静态形态） */
#define CA_BOOT    0u
#define CA_RUNTIME 1u

u64 ca_requests_total(void);
u64 ca_grants_total(void);
u64 ca_denied_total(void);
u64 ca_revokes_total(void);

/* 唯一授权入口：先查 charter 允许表，再落句柄 */
ifirm_status_t ca_authorize(u8 dom, u8 type, u16 rights, u32 object_id, u8 phase, cap_t *out);

i64 ca_request(trap_frame_t *f);    /* syscall 11：域在运行期申请能力 */
i64 ca_revoke_own(trap_frame_t *f); /* syscall 12：撤销自身某句柄并传播 */

#endif /* IFIRM_CA_H */
