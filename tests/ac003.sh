#!/usr/bin/env bash
# AC-003：完整文件系统 + 用户态可用 —— shell 从磁盘加载，还能再加载程序；
#         写入落盘可读回；程序崩溃不带走系统；程序字符串不在内核映像里。
set -u
cd "$(dirname "$0")/.."
mkdir -p build
LOG=build/serial-disk.log
DISK=build/disk-t.img

cp build/disk.img "$DISK"
QEMU='qemu-system-riscv64 -machine virt -m 256M -smp 1 -nographic -bios default'
DRIVE="-drive file=$DISK,format=raw,if=none,id=d0 -device virtio-blk-device,drive=d0"

# 串口 RX FIFO 只有 16 字节，必须等内核启动完再分批喂，否则命令会被丢掉
{ sleep 2; printf 'ls /bin\n'; sleep 1; printf 'cat /etc/motd\n'; sleep 1;
  printf 'hello alpha beta\n'; sleep 2; printf 'ping 2\n'; sleep 2;
  printf 'crash\n'; sleep 2; printf 'write /tmp/note hello-from-ifirm\n'; sleep 1;
  printf 'cat /tmp/note\n'; sleep 1; printf 'ls /tmp\n'; sleep 1;
  printf 'ps\n'; sleep 1; printf 'exit\n'; } \
  | timeout 60 $QEMU $DRIVE -kernel build/ifirm.elf >"$LOG" 2>&1 || true

fail=0
need() {
    if grep -aqF "$1" "$LOG"; then printf '  ok   %s\n' "$1"
    else printf '  FAIL %s\n' "$1"; fail=1; fi
}
forbid() {
    if grep -aqF "$1" "$LOG"; then printf '  FAIL forbidden %s\n' "$1"; fail=1; fi
}

need "IFIRM-FS on files="
need "IFIRM-INIT name=sh dom="
need "Ifirm 1.0 minimal system"

# ls /bin：磁盘上真的有这几个程序
need "sh"
need "cat"
need "hello"
need "crash"

# cat /etc/motd：从文件系统读内容
need "minimal system: CA + composite kernel + atomic templates"

# hello 带参数：argv 由内核拷到新域栈上
need "hello: loaded from disk (elf), argc=3"
need "hello: argv[1]=alpha"
need "hello: argv[2]=beta"

# ping：程序自己计时
need "ping: seq=1 t="
need "ping: 2 packets,"

# crash：写空指针 → 域被杀，shell 继续
need "crash: writing to address 0"
need "IFIRM-EVT id=7 dom="
forbid "crash: this line must never appear"

# 可写文件系统：写进去再读回来
need "write: /tmp/note"
need "hello-from-ifirm"

# ps：进程表里能看到 shell 自己和内核组合域
need "PID STATE   KIND PAGES CAPS FAULTS NAME"
need "sh"
need "domains="

# 内核映像里没有这些程序字符串，磁盘映像里有
if [ "$(grep -ac 'loaded from disk (elf)' build/ifirm.elf)" -eq 0 ]; then
    printf '  ok   %s\n' "program string absent from kernel image"
else
    printf '  FAIL %s\n' "program string absent from kernel image"; fail=1
fi
if [ "$(grep -ac 'loaded from disk (elf)' "$DISK")" -ge 1 ]; then
    printf '  ok   %s\n' "program string present on disk image"
else
    printf '  FAIL %s\n' "program string present on disk image"; fail=1
fi

# 负向：磁盘头坏了 → fs 组件失败，不许静默继续
cp "$DISK" build/disk-bad.img
printf 'X' | dd of=build/disk-bad.img bs=1 seek=0 count=1 conv=notrunc 2>/dev/null
LOG=build/serial-nodisk.log
timeout 20 $QEMU -drive file=build/disk-bad.img,format=raw,if=none,id=d0 \
    -device virtio-blk-device,drive=d0 -kernel build/ifirm.elf >"$LOG" 2>&1 || true
need "IFIRM-COMP FAIL name=fs"
if grep -aqF "IFIRM-INIT" "$LOG"; then
    printf '  FAIL %s\n' "no init after corrupt disk"; fail=1
else
    printf '  ok   %s\n' "no init after corrupt disk"
fi

rm -f "$DISK" build/disk-bad.img
if [ "$fail" -eq 0 ]; then printf 'TEST ac-003 PASS\n'; else printf 'TEST ac-003 FAIL\n'; exit 1; fi
