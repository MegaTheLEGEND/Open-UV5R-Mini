/* Open-UV5R-Mini small C library: string functions the mask ROM does not have (tools/sdk/README.md). */
#include <string.h>
#include <strings.h>
#include <stdlib.h>

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    for (; n; n--, p++)
        if (*p == (unsigned char)c)
            return (void *)p;
    return 0;
}

size_t strnlen(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && s[i])
        i++;
    return i;
}

char *strncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; i++)
        d[i] = s[i];
    for (; i < n; i++)
        d[i] = 0;
    return d;
}

char *strcat(char *d, const char *s)
{
    char *p = d;
    while (*p)
        p++;
    while ((*p++ = *s++) != 0)
        ;
    return d;
}

char *strncat(char *d, const char *s, size_t n)
{
    char *p = d;
    while (*p)
        p++;
    while (n-- && *s)
        *p++ = *s++;
    *p = 0;
    return d;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return 0;
    }
}

char *strrchr(const char *s, int c)
{
    const char *r = 0;
    for (;; s++) {
        if (*s == (char)c)
            r = s;
        if (!*s)
            return (char *)r;
    }
}

size_t strspn(const char *s, const char *a)
{
    size_t n = 0;
    while (s[n] && strchr(a, s[n]))
        n++;
    return n;
}

size_t strcspn(const char *s, const char *r)
{
    size_t n = 0;
    while (s[n] && !strchr(r, s[n]))
        n++;
    return n;
}

char *strpbrk(const char *s, const char *a)
{
    s += strcspn(s, a);
    return *s ? (char *)s : 0;
}

char *strtok_r(char *s, const char *d, char **save)
{
    if (!s)
        s = *save;
    s += strspn(s, d);
    if (!*s) {
        *save = s;
        return 0;
    }
    char *e = s + strcspn(s, d);
    if (*e)
        *e++ = 0;
    *save = e;
    return s;
}

char *strtok(char *s, const char *d)
{
    static char *save;
    return strtok_r(s, d, &save);
}

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

static int lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int d = lower((unsigned char)*a) - lower((unsigned char)*b);
        if (d || !*a)
            return d;
    }
    return 0;
}

int strcasecmp(const char *a, const char *b)
{
    return strncasecmp(a, b, (size_t)-1);
}

void bzero(void *d, size_t n)
{
    memset(d, 0, n);
}
