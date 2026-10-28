#ifndef RANGEROS_SERIAL_H
#define RANGEROS_SERIAL_H

#include <stdint.h>

/* COM1, 115200 8N1, polled output only (no IRQ). */
void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);
void serial_write_hex32(uint32_t value);

#endif
