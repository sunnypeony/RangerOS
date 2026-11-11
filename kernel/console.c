#include <stdarg.h>

#include "rangeros/console.h"
#include "rangeros/format.h"
#include "rangeros/serial.h"
#include "rangeros/vga.h"

void console_init(void)
{
    serial_init();
    vga_init();
}

void console_putc(char c)
{
    serial_putc(c);
    vga_putc(c);
}

static void console_sink(char c, void *ctx)
{
    (void)ctx;
    console_putc(c);
}

int kvprintf(const char *fmt, va_list ap)
{
    return format_vprint(console_sink, 0, fmt, ap);
}

int kprintf(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = kvprintf(fmt, ap);
    va_end(ap);
    return n;
}

static void serial_sink(char c, void *ctx)
{
    (void)ctx;
    serial_putc(c);
}

int kprintf_serial(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = format_vprint(serial_sink, 0, fmt, ap);
    va_end(ap);
    return n;
}
