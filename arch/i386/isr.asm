; Exception entry stubs and the common trap path.
;
; M1: hardware IRQs stay disabled (IF=0, PIC untouched), so only CPU
; exceptions (or a stray INT n) arrive here, and every one is fatal.
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

    ; Return path (unused in M1: isr_dispatch panics). Kept so M2 IRQs
    ; can reuse the same frame.
    pop eax
    mov gs, ax
    pop eax
    mov fs, ax
    pop eax
    mov es, ax
    pop eax
    mov ds, ax
    popad
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
