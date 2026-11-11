# RangerOS build. Runs on the aarch64 Ubuntu VM and cross-targets i386
# with clang/ld.lld (see SPEC.md section 2.1). No host gcc -m32.
SHELL := /bin/bash

CC            := clang
LD            := ld.lld
NASM          := nasm
PYTHON        := python3
HOST_CC       := gcc
GRUB_MKRESCUE := grub-mkrescue
QEMU          := qemu-system-i386
GRUB_MODDIR   ?= tools/grub/extracted/usr/lib/grub/i386-pc

BUILD  := build
KERNEL := $(BUILD)/kernel.elf
MAP    := $(BUILD)/kernel.map
ISO    := $(BUILD)/rangeros.iso
LOGDIR := $(BUILD)/test-logs
LDSCRIPT := arch/i386/linker.ld

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
LDFLAGS := -m elf_i386 -nostdlib -static --build-id=none -z max-page-size=0x1000

C_SRCS   := kernel/kmain.c kernel/console.c kernel/panic.c \
            arch/i386/gdt.c arch/i386/idt.c arch/i386/trap.c \
            drivers/serial.c drivers/vga.c lib/format.c lib/string.c
ASM_SRCS := arch/i386/boot.asm arch/i386/gdt_load.asm arch/i386/isr.asm
# boot.o first so _start leads .text (linker.ld already puts .multiboot first).
OBJS := $(ASM_SRCS:%.asm=$(BUILD)/%.o) $(C_SRCS:%.c=$(BUILD)/%.o)
KMAIN_OBJ   := $(BUILD)/kernel/kmain.o
COMMON_OBJS := $(filter-out $(KMAIN_OBJ),$(OBJS))

QEMU_FLAGS := -accel tcg -m 128M -smp 1 -nic none -no-reboot

# Test-only images; none of these objects or scripts reach $(KERNEL).
NEG_BAD_ELF     := $(BUILD)/neg/bad-multiboot.elf
NEG_BAD_ISO     := $(BUILD)/neg/bad-multiboot.iso
NEG_EXIT_ISO    := $(BUILD)/neg/early-exit.iso
NEG_NOLOAD_LD   := $(BUILD)/neg/noload-bss.ld
NEG_NOLOAD_ELF  := $(BUILD)/neg/noload-bss.elf
BADMAGIC_ELF    := $(BUILD)/test/bad-magic.elf
BADMAGIC_ISO    := $(BUILD)/test/bad-magic.iso
M0_TEST_IMAGES  := $(NEG_BAD_ELF) $(NEG_BAD_ISO) $(NEG_EXIT_ISO) $(NEG_NOLOAD_ELF) $(BADMAGIC_ISO)

# M1 selftest kernels: kmain.c rebuilt with -DRANGEROS_SELFTEST plus one
# tests/kernel/selftest_<case>.c that defines selftest_run().
SELFTESTS      := ud gp vga
SELFTEST_KMAIN := $(BUILD)/test/kmain-selftest.o
SELFTEST_ELFS  := $(SELFTESTS:%=$(BUILD)/test/selftest-%.elf)
SELFTEST_ISOS  := $(SELFTESTS:%=$(BUILD)/test/selftest-%.iso)
HOST_FORMAT    := $(BUILD)/host/test_format
M1_TEST_IMAGES := $(SELFTEST_ELFS) $(SELFTEST_ISOS) $(HOST_FORMAT)

# $(call make_iso,ELF,GRUB_CFG,ISO): stage ELF as /boot/kernel.elf in
# ISO.d (always exactly these two files, overwritten each time).
define make_iso
	mkdir -p $(3).d/boot/grub
	cp $(1) $(3).d/boot/kernel.elf
	cp $(2) $(3).d/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -d $(GRUB_MODDIR) -o $(3) $(3).d \
	    > $(3).mkrescue.log 2>&1 || { cat $(3).mkrescue.log; exit 1; }
endef

.PHONY: all iso check test test-m0 test-m1 host-test run run-vga versions clean
# Keep intermediate test ELFs produced by pattern rules.
.SECONDARY:

all: $(KERNEL) $(ISO)

iso: $(ISO)

$(BUILD)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(NASM) $(ASFLAGS) -o $@ $<

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(KERNEL): $(OBJS) $(LDSCRIPT)
	$(LD) $(LDFLAGS) -T $(LDSCRIPT) -Map=$(MAP) -o $@ $(OBJS)

$(MAP): $(KERNEL)

$(ISO): $(KERNEL) boot/grub/grub.cfg
	$(call make_iso,$(KERNEL),boot/grub/grub.cfg,$@)

