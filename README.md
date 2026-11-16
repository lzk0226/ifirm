# Ifirm 内核 v4.2（三要素 + 可用的用户态 + CA 授权队列 + 缺陷修复）

中文 · [English](README.en.md) · [架构说明](docs/architecture.md) · [安全边界](SECURITY.md) · [Apache-2.0](LICENSE)

按 Ifirm1.0.md 实现的 RISC-V 64 微内核。v4/v4.1 的判据是用户提的那三个要素必须能跑起来，
其余一切（网络、图形、SMP、持久化）都不在本版范围内：

| 要素 | v4 的落地方式 | 可以被证伪的地方 |
|---|---|---|
| 中央授权控制器 CA | 策略不在代码里，而在 charter 镜像里（32 字节头 + 12 字节/条规则）；唯一授权入口 ca_authorize()；派生树 + 树形撤销；256 条审计环；8 槽授权队列（满即 -AGAIN 背压、时钟片里批量处理、申请者凭票号轮询）；4 个计数器 | 改 charter 一个字节 → 摘要对不上 → IFIRM-COMP FAIL name=ca，一条 IFIRM-DOMAIN 都不出现（ac004） |
| 复合内核 | 13 个内核原子组件按声明（名字/提供/依赖/层号）组合，内核自己校验层号单调、依赖已被提供，并对每个组件算 FNV-1a32 摘要，最后给出整体 IFIRM-COMPOSE digest | 任一组件缺失或层号错乱 → IFIRM-COMP FAIL name=<组件>，引导在 stage=2 之前停住 |
| 原子组件 | 用户态模板是带 v2 清单的镜像：magic/版本/ABI/名字/服务位/栈页/摘要；内核侧注册表按名字查表，组合期检查"计划要的能力模板是否声明" | 篡改模板一个字节 → 摘要不符 → IFIRM-COMP FAIL name=tpl，没有 READY（ac002） |

外加一条同样重要的判据：**用户态真的能用**。shell、coreutils、可写文件系统、ELF 加载、
进程创建/等待全部在磁盘上而不是编进内核（ac003 检查 `loaded from disk (elf)` 在内核映像里 0 次、磁盘里 ≥1 次）。

## CA 授权队列

同步路径（a3=0）是“调用即授权”，够用但不满足“CA 是中介”：域会被卡在 ecall 上等策略判定。
v4.1 补的是异步路径，语义只有三条：

| 动作 | 接口 | 语义 |
|---|---|---|
| 入队 | SYS_CAP_REQUEST，a3 的 bit0 = 1 | 不判定策略，只占一个槽；成功返回**票号**（a1），队列满返回 -AGAIN |
| 处理 | 时钟中断里 ca_drain() | 按票号从小到大（FIFO）逐条走 ca_authorize()，不占内核线程 |
| 取结果 | SYS_CAP_POLL（27），a0=票号 a1=&句柄 | 0 完成（句柄写回 a1 指向的用户地址）、-11 未处理、-5 票号不属于本域、其它为拒绝码 |

票号是 (代次 << 8) | 槽位，槽位复用时**代次递增**：旧票号因此永远取不到新请求的结果，不存在 ABA。
票号还带归属：`ca_queue_poll(dom, ticket, ...)` 只认发起域，别的域枚举票号一律 -DENIED 且不释放槽。
队列满不是策略拒绝（不记 deny、不增 denied 计数），调用者应当稍后重试。

ac005 用三组断言把它钉住：启动期自检必须打出 depth=8 full=-11 drained=8 ok=5 deny=3 steal=1 test=ok
（5 条 charter 允许 + 3 条不允许填满 8 槽，第 9 条被背压，另有一次跨域偷票必须 -5）、域 1 必须拿到票号而不是阻塞、
票号最终必须被处理成 st=0 并撤销成功。

## 启动序列（一次 make run 能看到的）

~~~text
IFIRM-BOOT stage=0
IFIRM-BOARD model=riscv-virtio,qemu ram=256MiB timebase=10000000 uart=0x10000000 plic=0xc000000
IFIRM-BOOT stage=1
IFIRM-MEM total=65536 free=.. used_kernel=.. frag=..
IFIRM-VM on / IFIRM-TICK hz=100 / IFIRM-CAPS mmu=1 timer=1 plic=1 uart=1 ...
IFIRM-COMP n=1 name=board lvl=1 digest=0x..        ← 13 条，内核原子组件
...
IFIRM-CA charter=0x9a0b264 rules=9 digest=0x..
IFIRM-CA drain n=8 depth=8 t=108                                    ← 启动期自检
IFIRM-CA queue depth=8 full=-11 drained=8 ok=5 deny=3 steal=1 test=ok
IFIRM-TPL n=1 name=producer ver=2 abi=1 svc=0x3 stack=4 digest=0x..
IFIRM-TPLSET n=3 ok digest=0x..
IFIRM-DOMAIN id=1 state=READY name=producer tpl=0x.. tplname=producer ict=1 pages=8
...（5 个组合域）
IFIRM-READY domains=5 pages_used=.. t_ms=..
IFIRM-FS on files=8 bytes=203916 blk=v1 rw=1 free=7769
IFIRM-INIT name=sh dom=6 entry=0x10000 pages=2
IFIRM-REAP selftest=ok caps_used=0 ict=0                            ← 域回收自检（v4.2）
IFIRM-COMP n=13 name=init lvl=9 digest=0x..
IFIRM-COMPOSE n=13 ok digest=0x..
IFIRM-BOOT stage=2
~~~

