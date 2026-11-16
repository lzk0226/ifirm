#!/usr/bin/env bash
# AC-001：复合内核按声明组合、CA 从 charter 镜像授权、模板摘要校验、五个域就绪
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

# 1) 启动序列（由组件自己打印，不是 main 里顺序写死）
need "IFIRM-BOOT stage=0"
need "IFIRM-BOARD model=riscv-virtio,qemu ram=256MiB"
need "IFIRM-BOOT stage=1"
need "IFIRM-MEM total="
need "IFIRM-VM on"
need "IFIRM-CAPS mmu=1 timer=1 plic=1 uart=1"
need "IFIRM-BOOT stage=2"

# 2) 复合内核：13 个内核原子组件 + 复合摘要
for c in board pmm vmm trap cap ca ict ipc tpl domain fs spawn init; do
    need "name=$c lvl="
done
need "IFIRM-COMPOSE n=13 ok digest=0x"

# 3) 中央授权控制器：策略来自 charter 镜像，不是源码里的 if
need "IFIRM-CA charter=0x"
need "rules=9"
need "producer[1]: ca grant st=0"
need "faulty: ca denied=-5"
need "IFIRM-EVT id=8 dom=1 a=1"

# 4) 原子模板：v2 清单 + 摘要；同一模板两次实例化摘要相同
need "IFIRM-TPL n=1 name=producer"
need "IFIRM-TPLSET n=3 ok digest=0x"
if [ "$(grep -a 'name=producer tpl=' "$LOG" | sed 's/.*tpl=\(0x[0-9a-f]*\).*/\1/')" = \
     "$(grep -a 'name=producer#2' "$LOG" | sed 's/.*tpl=\(0x[0-9a-f]*\).*/\1/')" ]; then
    printf '  ok   %s\n' "same template, two instances, same digest"
else
    printf '  FAIL %s\n' "same template, two instances, same digest"; fail=1
fi

# 5) 域与 IPC
need "IFIRM-DOMAIN id=1 state=READY name=producer"
need "IFIRM-DOMAIN id=5 state=READY name=consumer#2"
need "IFIRM-READY domains=5"
need "producer[1]: seq=2 x=15 reply=30"
need "consumer[2]: seq=2 x=15 -> 30"
need "IFIRM-EVT id=7 dom=3"

# 6) 文件系统 + init
need "IFIRM-FS on files="
need "rw=1 free="
need "IFIRM-INIT name=sh dom="
forbid "IFIRM-BOOT FAIL"
forbid "IFIRM-PANIC"

if [ "$fail" -eq 0 ]; then printf 'TEST ac-001 PASS\n'; else printf 'TEST ac-001 FAIL\n'; exit 1; fi
