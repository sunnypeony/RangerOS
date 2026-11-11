/* Host unit test for lib/line_edit.c: append, backspace (column 0 and
 * normal), 127-character boundary, rejection beyond it, terminator and
 * no writes past the buffer. */
#include <stdio.h>
#include <string.h>

#include "rangeros/line_edit.h"

static int failures;
static int checks;

#define CHECK(cond)                                                     \
    do {                                                                \
        checks++;                                                       \
        if (!(cond)) {                                                  \
            failures++;                                                 \
            printf("FAIL line %d: %s\n", __LINE__, #cond);              \
        }                                                               \
    } while (0)

/* Guard bytes on both sides catch any out-of-bounds write. */
struct guarded {
    unsigned char before[32];
    struct line_edit le;
    unsigned char after[32];
};

static int guards_intact(const struct guarded *g)
{
    for (int i = 0; i < 32; i++)
        if (g->before[i] != 0xA5 || g->after[i] != 0xA5)
            return 0;
    return 1;
}

static void feed_str(struct line_edit *le, const char *s)
{
    while (*s)
        line_edit_feed(le, *s++);
}

int main(void)
{
    struct guarded g;
    struct line_edit *le = &g.le;

    memset(&g, 0xA5, sizeof(g));
    line_edit_reset(le);
    CHECK(le->len == 0 && le->buf[0] == '\0' && le->rejected == 0);

    /* empty line */
    CHECK(line_edit_feed(le, '\n') == LINE_SUBMIT);
    CHECK(le->len == 0 && le->buf[0] == '\0');

    /* backspace at column 0 is a no-op */
    CHECK(line_edit_feed(le, '\b') == LINE_ERASE_EMPTY);
    CHECK(line_edit_feed(le, '\b') == LINE_ERASE_EMPTY);
    CHECK(le->len == 0 && le->buf[0] == '\0');

    /* normal editing */
    CHECK(line_edit_feed(le, 't') == LINE_APPENDED);
    feed_str(le, "icx");
    CHECK(line_edit_feed(le, '\b') == LINE_ERASED);
    feed_str(le, "ks");
    CHECK(strcmp(le->buf, "ticks") == 0 && le->len == 5);

    /* non-printable bytes are ignored */
    CHECK(line_edit_feed(le, '\t') == LINE_IGNORED);
    CHECK(line_edit_feed(le, 0x7F) == LINE_IGNORED);
    CHECK(line_edit_feed(le, (char)0xC3) == LINE_IGNORED);
    CHECK(le->len == 5);

    /* 127-character boundary: exactly LINE_MAX_CHARS accepted */
    line_edit_reset(le);
    for (int i = 0; i < LINE_MAX_CHARS; i++)
        CHECK(line_edit_feed(le, (char)('a' + i % 26)) == LINE_APPENDED);
    CHECK(le->len == LINE_MAX_CHARS && le->rejected == 0);
    CHECK(le->buf[LINE_MAX_CHARS] == '\0');
    CHECK(strlen(le->buf) == LINE_MAX_CHARS);

    /* 128th and later: rejected, nothing stored, terminator kept */
    CHECK(line_edit_feed(le, 'X') == LINE_REJECTED);
    for (int i = 0; i < 500; i++)
        line_edit_feed(le, 'Y');
    CHECK(le->rejected == 501);
    CHECK(le->len == LINE_MAX_CHARS && le->buf[LINE_MAX_CHARS] == '\0');
    CHECK(strchr(le->buf, 'X') == NULL && strchr(le->buf, 'Y') == NULL);
    CHECK(guards_intact(&g));

    /* backspace after overflow frees one slot but the line stays rejected */
    CHECK(line_edit_feed(le, '\b') == LINE_ERASED);
    CHECK(line_edit_feed(le, 'Z') == LINE_APPENDED);
    CHECK(le->rejected == 501);
    CHECK(line_edit_feed(le, '\n') == LINE_SUBMIT);

    /* reset after submit: next line is normal */
    line_edit_reset(le);
    feed_str(le, "version");
    CHECK(strcmp(le->buf, "version") == 0 && le->rejected == 0);

    /* erase everything, then more backspaces stay at 0 */
    for (int i = 0; i < 7; i++)
        CHECK(line_edit_feed(le, '\b') == LINE_ERASED);
    CHECK(line_edit_feed(le, '\b') == LINE_ERASE_EMPTY);
    CHECK(le->len == 0 && le->buf[0] == '\0');
    CHECK(guards_intact(&g));

    printf("HOST_LINE_EDIT %s (%d checks, %d failed)\n", failures ? "FAIL" : "PASS", checks,
           failures);
    return failures ? 1 : 0;
}
