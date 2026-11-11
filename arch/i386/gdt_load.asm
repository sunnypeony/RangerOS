; void gdt_load(const struct gdt_ptr *ptr)
;
; Load GDTR, then reload every segment register so none keeps a cached
; descriptor from GRUB's GDT. Data segments are loaded with mov; CS can
; only change through a far control transfer, hence the far jmp.

GDT_KERNEL_CODE equ 0x08              ; must match arch/i386/gdt.h
GDT_KERNEL_DATA equ 0x10

section .text
global gdt_load

gdt_load:
    mov eax, [esp + 4]                ; ptr (cdecl arg0)
    lgdt [eax]

    mov ax, GDT_KERNEL_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax                        ; flat base 0: ESP stays valid

    jmp GDT_KERNEL_CODE:.reload_cs
.reload_cs:
    ret
