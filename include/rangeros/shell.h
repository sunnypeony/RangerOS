#ifndef RANGEROS_SHELL_H
#define RANGEROS_SHELL_H

#include <stdint.h>

/*
 * Interactive shell, run only from the main loop (never from an IRQ).
 * Input bytes come from the PS/2 keyboard queue (keyboard_read()); there
 * is no other input path. Output: VGA shows the prompt and echo; command
 * output goes to VGA and COM1; COM1 also gets a diagnostic log (KEY,
 * SHELL, KBD lines; see docs/shell.md).
 */
void shell_init(void);
void shell_input(uint8_t c);

#endif
