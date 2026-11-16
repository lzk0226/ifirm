#!/usr/bin/env bash
# AC-003：程序来自磁盘 —— 内核映像里没有 hello 的字符串，磁盘映像里有；破坏磁盘头必须引导失败
set -u
cd "$(dirname "$0")/.."
mkdir -p build
LOG=build/serial-disk.log

QEMU='qemu-system-riscv64 -machine virt -m 256M -smp 1 -nographic -bios default'
DRIVE='-drive file=build/disk.img,format=raw,if=none,id=d0 -device virtio-blk-device,drive=d0'

fail=0
need() {
    if grep -aqF "$1" "$LOG"; then printf '  ok   %s\n' "$1"
    else printf '  FAIL %s\n' "$1"; fail=1; fi
}

timeout 30 $QEMU $DRIVE -kernel build/ifirm.elf >"$LOG" 2>&1 || true

need "IFIRM-FS on files=2"
need "IFIRM-EXEC name=hello dom=6"
need "IFIRM-EXEC name=ping dom=7"
need "hello[1]: loaded from disk (elf)"
need "hello[1]: tick="
need "hello[1]: bye"
need "ping[7]: alive from disk (elf)"
need "ping[7]: sum 1..10=55"

# 最关键的一条：这段字符串不在内核映像里，只在磁盘映像里
if [ "$(grep -ac 'loaded from disk (elf)' build/ifirm.elf)" -eq 0 ]; then
    printf '  ok   %s\n' "program string absent from kernel image"
else
    printf '  FAIL %s\n' "program string absent from kernel image"; fail=1
fi
if [ "$(grep -ac 'loaded from disk (elf)' build/disk.img)" -ge 1 ]; then
    printf '  ok   %s\n' "program string present on disk image"
else
    printf '  FAIL %s\n' "program string present on disk image"; fail=1
fi

# 负向：破坏磁盘头 magic，引导必须停在没有文件系统这一步
cp build/disk.img build/disk.orig
printf 'X' | dd of=build/disk.img bs=1 seek=0 count=1 conv=notrunc 2>/dev/null
LOG=build/serial-nodisk.log
timeout 20 $QEMU $DRIVE -kernel build/ifirm.elf >"$LOG" 2>&1 || true
if grep -aqF "IFIRM-BOOT FAIL disk" "$LOG"; then
    printf '  ok   %s\n' "corrupt disk rejected"
else
    printf '  FAIL %s\n' "corrupt disk rejected"; fail=1
fi
if grep -aqF "IFIRM-EXEC" "$LOG"; then
    printf '  FAIL %s\n' "no exec after corrupt disk"; fail=1
else
    printf '  ok   %s\n' "no exec after corrupt disk"
fi
cp build/disk.orig build/disk.img
rm -f build/disk.orig

if [ $fail -eq 0 ]; then printf 'TEST ac-003 PASS\n'; else printf 'TEST ac-003 FAIL\n'; fi
exit $fail
