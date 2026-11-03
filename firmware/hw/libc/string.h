/* Freestanding <string.h> for fm1-base builds (see README.md): felucca-bsp's chip/libc.c has the first three. */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *memset(void *, int, size_t); void *memcpy(void *, const void *, size_t); int memcmp(const void *, const void *, size_t);
void *memmove(void *, const void *, size_t); size_t strlen(const char *); int strcmp(const char *, const char *);
char *strncpy(char *, const char *, size_t);
#ifdef __cplusplus
}
#endif
