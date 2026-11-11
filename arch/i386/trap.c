#include <stdint.h>

#include "arch/i386/cpu.h"
#include "arch/i386/trapframe.h"
#include "rangeros/console.h"
#include "rangeros/panic.h"
#include "rangeros/serial.h"

void isr_dispatch(struct trap_frame *tf);

static const char *const exception_names[32] = {
    "#DE", "#DB", "NMI", "#BP", "#OF", "#BR", "#UD", "#NM",
    "#DF", "CSO", "#TS", "#NP", "#SS", "#GP", "#PF", "RSV",
    "#MF", "#AC", "#MC", "#XM", "#VE", "#CP", "RSV", "RSV",
    "RSV", "RSV", "RSV", "RSV", "#HV", "#VC", "#SX", "RSV",
};

#define VECTOR_PAGE_FAULT 14

static int in_trap;

/* Called from isr_common (arch/i386/isr.asm) with IF=0. M1 has no
 * recoverable vectors: report the frame, then panic. */
void isr_dispatch(struct trap_frame *tf)
{
    if (in_trap) {
        /* Fault while reporting a fault: avoid kprintf/VGA, just stop. */
        serial_write("\nEXCEPTION during exception report\nHALT\n");
        cpu_halt_forever();
    }
    in_trap = 1;

    const char *name = tf->vector < 32 ? exception_names[tf->vector] : "INT";

    /* Keep every line under 80 columns so VGA does not wrap it. */
    kprintf("EXCEPTION vector=%u name=%s error=0x%08x eip=0x%08x cs=0x%04x\n",
            tf->vector, name, tf->error, tf->eip, tf->cs);
    kprintf("  eflags=0x%08x\n", tf->eflags);
    if (tf->vector == VECTOR_PAGE_FAULT)
        kprintf("  cr2=0x%08x (page-fault linear address)\n", read_cr2());
    kprintf("  eax=0x%08x ebx=0x%08x ecx=0x%08x edx=0x%08x\n",
            tf->eax, tf->ebx, tf->ecx, tf->edx);
    kprintf("  esi=0x%08x edi=0x%08x ebp=0x%08x esp=0x%08x\n",
            tf->esi, tf->edi, tf->ebp, trap_frame_interrupted_esp(tf));
    kprintf("  ds=0x%04x es=0x%04x fs=0x%04x gs=0x%04x\n", tf->ds, tf->es, tf->fs, tf->gs);
    kprintf("  frame=0x%08x pusha_esp=0x%08x\n", (uint32_t)tf, tf->esp_pusha);

    panic("unhandled %s %u", tf->vector < 32 ? "exception" : "interrupt", tf->vector);
}
