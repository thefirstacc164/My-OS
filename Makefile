# MyOS build system
# -----------------
#   make          build the bootable floppy image (docs/myos.img)
#   make clean    remove build/ and the image

CC      := gcc
AS      := as
LD      := ld
OBJCOPY := objcopy
PYTHON  := python3

CFLAGS  := -m32 -std=gnu11 -ffreestanding -fno-pic -fno-pie \
           -fno-stack-protector -fno-asynchronous-unwind-tables \
           -fno-unwind-tables -mno-sse -mno-mmx -mno-sse2 -mno-80387 \
           -mno-red-zone -fno-builtin -Wall -Wextra -O2 -Ikernel

LDFLAGS := -m elf_i386 -T kernel/linker.ld -nostdlib

C_SRC   := $(wildcard kernel/*.c)
S_SRC   := $(wildcard kernel/*.S)
OBJ     := $(patsubst kernel/%.c,build/%.o,$(C_SRC)) \
           $(patsubst kernel/%.S,build/%.o,$(S_SRC))

IMAGE   := docs/myos.img

.PHONY: all clean run

all: $(IMAGE)

build/%.o: kernel/%.c kernel/kernel.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: kernel/%.S
	@mkdir -p build
	$(AS) --32 $< -o $@

build/kernel.elf: $(OBJ)
	$(LD) $(LDFLAGS) -o $@ $(OBJ)

build/kernel.bin: build/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@printf "  kernel.bin: %s bytes\n" "$$(stat -c%s $@)"

# Number of 512-byte sectors the boot loader must read (evaluated late,
# after build/kernel.bin exists).
SECTORS = $(shell echo $$(( ($$(stat -c%s build/kernel.bin) + 511) / 512 )))

build/boot.bin: boot/boot.S build/kernel.bin
	@mkdir -p build
	$(AS) --32 --defsym KERNEL_SECTORS=$(SECTORS) $< -o build/boot.o
	$(LD) -m elf_i386 -Ttext 0x7C00 --oformat binary -o $@ build/boot.o
	@printf "  boot.bin:   %s bytes (%s kernel sectors + 1)\n" "$$(stat -c%s $@)" "$(SECTORS)"

$(IMAGE): build/boot.bin build/kernel.bin tools/mkimage.py
	$(PYTHON) tools/mkimage.py build/boot.bin build/kernel.bin $(IMAGE)

clean:
	rm -rf build $(IMAGE)
