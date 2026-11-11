#ifndef RANGEROS_CONSOLE_H
#define RANGEROS_CONSOLE_H

#include <stdarg.h>

/* Kernel console: every character goes to COM1 and to VGA text mode. */
void console_init(void);
void console_putc(char c);

/* Format syntax: see rangeros/format.h. */
int kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int kvprintf(const char *fmt, va_list ap);

#endif
