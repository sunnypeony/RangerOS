#ifndef RANGEROS_VGA_H
#define RANGEROS_VGA_H

/* 80x25 VGA text mode at 0xB8000, light grey on black, no IRQ use. */
#define VGA_COLS 80
#define VGA_ROWS 25

void vga_init(void);
void vga_clear(void);
/* '\n' newline, '\r' column 0, '\b' erase left, '\t' next multiple of 8.
 * Writing column 80 wraps; moving past row 25 scrolls up one row. */
void vga_putc(char c);
void vga_write(const char *s);

#endif
