#ifndef RANGEROS_ARCH_I386_CPU_H
#define RANGEROS_ARCH_I386_CPU_H

#include <stdint.h>

#define EFLAGS_IF 0x200u

static inline uint32_t read_eflags(void)
{
    uint32_t value;
    __asm__ volatile("pushfl\n\tpopl %0" : "=r"(value));
    return value;
}

/* Meaningful only inside a #PF handler (faulting linear address). */
static inline uint32_t read_cr2(void)
{
    uint32_t value;
    __asm__ volatile("movl %%cr2, %0" : "=r"(value));
    return value;
}

static inline uint16_t read_cs(void)
{
    uint16_t value;
    __asm__ volatile("movw %%cs, %0" : "=r"(value));
    return value;
}

static inline uint16_t read_ds(void)
{
    uint16_t value;
    __asm__ volatile("movw %%ds, %0" : "=r"(value));
    return value;
}

static inline uint16_t read_ss(void)
{
    uint16_t value;
    __asm__ volatile("movw %%ss, %0" : "=r"(value));
    return value;
}

/* IF=0 and hlt: nothing (except NMI) can resume execution. */
static inline __attribute__((noreturn)) void cpu_halt_forever(void)
{
    for (;;)
        __asm__ volatile("cli\n\thlt");
}

#endif
