#ifndef RANGEROS_LINE_EDIT_H
#define RANGEROS_LINE_EDIT_H

#include <stdint.h>

/* Longest command line accepted; buf keeps one more byte for '\0'. */
#define LINE_MAX_CHARS 127

/*
 * Fixed-size line buffer for the shell (SPEC section 4).
 *   - printable ASCII (0x20-0x7E) is appended while len < LINE_MAX_CHARS;
 *     beyond that it is rejected (counted in rejected, never stored);
 *   - '\b' removes the last stored character; at column 0 it is a no-op;
 *   - '\n' submits the line; the caller reads buf/len/rejected and then
 *     calls line_edit_reset();
 *   - anything else is ignored.
 * buf[len] is always '\0' and len never exceeds LINE_MAX_CHARS.
 * A line with rejected > 0 is reported as too long and not executed, even
 * if backspaces later shortened it. Pure logic; host-tested in
 * tests/host/test_line_edit.c.
 */
struct line_edit {
    char buf[LINE_MAX_CHARS + 1];
    uint32_t len;
    uint32_t rejected;
};

enum line_event {
    LINE_IGNORED,     /* byte has no meaning for the editor */
    LINE_APPENDED,    /* stored at buf[len - 1]; echo it */
    LINE_ERASED,      /* last character removed; erase it on screen */
    LINE_ERASE_EMPTY, /* backspace at column 0; nothing to erase */
    LINE_REJECTED,    /* line full; byte dropped */
    LINE_SUBMIT,      /* Enter */
};

void line_edit_reset(struct line_edit *le);
enum line_event line_edit_feed(struct line_edit *le, char c);

#endif
