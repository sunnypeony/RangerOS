#include "rangeros/console.h"
#include "rangeros/selftest.h"

/* #UD (vector 6, no CPU error code). ud2 sits at the global label
 * selftest_fault_site so the test can compare the reported EIP with the
 * symbol address from nm. Reaching the last line means panic failed. */
void selftest_run(void)
{
    kprintf("SELFTEST ud: executing ud2\n");
    __asm__ volatile(".global selftest_fault_site\n"
                     "selftest_fault_site:\n\t"
                     "ud2");
    kprintf("SELFTEST_FAIL continued after #UD\n");
}
