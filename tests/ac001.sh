#!/usr/bin/env bash
# AC-001：内核在 QEMU 上完成引导并跑到用户态互斥场景（对应 Ifirm1.0.md §7.6 启动契约）
set -u
cd "$(dirname "$0")/.."
LOG=build/serial.log
mkdir -p build
timeout 20 qemu-system-riscv64 -machine virt -m 256M -smp 1 -nographic \
    -bios default -kernel build/ifirm.elf >"$LOG" 2>&1 || true

fail=0
need() {
    if grep -qF "$1" "$LOG"; then
        printf '  ok   %s\n' "$1"
    else
        printf '  FAIL %s\n' "$1"
        fail=1
    fi
}
forbid() {
    if grep -qF "$1" "$LOG"; then
        printf '  FAIL forbidden %s\n' "$1"
        fail=1
    fi
}

need "IFIRM-BOOT stage=0"
need "IFIRM-BOARD model=riscv-virtio,qemu ram=256MiB"
need "IFIRM-BOOT stage=1"
need "IFIRM-MEM total=65536"
need "IFIRM-VM on"
need "IFIRM-TICK hz=100"
need "IFIRM-CAPS mmu=1 timer=1 plic=1 uart=1"
need "IFIRM-DOMAIN id=1 state=READY name=producer"
need "IFIRM-DOMAIN id=2 state=READY name=consumer"
need "IFIRM-DOMAIN id=3 state=READY name=faulty"
need "IFIRM-READY domains=3"
need "producer: ict=ok"
need "consumer: seq=1 x=8 -> 16"
need "consumer: seq=2 x=15 -> 30"
need "IFIRM-EVT id=7 dom=3"
need "producer: exit"
need "IFIRM-HALT all-threads-exited"
forbid "IFIRM-PANIC"

if [ "$fail" -eq 0 ]; then
    printf 'TEST ac-001 PASS\n'
else
    printf 'TEST ac-001 FAIL\n'
    exit 1
fi
