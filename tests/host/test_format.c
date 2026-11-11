/* Host unit test for lib/format.c (built with the VM's native gcc).
 * Compares against the host snprintf where the formats agree, plus
 * explicit expectations for RangerOS-specific behaviour. */
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "rangeros/format.h"

struct buf {
    char data[256];
    int len;
};

static void buf_sink(char c, void *ctx)
{
    struct buf *b = ctx;

    if (b->len < (int)sizeof(b->data) - 1)
        b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

static int failures;
static int cases;

static void check(const char *expect, int expect_n, const char *fmt, ...)
{
    struct buf b = { { 0 }, 0 };
    va_list ap;

    va_start(ap, fmt);
    int n = format_vprint(buf_sink, &b, fmt, ap);
    va_end(ap);
    cases++;
    if (strcmp(b.data, expect) != 0 || n != expect_n) {
        failures++;
        printf("FAIL fmt=\"%s\" got=\"%s\" (%d) expect=\"%s\" (%d)\n", fmt, b.data, n, expect,
               expect_n);
    }
}

/* Same format and arguments through snprintf and format_vprint. */
#define SAME(fmt, ...)                                                   \
    do {                                                                 \
        char ref[256];                                                   \
        int rn = snprintf(ref, sizeof(ref), fmt, __VA_ARGS__);           \
        check(ref, rn, fmt, __VA_ARGS__);                                \
    } while (0)

int main(void)
{
    SAME("%d %d %d", 0, 42, -42);
    SAME("%i|%5d|%-5d|%05d", 7, 7, 7, 7);
    SAME("%05d|%3d", -42, -12345);
    /* '-' overrides '0' (C semantics; gcc warns, so not via snprintf) */
    check("-42  ", 5, "%-05d", -42);
    SAME("%d %d", INT_MAX, INT_MIN);
    SAME("%u %u %u", 0u, 4000000000u, UINT_MAX);
    SAME("%x %X %08x %08X", 0xdeadbeefu, 0xdeadbeefu, 0x1234u, 0xabcu);
    SAME("%x|%4x|%-4x|", 0u, 0xau, 0xau);
    SAME("%c%c%3c|%-3c|", 'O', 'K', 'x', 'y');
    SAME("[%s] [%8s] [%-8s]", "abc", "abc", "abc");
    SAME("100%% %s", "done");
    SAME("row %02d dec=%d neg=%d hex=%x pad=%08X", 29, 29000, -1073, 0x20c14u, 0xDEAD001Du);

    check("0x00001234", 10, "%p", (void *)0x1234);
    check("0x00000000", 10, "%p", (void *)0);
    check("(null)", 6, "%s", (const char *)0);
    check("%q", 2, "%q");
    check("abc%", 4, "abc%");
    check("x%-0", 4, "x%-0");
    /* width capped at FORMAT_MAX_WIDTH (32) */
    check("                               7", 32, "%99d", 7);
    check("", 0, "");

    printf("HOST_FORMAT %s (%d cases, %d failed)\n", failures ? "FAIL" : "PASS", cases,
           failures);
    return failures ? 1 : 0;
}
