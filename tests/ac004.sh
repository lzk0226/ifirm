#!/usr/bin/env bash
# AC-004：CA 的策略是镜像不是代码 —— 改 charter 一个字节，授权表校验失败，系统拒绝启动
set -u
cd "$(dirname "$0")/.."
mkdir -p build
LOG=build/serial-charter.log

cp build/charter.bin build/charter.orig
# 偏移 32 = 第 1 条规则的 dom 字段：改成 7，摘要必然对不上
printf '\x07' | dd of=build/charter.bin bs=1 seek=32 count=1 conv=notrunc 2>/dev/null
( cd build && riscv64-linux-gnu-ld -r -b binary tpl_producer.img tpl_consumer.img tpl_faulty.img charter.bin -o blobs_badchr.o )

OBJS=$(ls build/*.o | grep -v '^build/blobs\.o$')
clang --target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mcmodel=medany \
    -nostdlib -fuse-ld=lld -T linker/kernel.ld -Wl,--gc-sections -Wl,--no-relax \
    -o build/ifirm-badchr.elf $(echo "$OBJS" | grep -v 'blobs_badchr\.o') build/blobs_badchr.o

timeout 20 qemu-system-riscv64 -machine virt -m 256M -smp 1 -nographic \
    -bios default -drive file=build/disk.img,format=raw,if=none,id=d0 \
    -device virtio-blk-device,drive=d0 -kernel build/ifirm-badchr.elf >"$LOG" 2>&1 || true

fail=0
need() {
    if grep -aqF "$1" "$LOG"; then printf '  ok   %s\n' "$1"
    else printf '  FAIL %s\n' "$1"; fail=1; fi
}
need "IFIRM-COMP FAIL name=ca"
if grep -aqF "IFIRM-DOMAIN" "$LOG"; then
    printf '  FAIL %s\n' "no domain composed with a bad charter"; fail=1
else
    printf '  ok   %s\n' "no domain composed with a bad charter"
fi

cp build/charter.orig build/charter.bin
rm -f build/charter.orig build/blobs_badchr.o build/ifirm-badchr.elf
( cd build && riscv64-linux-gnu-ld -r -b binary tpl_producer.img tpl_consumer.img tpl_faulty.img charter.bin -o blobs.o )

if [ "$fail" -eq 0 ]; then printf 'TEST ac-004 PASS\n'; else printf 'TEST ac-004 FAIL\n'; exit 1; fi
