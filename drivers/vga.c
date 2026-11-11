#include <stdint.h>

#include "arch/i386/io.h"
#include "rangeros/vga.h"

#define VGA_MEMORY ((volatile uint16_t *)0xB8000)
#define VGA_ATTR   0x07 /* light grey on black */
#define VGA_TAB    8

#define VGA_CRTC_INDEX       0x3D4
#define VGA_CRTC_DATA        0x3D5
#define VGA_CRTC_CURSOR_HIGH 0x0E
#define VGA_CRTC_CURSOR_LOW  0x0F

static unsigned vga_row;
static unsigned vga_col;

static uint16_t vga_cell(char c)
{
    return (uint16_t)((VGA_ATTR << 8) | (uint8_t)c);
}

static void vga_update_cursor(void)
{
    uint16_t pos = (uint16_t)(vga_row * VGA_COLS + vga_col);

    outb(VGA_CRTC_INDEX, VGA_CRTC_CURSOR_LOW);
    outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFF));
    outb(VGA_CRTC_INDEX, VGA_CRTC_CURSOR_HIGH);
    outb(VGA_CRTC_DATA, (uint8_t)(pos >> 8));
}

static void vga_scroll(void)
{
    for (unsigned i = 0; i < (VGA_ROWS - 1) * VGA_COLS; i++)
        VGA_MEMORY[i] = VGA_MEMORY[i + VGA_COLS];
    for (unsigned i = (VGA_ROWS - 1) * VGA_COLS; i < VGA_ROWS * VGA_COLS; i++)
        VGA_MEMORY[i] = vga_cell(' ');
}

static void vga_newline(void)
{
    vga_col = 0;
    if (++vga_row == VGA_ROWS) {
        vga_scroll();
        vga_row = VGA_ROWS - 1;
    }
}

static void vga_put_visible(char c)
{
    VGA_MEMORY[vga_row * VGA_COLS + vga_col] = vga_cell(c);
    if (++vga_col == VGA_COLS)
        vga_newline();
}

void vga_clear(void)
{
    for (unsigned i = 0; i < VGA_ROWS * VGA_COLS; i++)
        VGA_MEMORY[i] = vga_cell(' ');
    vga_row = 0;
    vga_col = 0;
    vga_update_cursor();
}

void vga_init(void)
{
    vga_clear();
}

void vga_putc(char c)
{
    switch (c) {
    case '\n':
        vga_newline();
        break;
    case '\r':
        vga_col = 0;
        break;
    case '\b':
        if (vga_col > 0) {
            vga_col--;
            VGA_MEMORY[vga_row * VGA_COLS + vga_col] = vga_cell(' ');
        }
        break;
    case '\t':
        do
            vga_put_visible(' ');
        while (vga_col % VGA_TAB != 0);
        break;
    default:
        vga_put_visible(c);
        break;
    }
    vga_update_cursor();
}

void vga_write(const char *s)
{
    while (*s)
        vga_putc(*s++);
}
