#include <stdint.h>

#include "arch/i386/gdt.h"
#include "arch/i386/idt.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

_Static_assert(sizeof(struct idt_entry) == 8, "IDT gate is 8 bytes");
_Static_assert(sizeof(struct idt_ptr) == 6, "IDTR operand is 6 bytes");

/* P=1, DPL=0, type 0xE: 32-bit interrupt gate (CPU clears IF on entry). */
#define IDT_GATE_INT32 0x8E

/* arch/i386/isr.asm */
extern const uint32_t isr_stub_table[IDT_VECTORS];

static struct idt_entry idt[IDT_VECTORS] __attribute__((aligned(8)));
static struct idt_ptr idtr;

void idt_init(void)
{
    for (unsigned v = 0; v < IDT_VECTORS; v++) {
        uint32_t handler = isr_stub_table[v];

        idt[v].offset_low = (uint16_t)(handler & 0xFFFF);
        idt[v].selector = GDT_KERNEL_CODE;
        idt[v].zero = 0;
        idt[v].type_attr = IDT_GATE_INT32;
        idt[v].offset_high = (uint16_t)(handler >> 16);
    }

    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint32_t)idt;
    __asm__ volatile("lidt %0" : : "m"(idtr));
}
