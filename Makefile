# Ifirm 1.0 —— 单条 make 即得可引导 ELF 与磁盘映像（§7.7）
CC      := clang
LD      := ld.lld
OBJCOPY := riscv64-linux-gnu-objcopy
HOSTCC  := cc

TARGET  := riscv64-unknown-elf
ARCH    := -march=rv64gc -mabi=lp64 -mcmodel=medany

CFLAGS  := --target=$(TARGET) $(ARCH) -std=c11 -O2 -g \
           -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
           -fno-omit-frame-pointer -ffunction-sections -fdata-sections \
           -fno-common -fno-pic -Wall -Wextra -Werror -Iinclude
ASFLAGS := --target=$(TARGET) $(ARCH)
UCFLAGS := --target=$(TARGET) $(ARCH) -std=c11 -Os -g \
           -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
           -fno-pic -Wall -Wextra -Werror
LDFLAGS := -nostdlib -fuse-ld=lld -T linker/kernel.ld \
           -Wl,--gc-sections -Wl,--no-relax

OBJS := build/start.o build/uart.o build/sbi.o build/trap.o build/trapc.o \
        build/printk.o build/board.o build/pmm.o build/vmm.o build/cap.o \
        build/thread.o build/ict.o build/ipc.o build/domain.o \
        build/syscall.o build/ca.o build/template.o build/tplreg.o \
        build/component.o build/virtio.o build/fs.o build/elf.o \
        build/spawn.o build/main.o build/libk.o build/blobs.o

TPL_NAMES := tpl_producer tpl_consumer tpl_faulty
TPL_IMGS  := $(addprefix build/,$(addsuffix .img,$(TPL_NAMES)))

PROGS := sh ls cat echo hello ping crash
PROG_ELFS := $(addprefix build/prog_,$(addsuffix .elf,$(PROGS)))

all: build/ifirm.elf build/disk.img

build/start.o: boot/start.S
	@mkdir -p build
	$(CC) $(ASFLAGS) -c $< -o $@

build/trap.o: arch/rv64/trap.S
	@mkdir -p build
	$(CC) $(ASFLAGS) -c $< -o $@

build/trapc.o: arch/rv64/trap.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/uart.o: arch/rv64/uart.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/sbi.o: arch/rv64/sbi.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: kernel/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

# ---- 原子模板（编译进内核映像的模板镜像） ----
build/%.elf: user/%.c user/ulib.h user/user.ld
	@mkdir -p build
	$(CC) $(UCFLAGS) -T user/user.ld $< -o $@

build/%.text.bin: build/%.elf
	$(OBJCOPY) -O binary --only-section=.text $< $@

build/%.data.bin: build/%.elf
	$(OBJCOPY) -O binary --only-section=.data $< $@

build/tools/mkimg: tools/mkimg.c
	@mkdir -p build/tools
	$(HOSTCC) -O2 -o $@ $<

# 服务位由模板名决定：producer 用 console+channel，consumer 用 channel+reply
SVC_tpl_producer := 0x03
SVC_tpl_consumer := 0x06
SVC_tpl_faulty   := 0x00

build/%.img: build/%.text.bin build/%.data.bin build/tools/mkimg
	build/tools/mkimg $(patsubst tpl_%,%,$*) $< build/$*.data.bin $@ $(SVC_$*)

# ---- 中央授权控制器策略（charter 是镜像，不是源码里的 if） ----
build/tools/mkcharter: tools/mkcharter.c
	@mkdir -p build/tools
	$(HOSTCC) -O2 -o $@ $<

build/charter.bin: charter/charter.txt build/tools/mkcharter
	build/tools/mkcharter $< $@

build/blobs.o: $(TPL_IMGS) build/charter.bin
	cd build && riscv64-linux-gnu-ld -r -b binary $(notdir $(TPL_IMGS)) charter.bin -o blobs.o

build/ifirm.elf: $(OBJS)
	$(CC) --target=$(TARGET) $(ARCH) $(LDFLAGS) -o $@ $(OBJS)

# ---- 磁盘上的程序（不编进内核映像） ----
build/prog_%.elf: user/prog_%.c user/ulib.h user/user.ld
	@mkdir -p build
	$(CC) $(UCFLAGS) -T user/user.ld $< -o $@

build/tools/mkfs: tools/mkfs.c
	@mkdir -p build/tools
	$(HOSTCC) -O2 -o $@ $<

DISK_ARGS := $(foreach p,$(PROGS),bin/$(p)=build/prog_$(p).elf)

build/disk.img: $(PROG_ELFS) build/tools/mkfs
	build/tools/mkfs $@ $(DISK_ARGS)

QEMU := qemu-system-riscv64 -machine virt -m 256M -smp 1 -nographic \
        -bios default -drive file=build/disk.img,format=raw,if=none,id=d0 \
        -device virtio-blk-device,drive=d0

run: build/ifirm.elf build/disk.img
	timeout 20 $(QEMU) -kernel build/ifirm.elf || true

test: build/ifirm.elf build/disk.img
	bash tests/ac001.sh
	bash tests/ac002.sh
	bash tests/ac003.sh
	bash tests/ac004.sh

clean:
	rm -rf build

.PHONY: all run test clean
