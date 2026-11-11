#include <stdarg.h>

#include "arch/i386/cpu.h"
#include "rangeros/console.h"
#include "rangeros/panic.h"
#include "rangeros/serial.h"

static int panicking;

void panic(const char *fmt, ...)
{
    va_list ap;

    __asm__ volatile("cli");
    if (panicking++) {
        /* Panic while panicking: serial only, no formatting. */
        serial_write("\nPANIC (nested)\nHALT\n");
        cpu_halt_forever();
    }

    kprintf("PANIC ");
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    kprintf("\nHALT\n");
    cpu_halt_forever();
}
