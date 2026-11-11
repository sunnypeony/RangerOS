#include <stdint.h>

#include "arch/i386/cpu.h"
#include "arch/i386/gdt.h"
#include "arch/i386/idt.h"
#include "rangeros/console.h"
#include "rangeros/multiboot.h"
#include "rangeros/panic.h"
#include "rangeros/version.h"
#ifdef RANGEROS_SELFTEST
#include "rangeros/selftest.h"
#endif

void kernel_main(uint32_t magic, uint32_t multiboot_info);

/* Called from arch/i386/boot.asm with IF=0 on the 16 KiB boot stack.
 * Init order follows SPEC section 3; hardware IRQs stay off in M1. */
void kernel_main(uint32_t magic, uint32_t multiboot_info)
{
    (void)multiboot_info;

    console_init();
    kprintf(RANGEROS_NAME " " RANGEROS_VERSION " (i386, multiboot1)\n");

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        kprintf("BOOT_FAIL bad multiboot magic 0x%08x\n", magic);
        cpu_halt_forever();
    }

    gdt_init();
    kprintf("GDT_OK cs=0x%04x ds=0x%04x ss=0x%04x\n", read_cs(), read_ds(), read_ss());

    idt_init();
    kprintf("IDT_OK vectors=%u\n", IDT_VECTORS);

#ifdef RANGEROS_SELFTEST
    /* Test kernels only (tests/kernel/selftest_*.c). */
    selftest_run();
#endif

    if (read_eflags() & EFLAGS_IF)
        panic("IF set before interrupt setup");
    kprintf("BOOT_OK\n");
    cpu_halt_forever();
}
