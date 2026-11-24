/* Open-UV5R-Mini small C library: stdlib (tools/sdk/README.md). malloc and friends are the SDK's. */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

int errno;

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }

div_t div(int a, int b)
{
    div_t r = { a / b, a % b };
    return r;
}

ldiv_t ldiv(long a, long b)
{
    ldiv_t r = { a / b, a % b };
    return r;
}

/* The common part of the strto* functions: sign, base prefix, digits. Saturates at `lim` (the magnitude limit). */
static unsigned long long parse(const char *s, char **end, int base, int *neg, unsigned long long lim)
{
    const char *p = s, *start;
    unsigned long long v = 0;
    int over = 0;
    while (isspace((unsigned char)*p))
        p++;
    *neg = 0;
    if (*p == '+' || *p == '-')
        *neg = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && isxdigit((unsigned char)p[2])) {
        p += 2;
        base = 16;
    } else if (base == 0) {
        base = *p == '0' ? 8 : 10;
    }
    start = p;
    for (;; p++) {
        int c = (unsigned char)*p, d;
        if (isdigit(c))
            d = c - '0';
        else if (isalpha(c))
            d = tolower(c) - 'a' + 10;
        else
            break;
        if (d >= base)
            break;
        if (v > (lim - (unsigned)d) / (unsigned)base)
            over = 1;
        else
            v = v * (unsigned)base + (unsigned)d;
    }
    if (end)
        *end = (char *)(p == start ? s : p);
    if (over) {
        errno = ERANGE;
        v = lim;
    }
    return v;
}

long strtol(const char *s, char **end, int base)
{
    int neg;
    unsigned long long v = parse(s, end, base, &neg, (unsigned long long)LONG_MAX + 1);
    if (!neg && v > LONG_MAX) {
        errno = ERANGE;
        return LONG_MAX;
    }
    return neg ? (long)(0 - (unsigned long)v) : (long)v;
}

unsigned long strtoul(const char *s, char **end, int base)
{
    int neg;
    unsigned long long v = parse(s, end, base, &neg, ULONG_MAX);
    return neg ? 0 - (unsigned long)v : (unsigned long)v;
}

long long strtoll(const char *s, char **end, int base)
{
    int neg;
    unsigned long long v = parse(s, end, base, &neg, (unsigned long long)LLONG_MAX + 1);
    if (!neg && v > LLONG_MAX) {
        errno = ERANGE;
        return LLONG_MAX;
    }
    return neg ? (long long)(0 - v) : (long long)v;
}

unsigned long long strtoull(const char *s, char **end, int base)
{
    int neg;
    unsigned long long v = parse(s, end, base, &neg, ULLONG_MAX);
    return neg ? 0 - v : v;
}

int atoi(const char *s) { return (int)strtol(s, 0, 10); }
long atol(const char *s) { return strtol(s, 0, 10); }

/* Decimal only: [sign] digits [. digits] [e [sign] digits]. Enough for configuration text. */
double strtod(const char *s, char **end)
{
    const char *p = s;
    double v = 0, scale = 1;
    int neg = 0, any = 0;
    while (isspace((unsigned char)*p))
        p++;
    if (*p == '+' || *p == '-')
        neg = *p++ == '-';
    for (; isdigit((unsigned char)*p); p++, any = 1)
        v = v * 10 + (*p - '0');
    if (*p == '.')
        for (p++; isdigit((unsigned char)*p); p++, any = 1)
            v += (*p - '0') * (scale /= 10);
    if (any && (*p == 'e' || *p == 'E')) {
        const char *q = p + 1;
        int eneg = 0, e = 0;
        if (*q == '+' || *q == '-')
            eneg = *q++ == '-';
        if (isdigit((unsigned char)*q)) {
            for (; isdigit((unsigned char)*q); q++)
                if (e < 400)
                    e = e * 10 + (*q - '0');
            while (e--)
                v = eneg ? v / 10 : v * 10;
            p = q;
        }
    }
    if (end)
        *end = (char *)(any ? p : s);
    return neg ? -v : v;
}

double atof(const char *s) { return strtod(s, 0); }

static unsigned long rand_state = 1;

void srand(unsigned seed) { rand_state = seed; }

int rand(void)
{
    rand_state = rand_state * 1103515245u + 12345u;
    return (int)((rand_state >> 1) & RAND_MAX);
}

static void swap(char *a, char *b, size_t n)
{
    while (n--) {
        char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

/* Shell sort: small, no recursion, no extra memory. */
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    char *b = base;
    for (size_t gap = n / 2; gap; gap /= 2)
        for (size_t i = gap; i < n; i++)
            for (size_t j = i; j >= gap && cmp(b + (j - gap) * size, b + j * size) > 0; j -= gap)
                swap(b + (j - gap) * size, b + j * size, size);
}

void *bsearch(const void *key, const void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    const char *b = base;
    while (n) {
        const char *m = b + (n / 2) * size;
        int c = cmp(key, m);
        if (!c)
            return (void *)m;
        if (c > 0) {
            b = m + size;
            n -= n / 2 + 1;
        } else {
            n /= 2;
        }
    }
    return 0;
}

void abort(void)
{
    for (;;)
        ;
}

void exit(int code)
{
    (void)code;
    abort();
}

void __minilibc_assert(const char *expr, const char *file, int line)
{
    (void)expr;
    (void)file;
    (void)line;
    abort();
}
