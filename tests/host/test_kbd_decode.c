/* Host unit test for lib/kbd_decode.c (scan code set 1). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rangeros/kbd_decode.h"

static int failures;
static int cases;

/* Feed bytes, collect produced characters, compare with expect. */
static void feed(struct kbd_decoder *d, const char *name, const uint8_t *bytes, int n,
                 const char *expect)
{
    char out[64];
    int len = 0;

    for (int i = 0; i < n; i++) {
        int c = kbd_decode(d, bytes[i]);
        if (c != KBD_NONE && len < (int)sizeof(out) - 1)
            out[len++] = (char)c;
    }
    out[len] = '\0';
    cases++;
    if (strcmp(out, expect) != 0) {
        failures++;
        printf("FAIL %s: got \"%s\" expect \"%s\"\n", name, out, expect);
    }
}

#define FEED(d, name, expect, ...)                                              \
    do {                                                                        \
        static const uint8_t seq_[] = { __VA_ARGS__ };                          \
        feed(d, name, seq_, (int)sizeof(seq_), expect);                         \
    } while (0)

int main(void)
{
    struct kbd_decoder d;

    kbd_decoder_init(&d);
    FEED(&d, "letters make+break", "abz", 0x1E, 0x9E, 0x30, 0xB0, 0x2C, 0xAC);
    FEED(&d, "digits", "1234567890", 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B);
    FEED(&d, "space enter backspace", " \n\b", 0x39, 0xB9, 0x1C, 0x9C, 0x0E, 0x8E);
    FEED(&d, "break codes alone", "", 0x9E, 0xB0, 0x82);

    FEED(&d, "left shift letter then release", "Aa", 0x2A, 0x1E, 0x9E, 0xAA, 0x1E, 0x9E);
    FEED(&d, "right shift", "Hi", 0x36, 0x23, 0xA3, 0xB6, 0x17, 0x97);
    FEED(&d, "shift digits", "!@#)", 0x2A, 0x02, 0x03, 0x04, 0x0B, 0xAA);
    FEED(&d, "both shifts, release one", "AA", 0x2A, 0x36, 0x1E, 0xAA, 0x1E, 0xB6);
    FEED(&d, "shift released", "a", 0x1E);
    FEED(&d, "shift does not change space/enter", " \n", 0x2A, 0x39, 0x1C, 0xAA);

    /* extended (E0) keys are ignored, make and break */
    FEED(&d, "keypad enter / arrows", "", 0xE0, 0x1C, 0xE0, 0x9C, 0xE0, 0x48, 0xE0, 0xC8);
    FEED(&d, "after extended", "a", 0x1E);
    /* PrintScreen's fake shift E0 2A must not set Shift */
    FEED(&d, "fake shift", "a", 0xE0, 0x2A, 0xE0, 0x37, 0x1E, 0xE0, 0xB7, 0xE0, 0xAA);
    /* Pause: E1 1D 45 E1 9D C5, all six ignored, then normal again */
    FEED(&d, "pause sequence", "q", 0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5, 0x10);

    FEED(&d, "unsupported keys", "", 0x3B, 0x01, 0x0F, 0x1D, 0x38, 0x3A, 0x0C, 0x58);
    FEED(&d, "replies", "", 0xFA, 0xFE, 0xEE, 0x00, 0xFF);
    FEED(&d, "still decoding", "ok", 0x18, 0x25);
    FEED(&d, "Hi RangerOS 123!", "Hi RangerOS 123!",
         0x2A, 0x23, 0xA3, 0xAA, 0x17, 0x97, 0x39, 0xB9,
         0x2A, 0x13, 0x93, 0xAA, 0x1E, 0x31, 0x22, 0x12, 0x13,
         0x2A, 0x18, 0x98, 0xAA, 0x2A, 0x1F, 0x9F, 0xAA, 0x39,
         0x02, 0x03, 0x04, 0x2A, 0x02, 0x82, 0xAA);

    printf("HOST_KBD_DECODE %s (%d cases, %d failed)\n", failures ? "FAIL" : "PASS", cases,
           failures);
    return failures ? 1 : 0;
}
