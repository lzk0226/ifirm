# Ifirm 1.0 内核（L1 实现）

规范：../Ifirm_OS.md（45 章，375 条 REQ，46 条 AC）

## 当前状态

M0 / GATE-0：Day-0 最小闭环成立（AC-001 PASS，498 ms）。

## 构建与运行

    make            # 编译 -> build/ifirm.elf, build/ifirm.bin
    make run        # QEMU 启动，Ctrl-A X 退出
    make test       # AC-001 自动验收
    make clean

## 目录

    boot/start.S       入口、栈、清 BSS
    arch/rv64/         RISC-V 平台相关（UART 等）
    kernel/            平台无关内核逻辑
    include/           公共头
    linker/kernel.ld   布局，0x80200000（OpenSBI 之后）
    tests/             验收脚本

## 约定

- 代码一律 C11 freestanding，无 libc、无第三方库。
- 每个源文件头写 ifirm: req=...,ac=... 注释，以便 make trace 生成追踪矩阵。
- 提交信息格式：类别: 一句话 (REQ|AC)。
