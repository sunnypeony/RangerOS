#ifndef RANGEROS_KBD_DECODE_H
#define RANGEROS_KBD_DECODE_H

#include <stdint.h>

/*
 * PS/2 scan code set 1 decoder (what the i8042 delivers with translation
 * enabled, the QEMU pc default; see docs/interrupts.md).
 *
 * Produces ASCII for: a-z (A-Z with Shift), 0-9 (!@#$%^&*() with Shift),
 * space, Enter ('\n'), Backspace ('\b'). Left and right Shift are tracked
 * separately. Everything else returns KBD_NONE and changes no state except:
 *   - 0xE0 prefix: the next byte (an extended key, make or break) is
 *     ignored, so fake shifts such as E0 2A never touch Shift state;
 *   - 0xE1 (Pause): the remaining 5 bytes of the sequence are ignored;
 *   - break codes (bit 7 set) other than the Shift releases;
 *   - controller/keyboard replies 0x00, 0xEE, 0xFA, 0xFE, 0xFF.
 * Pure logic; also built on the host for tests/host/test_kbd_decode.c.
 */
#define KBD_NONE (-1)

struct kbd_decoder {
    uint8_t shift;      /* bit 0: left Shift down, bit 1: right Shift down */
    uint8_t extended;   /* previous byte was 0xE0 */
    uint8_t skip;       /* bytes of a Pause sequence still to ignore */
};

void kbd_decoder_init(struct kbd_decoder *d);
/* Feed one scan code byte; returns an ASCII byte or KBD_NONE. */
int kbd_decode(struct kbd_decoder *d, uint8_t scancode);

#endif
