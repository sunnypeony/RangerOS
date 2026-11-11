#ifndef RANGEROS_SELFTEST_H
#define RANGEROS_SELFTEST_H

/* Exists only in test kernels: kernel/kmain.c built with
 * -DRANGEROS_SELFTEST calls it at each stage. Each
 * tests/kernel/selftest_<case>.c defines it and ignores the stages it
 * does not use; build/kernel.elf has none. */
enum selftest_stage {
    SELFTEST_STAGE_EARLY,  /* after IDT setup, IF=0, PIC not touched */
    SELFTEST_STAGE_IRQ_ON, /* after sti and BOOT_OK, before the main loop */
};

void selftest_run(enum selftest_stage stage);

#endif
