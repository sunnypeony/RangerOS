#include <stdint.h>

#include "arch/i386/gdt.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t flags_limit_high; /* flags in bits 7:4, limit 19:16 in bits 3:0 */
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

_Static_assert(sizeof(struct gdt_entry) == 8, "GDT descriptor is 8 bytes");
_Static_assert(sizeof(struct gdt_ptr) == 6, "GDTR operand is 6 bytes");

/* Access byte: P=1, DPL=0, S=1 (code/data), then type. */
#define GDT_ACCESS_CODE 0x9A /* execute/read */
#define GDT_ACCESS_DATA 0x92 /* read/write */
/* Flags: G=1 (4 KiB granularity), D/B=1 (32-bit). */
#define GDT_FLAGS_4K_32 0xC0
#define GDT_LIMIT_4G    0xFFFFF

static struct gdt_entry gdt[GDT_ENTRIES] __attribute__((aligned(8)));
static struct gdt_ptr gdtr;

/* arch/i386/gdt_load.asm */
void gdt_load(const struct gdt_ptr *ptr);

static void gdt_set(unsigned index, uint32_t base, uint32_t limit, uint8_t access,
                    uint8_t flags)
{
    struct gdt_entry *e = &gdt[index];

    e->limit_low = (uint16_t)(limit & 0xFFFF);
    e->base_low = (uint16_t)(base & 0xFFFF);
    e->base_mid = (uint8_t)((base >> 16) & 0xFF);
    e->access = access;
    e->flags_limit_high = (uint8_t)(flags | ((limit >> 16) & 0x0F));
    e->base_high = (uint8_t)((base >> 24) & 0xFF);
}

void gdt_init(void)
{
    gdt_set(0, 0, 0, 0, 0); /* null descriptor */
    gdt_set(GDT_KERNEL_CODE / 8, 0, GDT_LIMIT_4G, GDT_ACCESS_CODE, GDT_FLAGS_4K_32);
    gdt_set(GDT_KERNEL_DATA / 8, 0, GDT_LIMIT_4G, GDT_ACCESS_DATA, GDT_FLAGS_4K_32);

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uint32_t)gdt;
    gdt_load(&gdtr);
}