调度开始后，域 1 走一次异步申请（不阻塞）：

~~~text
producer[1]: ca async ticket=2304
IFIRM-CA drain n=1 depth=1 t=365
producer[1]: ca async done st=0 h=57348
producer[1]: ca async revoke=0
~~~

任何一步失败都打 `IFIRM-COMP FAIL name=<组件> st=<状态码>` 或 `IFIRM-BOOT FAIL <原因> (<码>)` 并停机。

## 用户态：从磁盘上的 /bin/sh 开始

启动完成后进入 shell（它自己也是从 virtio-blk 读出来、经 ELF 加载器装进新域的普通程序）：

~~~text
Ifirm 1.0 minimal system
type 'help' for commands
ifirm> ls /
/:
  bin             <dir>
  tmp             <dir>
  etc             <dir>
  dev             <dir>
4 entries
ifirm> ca
CA audit (newest first, 13 shown)
  revoke dom=1 type=4 rights=0x0 obj=1 st=1 t=331ms
  grant dom=1 type=5 rights=0x2 obj=1 st=0 t=312ms
  deny  dom=3 type=5 rights=0x400 obj=1 st=-5 t=311ms
  ...
ifirm> mkdir /tmp/demo
ifirm> write /tmp/demo/a.txt hello-ca
write: /tmp/demo/a.txt 9 bytes
ifirm> cat /tmp/demo/a.txt
hello-ca
ifirm> ps
PID STATE   KIND PAGES CAPS FAULTS NAME
6   ready   1     7     1      0      sh
domains=1
ifirm> exit
IFIRM-CA requests=2 grants=11 denied=1 revokes=2
IFIRM-HALT all-threads-exited blocked=0 t_ms=9760
~~~

内建命令：`help ps ca mkdir rm write exit`；外部程序在 /bin 里：`sh ls cat echo hello ping crash bugcheck`，
由 shell 按名字 spawn，内核从文件系统读 ELF、建地址空间、传 argc/argv、等退出码。

## 文件系统 IFIRMFS2（可写）

| 结构 | 位置 | 说明 |
|---|---|---|
| 超级块 | LBA 0（512 B） | magic "IFIRMFS2"、版本 2、inode 数 64、位图 LBA 1 起 2 扇区、inode 表 LBA 5 起 8 扇区、数据 LBA 13、总块数 8192 |
| 位图 | LBA 1–2 | 1 bit/块，1024 字节覆盖 8192 块 |
| inode | 64 B × 64 | type/used/nlink/size/parent/blocks[7]/indirect/name[16]/mtime |
| 数据 | LBA 13 起 | 4 MiB 磁盘；单文件 7 直接块 + 128 间接块 = 67.5 KiB |

读写都是 write-through：改 inode/位图/超级块立即按 512 B 扇区写回，没有日志、没有缓存层。
目录也是 inode，`fs_readdir` 遍历整张 inode 表按 parent 过滤（64 个 inode 的规模下够用）。

## 系统调用（a7 = 号，a6 非 0 时走 ICT 槽）

```
1 IPC_SEND   2 IPC_RECV   3 IPC_CALL   4 IPC_REPLY   5 YIELD      6 EXIT
7 CLOCK      8 DEBUG_WRITE 9 DOMAIN_INFO 10 CONSOLE_WRITE 11 CAP_REQUEST 12 CAP_REVOKE
13 CAP_DELEGATE 14 CAP_AUDIT 15 SPAWN    16 WAIT         17 CONSOLE_READ
18 FS_OPEN   19 FS_READ   20 FS_WRITE  21 FS_CLOSE    22 FS_UNLINK
23 FS_READDIR 24 FS_STAT  25 PROC_LIST 26 FS_MKDIR
27 CAP_POLL
```

