#include <stdint.h>

#include "arch/i386/io.h"
#include "rangeros/pic.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_ICW4     0x01 /* ICW4 follows */
#define ICW1_INIT     0x10
#define ICW3_MASTER   (1u << PIC_CASCADE_IRQ) /* slave wired to IRQ2 */
#define ICW3_SLAVE    PIC_CASCADE_IRQ         /* slave's cascade identity */
#define ICW4_8086     0x01
#define OCW2_EOI      0x20 /* non-specific EOI */
#define OCW3_READ_ISR 0x0B

#define PIC_MASK_ALL 0xFF

static uint8_t mask_master = PIC_MASK_ALL;
static uint8_t mask_slave = PIC_MASK_ALL;

/* A write to an unused port gives the PIC time between ICW bytes. */
static void io_wait(void)
{
    outb(0x80, 0);
}

void pic_init(void)
{
    /* Mask everything before touching the ICWs. */
    outb(PIC1_DATA, PIC_MASK_ALL);
    outb(PIC2_DATA, PIC_MASK_ALL);

    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC1_DATA, PIC_MASTER_VECTOR); /* ICW2: vector base */
    io_wait();
    outb(PIC2_DATA, PIC_SLAVE_VECTOR);
    io_wait();
    outb(PIC1_DATA, ICW3_MASTER);
    io_wait();
    outb(PIC2_DATA, ICW3_SLAVE);
    io_wait();
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /* Initialisation clears the IMR; mask all lines again. */
    mask_master = PIC_MASK_ALL;
    mask_slave = PIC_MASK_ALL;
    outb(PIC1_DATA, mask_master);
    outb(PIC2_DATA, mask_slave);
}

void pic_unmask(unsigned irq)
{
    if (irq < 8) {
        mask_master &= (uint8_t)~(1u << irq);
        outb(PIC1_DATA, mask_master);
    } else if (irq < PIC_IRQ_LINES) {
        /* A slave line also needs the cascade input open on the master. */
        mask_slave &= (uint8_t)~(1u << (irq - 8));
        outb(PIC2_DATA, mask_slave);
        pic_unmask(PIC_CASCADE_IRQ);
    }
}

void pic_send_eoi(unsigned irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, OCW2_EOI);
    outb(PIC1_CMD, OCW2_EOI);
}

int pic_is_spurious(unsigned irq)
{
    uint16_t cmd = irq < 8 ? PIC1_CMD : PIC2_CMD;

    outb(cmd, OCW3_READ_ISR);
    return (inb(cmd) & (1u << (irq & 7))) == 0;
}

uint8_t pic_master_mask(void)
{
    return inb(PIC1_DATA);
}

uint8_t pic_slave_mask(void)
{
    return inb(PIC2_DATA);
}
