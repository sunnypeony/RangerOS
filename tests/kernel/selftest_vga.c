#include <stdarg.h>

#include "arch/i386/cpu.h"
#include "rangeros/format.h"
#include "rangeros/selftest.h"
#include "rangeros/serial.h"
#include "rangeros/vga.h"

/* VGA newline / wrap / scroll / number-format test. Output goes to VGA
 * only. tests/scripts/check_vga.py --scroll-test recomputes the expected
 * 80x25 screen from the same text, so keep the two in sync. */
#define VGA_TEST_ROWS     30
#define VGA_TEST_WRAP_LEN 90

static void vga_sink(char c, void *ctx)
{
    (void)ctx;
    vga_putc(c);
}

static void vga_printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    format_vprint(vga_sink, 0, fmt, ap);
    va_end(ap);
}

void selftest_run(enum selftest_stage stage)
{
    if (stage != SELFTEST_STAGE_EARLY)
        return;
    serial_write("SELFTEST vga: start\n");
    vga_clear();
    for (int i = 0; i < VGA_TEST_ROWS; i++)
        vga_printf("row %02d dec=%d neg=%d hex=%x pad=%08X\n", i, i * 1000, -(i * 37),
                   (unsigned)i * 0x1234u, 0xDEAD0000u + (unsigned)i);
    for (int i = 0; i < VGA_TEST_WRAP_LEN; i++)
        vga_putc((char)('A' + i % 26));
    vga_printf("\nVGA_END");
    serial_write("VGA_TEST_DONE\n");
    cpu_halt_forever();
}
