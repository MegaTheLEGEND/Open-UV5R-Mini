/* Open-UV5R-Mini small C library for building JieLi's BR23 SDK with the pi32v2-llvm drop-in (tools/sdk/README.md).
 * memcpy, memmove, memcmp, memset, memmem, strcmp, strcpy, strlen, strncmp, strstr are in the chip's mask ROM
 * (the SDK's cpu/br23/maskrom_stubs.ld gives their addresses); the rest is in libc.a. */
#ifndef MINILIBC_STRING_H
#define MINILIBC_STRING_H
#include <stddef.h>

void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memset(void *d, int c, size_t n);
void *memchr(const void *s, int c, size_t n);
void *memmem(const void *h, size_t hn, const void *n, size_t nn);

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
char *strcat(char *d, const char *s);
char *strncat(char *d, const char *s, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *h, const char *n);
size_t strspn(const char *s, const char *a);
size_t strcspn(const char *s, const char *r);
char *strpbrk(const char *s, const char *a);
char *strtok(char *s, const char *d);
char *strtok_r(char *s, const char *d, char **save);
char *strdup(const char *s);

#endif