# --- M0 negative / test images ------------------------------------------

$(NEG_BAD_ELF): $(KERNEL) tests/scripts/make_bad_multiboot.py
	@mkdir -p $(dir $@)
	$(PYTHON) tests/scripts/make_bad_multiboot.py $(KERNEL) $@

$(NEG_BAD_ISO): $(NEG_BAD_ELF) boot/grub/grub.cfg
	$(call make_iso,$(NEG_BAD_ELF),boot/grub/grub.cfg,$@)

# GRUB resets before any kernel runs; the ELF is just a placeholder.
$(NEG_EXIT_ISO): $(KERNEL) tests/grub/early-exit.cfg
	$(call make_iso,$(KERNEL),tests/grub/early-exit.cfg,$@)

# Same objects, but .bss marked NOLOAD: ld.lld then leaves .bss out of
# every PT_LOAD. check_elf.py must reject it by address ranges.
$(NEG_NOLOAD_LD): $(LDSCRIPT)
	@mkdir -p $(dir $@)
	sed 's/^\(\s*\.bss ALIGN(4K)\) :/\1 (NOLOAD) :/' $< > $@
	grep -q '(NOLOAD)' $@

$(NEG_NOLOAD_ELF): $(OBJS) $(NEG_NOLOAD_LD)
	$(LD) $(LDFLAGS) -T $(NEG_NOLOAD_LD) -o $@ $(OBJS)

# Normal objects plus a test entry that jumps to _start with EAX != magic.
$(BADMAGIC_ELF): $(OBJS) $(BUILD)/tests/kernel/bad_magic_entry.o $(LDSCRIPT)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -T $(LDSCRIPT) --entry=selftest_bad_magic_entry -o $@ \
	    $(OBJS) $(BUILD)/tests/kernel/bad_magic_entry.o

$(BADMAGIC_ISO): $(BADMAGIC_ELF) boot/grub/grub.cfg
	$(call make_iso,$(BADMAGIC_ELF),boot/grub/grub.cfg,$@)

# --- M1 selftest kernels and host unit test -----------------------------

$(SELFTEST_KMAIN): kernel/kmain.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DRANGEROS_SELFTEST -c -o $@ $<

$(BUILD)/test/selftest-%.elf: $(COMMON_OBJS) $(SELFTEST_KMAIN) $(BUILD)/tests/kernel/selftest_%.o $(LDSCRIPT)
	$(LD) $(LDFLAGS) -T $(LDSCRIPT) -o $@ \
	    $(COMMON_OBJS) $(SELFTEST_KMAIN) $(BUILD)/tests/kernel/selftest_$*.o

$(BUILD)/test/selftest-%.iso: $(BUILD)/test/selftest-%.elf boot/grub/grub.cfg
	$(call make_iso,$<,boot/grub/grub.cfg,$@)

$(HOST_FORMAT): tests/host/test_format.c lib/format.c include/rangeros/format.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude -o $@ \
	    tests/host/test_format.c lib/format.c

# --- checks and tests ---------------------------------------------------

check: $(KERNEL)
	@mkdir -p $(LOGDIR)
	$(PYTHON) tests/scripts/check_elf.py $(KERNEL) | tee $(LOGDIR)/elf-check.log; \
	    exit $${PIPESTATUS[0]}

host-test: $(HOST_FORMAT)
	$(HOST_FORMAT)

test: test-m0 test-m1

test-m0: all $(M0_TEST_IMAGES)
	bash tests/scripts/m0_test.sh

test-m1: all $(M1_TEST_IMAGES)
	bash tests/scripts/m1_test.sh

# Interactive run on the serial console. Quit QEMU with Ctrl-A then X.
run: $(ISO)
	$(QEMU) $(QEMU_FLAGS) -nographic -boot d -cdrom $(ISO)

# VGA text console drawn in the terminal by curses (needs a real TTY,
# e.g. ssh -t). Serial goes to build/run-vga.serial.log.
# Quit: Esc then 2 opens the QEMU monitor; type quit and press Enter.
run-vga: $(ISO)
	$(QEMU) $(QEMU_FLAGS) -display curses -serial file:$(BUILD)/run-vga.serial.log \
	    -boot d -cdrom $(ISO)

versions:
	@mkdir -p $(BUILD)
	bash tests/scripts/tool_versions.sh | tee $(BUILD)/versions.txt

clean:
	rm -rf build

-include $(OBJS:.o=.d) $(SELFTEST_KMAIN:.o=.d) $(SELFTESTS:%=$(BUILD)/tests/kernel/selftest_%.d)
