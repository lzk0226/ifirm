#!/usr/bin/env bash
# AC-002：模板镜像被改一个字节 → 摘要对不上 → 组合失败，且不出现 READY
set -u
cd "$(dirname "$0")/.."
mkdir -p build
LOG=build/serial-corrupt.log

cp build/tpl_consumer.img build/tpl_consumer.orig
# 改 .text 的第一个字节：头是 64 字节，摘要覆盖 text||data，必然对不上
printf '\xff' | dd of=build/tpl_consumer.img bs=1 seek=64 count=1 conv=notrunc 2>/dev/null
( cd build && riscv64-linux-gnu-ld -r -b binary tpl_producer.img tpl_consumer.img tpl_faulty.img charter.bin -o blobs_corrupt.o )

OBJS=$(ls build/*.o | grep -v '^build/blobs\.o$')
clang --target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mcmodel=medany \
    -nostdlib -fuse-ld=lld -T linker/kernel.ld -Wl,--gc-sections -Wl,--no-relax \
    -o build/ifirm-corrupt.elf $(echo "$OBJS" | grep -v 'blobs_corrupt\.o') build/blobs_corrupt.o

timeout 20 qemu-system-riscv64 -machine virt -m 256M -smp 1 -nographic \
    -bios default -drive file=build/disk.img,format=raw,if=none,id=d0 \
    -device virtio-blk-device,drive=d0 -kernel build/ifirm-corrupt.elf >"$LOG" 2>&1 || true

fail=0
need() {
    if grep -aqF "$1" "$LOG"; then printf '  ok   %s\n' "$1"
    else printf '  FAIL %s\n' "$1"; fail=1; fi
}
need "IFIRM-COMP FAIL name=tpl"
need "IFIRM-BOOT FAIL compose"
if grep -aqF "IFIRM-READY" "$LOG"; then
    printf '  FAIL %s\n' "no READY after template corruption"; fail=1
else
    printf '  ok   %s\n' "no READY after template corruption"
fi

cp build/tpl_consumer.orig build/tpl_consumer.img
rm -f build/tpl_consumer.orig build/blobs_corrupt.o build/ifirm-corrupt.elf
( cd build && riscv64-linux-gnu-ld -r -b binary tpl_producer.img tpl_consumer.img tpl_faulty.img charter.bin -o blobs.o )

if [ "$fail" -eq 0 ]; then printf 'TEST ac-002 PASS\n'; else printf 'TEST ac-002 FAIL\n'; exit 1; fi
