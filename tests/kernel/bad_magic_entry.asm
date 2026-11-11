; Test-only ELF entry (never linked into build/kernel.elf).
; Enters the real _start with a wrong EAX so kernel_main takes the
; BOOT_FAIL path; EBX still holds GRUB's multiboot_info pointer.

global selftest_bad_magic_entry
extern _start

BAD_MAGIC equ 0xDEADBEEF

section .text
selftest_bad_magic_entry:
    mov eax, BAD_MAGIC
    jmp _start