返回值约定：a0 = 状态码（0 成功、负数为错误），需要额外结果的调用把值放在 a1
（例如 SPAWN 回 a1 = 新域号、FS_READ 回 a1 = 实读字节数）。**除约定外的寄存器一律不动**——
这一条是 v4 的一个真 bug 换来的，见文末。

## 构建与运行

需要 clang、lld、riscv64-linux-gnu-objcopy/-ld、qemu-system-riscv64（Ubuntu 24.04 的 qemu-system-misc），
宿主另需一个 cc 编译 tools/mkimg.c、tools/mkfs.c 与 tools/mkcharter.c。

~~~sh
make          # mkimg ×3 → blobs.o → ifirm.elf；mkcharter → charter.bin；mkfs → disk.img
make run      # QEMU 串口输出到当前终端（20 秒自动退出）
make test     # AC-001（正向）+ AC-002/003/004（三个负向）+ AC-005（授权队列）+ AC-006（缺陷回归）
~~~

QEMU 参数固定：`-machine virt -m 256M -smp 1 -nographic -bios default` +
`-drive file=build/disk.img,format=raw,if=none,id=d0 -device virtio-blk-device,drive=d0`。
串口 RX FIFO 只有 16 字节，测试脚本喂命令必须 `sleep` 分批，否则命令在内核启动期间就被丢掉。

## 目录

| 路径 | 内容 |
|---|---|
| boot/start.S | 入口、清 .bss、跳到 C |
| arch/rv64/ | UART（含接收）、SBI、陷阱入口与分发 |
| kernel/component.c | 复合内核：13 个原子组件的声明、层号/依赖校验、组合摘要 |
| kernel/ca.c | 中央授权控制器：charter 解析、规则匹配、审计环、派生与树形撤销、8 槽授权队列 |
| kernel/template.c + tplreg.c | 模板 v2 清单校验与注册表 |
| kernel/cap.c | 能力表、代次、派生链、撤销传播到 ICT |
| kernel/domain.c / spawn.c | 静态组合五个域 / 从磁盘 spawn 新域（argv 拷到用户栈） |
| kernel/fs.c + virtio.c + elf.c | IFIRMFS2、virtio-mmio 块设备、ELF64 加载器 |
| kernel/syscall.c | 27 个系统调用 + 用户指针校验 |
| user/ | ulib.h + shell + 七个程序（含 bugcheck 回归程序）+ 三个模板 |
| tools/ | mkimg（模板打包）、mkfs（磁盘镜像）、mkcharter（策略编译） |
| tests/ | ac001 正向、ac002 篡改模板、ac003 用 shell 跑完整用户态、ac004 篡改 charter、ac005 授权队列、ac006 缺陷回归 |

规模：源码 6,117 行（kernel/arch/user/tools/include/boot），内核映像 text 35,354 + data 10,656 + bss 270,460 字节。

## 开源与许可

Apache-2.0，见 [LICENSE](LICENSE)。英文说明见 [README.en.md](README.en.md)，架构与对比见
[docs/architecture.md](docs/architecture.md)，威胁模型与已知弱点见 [SECURITY.md](SECURITY.md)。
CI 只做构建 + 三个不依赖键盘输入的验收（AC-001/002/004），因为完整套件在 TCG 上约 80 秒且
有三处依赖 sleep 的串口时序。

## 仍然有意省略

- 无 SMP、无 PLIC/设备中断、无 DMA、无抢占（时间片只在陷阱返回时轮转）、无掉电持久化。
- 无 KIP 页、无 journal、无模板注册表缓存（CAC/IRC/BBC 只打印 cache=C1）。
- CA 不建内核线程：授权队列只在时钟中断里批量处理，没有独立的 CA 线程、优先级和取消；撤销仍是同步扫表。
- 文件系统有日志之外的一切简化：无硬链接语义、无权限位、无时间戳更新（mtime 只写一次）。
- 域崩溃只打印 + 杀线程，不重启；没有真实驱动模型，控制台是唯一设备。

## 被测试抓到的真 bug（留档）

**v2：模板载荷不完整。** 模板最初只拷 .text，而 -Os 把除法魔数放进 .data，于是用户态所有多位数
打印成 00 / 0? / 0>。根因不是格式化代码，而是模板载荷不完整；修法是镜像补 .data、摘要覆盖 text||data。

**v3：描述符标志位。** 读盘时数据描述符漏了 VIRTQ_DESC_F_WRITE，设备把 4 KiB 当成只读缓冲，
used.len 只回 1，内核读到全零的文件系统头，症状是 IFIRM-BOOT FAIL disk——很容易误判成 fs.c 写错。

**v3：恒等映射骗过加载器。** 内核把 0..1 GiB 恒等映射给 UART/virtio 用，ELF 加载器原来用 vmm_walk()
判断"这页是否已映射"，于是把用户 .text 的 0x10000 当成已映射，直接往物理 0x10000 写（QEMU virt 上
那里没有 RAM）→ store access fault。修法：只有带 PTE_U 的 4 KiB 项才算"自己的页"。

