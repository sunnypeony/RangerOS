#ifndef RANGEROS_PIC_H
#define RANGEROS_PIC_H

#include <stdint.h>

/* 8259A pair remapped so IRQ0-7 -> 0x20-0x27 and IRQ8-15 -> 0x28-0x2F,
 * clear of the CPU exception vectors 0-31 (SPEC D02). */
#define PIC_MASTER_VECTOR 0x20
#define PIC_SLAVE_VECTOR  0x28
#define PIC_IRQ_LINES     16
#define PIC_CASCADE_IRQ   2

/* Remap both PICs and leave every line masked. Must run with IF=0. */
void pic_init(void);
/* Clear the mask bit of one line (IF=0 during init). */
void pic_unmask(unsigned irq);
/* End of interrupt: slave first (if irq >= 8), then master. */
void pic_send_eoi(unsigned irq);
/* For IRQ7/IRQ15: true if the line's ISR bit is clear (spurious). */
int pic_is_spurious(unsigned irq);
/* Current interrupt mask registers, read back from the hardware. */
uint8_t pic_master_mask(void);
uint8_t pic_slave_mask(void);

#endif
