#ifndef RANGEROS_SELFTEST_H
#define RANGEROS_SELFTEST_H

/* Exists only in test kernels: kernel/kmain.c built with
 * -DRANGEROS_SELFTEST calls it after IDT setup. Each
 * tests/kernel/selftest_<case>.c defines it; build/kernel.elf has none. */
void selftest_run(void);

#endif
