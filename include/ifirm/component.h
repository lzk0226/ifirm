#ifndef IFIRM_COMPONENT_H
#define IFIRM_COMPONENT_H

#include <ifirm/types.h>

/*
 * 复合内核：内核不是一整块，而是一组「内核原子组件」按声明组合起来的。
 * 每个组件声明 名字 / 提供的服务 / 依赖的服务 / 层号 / 初始化函数。
 * 组合器在启动期做三件事：
 *   1) 按层号升序初始化（层号即依赖顺序，避免运行期拓扑排序）
 *   2) 逐个校验依赖的服务已经被更早的组件提供（缺失即拒绝启动）
 *   3) 对每个组件算接口摘要，并给出整核复合摘要
 */
#define KCOMP_MAX 16
#define KCOMP_SVC_MAX 256

typedef struct {
    const char *name;       /* 组件名，如 "pmm" */
    const char *provides;   /* 提供的服务，空格分隔 */
    const char *requires;   /* 依赖的服务，空格分隔 */
    u8          level;      /* 层号：必须严格递减依赖，1 起 */
    ifirm_status_t (*init)(void);
} kcomponent_t;

typedef struct {
    const char *name;
    const char *provides;
    u32         digest;     /* 接口摘要（名|提供|依赖） */
} kcomp_info_t;

/* 启动上下文：交给组件的参数，避免组件自己去读全局 */
void kcomp_set_context(u64 hartid, u64 dtb_pa, u64 kernel_end);

/* 组合并初始化整个内核；任一组件失败即返回其错误码 */
ifirm_status_t kernel_compose(void);

u32                 kcomp_count(void);
const kcomp_info_t *kcomp_at(u32 i);
u32                 kcomp_composite_digest(void);
bool                kcomp_provided(const char *service);

#endif /* IFIRM_COMPONENT_H */
