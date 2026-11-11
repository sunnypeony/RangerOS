#include <stdint.h>

#include "arch/i386/cpu.h"
#include "rangeros/console.h"
#include "rangeros/irq.h"
#include "rangeros/pit.h"
#include "rangeros/selftest.h"

/*
 * IRQ-return test (stage IRQ_ON, IF=1, PIT at 100 Hz). irqret_probe
 * (irqret_probe.asm) spins for IRQRET_WAIT_TICKS while PIT IRQs land on
 * it and checks registers, ESP, stack sentinels, DF and IF after every
 * iteration. Here the C side also checks its own stack array and
 * recomputes the probe's LCG result. Prints IRQRET_OK or IRQRET_FAIL.
 *
 * Built with -DIRQRET_SABOTAGE (selftest-irqret-sabotage.elf, linked with
 * an isr.o assembled with RANGEROS_IRQ_RETURN_SABOTAGE), it arms a return
 * path that corrupts ESI; the probe must then report code 4.
 */
#define IRQRET_WAIT_TICKS 150
#define IRQRET_SEED       0xA5A5A5A5u
#define IRQRET_LCG_MUL    1103515245u
#define IRQRET_LCG_ADD    12345u
#define IRQRET_ARRAY      64

struct irqret_result {
    uint32_t eax;
    uint32_t iterations;
    uint32_t fail;
};

int irqret_probe(uint32_t wait_ticks, struct irqret_result *out);

/* Read by isr.asm only when assembled with RANGEROS_IRQ_RETURN_SABOTAGE. */
uint32_t irqret_sabotage_armed;

static const char *const fail_names[] = {
    "none", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp", "stack", "df", "if",
};

static uint32_t checksum(const uint32_t *a, unsigned n)
{
    uint32_t sum = 0;

    for (unsigned i = 0; i < n; i++)
        sum = (sum << 5 | sum >> 27) ^ a[i];
    return sum;
}

void selftest_run(enum selftest_stage stage)
{
    uint32_t local[IRQRET_ARRAY];
    struct irqret_result res = { 0, 0, 0 };

    if (stage != SELFTEST_STAGE_IRQ_ON)
        return;

    for (unsigned i = 0; i < IRQRET_ARRAY; i++)
        local[i] = 0x9E3779B9u * (i + 1);
    /* Barriers: make the compiler store local[] and re-read it after the
     * probe instead of reusing the first checksum. */
    __asm__ volatile("" : : "r"(local) : "memory");
    uint32_t sum_before = checksum(local, IRQRET_ARRAY);

    struct irq_stats before;
    irq_stats_snapshot(&before);
    uint32_t t0 = timer_ticks();
    kprintf_serial("SELFTEST irqret: start ticks=%u wait=%u if=%u\n", t0, IRQRET_WAIT_TICKS,
                   (read_eflags() & EFLAGS_IF) ? 1u : 0u);

#ifdef IRQRET_SABOTAGE
    irqret_sabotage_armed = 1;
#endif
    int rc = irqret_probe(IRQRET_WAIT_TICKS, &res);
    irqret_sabotage_armed = 0;

    uint32_t t1 = timer_ticks();
    struct irq_stats after;
    irq_stats_snapshot(&after);
    uint32_t irq0 = after.count[0] - before.count[0];

    uint32_t expect = IRQRET_SEED;
    for (uint32_t i = 0; i < res.iterations; i++)
        expect = expect * IRQRET_LCG_MUL + IRQRET_LCG_ADD;
    __asm__ volatile("" : : "r"(local) : "memory");
    uint32_t sum_after = checksum(local, IRQRET_ARRAY);

    if (rc != 0) {
        kprintf_serial("IRQRET_FAIL code=%d (%s) iters=%u\n", rc,
                       (unsigned)rc < sizeof(fail_names) / sizeof(fail_names[0])
                           ? fail_names[rc] : "?",
                       res.iterations);
        cpu_halt_forever();
    }
    if (res.eax != expect || sum_after != sum_before || res.iterations == 0 ||
        !tick_reached(t1, t0 + IRQRET_WAIT_TICKS) || irq0 < IRQRET_WAIT_TICKS) {
        kprintf_serial("IRQRET_FAIL code=0 (result) eax=0x%08x expect=0x%08x "
                       "stack=0x%08x/0x%08x iters=%u ticks=%u irq0=%u\n",
                       res.eax, expect, sum_before, sum_after, res.iterations, t1 - t0, irq0);
        cpu_halt_forever();
    }
    kprintf_serial("IRQRET_OK iters=%u ticks=%u irq0=%u eax=0x%08x stack=0x%08x\n",
                   res.iterations, t1 - t0, irq0, res.eax, sum_after);
}
