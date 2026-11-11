#ifndef RANGEROS_ARCH_I386_TRAPFRAME_H
#define RANGEROS_ARCH_I386_TRAPFRAME_H

#include <stddef.h>
#include <stdint.h>

/*
 * Exception frame built by arch/i386/isr.asm, lowest address first.
 * The layout table in isr.asm and docs/context.md must match these
 * offsets; the static asserts below pin them.
 */
struct trap_frame {
    /* pushed by isr_common, zero-extended to 32 bits */
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    /* pushad */
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_pusha; /* ESP before pushad, i.e. &vector; not the interrupted ESP */
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    /* pushed by the per-vector stub */
    uint32_t vector;
    uint32_t error; /* CPU error code, or 0 pushed by the stub */
    /* pushed by the CPU; ring 0 -> ring 0 pushes no SS:ESP */
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
};

#define TF_OFFSET_ESP_PUSHA 28
#define TF_OFFSET_VECTOR    48
#define TF_OFFSET_ERROR     52
#define TF_OFFSET_EIP       56
#define TF_SIZE             68

_Static_assert(offsetof(struct trap_frame, ds) == 12, "trap_frame.ds");
_Static_assert(offsetof(struct trap_frame, edi) == 16, "trap_frame.edi");
_Static_assert(offsetof(struct trap_frame, esp_pusha) == TF_OFFSET_ESP_PUSHA, "esp_pusha");
_Static_assert(offsetof(struct trap_frame, eax) == 44, "trap_frame.eax");
_Static_assert(offsetof(struct trap_frame, vector) == TF_OFFSET_VECTOR, "trap_frame.vector");
_Static_assert(offsetof(struct trap_frame, error) == TF_OFFSET_ERROR, "trap_frame.error");
_Static_assert(offsetof(struct trap_frame, eip) == TF_OFFSET_EIP, "trap_frame.eip");
_Static_assert(sizeof(struct trap_frame) == TF_SIZE, "trap_frame size");

/* ESP of the interrupted ring-0 code: first byte above the CPU frame. */
static inline uint32_t trap_frame_interrupted_esp(const struct trap_frame *tf)
{
    return (uint32_t)tf + TF_SIZE;
}

#endif
