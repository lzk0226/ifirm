#!/bin/sh
exec qemu-system-riscv64 -machine virt -m 256M -smp 1 -nographic \
    -bios default -kernel build/ifirm.bin
