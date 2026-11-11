#ifndef RANGEROS_PANIC_H
#define RANGEROS_PANIC_H

/* Print "PANIC <message>" and "HALT", then stop with IF=0 forever.
 * Never returns and never schedules. */
__attribute__((noreturn, format(printf, 1, 2))) void panic(const char *fmt, ...);

#endif
