#ifndef RANGEROS_WAIT_H
#define RANGEROS_WAIT_H

#include <stdint.h>

/*
 * Main-loop waits (never call from an IRQ handler or a critical section).
 *
 * Contract: called with IF=1, returns with IF=1; panics if entered with
 * IF=0, since they end with sti. Each wait is
 *     cli; check condition; if true: sti, return; else: sti; hlt
 * sti only takes effect after the following instruction, so an IRQ that
 * makes the condition true after the check still wakes the hlt: no lost
 * wake-up. IRQ0 (PIT) is always enabled, so hlt always has a wake source.
 */

/* Return as soon as input is queued, or after the next IRQ otherwise. */
void wait_for_input(void);
/* Sleep (hlt, not a busy loop) until tick_reached(timer_ticks(), target). */
void wait_until_tick(uint32_t target);

#endif
