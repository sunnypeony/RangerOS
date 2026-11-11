#ifndef RANGEROS_SERIAL_H
#define RANGEROS_SERIAL_H

/* COM1, 115200 8N1, polled output only (no IRQ). Formatted output goes
 * through kprintf (rangeros/console.h). */
void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);

#endif
