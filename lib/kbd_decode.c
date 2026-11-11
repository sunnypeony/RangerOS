#include <stdint.h>

#include "rangeros/kbd_decode.h"

#define SC_EXTENDED      0xE0
#define SC_PAUSE         0xE1
#define SC_PAUSE_TAIL    5 /* E1 1D 45 E1 9D C5 */
#define SC_BREAK         0x80
#define SC_LSHIFT        0x2A
#define SC_RSHIFT        0x36
#define SHIFT_LEFT       0x1
#define SHIFT_RIGHT      0x2
#define SC_TABLE_SIZE    0x3A /* make codes 0x00-0x39 */

/* Set 1 make code -> ASCII without Shift; 0 = not supported. */
static const char normal[SC_TABLE_SIZE] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0E] = '\b',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm',
    [0x39] = ' ',
};

static const char shifted_digits[10] = { ')', '!', '@', '#', '$', '%', '^', '&', '*', '(' };

void kbd_decoder_init(struct kbd_decoder *d)
{
    d->shift = 0;
    d->extended = 0;
    d->skip = 0;
}

static int is_reply_byte(uint8_t sc)
{
    return sc == 0x00 || sc == 0xEE || sc == 0xFA || sc == 0xFE || sc == 0xFF;
}

int kbd_decode(struct kbd_decoder *d, uint8_t sc)
{
    if (d->skip) {
        d->skip--;
        return KBD_NONE;
    }
    if (sc == SC_PAUSE) {
        d->skip = SC_PAUSE_TAIL;
        return KBD_NONE;
    }
    if (sc == SC_EXTENDED) {
        d->extended = 1;
        return KBD_NONE;
    }
    if (d->extended) {
        /* Extended keys (arrows, right Ctrl/Alt, keypad Enter, fake
         * shifts) are not supported yet. */
        d->extended = 0;
        return KBD_NONE;
    }
    if (is_reply_byte(sc))
        return KBD_NONE;

    uint8_t code = sc & (uint8_t)~SC_BREAK;
    int release = (sc & SC_BREAK) != 0;

    if (code == SC_LSHIFT || code == SC_RSHIFT) {
        uint8_t bit = code == SC_LSHIFT ? SHIFT_LEFT : SHIFT_RIGHT;
        if (release)
            d->shift &= (uint8_t)~bit;
        else
            d->shift |= bit;
        return KBD_NONE;
    }
    if (release || code >= SC_TABLE_SIZE || normal[code] == 0)
        return KBD_NONE;

    char c = normal[code];
    if (d->shift) {
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        else if (c >= '0' && c <= '9')
            c = shifted_digits[c - '0'];
    }
    return (unsigned char)c;
}
