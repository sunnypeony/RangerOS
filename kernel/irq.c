#include <stdint.h>

#include "arch/i386/cpu.h"
#include "arch/i386/irqflags.h"
#include "rangeros/irq.h"
#include "rangeros/pic.h"

static irq_handler_fn handlers[IRQ_LINES];
static struct irq_stats stats;
static uint32_t depth;

void irq_register(unsigned irq, irq_handler_fn fn)
{
    if (irq < IRQ_LINES)
        handlers[irq] = fn;
}

/*
 * Order for every IRQ: count -> device handler -> EOI -> return. IF stays 0
 * throughout (interrupt gate), so the EOI cannot let a second IRQ nest
 * here; the PIC may raise the next one, but the CPU only takes it after
 * iretd restores the interrupted EFLAGS (IF=1).
 */
void irq_dispatch(unsigned irq)
{
    if (++depth > stats.max_depth)
        stats.max_depth = depth;
    if (read_eflags() & EFLAGS_IF)
        stats.if_in_handler++;

    if ((irq == 7 || irq == 15) && pic_is_spurious(irq)) {
        /* No EOI to the PIC that raised it; a spurious IRQ15 still went
         * through the master's cascade input, which needs its EOI. */
        stats.spurious++;
        if (irq == 15)
            pic_send_eoi(PIC_CASCADE_IRQ);
        depth--;
        return;
    }

    stats.count[irq]++;
    if (handlers[irq])
        handlers[irq]();
    else
        stats.unexpected++; /* masked lines should never get here */

    pic_send_eoi(irq);
    stats.eoi[irq]++;
    depth--;
}

void irq_stats_snapshot(struct irq_stats *out)
{
    irqflags_t flags = irq_save();

    *out = stats;
    irq_restore(flags);
}
