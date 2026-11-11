#include <stdint.h>

#include "rangeros/console.h"
#include "rangeros/keyboard.h"
#include "rangeros/line_edit.h"
#include "rangeros/pit.h"
#include "rangeros/shell.h"
#include "rangeros/version.h"
#include "rangeros/vga.h"
#include "rangeros/wait.h"

#define SHELL_PROMPT     "> "
#define DEMO_STEPS       5
#define DEMO_STEP_TICKS  20

struct command {
    const char *name;
    const char *help;
    void (*run)(void);
};

static struct line_edit line;

static void cmd_help(void);
static void cmd_version(void);
static void cmd_ticks(void);
static void cmd_clear(void);
static void cmd_demo(void);

static const struct command commands[] = {
    { "help", "list commands", cmd_help },
    { "version", "kernel version and MVP feature set", cmd_version },
    { "ticks", "PIT ticks since boot (100 Hz, simulated time)", cmd_ticks },
    { "clear", "clear the VGA screen", cmd_clear },
    { "demo", "show 5 tick steps of 20 ticks each (about 1 s)", cmd_demo },
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void cmd_help(void)
{
    kprintf("commands:\n");
    for (unsigned i = 0; i < COMMAND_COUNT; i++)
        kprintf("  %-8s %s\n", commands[i].name, commands[i].help);
}

static void cmd_version(void)
{
    kprintf(RANGEROS_NAME " " RANGEROS_VERSION " (i386, multiboot1)\n");
    kprintf("MVP: VGA+serial, GDT/IDT, exceptions, PIC/PIT 100 Hz, PS/2 keyboard, shell\n");
}

static void cmd_ticks(void)
{
    uint32_t t = timer_ticks();

    kprintf("ticks=%u hz=%u uptime=%u.%02us (simulated)\n", t, PIT_HZ, t / PIT_HZ, t % PIT_HZ);
}

static void cmd_clear(void)
{
    vga_clear();
    kprintf_serial("CLEAR vga\n");
}

/* Bounded tick/event display: sleeps with hlt between steps, so keys
 * typed meanwhile are queued by IRQ1 and handled after the demo. */
static void cmd_demo(void)
{
    uint32_t start = timer_ticks();
    struct keyboard_stats ks;

    kprintf("demo: %u steps of %u ticks\n", DEMO_STEPS, DEMO_STEP_TICKS);
    for (unsigned step = 1; step <= DEMO_STEPS; step++) {
        wait_until_tick(start + step * DEMO_STEP_TICKS);
        keyboard_stats_snapshot(&ks);
        kprintf("demo step %u/%u tick=%u scancodes=%u queued=%u\n", step, DEMO_STEPS,
                timer_ticks(), ks.scancodes, keyboard_queued());
    }
    kprintf("demo done after %u ticks\n", timer_ticks() - start);
}

/* Split "  cmd  args  " into the command word and the rest (trimmed). */
static void execute(char *s)
{
    while (*s == ' ')
        s++;
    char *end = s;
    while (*end)
        end++;
    while (end > s && end[-1] == ' ')
        *--end = '\0';
    if (*s == '\0')
        return; /* empty line */

    char *args = s;
    while (*args && *args != ' ')
        args++;
    if (*args) {
        *args++ = '\0';
        while (*args == ' ')
            args++;
    }

    for (unsigned i = 0; i < COMMAND_COUNT; i++) {
        if (str_eq(s, commands[i].name)) {
            if (*args)
                kprintf("shell: %s takes no arguments\n", s);
            else
                commands[i].run();
            return;
        }
    }
    kprintf("unknown command: %s (type help)\n", s);
}

static void log_key(uint8_t c)
{
    switch (c) {
    case '\n':
        kprintf_serial("KEY 0x0a ENTER\n");
        break;
    case '\b':
        kprintf_serial("KEY 0x08 BACKSPACE\n");
        break;
    case ' ':
        kprintf_serial("KEY 0x20 SPACE\n");
        break;
    default:
        kprintf_serial("KEY 0x%02x '%c'\n", c, c);
        break;
    }
}

static void submit(void)
{
    struct keyboard_stats ks;

    vga_putc('\n');
    kprintf_serial("SHELL len=%u rejected=%u [%s]\n", line.len, line.rejected, line.buf);
    /* Messages start with "shell:", never "error:" (the test runner treats
     * "^error: " as a GRUB failure). */
    if (line.rejected)
        kprintf("shell: line too long (max %u chars), %u rejected; discarded\n",
                LINE_MAX_CHARS, line.rejected);
    else
        execute(line.buf);

    keyboard_stats_snapshot(&ks);
    kprintf_serial("KBD chars=%u scancodes=%u dropped=%u ignored=%u\n", ks.chars, ks.scancodes,
                   ks.dropped, ks.ignored);
    line_edit_reset(&line);
    vga_write(SHELL_PROMPT);
}

void shell_init(void)
{
    line_edit_reset(&line);
    kprintf("RangerOS shell: type help\n");
    vga_write(SHELL_PROMPT);
}

void shell_input(uint8_t c)
{
    log_key(c);
    switch (line_edit_feed(&line, (char)c)) {
    case LINE_APPENDED:
        vga_putc((char)c);
        break;
    case LINE_ERASED:
        vga_putc('\b');
        break;
    case LINE_SUBMIT:
        submit();
        break;
    case LINE_ERASE_EMPTY:
    case LINE_REJECTED:
    case LINE_IGNORED:
        break;
    }
}
