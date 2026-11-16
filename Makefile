CC       := clang
OBJCOPY  := riscv64-linux-gnu-objcopy
TARGET   := --target=riscv64-unknown-elf
PLATFORM := -march=rv64gc -mabi=lp64 -mcmodel=medany

CFLAGS   := $(TARGET) -std=c11 -ffreestanding -nostdlib -fno-builtin \
            $(PLATFORM) -fno-pic -mno-relax \
            -ffunction-sections -fdata-sections -O2 -g -Wall -Wextra -Werror -Iinclude
LDFLAGS  := $(TARGET) -fuse-ld=lld -nostdlib -T linker/kernel.ld \
            -Wl,--gc-sections -Wl,--no-relax

SRCS := boot/start.S arch/rv64/uart.c kernel/main.c
OBJS := $(patsubst %.c,build/%.o,$(patsubst %.S,build/%.o,$(SRCS)))

all: build/ifirm.elf build/ifirm.bin

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/ifirm.elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

build/ifirm.bin: build/ifirm.elf
	$(OBJCOPY) -O binary $< $@

run: all
	qemu-system-riscv64 -machine virt -m 256M -smp 1 -nographic -bios default -kernel build/ifirm.bin

test: all
	sh tests/ac001.sh

clean:
	rm -rf build
