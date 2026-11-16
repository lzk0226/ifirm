#ifndef IFIRM_SPAWN_H
#define IFIRM_SPAWN_H

#include <ifirm/types.h>
#include <ifirm/elf.h>

/*
 * 从文件系统里的 ELF 建一个新域。程序不是编进内核的：路径解析、读盘、
 * 装载、能力申请、栈上 argv 拷贝都在这里完成（§26.4）。
 *   argc/argv 为 0/NULL 表示不带参数（init 与内核自起的域）。
 */
ifirm_status_t domain_spawn_path(const char *path, u32 argc, const char *const *argv,
                                 u16 *out_id, elf_info_t *info);

/* 回收已退出（DEAD）的域：释放地址空间与线程槽，spawn 前调用 */
void domain_reap(void);

/* 启动期自检：域回收必须同时清掉 cap 表与 ICT 槽（否则域号复用会继承旧状态） */
i64 spawn_reap_selftest(void);

#endif /* IFIRM_SPAWN_H */
