#!/usr/bin/env bash
# AC-006：缺陷回归 —— 坏 ELF 不打崩内核、失败不泄漏域槽、根目录不可删、
#         跨页用户指针被拒、域回收清空 cap/ICT。
set -u
cd "$(dirname "$0")/.."
mkdir -p build
LOG=build/serial-bug.log
DISK=build/disk-b.img

cp build/disk.img "$DISK"
QEMU='qemu-system-riscv64 -machine virt -m 256M -smp 1 -nographic -bios default'
DRIVE="-drive file=$DISK,format=raw,if=none,id=d0 -device virtio-blk-device,drive=d0"

{ sleep 2; printf 'bugcheck\n'; sleep 12; printf 'ls /bin\n'; sleep 1;
  printf 'exit\n'; } \
  | timeout 60 $QEMU $DRIVE -kernel build/ifirm.elf >"$LOG" 2>&1 || true

fail=0
need() {
    if grep -aqF "$1" "$LOG"; then printf '  ok   %s\n' "$1"
    else printf '  FAIL %s\n' "$1"; fail=1; fi
}
forbid() {
    if grep -aqF "$1" "$LOG"; then printf '  FAIL forbidden %s\n' "$1"; fail=1; fi
}

need "Ifirm 1.0 minimal system"
need "IFIRM-REAP selftest=ok caps_used=0 ict=0"

# 1) badph：ELF 头偏移回绕，必须返回 CORRUPT 而不是内核 panic
need "bugcheck: make ph st=0"
need "bugcheck: badph st=-14"

# 2) 12 次装载失败后域槽仍可用：好程序拿到 7 号域
need "bugcheck: make seg st=0"
need "bugcheck: badseg last=-14"
need "bugcheck: good id=8"
need "hello: loaded from disk (elf)"

# 3) 根目录不可删除，删完文件系统仍然可用
need "bugcheck: unlink-root st=-2"
need "bugcheck: fs n="

# 4) 栈顶前一字节 + 长度 2：第二字节落在未映射页，必须 -2
need "bugcheck: badptr st=-2"
need "bugcheck: DONE"

# 删根之后 shell 还能列目录（根 inode 完好）
forbid "ls: /bin"

forbid "IFIRM-PANIC"
forbid "IFIRM-BOOT FAIL"
forbid "bugcheck: good st="
forbid "bugcheck: fs st="

if [ "$fail" -ne 0 ]; then
    printf 'TEST ac-006 FAIL\n'
    exit 1
fi
printf 'TEST ac-006 PASS\n'
