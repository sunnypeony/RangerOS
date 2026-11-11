; Exception/IRQ entry stubs and the common trap path.
;
; CPU exceptions (0-31) and unknown vectors are fatal: isr_dispatch
; panics. Hardware IRQs (0x20-0x2F after PIC remap) return through the
; same frame (docs/interrupts.md).
;
; Every vector gets a stub that leaves the same frame shape:
;   - the CPU pushes EFLAGS, CS, EIP (same privilege level: no SS:ESP);
;   - for vectors 8, 10-14, 17, 21, 29, 30 the CPU also pushes an error
;     code; all other stubs push a dummy 0 in its place;
;   - the stub pushes its vector number and jumps to isr_common.
; isr_common saves the general registers (pushad) and the data segment
; registers, giving struct trap_frame (arch/i386/trapframe.h):
;
;   offset  field       pushed by
;     0     gs          isr_common
;     4     fs          isr_common
;     8     es          isr_common
;    12     ds          isr_common
;    16     edi         pushad
;    20     esi         pushad
;    24     ebp         pushad
;    28     esp_pusha   pushad (value = frame + 48, i.e. &vector)
;    32     ebx         pushad
;    36     edx         pushad
;    40     ecx         pushad
;    44     eax         pushad
;    48     vector      stub
;    52     error       CPU, or stub (dummy 0)
;    56     eip         CPU
;    60     cs          CPU
;    64     eflags      CPU
;    68     interrupted code's stack (its ESP before the exception)

GDT_KERNEL_DATA equ 0x10              ; must match arch/i386/gdt.h

extern isr_dispatch

section .text

%assign vec 0
%rep 256
isr_stub_ %+ vec:
%if vec == 8 || (vec >= 10 && vec <= 14) || vec == 17 || vec == 21 || vec == 29 || vec == 30
    ; CPU already pushed an error code
%else
    push dword 0                      ; dummy error code
%endif
    push dword vec
    jmp isr_common
%assign vec vec + 1
%endrep

isr_common:
    pushad

    ; Segment pushes are done through EAX: a 32-bit "push ds" may write
    ; only 16 bits, leaving the upper half of the slot undefined.
    xor eax, eax
    mov ax, ds
    push eax
    mov ax, es
    push eax
    mov ax, fs
    push eax
    mov ax, gs
    push eax

    mov ax, GDT_KERNEL_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld                               ; C code assumes DF=0 (D01)

    ; isr_dispatch(struct trap_frame *) with ESP 16-byte aligned at the
    ; call. EBX is callee-saved, so it still holds the frame afterwards.
    mov ebx, esp
    and esp, -16
    sub esp, 12
    push ebx
    call isr_dispatch
    mov esp, ebx

    ; Return path, taken by hardware IRQs (isr_dispatch returns; CPU
    ; exceptions never get here). Restores the interrupted segment and
    ; general registers from the frame; iretd then restores EIP, CS and
    ; EFLAGS (including IF and DF), which also puts ESP back exactly at
    ; frame + 68, the interrupted code's stack pointer.
    pop eax
    mov gs, ax
    pop eax
    mov fs, ax
    pop eax
    mov es, ax
    pop eax
    mov ds, ax
    popad
%ifdef RANGEROS_IRQ_RETURN_SABOTAGE
    ; Test-only (never defined for build/kernel.elf): corrupt ESI on the
    ; way out once the IRQ-return selftest arms it, to prove the probe in
    ; tests/kernel/irqret_probe.asm detects a broken return path. cmp
    ; changes flags, but iretd reloads EFLAGS from the frame.
extern irqret_sabotage_armed
    cmp dword [irqret_sabotage_armed], 0
    je .no_sabotage
    mov esi, 0xBAD0BAD0
.no_sabotage:
%endif
    add esp, 8                        ; drop vector and error code
    iretd

section .rodata
align 4
global isr_stub_table
isr_stub_table:
%assign vec 0
%rep 256
    dd isr_stub_ %+ vec
%assign vec vec + 1
%endrep
