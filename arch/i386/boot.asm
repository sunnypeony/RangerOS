; RangerOS i386 entry point (Multiboot 1).
;
; GRUB loads the ELF segments, then jumps to _start in 32-bit protected
; mode with paging off, IF=0, EAX = 0x2BADB002 and EBX = physical address
; of the multiboot_info structure. ESP is undefined, so no stack exists yet.

MB_MAGIC      equ 0x1BADB002
MB_FLAG_ALIGN equ 1 << 0              ; page-align loaded modules
MB_FLAG_MEM   equ 1 << 1              ; ask GRUB for mem_lower/mem_upper
MB_FLAGS      equ MB_FLAG_ALIGN | MB_FLAG_MEM
MB_CHECKSUM   equ -(MB_MAGIC + MB_FLAGS)

BOOT_STACK_SIZE equ 16384

; The header must sit 4-byte aligned in the first 8 KiB of the file.
; linker.ld places .multiboot first in the first loaded segment.
section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM

; Boot stack lives in .bss; zeroed below before it is first used.
section .bss
align 16
boot_stack_bottom:
    resb BOOT_STACK_SIZE
boot_stack_top:

section .text
global _start
extern kernel_main
extern __bss_start
extern __bss_end

_start:
    cli                                ; D01: no interrupts until IDT exists
    cld                                ; D01: C code assumes DF=0

    ; Clear .bss (includes the boot stack). rep stosd clobbers EAX/ECX/EDI,
    ; so keep the multiboot magic in EDX; EBX is untouched.
    mov edx, eax
    xor eax, eax
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    shr ecx, 2                         ; linker.ld keeps .bss 4-byte sized
    rep stosd

    mov esp, boot_stack_top            ; 16-byte aligned
    xor ebp, ebp                       ; terminate frame-pointer chain

    ; i386 SysV: ESP must be 16-byte aligned at the call instruction.
    ; 8 bytes padding + 2 args (8 bytes) keeps that. See docs/context.md.
    sub esp, 8
    push ebx                           ; arg1: multiboot_info physical address
    push edx                           ; arg0: multiboot magic
    call kernel_main

    ; kernel_main must not return; halt forever if it does.
.hang:
    cli
    hlt
    jmp .hang
