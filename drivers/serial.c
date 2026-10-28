#include <stdint.h>

#include "arch/i386/io.h"
#include "rangeros/serial.h"

#define COM1 0x3F8

/* 16550 UART register offsets from the base port. */
#define UART_DATA 0 /* DLL when DLAB=1 */
#define UART_IER  1 /* DLM when DLAB=1 */
#define UART_FCR  2
#define UART_LCR  3
#define UART_MCR  4
#define UART_LSR  5

#define LCR_DLAB      0x80
#define LCR_8N1       0x03
#define LSR_THR_EMPTY 0x20

void serial_init(void)
{
    outb(COM1 + UART_IER, 0x00);      /* no UART interrupts */
    outb(COM1 + UART_LCR, LCR_DLAB);
    outb(COM1 + UART_DATA, 0x01);     /* divisor 1 -> 115200 baud */
    outb(COM1 + UART_IER, 0x00);
    outb(COM1 + UART_LCR, LCR_8N1);
    outb(COM1 + UART_FCR, 0xC7);      /* enable + clear FIFOs, 14-byte level */
    outb(COM1 + UART_MCR, 0x03);      /* DTR + RTS; OUT2 stays off (no IRQ) */
}

void serial_putc(char c)
{
    if (c == '\n')
        serial_putc('\r');
    while ((inb(COM1 + UART_LSR) & LSR_THR_EMPTY) == 0)
        ;
    outb(COM1 + UART_DATA, (uint8_t)c);
}

void serial_write(const char *s)
{
    while (*s)
        serial_putc(*s++);
}

void serial_write_hex32(uint32_t value)
{
    static const char digits[] = "0123456789abcdef";

    serial_write("0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        serial_putc(digits[(value >> shift) & 0xF]);
}
