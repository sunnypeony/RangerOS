; Test-only (never linked into build/kernel.elf).
;
; int irqret_probe(uint32_t wait_ticks, struct irqret_result *out)
;
; Spins with IF=1 until pit_ticks has advanced by wait_ticks, so every
; PIT IRQ in that window interrupts this loop. Each iteration checks
; that the interrupted context came back intact:
;   - EBX ECX EDX ESI EDI EBP still hold their patterns;
;   - ESP is unchanged and four stack sentinels are intact;
;   - DF is still 1 (set here; isr_common runs C with DF=0, so only a
;     correct iretd restores it) and IF is still 1;
;   - EAX runs an LCG chain the caller recomputes from the iteration
;     count, so a corrupted EAX shows up as a wrong final value.
; The deadline check itself pushes/pops ECX, so IRQs also land while
; the stack holds a temporary value.
;
; Returns 0 and fills out on success, or the failing check's code.

extern pit_ticks
global irqret_probe

PAT_EBX   equ 0x11111111
PAT_ECX   equ 0x22222222
PAT_EDX   equ 0x33333333
PAT_ESI   equ 0x44444444
PAT_EDI   equ 0x55555555
PAT_EBP   equ 0x66666666
EAX_SEED  equ 0xA5A5A5A5
LCG_MUL   equ 1103515245
LCG_ADD   equ 12345
SENTINEL  equ 0x5E471E00
EFLAGS_IF equ 0x200
EFLAGS_DF equ 0x400

; struct irqret_result (tests/kernel/selftest_irqret.c)
RES_EAX   equ 0
RES_ITERS equ 4
RES_FAIL  equ 8

; failure codes (also in selftest_irqret.c)
FAIL_EBX  equ 1
FAIL_ECX  equ 2
FAIL_EDX  equ 3
FAIL_ESI  equ 4
FAIL_EDI  equ 5
FAIL_EBP  equ 6
FAIL_ESP  equ 7
FAIL_STACK equ 8
FAIL_DF   equ 9
FAIL_IF   equ 10

section .bss
probe_esp:      resd 1
probe_deadline: resd 1
probe_iters:    resd 1
probe_out:      resd 1

section .text
irqret_probe:
    push ebp
    push ebx
    push esi
    push edi
    mov eax, [esp + 20]               ; wait_ticks
    mov edx, [esp + 24]               ; out
    mov [probe_out], edx
    add eax, [pit_ticks]
    mov [probe_deadline], eax
    mov dword [probe_iters], 0

    push dword SENTINEL + 0
    push dword SENTINEL + 1
    push dword SENTINEL + 2
    push dword SENTINEL + 3
    mov [probe_esp], esp

    std
    mov eax, EAX_SEED
    mov ebx, PAT_EBX
    mov ecx, PAT_ECX
    mov edx, PAT_EDX
    mov esi, PAT_ESI
    mov edi, PAT_EDI
    mov ebp, PAT_EBP

.loop:
    imul eax, eax, LCG_MUL
    add eax, LCG_ADD
    inc dword [probe_iters]

    cmp ebx, PAT_EBX
    jne .fail_ebx
    cmp ecx, PAT_ECX
    jne .fail_ecx
    cmp edx, PAT_EDX
    jne .fail_edx
    cmp esi, PAT_ESI
    jne .fail_esi
    cmp edi, PAT_EDI
    jne .fail_edi
    cmp ebp, PAT_EBP
    jne .fail_ebp
    cmp esp, [probe_esp]
    jne .fail_esp
    cmp dword [esp + 0], SENTINEL + 3
    jne .fail_stack
    cmp dword [esp + 4], SENTINEL + 2
    jne .fail_stack
    cmp dword [esp + 8], SENTINEL + 1
    jne .fail_stack
    cmp dword [esp + 12], SENTINEL + 0
    jne .fail_stack

    pushfd
    test dword [esp], EFLAGS_DF
    jz .fail_df
    test dword [esp], EFLAGS_IF
    jz .fail_if
    add esp, 4

    ; Deadline check; ECX is the only scratch register and is restored
    ; by pop, which does not change flags.
    push ecx
    mov ecx, [pit_ticks]
    sub ecx, [probe_deadline]
    pop ecx
    js .loop

    cld
    mov ecx, [probe_out]
    mov [ecx + RES_EAX], eax
    mov eax, [probe_iters]
    mov [ecx + RES_ITERS], eax
    mov dword [ecx + RES_FAIL], 0
    xor eax, eax
    jmp .out

.fail_ebx:
    mov eax, FAIL_EBX
    jmp .fail
.fail_ecx:
    mov eax, FAIL_ECX
    jmp .fail
.fail_edx:
    mov eax, FAIL_EDX
    jmp .fail
.fail_esi:
    mov eax, FAIL_ESI
    jmp .fail
.fail_edi:
    mov eax, FAIL_EDI
    jmp .fail
.fail_ebp:
    mov eax, FAIL_EBP
    jmp .fail
.fail_esp:
    mov eax, FAIL_ESP
    jmp .fail
.fail_stack:
    mov eax, FAIL_STACK
    jmp .fail
.fail_df:
    mov eax, FAIL_DF
    jmp .fail
.fail_if:
    mov eax, FAIL_IF
.fail:
    cld
    mov esp, [probe_esp]
    mov ecx, [probe_out]
    mov [ecx + RES_FAIL], eax
    mov edx, [probe_iters]
    mov [ecx + RES_ITERS], edx
    mov dword [ecx + RES_EAX], 0

.out:
    mov esp, [probe_esp]
    add esp, 16                       ; sentinels
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
