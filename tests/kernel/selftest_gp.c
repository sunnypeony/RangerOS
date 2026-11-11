#include "rangeros/console.h"
#include "rangeros/selftest.h"

/* #GP with a CPU error code (vector 13). Loading DS with selector 0x28
 * (GDT index 5) fails because our GDT has 3 descriptors (limit 0x17):
 * the CPU raises #GP with error code = selector index bits = 0x28
 * (EXT=0, IDT=0, TI=0). EIP must point at the faulting mov. */
#define SELFTEST_BAD_SELECTOR 0x28

void selftest_run(void)
{
    kprintf("SELFTEST gp: loading ds=0x%04x\n", SELFTEST_BAD_SELECTOR);
    __asm__ volatile("movw %0, %%ax\n\t"
                     ".global selftest_fault_site\n"
                     "selftest_fault_site:\n\t"
                     "movw %%ax, %%ds"
                     :
                     : "i"(SELFTEST_BAD_SELECTOR)
                     : "eax", "memory");
    kprintf("SELFTEST_FAIL continued after #GP\n");
}
