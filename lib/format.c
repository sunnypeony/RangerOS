#include <stdarg.h>
#include <stdint.h>

#include "rangeros/format.h"

#define FLAG_LEFT 0x1
#define FLAG_ZERO 0x2

struct format_out {
    format_sink sink;
    void *ctx;
    int count;
};

static void emit(struct format_out *out, char c)
{
    out->sink(c, out->ctx);
    out->count++;
}

static void emit_repeat(struct format_out *out, char c, int n)
{
    while (n-- > 0)
        emit(out, c);
}

/* [spaces][sign][zeros]digits[spaces], total at least width. */
static void emit_field(struct format_out *out, char sign, const char *text, int len,
                       int width, int flags)
{
    int body = len + (sign != 0);
    int pad = width > body ? width - body : 0;

    if (!(flags & FLAG_LEFT) && !(flags & FLAG_ZERO))
        emit_repeat(out, ' ', pad);
    if (sign)
        emit(out, sign);
    if (!(flags & FLAG_LEFT) && (flags & FLAG_ZERO))
        emit_repeat(out, '0', pad);
    for (int i = 0; i < len; i++)
        emit(out, text[i]);
    if (flags & FLAG_LEFT)
        emit_repeat(out, ' ', pad);
}

/* Digits of value in base, most significant first; returns length. */
static int format_unsigned(uint32_t value, unsigned base, int upper, char *buf)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[32];
    int n = 0;

    do {
        tmp[n++] = digits[value % base];
        value /= base;
    } while (value != 0);
    for (int i = 0; i < n; i++)
        buf[i] = tmp[n - 1 - i];
    return n;
}

int format_vprint(format_sink sink, void *ctx, const char *fmt, va_list ap)
{
    struct format_out out = { sink, ctx, 0 };
    char buf[32];

    while (*fmt) {
        if (*fmt != '%') {
            emit(&out, *fmt++);
            continue;
        }
        const char *spec = fmt++;

        int flags = 0;
        for (;; fmt++) {
            if (*fmt == '-')
                flags |= FLAG_LEFT;
            else if (*fmt == '0')
                flags |= FLAG_ZERO;
            else
                break;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
            if (width > FORMAT_MAX_WIDTH)
                width = FORMAT_MAX_WIDTH;
        }
        if (flags & FLAG_LEFT)
            flags &= ~FLAG_ZERO;

        char conv = *fmt;
        if (conv == '\0') {
            /* Lone trailing '%' (possibly with flags): print as given. */
            while (spec < fmt)
                emit(&out, *spec++);
            break;
        }
        fmt++;

        switch (conv) {
        case 'c':
            buf[0] = (char)va_arg(ap, int);
            emit_field(&out, 0, buf, 1, width, flags & ~FLAG_ZERO);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            int len = 0;
            if (!s)
                s = "(null)";
            while (s[len])
                len++;
            emit_field(&out, 0, s, len, width, flags & ~FLAG_ZERO);
            break;
        }
        case 'd':
        case 'i': {
            int v = va_arg(ap, int);
            uint32_t mag = (uint32_t)v;
            char sign = 0;
            if (v < 0) {
                sign = '-';
                mag = 0u - mag; /* also correct for INT_MIN */
            }
            emit_field(&out, sign, buf, format_unsigned(mag, 10, 0, buf), width, flags);
            break;
        }
        case 'u':
            emit_field(&out, 0, buf, format_unsigned(va_arg(ap, unsigned), 10, 0, buf),
                       width, flags);
            break;
        case 'x':
        case 'X':
            emit_field(&out, 0, buf,
                       format_unsigned(va_arg(ap, unsigned), 16, conv == 'X', buf), width,
                       flags);
            break;
        case 'p': {
            uint32_t v = (uint32_t)(uintptr_t)va_arg(ap, void *);
            int n = format_unsigned(v, 16, 0, buf);
            emit(&out, '0');
            emit(&out, 'x');
            emit_repeat(&out, '0', 8 - n);
            for (int i = 0; i < n; i++)
                emit(&out, buf[i]);
            break;
        }
        case '%':
            emit(&out, '%');
            break;
        default:
            /* Unknown conversion: print the whole spec verbatim. */
            while (spec < fmt)
                emit(&out, *spec++);
            break;
        }
    }
    return out.count;
}
