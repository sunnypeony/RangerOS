#ifndef RANGEROS_KEYBOARD_H
#define RANGEROS_KEYBOARD_H

#include <stdint.h>

/* PS/2 keyboard on the i8042 first port, IRQ1, scan code set 1 as
 * delivered with controller translation on (docs/interrupts.md). */

/* i8042 configuration byte bit: translate keyboard set 2 to set 1. */
#define I8042_CONFIG_TRANSLATE 0x40

struct keyboard_stats {
    uint32_t irqs;      /* IRQ1 handler runs */
    uint32_t scancodes; /* bytes read from port 0x60 */
    uint32_t chars;     /* decoded bytes accepted by the queue */
    uint32_t ignored;   /* bytes that decoded to nothing (breaks, prefixes...) */
    uint32_t dropped;   /* decoded bytes rejected by a full queue */
    uint32_t empty;     /* IRQ1 with no byte in the output buffer */
};

/* Flush the controller, enable IRQ1 + translation, register IRQ1.
 * Returns 0 on success, -1 if the controller did not answer. IF=0. */
int keyboard_init(void);
/* Controller configuration byte after keyboard_init(). */
uint8_t keyboard_config(void);
/* Next queued byte, or -1 if empty. */
int keyboard_read(void);
int keyboard_pending(void);
/* Bytes currently waiting in the input queue (0..INPUT_QUEUE_CAPACITY). */
uint32_t keyboard_queued(void);
void keyboard_stats_snapshot(struct keyboard_stats *out);

#endif
