# Ifirm 内核 v2（CA / 原子模板 / 复合内核真正落地）

按 Ifirm1.0.md 实现的可引导 RISC-V 64 微内核。v2 与 v1 的差别只有一句话：
**三个核心构想不再是装饰，而是有可证伪断言支撑的机制。**

## 三个构想现在的形态

| 构想 | v1（装饰） | v2（可证伪） |
|---|---|---|
| 中央授权控制器 CA | 启动期两次 cap_grant | 唯一授权入口 ca_authorize() + charter 规则表 + 运行期申请 syscall 11 + 撤销 syscall 12 + 撤销传播到 ICT 槽 + 四个计数器 |
| 原子模板 | 裸 .text 拷进一页 | 40 字节模板头（magic/version/text_len/data_len/name_hash/FNV-1a64 摘要）+ 组合期验摘要 + 摘要不符直接引导失败 |
| 复合内核 | 三个域、三个不同模板 | 五个域、两个模板各实例化两次；计划表构建期固定，运行时不建域 |
## 现在能跑什么

~~~text
IFIRM-BOOT stage=0 / stage=1
IFIRM-BOARD model=riscv-virtio,qemu ram=256MiB
IFIRM-MEM total=65536 free=.. used_kernel=.. frag=..
IFIRM-VM on / IFIRM-TICK hz=100 / IFIRM-CAPS ...
IFIRM-DOMAIN id=1..5 state=READY name=.. tpl=0x.. ict=.. pages=..
IFIRM-READY domains=5 pages_used=625
producer[1]: ict=ok              经 ICT 槽 0 进内核，槽里带 console 能力
producer[1]: ca grant st=0       运行期向 CA 申请能力，命中 charter 的 CA_RUNTIME 规则
producer[1]: revoke con=0        CA 撤销刚发出去的能力
IFIRM-EVT id=8 dom=1 a=1 b=2     撤销传播：1 个 ICT 槽被标记失效
producer[1]: ict after revoke=-15  同一个槽立刻返回 REVOKED
producer[2]: seq=0..2 x=.. reply=..  同一模板的第二个实例，走另一条通道
consumer[1]/[2]: seq=0..2 x=.. -> ..  两个 consumer 实例各自算 x*2 回复
faulty: ca denied=-5             charter 里没有的对象，CA 拒绝
IFIRM-EVT id=7 dom=3             faulty 写地址 0，被杀后系统继续
producer[1]: revoke chan=0 / call after revoke=-15  撤销通道后旧句柄被代次挡住
IFIRM-CA requests=2 grants=6 denied=1 revokes=2
IFIRM-HALT all-threads-exited
~~~

## 构建与运行

需要 clang、lld、riscv64-linux-gnu-objcopy / -ld、qemu-system-riscv64（Ubuntu 24.04 的 qemu-system-misc 包），
宿主另外需要一个 cc 来编译模板打包工具 tools/mkimg.c。

~~~sh
make          # build/tools/mkimg → 三个 .img → blobs.o → build/ifirm.elf
make run      # QEMU 串口输出到当前终端
make test     # AC-001（28 项断言）+ AC-002（篡改模板必须引导失败）
~~~

QEMU 参数固定为 -machine virt -m 256M -smp 1 -nographic -bios default -kernel。

## 四项增量的落点

1. 模板头与摘要校验：include/ifirm/template.h、kernel/template.c、tools/mkimg.c。镜像 = 40 字节头 + .text + .data；摘要覆盖 text||data（FNV-1a 64，可分段续算）。组合期 tpl_verify() 失败即 IFIRM-BOOT FAIL tpl-digest / tpl-corrupt。
2. CA 计数器与运行期入口：kernel/ca.c。charter 是 7 条 {dom,type,phase,rights,object_id} 规则；CA_BOOT 只在组合期可用，运行期申请只有 CA_RUNTIME 能过；每个域只能拿到 charter 写明的对象，对象还必须真实存在。
3. 撤销传播到 ICT：ict_revoke_cap()（kernel/ict.c）用影子表 g_slot_cap / g_slot_dead，槽的 16 字节布局不变；cap_check() 改成先比代次再查有效位，所以撤销后旧句柄报 -15 REVOKED 而不是 -6 BAD_HANDLE。
4. 同一模板两实例：kernel/domain.c 的 g_plan[] 让 producer/consumer 各出现两次，组合行打印 tpl=0x... 摘要，AC-001 直接比对两个实例的 tpl 必须相同。组合失败走 compose_rollback() → vmm_release()，已映射的页整套回收（v1 是泄漏）。

## 目录

| 路径 | 内容 |
|---|---|
| boot/start.S | 入口、清 .bss、跳到 C |
| arch/rv64/ | UART、SBI、陷阱入口与分发 |
| kernel/ | 打印、板级、物理内存、Sv39、能力、CA、模板、线程、ICT、IPC、域组合、系统调用 |
| include/ifirm/ | 同名头文件（template.h、ca.h 为 v2 新增） |
| user/ | 用户库与三个模板（producer / consumer / faulty） |
| tools/mkimg.c | 宿主工具：打包模板镜像，不进内核 |
| tests/ | ac001 正向场景、ac002 篡改模板负向场景 |

## 仍然有意省略（对照手册）

- 无 KIP 页、无 journal/检查点、无模板注册表与组合缓存（CAC/IRC/BBC 仍只打印 cache=C1）。
- CA 不建内核线程，不实现异步队列与反向引用表；撤销是同步扫影子表。
- 运行期不创建/销毁域（决策：动态授权 + 静态组合），域数量与模板在构建期固定。
- 无 PLIC / 设备中断、无 DMA、无 SMP、无持久化；单 hart、单 CA、无抢占式调度。
- 崩溃只做 domain_fault 打印 + 杀线程，不做域重启。

## v2 被测试抓到的真 bug（留档）

模板最初只拷 .text，而 -Os 把除以 10 的魔数常量放进了 .data，于是用户态所有多位数都打印成
00 / 0? / 0>（16 → 00，15 → 0?，30 → 0>）。根因不是格式化代码，而是模板载荷不完整：.data 段
没被拷进可写页。修法是镜像里补 .data、摘要覆盖 text||data。这条 bug 正好说明 AC-002 这种
能失败的测试比 17 条只会通过的断言有用。

