#include <stdint.h>

#include "rangeros/multiboot.h"
#include "rangeros/serial.h"
#include "rangeros/version.h"

void kernel_main(uint32_t magic, uint32_t multiboot_info);

/* Called from arch/i386/boot.asm with IF=0 on the 16 KiB boot stack. */
void kernel_main(uint32_t magic, uint32_t multiboot_info)
{
    (void)multiboot_info;

    serial_init();
    serial_write(RANGEROS_NAME " " RANGEROS_VERSION " (i386, multiboot1)\n");

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        serial_write("BOOT_FAIL bad multiboot magic ");
        serial_write_hex32(magic);
        serial_write("\n");
        return;
    }

    serial_write("BOOT_OK\n");
}
