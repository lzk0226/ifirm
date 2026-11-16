# Ifirm 内核 v3（从磁盘加载并运行外部程序）

按 Ifirm1.0.md 实现的 RISC-V 64 微内核。v3 的一句话判据：
**内核能运行一个不是自己编译进去的程序。**

v1 是能引导的骨架；v2 把 CA / 模板摘要 / 复合内核从装饰变成可证伪的机制；
v3 补上了 Linux 0.01 意义上的那一层：磁盘驱动 + 文件系统 + ELF 加载器 + spawn。

## v3 新增：程序来自 virtio-blk，不来自内核映像

四个部件，全部在本仓库内自实现，没有链接任何库：

| 部件 | 位置 | 关键点 |
|---|---|---|
| virtio-mmio 块设备 | kernel/virtio.c | 同时支持 legacy（v1）与 modern（v2）两种寄存器语义；8 个 mmio 槽扫描；单请求同步读 |
| 只读文件系统 | kernel/fs.c + tools/mkfs.c | 8 个扇区头 + 24 字节/条目录 + 512 对齐数据区，最多 32 文件 |
| ELF64 加载器 | kernel/elf.c | 只认 ET_EXEC / EM_RISCV；按 PT_LOAD 逐页分配、整页填零、按段权限映射（W^X 仍由 vmm_map 强制） |
| spawn | kernel/spawn.c | 从 id 6 起找空域；建地址空间、铺 2 MiB 模板槽外的用户段、4 页用户栈、装 ICT、经 CA 拿 console 能力、建线程 |

启动输出多出三行：

~~~text
IFIRM-FS on files=2 bytes=46080 blk=v1
IFIRM-EXEC name=hello dom=6 entry=0x10000 segs=2 pages=2
IFIRM-EXEC name=ping dom=7 entry=0x10000 segs=1 pages=1
hello[1]: loaded from disk (elf)
ping[7]: alive from disk (elf)
hello[1]: tick=150708400
hello[1]: bye
~~~

AC-003 把"程序不在内核里"变成两条能失败的断言：

1. 字符串 `loaded from disk (elf)` 在 build/ifirm.elf 里出现 0 次，在 build/disk.img 里出现 ≥1 次；
2. 把 disk.img 的第一个字节改掉（破坏文件系统 magic），引导必须停 at `IFIRM-BOOT FAIL disk`，且不出现任何 IFIRM-EXEC。

## v2 的三个构想（保持有效）

| 构想 | v1（装饰） | v2/v3（可证伪） |
|---|---|---|
| 中央授权控制器 CA | 启动期两次 cap_grant | 唯一授权入口 ca_authorize() + charter 规则表 + 运行期申请 + 撤销传播到 ICT + 四个计数器 |
| 原子模板 | 裸 .text 拷进一页 | 40 字节头 + FNV-1a64 摘要 + 组合期校验，摘要不符即引导失败 |
| 复合内核 | 三个域 | 五个域、两个模板各实例化两次；计划表构建期固定，运行时不建域（v3 的 spawn 是这条决策的唯一例外，见"有意省略"） |

## 现在能跑什么

~~~text
IFIRM-BOOT stage=0 / stage=1
IFIRM-BOARD model=riscv-virtio,qemu ram=256MiB
IFIRM-MEM total=65536 free=.. used_kernel=.. frag=..
IFIRM-VM on / IFIRM-TICK hz=100 / IFIRM-CAPS ...
IFIRM-DOMAIN id=1..5 state=READY name=.. tpl=0x.. ict=.. pages=..
IFIRM-READY domains=5 pages_used=644
IFIRM-FS on files=2 bytes=46080 blk=v1
IFIRM-EXEC name=hello dom=6 ... / IFIRM-EXEC name=ping dom=7 ...
hello[1]: loaded from disk (elf) / ping[7]: alive from disk (elf)
producer[1]: ict=ok              经 ICT 槽 0 进内核，槽里带 console 能力
producer[1]: ca grant st=0       运行期向 CA 申请能力，命中 charter 的 CA_RUNTIME 规则
producer[1]: revoke con=0        CA 撤销刚发出去的能力
IFIRM-EVT id=8 dom=1 a=1 b=2     撤销传播：1 个 ICT 槽被标记失效
producer[1]: ict after revoke=-15
producer[2]/consumer[1]/consumer[2]: 同一模板的第二实例，走另一条通道
faulty: ca denied=-5             charter 里没有的对象，CA 拒绝
IFIRM-EVT id=7 dom=3             faulty 写地址 0，被杀后系统继续
producer[1]: revoke chan=0 / call after revoke=-15
IFIRM-CA requests=2 grants=7 denied=1 revokes=2
IFIRM-HALT all-threads-exited
~~~

## 构建与运行

需要 clang、lld、riscv64-linux-gnu-objcopy / -ld、qemu-system-riscv64（Ubuntu 24.04 的 qemu-system-misc 包），
宿主另外需要一个 cc 来编译模板打包工具 tools/mkimg.c 与磁盘制作工具 tools/mkfs.c。

