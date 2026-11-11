#include <stdint.h>

#include "arch/i386/cpu.h"
#include "rangeros/keyboard.h"
#include "rangeros/panic.h"
#include "rangeros/pit.h"
#include "rangeros/wait.h"

static void require_if_set(const char *who)
{
    if (!(read_eflags() & EFLAGS_IF))
        panic("%s called with IF=0", who);
}

void wait_for_input(void)
{
    require_if_set("wait_for_input");
    __asm__ volatile("cli" : : : "memory");
    if (keyboard_pending()) {
        __asm__ volatile("sti" : : : "memory");
        return;
    }
    __asm__ volatile("sti\n\thlt" : : : "memory");
}

void wait_until_tick(uint32_t target)
{
    require_if_set("wait_until_tick");
    for (;;) {
        __asm__ volatile("cli" : : : "memory");
        if (tick_reached(timer_ticks(), target)) {
            __asm__ volatile("sti" : : : "memory");
            return;
        }
        __asm__ volatile("sti\n\thlt" : : : "memory");
    }
}
