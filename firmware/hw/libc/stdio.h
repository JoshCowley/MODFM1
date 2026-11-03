/* Freestanding <stdio.h> for fm1-base builds (see README.md): declarations only, nothing here is linked */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int printf(const char *, ...); int sprintf(char *, const char *, ...); int snprintf(char *, size_t, const char *, ...);
#ifdef __cplusplus
}
#endif