~~~sh
make          # mkimg → 三个模板镜像 → blobs.o → ifirm.elf；mkfs → disk.img
make run      # QEMU 串口输出到当前终端（20 秒自动退出）
make test     # AC-001（28 项）+ AC-002（3 项）+ AC-003（9 项）
~~~

QEMU 参数固定为 -machine virt -m 256M -smp 1 -nographic -bios default，
外加 -drive file=build/disk.img,format=raw,if=none,id=d0 -device virtio-blk-device,drive=d0。

## v3 代码落点

1. 块设备：kernel/virtio.c。virtio-mmio 从 0x10001000 起 8 个槽，每槽 0x1000；legacy 用 QueuePFN + 连续两页
   （desc/avail 在页 0、used 在页 1），modern 用三个独立页 + FEATURES_OK + 接受 VIRTIO_F_VERSION_1；
   一次读 = 3 个描述符（请求头 / 数据 / 状态字节），同步轮询 used->idx。
2. 文件系统：tools/mkfs.c（宿主）+ kernel/fs.c（内核）共用一套格式：magic "IFIRMFS1"、u32 版本/文件数/字节数、
   32 条 24 字节目录、数据从 4096 字节起按 512 对齐。挂载只校验头，不建缓存。
3. ELF 加载：kernel/elf.c。判据是 PT_LOAD 的 vaddr/filesz/memsz；每页先看目标地址空间里是否已有自己的
   4 KiB 映射（带 PTE_U 的项才算），否则新分配、整页填零、按 PF_R/W/X 映射。
4. spawn：kernel/spawn.c。console 能力只能由 CA 发：ca_authorize(id, OBJ_DEVICE, CAP_RIGHT_WRITE, 1u, CA_BOOT, &con)；
   charter 里 dom=0 的规则表示"派生域类"通配，只对 id>=6 生效。

## 目录

| 路径 | 内容 |
|---|---|
| boot/start.S | 入口、清 .bss、跳到 C |
| arch/rv64/ | UART、SBI、陷阱入口与分发 |
| kernel/ | 打印、板级、物理内存、Sv39、能力、CA、模板、线程、ICT、IPC、域组合、系统调用、virtio、fs、elf、spawn |
| include/ifirm/ | 同名头文件（v2 增 template.h/ca.h，v3 增 virtio.h/fs.h/elf.h/spawn.h） |
| user/ | 用户库、三个模板（producer/consumer/faulty）、两个磁盘程序（prog_hello.c、prog_ping.c） |
| tools/ | mkimg.c 打包模板镜像；mkfs.c 制作 disk.img。都不进内核 |
| tests/ | ac001 正向场景、ac002 篡改模板负向、ac003 程序来自磁盘 + 破坏磁盘负向 |

## 仍然有意省略（对照手册）

- 无 KIP 页、无 journal/检查点、无模板注册表与组合缓存（CAC/IRC/BBC 仍只打印 cache=C1）。
- CA 不建内核线程，不实现异步队列与反向引用表；撤销是同步扫影子表。
- 文件系统只读、无目录、无创建/删除；磁盘程序名写死在 main.c 的两元素表里（"程序来自磁盘"成立，"程序可被任意启动"不成立）。
- spawn 是"运行期建域"，与 v2 的"静态组合"决策冲突；当前只在引导末尾用一次，CLI 的运行时建域事务仍未实现。
- 无 PLIC / 设备中断、无 DMA、无 SMP、无持久化；单 hart、单 CA、无抢占式调度。
- 崩溃只做 domain_fault 打印 + 杀线程，不做域重启。

## 被测试抓到的真 bug（留档）

**v2：模板载荷不完整。** 模板最初只拷 .text，而 -Os 把除以 10 的魔数常量放进了 .data，于是用户态
所有多位数都打印成 00 / 0? / 0>。根因不是格式化代码，而是模板载荷不完整；修法是镜像里补 .data、
摘要覆盖 text||data。这条说明能失败的测试比 17 条只会通过的断言有用。

**v3：描述符标志位。** 读盘时数据描述符漏了 VIRTQ_DESC_F_WRITE，设备于是把这 4 KiB 当只读缓冲，
used.len 只回 1（状态字节），内核读到全零的文件系统头。症状是 `IFIRM-BOOT FAIL disk`，
但错误码来自"magic 不匹配"，很容易误判成 fs.c 写错。

**v3：恒等映射骗过了加载器。** 内核地址空间把 0..1 GiB 恒等映射（给 UART/PLIC/virtio 用），
ELF 加载器原来用 vmm_walk() 判断"这页是否已映射"，于是把用户 .text 的 0x10000 当成已映射，
直接往物理地址 0x10000 写（QEMU virt 上那里没有 RAM），触发 store access fault。
修法：只有带 PTE_U 的 4 KiB 项才算"自己的页"，恒等大页必须被 vmm_map 拆分后覆盖。
