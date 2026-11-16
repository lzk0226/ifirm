# Ifirm 第一版内核 —— 单条 make 即得可引导 ELF（§7.7）
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
        build/syscall.o build/ca.o build/template.o build/main.o build/libk.o \
        build/blobs.o

TEXTBINS := build/tpl_producer.text.bin build/tpl_consumer.text.bin build/tpl_faulty.text.bin
TPL_IMGS := build/tpl_producer.img build/tpl_consumer.img build/tpl_faulty.img

all: build/ifirm.elf

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

build/tpl_%.elf: user/tpl_%.c user/ulib.h user/user.ld
	@mkdir -p build
	$(CC) $(UCFLAGS) -T user/user.ld $< -o $@

build/%.text.bin: build/%.elf
	$(OBJCOPY) -O binary --only-section=.text $< $@

build/%.data.bin: build/%.elf
	$(OBJCOPY) -O binary --only-section=.data $< $@

# 模板镜像 = 40 字节头（含摘要）+ 模板 .text（tools/mkimg.c，宿主工具，不进内核）
build/tools/mkimg: tools/mkimg.c
	@mkdir -p build/tools
	$(HOSTCC) -O2 -o $@ $<

build/%.img: build/%.text.bin build/%.data.bin build/tools/mkimg
	build/tools/mkimg $* $< build/$*.data.bin $@

build/blobs.o: $(TPL_IMGS)
	cd build && riscv64-linux-gnu-ld -r -b binary $(notdir $(TPL_IMGS)) -o blobs.o

build/ifirm.elf: $(OBJS)
	$(CC) --target=$(TARGET) $(ARCH) $(LDFLAGS) -o $@ $(OBJS)

run: build/ifirm.elf
	timeout 20 qemu-system-riscv64 -machine virt -m 256M -smp 1 -nographic \
	    -bios default -kernel build/ifirm.elf || true

test: build/ifirm.elf
	bash tests/ac001.sh
	bash tests/ac002.sh

clean:
	rm -rf build

.PHONY: all run test clean
