#ifndef RANGEROS_ARCH_I386_IRQFLAGS_H
#define RANGEROS_ARCH_I386_IRQFLAGS_H

#include <stdint.h>

/*
 * Short critical sections on a single CPU.
 *
 * irq_save() returns the current EFLAGS and executes cli; irq_restore()
 * writes the saved EFLAGS back, so IF returns to exactly its previous
 * value (it is never set unconditionally). Nesting is safe.
 *
 * The "memory" clobbers make each call a compiler barrier: shared data
 * read inside the section is reloaded from memory, and writes are not
 * moved outside it. That ordering, together with IF=0, is what protects
 * plain (non-volatile) shared variables; volatile alone would only stop
 * caching, not make a multi-field update atomic with respect to an IRQ.
 *
 * Never yield or sleep inside a critical section (SPEC section 4).
 */
typedef uint32_t irqflags_t;

static inline irqflags_t irq_save(void)
{
    irqflags_t flags;

    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(irqflags_t flags)
{
    __asm__ volatile("pushl %0\n\tpopfl" : : "r"(flags) : "memory", "cc");
}

/* Used once, by kernel_main, after every handler and queue is ready. */
static inline void irq_enable(void)
{
    __asm__ volatile("sti" : : : "memory");
}

#endif
