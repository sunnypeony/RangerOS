#include <stdint.h>

#include "arch/i386/cpu.h"
#include "rangeros/console.h"
#include "rangeros/keyboard.h"
#include "rangeros/pit.h"
#include "rangeros/selftest.h"
#include "rangeros/wait.h"

/*
 * Kernel queue-full test (stage IRQ_ON, IF=1). Consumption is paused in a
 * controlled way: the main loop (the only consumer) has not started yet,
 * while IRQ1 keeps receiving and enqueueing. The test injects
 * QFULL_TARGET printable keys; with a 128-byte queue the last ones must
 * be dropped and counted. After QFULL_SETTLE_TICKS without new bytes the
 * test reports and returns, kernel_main starts the normal shell, which
 * then drains the queue through the regular input path.
 */
#define QFULL_TARGET        140
#define QFULL_SETTLE_TICKS  50
#define QFULL_TIMEOUT_TICKS 6000

void selftest_run(enum selftest_stage stage)
{
    struct keyboard_stats ks;

    if (stage != SELFTEST_STAGE_IRQ_ON)
        return;

    kprintf_serial("QFULL_PAUSE ready target=%u queue_cap=128\n", QFULL_TARGET);
    uint32_t start = timer_ticks();
    uint32_t last_seen = 0;
    uint32_t last_change = start;

    for (;;) {
        uint32_t now = timer_ticks();

        keyboard_stats_snapshot(&ks);
        uint32_t seen = ks.chars + ks.dropped;
        if (seen != last_seen) {
            last_seen = seen;
            last_change = now;
        }
        if (seen >= QFULL_TARGET && tick_reached(now, last_change + QFULL_SETTLE_TICKS))
            break;
        if (tick_reached(now, start + QFULL_TIMEOUT_TICKS)) {
            kprintf_serial("QFULL_TIMEOUT seen=%u\n", seen);
            break;
        }
        wait_until_tick(now + 1);
    }

    keyboard_stats_snapshot(&ks);
    kprintf_serial("QFULL_PAUSE done queued=%u chars=%u dropped=%u scancodes=%u\n",
                   keyboard_queued(), ks.chars, ks.dropped, ks.scancodes);
}
