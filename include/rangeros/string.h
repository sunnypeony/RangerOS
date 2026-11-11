#ifndef RANGEROS_STRING_H
#define RANGEROS_STRING_H

#include <stddef.h>

/* Freestanding builds still need these: the compiler may emit calls to
 * them for struct copies and zero-initialisation. */
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);

#endif
