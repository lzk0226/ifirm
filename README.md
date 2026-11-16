# Ifirm 第一版内核（v1）

按 `Ifirm1.0.md` 实现的可引导 RISC-V 64 微内核最小版本：引导 → 物理/虚拟内存 →
能力空间 → 域组合 → 用户态 IPC 调用/回复，外加一次故障域被杀后系统继续运行。

## 现在能跑什么

```
IFIRM-BOOT stage=0 / stage=1      ← 引导契约
IFIRM-BOARD ...                   ← 板级探测（QEMU virt）
IFIRM-MEM ...                     ← 位图页分配器
IFIRM-VM on / IFIRM-TICK hz=100   ← Sv39 开启、S 态定时器
IFIRM-CAPS ...                    ← 能力探测
IFIRM-DOMAIN id=1..3 READY        ← 三个域完成组合
IFIRM-READY domains=3
producer: ict=ok                  ← 经 ICT 槽进入内核（function 8 DEBUG_WRITE）
consumer: seq=0..3 x=.. -> ..     ← producer/consumer 三轮 IPC CALL/REPLY
IFIRM-EVT id=7 dom=3              ← faulty 域写地址 0，被内核杀掉
IFIRM-HALT all-threads-exited     ← 全部线程正常退出
```

## 构建与运行

需要 `clang`、`lld`、`riscv64-linux-gnu-objcopy`/`-ld`、`qemu-system-riscv64`。

```sh
make            # 产出 build/ifirm.elf
make run        # QEMU 串口输出到当前终端
make test       # 跑 AC-001 断言（tests/ac001.sh）
```

QEMU 参数固定为 `-machine virt -m 256M -smp 1 -nographic -bios default -kernel`。

## 目录

| 路径 | 内容 |
|---|---|
| `boot/start.S` | 入口、清 .bss、跳到 C |
| `arch/rv64/` | UART、SBI、陷阱入口与分发 |
| `kernel/` | 打印、板级、物理内存、Sv39、能力、线程、ICT、IPC、域组合、系统调用 |
| `user/` | 用户库与三个模板（producer/consumer/faulty） |
| `linker/`、`Makefile`、`tests/` | 链接脚本、构建、验收脚本 |

## 与 `Ifirm1.0.md` 的偏差（v1 有意省略）

- 无 KIP 页、无 journal/检查点、无模板注册表与组合缓存（CAC/IRC/BBC 只打印 C1）。
- CA 只在启动期做 charter 校验与授权，不建内核线程；无撤销流程。
- ICT 主要用于验证"安装期权限检查 + 槽跳转"，跨域通信走 IPC 系统调用。
- 无 PLIC/设备中断、无 DMA、无 SMP、无持久化。

## 已知限制

- 单 hart、单 CA、无抢占式调度（时间片到了只更新时间，不强制切换）。
- 用户态没有 `memset/memcpy`（模板里避免结构体初始化）。
- 崩溃只做 `domain_fault` 打印 + 杀线程，不做域重启。
