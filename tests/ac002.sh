#!/usr/bin/env bash
# AC-002：模板摘要校验 —— 篡改模板镜像一个字节，必须引导失败且不出现 IFIRM-READY
set -u
cd "$(dirname "$0")/.."
mkdir -p build
LOG=build/serial-corrupt.log

cp build/tpl_consumer.img build/tpl_consumer.orig
# 改掉 .text 的第一个字节（头部之后，offset 40），摘要必然对不上
printf '\xff' | dd of=build/tpl_consumer.img bs=1 seek=40 count=1 conv=notrunc 2>/dev/null
( cd build && riscv64-linux-gnu-ld -r -b binary tpl_producer.img tpl_consumer.img tpl_faulty.img -o blobs_corrupt.o )

OBJS=$(ls build/*.o | grep -v 'blobs\.o' | grep -v 'blobs_corrupt\.o')
clang --target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mcmodel=medany \
    -nostdlib -fuse-ld=lld -T linker/kernel.ld -Wl,--gc-sections -Wl,--no-relax \
    -o build/ifirm-corrupt.elf $OBJS build/blobs_corrupt.o

timeout 20 qemu-system-riscv64 -machine virt -m 256M -smp 1 -nographic \
    -bios default -kernel build/ifirm-corrupt.elf >"$LOG" 2>&1 || true

fail=0
if grep -aqF "IFIRM-BOOT FAIL" "$LOG"; then
    printf '  ok   %s\n' "corrupted template rejected"
else
    printf '  FAIL %s\n' "corrupted template rejected"; fail=1
fi
if grep -aqF "IFIRM-READY" "$LOG"; then
    printf '  FAIL %s\n' "no READY after corruption"; fail=1
else
    printf '  ok   %s\n' "no READY after corruption"
fi
if grep -aqF "tpl-digest" "$LOG"; then
    printf '  ok   %s\n' "reason=tpl-digest"
else
    printf '  FAIL %s\n' "reason=tpl-digest"; fail=1
fi

# 复原，避免污染后续 make
cp build/tpl_consumer.orig build/tpl_consumer.img
rm -f build/tpl_consumer.orig build/blobs_corrupt.o build/ifirm-corrupt.elf
# 复原 blobs.o，避免污染后续 make
( cd build && riscv64-linux-gnu-ld -r -b binary tpl_producer.img tpl_consumer.img tpl_faulty.img -o blobs.o )

if [ "$fail" -eq 0 ]; then
    printf 'TEST ac-002 PASS\n'
else
    printf 'TEST ac-002 FAIL\n'
    exit 1
fi
