#include <stdint.h>

#include "arch/i386/io.h"
#include "arch/i386/irqflags.h"
#include "rangeros/input_queue.h"
#include "rangeros/irq.h"
#include "rangeros/kbd_decode.h"
#include "rangeros/keyboard.h"

#define I8042_DATA   0x60
#define I8042_STATUS 0x64
#define I8042_CMD    0x64

#define STATUS_OUTPUT_FULL 0x01
#define STATUS_INPUT_FULL  0x02
#define STATUS_AUX_DATA    0x20 /* byte came from the mouse port */

#define CMD_READ_CONFIG  0x20
#define CMD_WRITE_CONFIG 0x60

#define CONFIG_IRQ1        0x01
#define CONFIG_IRQ12       0x02
#define CONFIG_PORT1_CLOCK 0x10 /* 1 = first port clock disabled */
#define CONFIG_TRANSLATE   I8042_CONFIG_TRANSLATE

#define I8042_SPIN  100000
#define FLUSH_LIMIT 32

/* Shared between the IRQ1 handler (writer, IF=0) and the main loop
 * (reader, inside irq_save/irq_restore). */
static struct input_queue queue;
static struct kbd_decoder decoder;
static struct keyboard_stats stats;
static uint8_t config;

static int wait_input_empty(void)
{
    for (int i = 0; i < I8042_SPIN; i++)
        if (!(inb(I8042_STATUS) & STATUS_INPUT_FULL))
            return 0;
    return -1;
}

static int wait_output_full(void)
{
    for (int i = 0; i < I8042_SPIN; i++)
        if (inb(I8042_STATUS) & STATUS_OUTPUT_FULL)
            return 0;
    return -1;
}

static void flush_output(void)
{
    for (int i = 0; i < FLUSH_LIMIT && (inb(I8042_STATUS) & STATUS_OUTPUT_FULL); i++)
        (void)inb(I8042_DATA);
}

/* IRQ1: one byte per interrupt. Read, decode (Shift state), enqueue,
 * count. No printing; EOI is sent by irq_dispatch afterwards. */
static void keyboard_irq(void)
{
    stats.irqs++;
    uint8_t status = inb(I8042_STATUS);
    if (!(status & STATUS_OUTPUT_FULL)) {
        stats.empty++;
        return;
    }
    uint8_t sc = inb(I8042_DATA);
    if (status & STATUS_AUX_DATA)
        return; /* mouse byte; port 2 IRQ is disabled, so unexpected */
    stats.scancodes++;

    int c = kbd_decode(&decoder, sc);
    if (c == KBD_NONE)
        stats.ignored++;
    else if (input_queue_push(&queue, (uint8_t)c))
        stats.chars++;
    else
        stats.dropped++;
}

int keyboard_init(void)
{
    input_queue_init(&queue);
    kbd_decoder_init(&decoder);
    flush_output();

    if (wait_input_empty() < 0)
        return -1;
    outb(I8042_CMD, CMD_READ_CONFIG);
    if (wait_output_full() < 0)
        return -1;
    uint8_t old = inb(I8042_DATA);

    config = (uint8_t)((old | CONFIG_IRQ1 | CONFIG_TRANSLATE) &
                       ~(CONFIG_IRQ12 | CONFIG_PORT1_CLOCK));
    if (config != old) {
        if (wait_input_empty() < 0)
            return -1;
        outb(I8042_CMD, CMD_WRITE_CONFIG);
        if (wait_input_empty() < 0)
            return -1;
        outb(I8042_DATA, config);
    }
    flush_output();
    irq_register(1, keyboard_irq);
    return 0;
}

uint8_t keyboard_config(void)
{
    return config;
}

int keyboard_read(void)
{
    irqflags_t flags = irq_save();
    uint8_t byte;
    int ok = input_queue_pop(&queue, &byte);

    irq_restore(flags);
    return ok ? byte : -1;
}

int keyboard_pending(void)
{
    irqflags_t flags = irq_save();
    int pending = input_queue_count(&queue) != 0;

    irq_restore(flags);
    return pending;
}

void keyboard_stats_snapshot(struct keyboard_stats *out)
{
    irqflags_t flags = irq_save();

    *out = stats;
    irq_restore(flags);
}
