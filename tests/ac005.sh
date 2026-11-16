#!/usr/bin/env bash
# AC-005：CA 授权队列——入队/背压/时钟片批量处理/轮询取结果
set -u
cd "$(dirname "$0")/.."
LOG=build/serial.log
mkdir -p build
timeout 30 qemu-system-riscv64 -machine virt -m 256M -smp 1 -nographic \
    -bios default -drive file=build/disk.img,format=raw,if=none,id=d0 \
    -device virtio-blk-device,drive=d0 -kernel build/ifirm.elf >"$LOG" 2>&1 || true

fail=0
need() {
    if grep -aqF "$1" "$LOG"; then printf '  ok   %s\n' "$1"
    else printf '  FAIL %s\n' "$1"; fail=1; fi
}
forbid() {
    if grep -aqF "$1" "$LOG"; then printf '  FAIL forbidden %s\n' "$1"; fail=1; fi
}

# 1) 启动期自检：8 槽填满、第 9 条被背压挡住、批量处理、5 授予 3 拒绝
need "IFIRM-CA queue depth=8 full=-11 drained=8 ok=5 deny=3 steal=1 test=ok"
need "IFIRM-CA drain n=8"

# 2) 域在运行期走异步路径：入队拿票号，而不是阻塞到授权完成
need "producer[1]: ca async ticket="
need "producer[1]: ca async done st=0"
need "producer[1]: ca async revoke=0"
forbid "ca async ticket=-1"
forbid "ca async ticket=0"
forbid "ca async lost"

# 3) 队列不能破坏既有语义
need "producer[1]: ca grant st=0"
need "IFIRM-COMPOSE n=13 ok digest=0x"
forbid "IFIRM-BOOT FAIL"
forbid "IFIRM-PANIC"

if [ "$fail" -eq 0 ]; then printf 'TEST ac-005 PASS\n'; else printf 'TEST ac-005 FAIL\n'; exit 1; fi
