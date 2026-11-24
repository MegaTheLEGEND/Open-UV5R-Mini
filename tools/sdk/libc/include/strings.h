/* Open-UV5R-Mini small C library (tools/sdk/README.md). */
#ifndef MINILIBC_STRINGS_H
#define MINILIBC_STRINGS_H
#include <stddef.h>

int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);
void bzero(void *d, size_t n);

#endif
