#ifndef RANGEROS_IRQ_H
#define RANGEROS_IRQ_H

#include <stdint.h>

#include "rangeros/pic.h"

#define IRQ_LINES PIC_IRQ_LINES

/* Runs with IF=0 inside irq_dispatch: read the device, update counters
 * or enqueue, return. No printing, no allocation, no task switch. */
typedef void (*irq_handler_fn)(void);

struct irq_stats {
    uint32_t count[IRQ_LINES]; /* handled (non-spurious) IRQs */
    uint32_t eoi[IRQ_LINES];   /* EOIs sent after handling */
    uint32_t spurious;         /* IRQ7/15 with ISR bit clear */
    uint32_t unexpected;       /* IRQ on a line with no handler */
    uint32_t max_depth;        /* deepest irq_dispatch nesting seen */
    uint32_t if_in_handler;    /* times a handler saw IF=1 (must stay 0) */
};

/* Install the handler for a line. Init only, before sti. */
void irq_register(unsigned irq, irq_handler_fn fn);
/* Called from isr_dispatch with IF=0 for vectors 0x20-0x2F; returns so
 * isr_common can restore the interrupted context with iretd. */
void irq_dispatch(unsigned irq);
/* Consistent copy of the counters (short critical section). */
void irq_stats_snapshot(struct irq_stats *out);

#endif
