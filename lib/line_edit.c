#include <stdint.h>

#include "rangeros/line_edit.h"

void line_edit_reset(struct line_edit *le)
{
    le->len = 0;
    le->rejected = 0;
    le->buf[0] = '\0';
}

enum line_event line_edit_feed(struct line_edit *le, char c)
{
    if (c == '\n')
        return LINE_SUBMIT;
    if (c == '\b') {
        if (le->len == 0)
            return LINE_ERASE_EMPTY;
        le->buf[--le->len] = '\0';
        return LINE_ERASED;
    }
    if (c < 0x20 || c > 0x7E)
        return LINE_IGNORED;
    if (le->len >= LINE_MAX_CHARS) {
        le->rejected++;
        return LINE_REJECTED;
    }
    le->buf[le->len++] = c;
    le->buf[le->len] = '\0';
    return LINE_APPENDED;
}
