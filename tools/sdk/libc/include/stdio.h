/* Open-UV5R-Mini small C library (tools/sdk/README.md). The printf family comes from the SDK's system library;
 * these declarations match the SDK's include_lib/system/generic/printf.h exactly (size as unsigned long). */
#ifndef MINILIBC_STDIO_H
#define MINILIBC_STDIO_H
#include <stddef.h>
#include <stdarg.h>

#define EOF (-1)

typedef struct __minilibc_file FILE;

int putchar(int c);
int puts(const char *s);
int printf(const char *fmt, ...);
int sprintf(char *out, const char *fmt, ...);
int snprintf(char *buf, unsigned long size, const char *fmt, ...);
int vprintf(const char *fmt, __builtin_va_list va);
int vsnprintf(char *buf, unsigned long size, const char *fmt, __builtin_va_list va);
int sscanf(const char *buf, const char *fmt, ...);

#endif
