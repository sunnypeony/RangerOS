#ifndef RANGEROS_ARCH_I386_IDT_H
#define RANGEROS_ARCH_I386_IDT_H

#define IDT_VECTORS 256

/* Install a ring-0 32-bit interrupt gate for every vector (stubs in
 * isr.asm) and load IDTR. Does not enable interrupts. */
void idt_init(void);

#endif