**v4：系统调用回写了调用者的 a2/a3。** SPAWN 为了"顺手"把 entry 和 pages 也放进 a2/a3，
但用户编译器只被告知 a0/a1 会变，于是 shell 把一个算好的指针（当时的 a3）继续用，
值变成 1，下一条 `ld a1,0(a3)` 就 load page fault——崩溃发生在 shell，根因在内核的返回约定。
修法：只回写 a1，其余寄存器原样保留；顺带把 CAP_AUDIT 的计数也改成 a0 返回。

**v4：审计记录结构体对齐。** shell 里把 result 写成 i64，内核是 i32，差 4 字节填充，整张审计表
错位成乱码（dom=166、type=72）。修法：用户侧 `int result`，并在注释里写死 28 字节的约定。

**v4：console 读的返回约定。** 内核把读到的字节数放 a1、a0 返回 0，但用户库 `cread()` 只看 a0，
于是 shell 永远认为没输入。修法：直接把字节数放 a0 返回（读 0 字节表示暂时没有输入）。

## v4.2 缺陷审计（11 个，全部已修）

ac006 是新加的回归测试：它用一个普通用户态程序 `bugcheck` 去**故意**打内核的四个薄弱面
（坏 ELF、连续装载失败、删根目录、跨页指针），断言内核既不 panic 也不泄漏资源。

| # | 症状 | 根因 | 修法 | 覆盖 |
|---|---|---|---|---|
| 1 | `rm /` 之后整个文件系统不可用 | fs_unlink 对 "/" 特判返回根 inode，随后零化它 | 根 inode 拒删，返回 -2 | ac006 |
| 2 | 装载失败 10 次后 spawn 永久 -NO_SPACE | 失败路径只释放地址空间，state 停在 READY，槽再也选不中 | 失败统一走 spawn_reset(id) | ac006（12 次坏 ELF 后仍能起来） |
| 3 | 域号复用后句柄/ICT 槽"继承"上一代 | domain_reap 只清 root/state，不清 caps[256] 与 ICT 影子表 | spawn_reset 清空能力表并调 ict_reset | ac001 + ac006（REAP 自检） |
| 4 | 恶意 ELF 能让内核 panic | `phoff + phnum*56`、`off + filesz` 用加法判界，大数回绕通过检查 | 改成减法形式，两处都返回 -CORRUPT | ac006（badph/badseg 均 -14，无 PANIC） |
| 5 | 用户指针跨页时把数据写进低物理区/MMIO | user_range_ok 只查每页首地址，xcopy 不再验 PTE_U | 逐页查 V\|U，xcopy 用 PTE 物理地址重算 | ac006（跨页指针 -2） |
| 6 | IPC 消息被吞、调用者永久阻塞 | try_deliver 先出队再判接收者缓冲大小 | 先 peek 长度，溢出只唤醒不出队 | ac001（producer/consumer 回归） |
| 7 | 回复 xcopy 失败后调用者永远等 | do_ipc_reply 提前 return，没清 call_pending、没唤醒 | 成功失败都清 pending 并唤醒（带错误码） | 手工审查；无独立 AC |
| 8 | 任何域能凭票号偷走别人的句柄 | ca_queue_poll 只比 slot/gen，不校验发起域 | 增加 dom 参数，不匹配返回 -DENIED | ac005（自检里 steal=1） |
| 9 | 大 off 写文件绕过 67.5 KiB 上限；目录读错误被当成读完 | `off+len` 是 u32 加法；readdir 把一切负值映射成 0 | 用 u64 比较；只有 -NOT_FOUND 映射成 0 | 代码审查 |
| 10 | 已死域还能被授予新句柄 | grant_at 只查 d != NULL | 拒绝 FREE/DEAD 域，堵住回收前的窗口 | 代码审查 |
| 11 | 多域并发输出交错成乱码（"bhueglclhoe:"），且一行几十次系统调用 | cprintf 每字符一次 CONSOLE_WRITE | 160 字节行缓冲，整行一次写出 | ac006（按行断言的前提） |

两处修复没有独立自动化覆盖：**7** 需要构造"回复时对端地址空间已坏"的场景，**10** 依赖时序窗口，
两者目前靠代码审查与 ac001 的 IPC 回归间接保证。仍然存在的设计问题（本轮未改）：
`SYS_CONSOLE_WRITE` 不检查调用者是否持有 console 能力——这是 ABI 级设计缺陷，
所有域都能直接打印；要修得先给该调用加 cap 检查并同步改所有用户程序。
