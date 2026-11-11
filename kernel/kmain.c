#include <stdint.h>

#include "arch/i386/cpu.h"
#include "arch/i386/gdt.h"
#include "arch/i386/idt.h"
#include "arch/i386/irqflags.h"
#include "rangeros/console.h"
#include "rangeros/input_queue.h"
#include "rangeros/irq.h"
#include "rangeros/keyboard.h"
#include "rangeros/multiboot.h"
#include "rangeros/panic.h"
#include "rangeros/pic.h"
#include "rangeros/pit.h"
#include "rangeros/shell.h"
#include "rangeros/version.h"
#include "rangeros/wait.h"
#ifdef RANGEROS_SELFTEST
#include "rangeros/selftest.h"
#define SELFTEST(stage) selftest_run(stage)
#else
#define SELFTEST(stage) ((void)0)
#endif

/* Serial status line every this many ticks (1 s at 100 Hz). */
#define TICK_REPORT_INTERVAL 100

void kernel_main(uint32_t magic, uint32_t multiboot_info);

static void report_ticks(uint32_t now)
{
    struct irq_stats is;
    struct keyboard_stats ks;

    irq_stats_snapshot(&is);
    keyboard_stats_snapshot(&ks);
    kprintf_serial("TICKS %u irq0=%u eoi0=%u irq1=%u eoi1=%u spurious=%u unexpected=%u "
                   "maxdepth=%u ifin=%u kbd_scancodes=%u kbd_empty=%u dropped=%u\n",
                   now, is.count[0], is.eoi[0], is.count[1], is.eoi[1], is.spurious,
                   is.unexpected, is.max_depth, is.if_in_handler, ks.scancodes, ks.empty,
                   ks.dropped);
}

/* The only consumer of the PS/2 input queue. IRQ1 only enqueues bytes;
 * all line editing and command execution happen here, with IF=1. */
static __attribute__((noreturn)) void main_loop(void)
{
    uint32_t next_report = TICK_REPORT_INTERVAL;

    for (;;) {
        int c;

        while ((c = keyboard_read()) >= 0)
            shell_input((uint8_t)c);

        uint32_t now = timer_ticks();
        if (tick_reached(now, next_report)) {
            report_ticks(now);
            next_report += TICK_REPORT_INTERVAL;
        }
        wait_for_input();
    }
}

/* Called from arch/i386/boot.asm with IF=0 on the 16 KiB boot stack.
 * Init order follows SPEC section 3. */
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

    SELFTEST(SELFTEST_STAGE_EARLY);

    /* Every line masked while devices and queues are set up. */
    pic_init();
    pit_init(PIT_HZ);
    kprintf("PIT_OK hz=%u divisor=%u\n", PIT_HZ, pit_divisor());
    if (keyboard_init() < 0)
        panic("i8042 keyboard controller did not respond");
    kprintf("KBD_OK set=1 translate=%u config=0x%02x queue=%u\n",
            (keyboard_config() & I8042_CONFIG_TRANSLATE) ? 1u : 0u, keyboard_config(),
            (unsigned)INPUT_QUEUE_CAPACITY);

    /* Only IRQ0 (PIT) and IRQ1 (keyboard); slave and cascade stay masked. */
    pic_unmask(0);
    pic_unmask(1);
    kprintf("PIC_OK vectors=0x%02x/0x%02x mask=0x%02x/0x%02x\n", PIC_MASTER_VECTOR,
            PIC_SLAVE_VECTOR, pic_master_mask(), pic_slave_mask());

    if (read_eflags() & EFLAGS_IF)
        panic("IF set before interrupt setup");
    irq_enable();
    kprintf("IRQ_ON\n");
    kprintf("BOOT_OK\n");

    SELFTEST(SELFTEST_STAGE_IRQ_ON);
    shell_init();
    main_loop();
}
