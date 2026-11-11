#ifndef RANGEROS_PIT_H
#define RANGEROS_PIT_H

#include <stdint.h>

#define PIT_HZ 100

/*
 * Ticks: 32-bit unsigned count of IRQ0s since pit_init(), the kernel's
 * simulated time base (SPEC D04; no claim of wall-clock accuracy). It
 * wraps after 2^32 ticks (about 497 days at 100 Hz). Compare ticks only
 * through tick_reached(), which is correct across the wrap as long as
 * the two values are less than 2^31 ticks apart.
 */
static inline int tick_reached(uint32_t now, uint32_t target)
{
    return (int32_t)(now - target) >= 0;
}

/* Program channel 0 as a rate generator at hz and register IRQ0. */
void pit_init(uint32_t hz);
uint32_t pit_divisor(void);
/* Current tick count (short critical section). */
uint32_t timer_ticks(void);

#endif
