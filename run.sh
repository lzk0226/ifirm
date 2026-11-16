#!/bin/sh
# 一条命令跑起来；退出码来自 QEMU（超时 124 是可接受结局）
set -e
cd "$(dirname "$0")"
make
exec timeout 20 qemu-system-riscv64 -machine virt -m 256M -smp 1 \
     -nographic -bios default -kernel build/ifirm.elf
