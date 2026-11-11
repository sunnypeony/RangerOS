#ifndef RANGEROS_ARCH_I386_GDT_H
#define RANGEROS_ARCH_I386_GDT_H

/* Selectors (index * 8, TI=0, RPL=0). Mirrored in gdt_load.asm and isr.asm. */
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_ENTRIES     3

/* Flat ring-0 code/data descriptors over 0..4 GiB, then reload all
 * segment registers. Must run with IF=0. */
void gdt_init(void);

#endif
