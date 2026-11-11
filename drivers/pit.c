#include <stdint.h>

#include "arch/i386/io.h"
#include "arch/i386/irqflags.h"
#include "rangeros/irq.h"
#include "rangeros/pit.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_HZ  1193182u
/* Channel 0, lobyte/hibyte access, mode 2 (rate generator), binary. */
#define PIT_CMD_CH0_RATE 0x34

/*
 * Written only by the IRQ0 handler (IF=0). Everyone else reads it through
 * timer_ticks(). Not static: the IRQ-return selftest probe
 * (tests/kernel/irqret_probe.asm) reads it with a single aligned 32-bit
 * load, which cannot tear on i386.
 */
uint32_t pit_ticks;

static uint32_t divisor;

static void pit_irq(void)
{
    pit_ticks++;
}

void pit_init(uint32_t hz)
{
    divisor = (PIT_BASE_HZ + hz / 2) / hz; /* 100 Hz -> 11932 */
    pit_ticks = 0;
    outb(PIT_COMMAND, PIT_CMD_CH0_RATE);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));
    irq_register(0, pit_irq);
}

uint32_t pit_divisor(void)
{
    return divisor;
}

uint32_t timer_ticks(void)
{
    irqflags_t flags = irq_save();
    uint32_t now = pit_ticks;

    irq_restore(flags);
    return now;
}
