# RangerOS build. Runs on the aarch64 Ubuntu VM and cross-targets i386
SHELL := /bin/bash
# with clang/ld.lld (see SPEC.md section 2.1). No host gcc -m32.

CC            := clang
LD            := ld.lld
NASM          := nasm
PYTHON        := python3
GRUB_MKRESCUE := grub-mkrescue
QEMU          := qemu-system-i386
GRUB_MODDIR   ?= tools/grub/extracted/usr/lib/grub/i386-pc

BUILD  := build
KERNEL := $(BUILD)/kernel.elf
MAP    := $(BUILD)/kernel.map
ISO    := $(BUILD)/rangeros.iso
LOGDIR := $(BUILD)/test-logs

# Only clang's own freestanding headers (stdint.h etc.), never the
# aarch64 host /usr/include.
CLANG_INC := $(shell $(CC) -print-resource-dir)/include

CFLAGS := --target=i686-none-elf -std=c11 -O2 -g \
          -ffreestanding -fno-builtin -nostdinc -isystem $(CLANG_INC) \
          -fno-stack-protector -fno-pic -fno-pie \
          -fno-omit-frame-pointer -fno-asynchronous-unwind-tables \
          -mno-mmx -mno-sse -mno-sse2 -mno-80387 \
          -Wall -Wextra -Werror -I. -Iinclude -MMD -MP
ASFLAGS := -f elf32 -g -F dwarf -w+all
LDFLAGS := -m elf_i386 -T arch/i386/linker.ld -nostdlib -static \
           --build-id=none -z max-page-size=0x1000 -Map=$(MAP)

C_SRCS   := kernel/kmain.c drivers/serial.c
ASM_SRCS := arch/i386/boot.asm
# boot.o first so _start/.multiboot lead the image.
OBJS := $(ASM_SRCS:%.asm=$(BUILD)/%.o) $(C_SRCS:%.c=$(BUILD)/%.o)

QEMU_FLAGS := -accel tcg -m 128M -smp 1 -nic none -no-reboot

# Negative self-test images for the test runner (never part of the
# normal ISO).
NEG_BAD_ISO   := $(BUILD)/neg/bad-multiboot.iso
NEG_BAD_ELF   := $(BUILD)/neg/bad-multiboot/boot/kernel.elf
NEG_EXIT_ISO  := $(BUILD)/neg/early-exit.iso

.PHONY: all iso check test run versions clean

all: $(KERNEL) $(ISO)

iso: $(ISO)

$(BUILD)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(NASM) $(ASFLAGS) -o $@ $<

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(KERNEL): $(OBJS) arch/i386/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(MAP): $(KERNEL)

$(ISO): $(KERNEL) boot/grub/grub.cfg
	rm -rf $(BUILD)/isodir
	mkdir -p $(BUILD)/isodir/boot/grub
	cp $(KERNEL) $(BUILD)/isodir/boot/kernel.elf
	cp boot/grub/grub.cfg $(BUILD)/isodir/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -d $(GRUB_MODDIR) -o $@ $(BUILD)/isodir \
	    > $(BUILD)/grub-mkrescue.log 2>&1 || { cat $(BUILD)/grub-mkrescue.log; exit 1; }

$(NEG_BAD_ELF): $(KERNEL) tests/scripts/make_bad_multiboot.py
	@mkdir -p $(dir $@)
	$(PYTHON) tests/scripts/make_bad_multiboot.py $(KERNEL) $@

$(NEG_BAD_ISO): $(NEG_BAD_ELF) boot/grub/grub.cfg
	mkdir -p $(BUILD)/neg/bad-multiboot/boot/grub
	cp boot/grub/grub.cfg $(BUILD)/neg/bad-multiboot/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -d $(GRUB_MODDIR) -o $@ $(BUILD)/neg/bad-multiboot \
	    > $(BUILD)/neg/bad-multiboot.mkrescue.log 2>&1

$(NEG_EXIT_ISO): tests/grub/early-exit.cfg
	rm -rf $(BUILD)/neg/early-exit
	mkdir -p $(BUILD)/neg/early-exit/boot/grub
	cp tests/grub/early-exit.cfg $(BUILD)/neg/early-exit/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -d $(GRUB_MODDIR) -o $@ $(BUILD)/neg/early-exit \
	    > $(BUILD)/neg/early-exit.mkrescue.log 2>&1

check: $(KERNEL)
	@mkdir -p $(LOGDIR)
	$(PYTHON) tests/scripts/check_elf.py $(KERNEL) | tee $(LOGDIR)/elf-check.log; \
	    exit $${PIPESTATUS[0]}

test: all $(NEG_BAD_ISO) $(NEG_EXIT_ISO)
	bash tests/scripts/m0_test.sh

# Interactive run on the serial console. Quit QEMU with Ctrl-A then X.
run: $(ISO)
	$(QEMU) $(QEMU_FLAGS) -nographic -boot d -cdrom $(ISO)

versions:
	@mkdir -p $(BUILD)
	bash tests/scripts/tool_versions.sh | tee $(BUILD)/versions.txt

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
