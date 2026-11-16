#!/usr/bin/env bash
# AC-001：内核完成引导、组合五个域、CA 授权与撤销、同一模板两次实例化
set -u
cd "$(dirname "$0")/.."
LOG=build/serial.log
mkdir -p build
timeout 30 qemu-system-riscv64 -machine virt -m 256M -smp 1 -nographic \
    -bios default -drive file=build/disk.img,format=raw,if=none,id=d0 \
    -device virtio-blk-device,drive=d0 -kernel build/ifirm.elf >"$LOG" 2>&1 || true

fail=0
need() {
    if grep -aqF "$1" "$LOG"; then
        printf '  ok   %s\n' "$1"
    else
        printf '  FAIL %s\n' "$1"
        fail=1
    fi
}
forbid() {
    if grep -aqF "$1" "$LOG"; then
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
# 复合内核：五个域，两个模板各两次实例化
need "IFIRM-DOMAIN id=1 state=READY name=producer"
need "IFIRM-DOMAIN id=2 state=READY name=consumer"
need "IFIRM-DOMAIN id=3 state=READY name=faulty"
need "IFIRM-DOMAIN id=4 state=READY name=producer#2"
need "IFIRM-DOMAIN id=5 state=READY name=consumer#2"
need "IFIRM-READY domains=5"
# 同一模板两个实例的 tpl= 摘要必须相同
if [ "$(grep -ac 'tpl=0x' "$LOG")" -ge 5 ]; then printf '  ok   %s\n' "tpl digest printed"; else printf '  FAIL %s\n' "tpl digest printed"; fail=1; fi
if [ "$(grep -a 'name=producer ' "$LOG" | sed 's/.*tpl=\(0x[0-9a-f]*\).*/\1/')" = "$(grep -a 'name=producer#2' "$LOG" | sed 's/.*tpl=\(0x[0-9a-f]*\).*/\1/')" ]; then
    printf '  ok   %s\n' "same template instance digest"
else
    printf '  FAIL %s\n' "same template instance digest"; fail=1
fi
# CA：启动期授权 + 运行期申请 + 撤销传播
need "producer[1]: ict=ok"
need "producer[1]: ca grant st=0"
need "producer[1]: ict after revoke=-15"
need "IFIRM-EVT id=8 dom=1 a=1"   # 撤销传播到 1 个 ICT 槽
need "faulty: ca denied=-5"
# 同一模板两个实例都跑出各自通道的结果
need "producer[1]: seq=2 x=15 reply=30"
need "producer[2]: seq=2 x=15 reply=30"
need "consumer[1]: seq=2 x=15 -> 30"
need "consumer[2]: seq=2 x=15 -> 30"
need "producer[1]: call after revoke=-15"
need "IFIRM-EVT id=7 dom=3"
need "IFIRM-CA requests="
need "IFIRM-HALT all-threads-exited"
forbid "IFIRM-PANIC"

if [ "$fail" -eq 0 ]; then
    printf 'TEST ac-001 PASS\n'
else
    printf 'TEST ac-001 FAIL\n'
    exit 1
fi
