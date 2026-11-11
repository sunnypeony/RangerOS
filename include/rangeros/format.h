#ifndef RANGEROS_FORMAT_H
#define RANGEROS_FORMAT_H

#include <stdarg.h>

/*
 * Minimal printf-style formatter, sink-based so it also builds on the
 * host for unit tests (tests/host/test_format.c).
 *
 * Supported: %[-][0][width]{c,s,d,i,u,x,X} plus %p (always 0x%08x) and %%.
 * Width is capped at FORMAT_MAX_WIDTH. No precision, no length
 * modifiers, no floating point. An unknown conversion is printed
 * verbatim ("%q" -> "%q"); a NULL %s prints "(null)".
 */
#define FORMAT_MAX_WIDTH 32

typedef void (*format_sink)(char c, void *ctx);

/* Returns the number of characters sent to sink. */
int format_vprint(format_sink sink, void *ctx, const char *fmt, va_list ap);

#endif
