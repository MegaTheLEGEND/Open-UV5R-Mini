/* PC test of tools/sdk/libc (the small C library for building JieLi's BR23 SDK). Run by tests/sdk_libc_host.py,
 * which builds the library's sources with the host gcc and renames their symbols with a t_ prefix, so they can be
 * checked here against the host's own C library. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

/* the library under test (t_ prefix) */
size_t t_strnlen(const char *, size_t);
char *t_strncpy(char *, const char *, size_t);
char *t_strcat(char *, const char *);
char *t_strncat(char *, const char *, size_t);
char *t_strchr(const char *, int);
char *t_strrchr(const char *, int);
size_t t_strspn(const char *, const char *);
size_t t_strcspn(const char *, const char *);
char *t_strpbrk(const char *, const char *);
char *t_strtok(char *, const char *);
char *t_strdup(const char *);
int t_strcasecmp(const char *, const char *);
int t_strncasecmp(const char *, const char *, size_t);
void *t_memchr(const void *, int, size_t);
long t_strtol(const char *, char **, int);
unsigned long t_strtoul(const char *, char **, int);
long long t_strtoll(const char *, char **, int);
double t_strtod(const char *, char **);
int t_atoi(const char *);
void t_qsort(void *, size_t, size_t, int (*)(const void *, const void *));
void *t_bsearch(const void *, const void *, size_t, size_t, int (*)(const void *, const void *));
extern int t_errno;
double t_fabs(double), t_floor(double), t_ceil(double), t_round(double), t_trunc(double), t_sqrt(double);
double t_exp(double), t_log(double), t_log10(double), t_pow(double, double), t_fmod(double, double);
double t_sin(double), t_cos(double), t_tan(double), t_atan(double), t_atan2(double, double);
double t_asin(double), t_acos(double), t_frexp(double, int *), t_ldexp(double, int);
float t_fabsf(float), t_ceilf(float), t_sqrtf(float);

struct t_tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };
long t_mktime(struct t_tm *);
struct t_tm *t_gmtime_r(const long *, struct t_tm *);

/* what the library takes from outside (the mask ROM and the SDK on the radio) */
void *t_malloc(size_t n) { return malloc(n); }
void *t_memcpy(void *d, const void *s, size_t n) { return memcpy(d, s, n); }
void *t_memset(void *d, int c, size_t n) { return memset(d, c, n); }
size_t t_strlen(const char *s) { return strlen(s); }

static int fails, checks;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        fails++;
        printf("FAIL %s\n", what);
    }
}

static void near(double got, double want, double tol, const char *what, double x)
{
    char buf[128];
    int ok;
    if (isnan(want))
        ok = isnan(got);
    else if (isinf(want))
        ok = got == want;
    else
        ok = fabs(got - want) <= tol * (fabs(want) > 1 ? fabs(want) : 1);
    snprintf(buf, sizeof buf, "%s(%.17g) = %.17g, want %.17g", what, x, got, want);
    check(ok, buf);
}

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

static void test_string(void)
{
    char b[32];
    check(t_strnlen("hello", 3) == 3 && t_strnlen("hi", 9) == 2, "strnlen");
    memset(b, 'x', sizeof b);
    t_strncpy(b, "ab", 5);
    check(!memcmp(b, "ab\0\0\0x", 6), "strncpy pads with zeros");
    strcpy(b, "ab");
    check(!strcmp(t_strcat(b, "cd"), "abcd"), "strcat");
    check(!strcmp(t_strncat(b, "efgh", 2), "abcdef"), "strncat");
    const char *s = "a/b/c", *p = "abc";
    check(t_strchr(s, '/') == s + 1 && t_strrchr(s, '/') == s + 3 && t_strchr(s, 0) == s + 5, "strchr/strrchr");
    check(t_strchr(s, 'z') == 0 && t_strrchr(s, 'z') == 0, "strchr miss");
    check(t_strspn("aab", "a") == 2 && t_strcspn("aab", "b") == 2 && t_strpbrk(p, "xc") == p + 2 &&
          t_strpbrk(p, "xy") == 0, "strspn/strcspn/strpbrk");
    strcpy(b, " a,b,,c ");
    char *t1 = t_strtok(b, " ,"), *t2 = t_strtok(0, " ,"), *t3 = t_strtok(0, " ,"), *t4 = t_strtok(0, " ,");
    check(t1 && t2 && t3 && !strcmp(t1, "a") && !strcmp(t2, "b") && !strcmp(t3, "c") && !t4, "strtok");
    char *d = t_strdup("dup");
    check(d && !strcmp(d, "dup"), "strdup");
    free(d);
    check(t_strcasecmp("HeLLo", "hello") == 0 && t_strcasecmp("a", "B") < 0 && t_strncasecmp("abX", "ABy", 2) == 0,
          "strcasecmp");
    check(t_memchr("abc", 'c', 3) != 0 && t_memchr("abc", 'c', 2) == 0, "memchr");
}

static void test_stdlib(void)
{
    static const struct { const char *s; int base; } cases[] = {
        { "0", 10 }, { "-17", 10 }, { "  +42xyz", 10 }, { "0x1F", 0 }, { "0x1F", 16 }, { "017", 0 }, { "z", 36 },
        { "2147483647", 10 }, { "2147483648", 10 }, { "-2147483648", 10 }, { "-2147483649", 10 }, { "", 10 },
        { "0x", 16 }, { "  -0x10", 0 }, { "777777777777777777", 8 },
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char *e1, *e2, buf[96];
        errno = 0;
        t_errno = 0;
        long a = strtol(cases[i].s, &e1, cases[i].base);
        int ea = errno;
        long b = t_strtol(cases[i].s, &e2, cases[i].base);
        snprintf(buf, sizeof buf, "strtol(\"%s\", %d) = %ld end %d errno %d, want %ld end %d errno %d", cases[i].s,
                 cases[i].base, b, (int)(e2 - cases[i].s), t_errno, a, (int)(e1 - cases[i].s), ea);
        check(a == b && e1 - cases[i].s == e2 - cases[i].s && (ea == ERANGE) == (t_errno == ERANGE), buf);
        unsigned long ua = strtoul(cases[i].s, &e1, cases[i].base), ub = t_strtoul(cases[i].s, &e2, cases[i].base);
        snprintf(buf, sizeof buf, "strtoul(\"%s\", %d)", cases[i].s, cases[i].base);
        check(ua == ub && e1 == e2, buf);
        check(strtoll(cases[i].s, 0, cases[i].base) == t_strtoll(cases[i].s, 0, cases[i].base), "strtoll");
    }
    check(t_atoi("  -123abc") == -123, "atoi");
    static const char *ds[] = { "0", "1.5", "-2.25e3", "3.14159", "1e-5", ".5", "7.", "abc", "12e", "6.02e23" };
    for (unsigned i = 0; i < sizeof ds / sizeof ds[0]; i++) {
        char *e1, *e2;
        double a = strtod(ds[i], &e1), b = t_strtod(ds[i], &e2);
        near(b, a, 1e-14, ds[i], 0);
        check(e1 == e2, "strtod end");
    }
    int v[200];
    for (int i = 0; i < 200; i++)
        v[i] = (i * 7919) % 1000 - 500;
    t_qsort(v, 200, sizeof v[0], cmp_int);
    int sorted = 1;
    for (int i = 1; i < 200; i++)
        sorted &= v[i - 1] <= v[i];
    check(sorted, "qsort");
    int key = v[123];
    int *f = t_bsearch(&key, v, 200, sizeof v[0], cmp_int);
    key = 10000;
    check(f && *f == v[123] && !t_bsearch(&key, v, 200, sizeof v[0], cmp_int), "bsearch");
}

static void test_math(void)
{
    static const double xs[] = { 0, 0.5, -0.5, 1, -1, 1.5, -1.5, 2.5, -2.5, 3, 10, -10, 0.1, 123.456, -123.456,
                                 1e-10, 1e10, 4503599627370495.5, 1e300, -1e300, 0.7853981633974483, 3.141592653589793,
                                 6.283185307179586, 100, 1000.5 };
    for (unsigned i = 0; i < sizeof xs / sizeof xs[0]; i++) {
        double x = xs[i];
        near(t_fabs(x), fabs(x), 0, "fabs", x);
        near(t_floor(x), floor(x), 0, "floor", x);
        near(t_ceil(x), ceil(x), 0, "ceil", x);
        near(t_trunc(x), trunc(x), 0, "trunc", x);
        near(t_round(x), round(x), 0, "round", x);
        near(t_sqrt(fabs(x)), sqrt(fabs(x)), 1e-15, "sqrt", fabs(x));
        if (fabs(x) < 700)
            near(t_exp(x), exp(x), 1e-14, "exp", x);
        if (x > 0)
            near(t_log(x), log(x), 1e-14, "log", x), near(t_log10(x), log10(x), 1e-14, "log10", x);
        if (fabs(x) < 1e6) {
            near(t_sin(x), sin(x), 1e-14, "sin", x);
            near(t_cos(x), cos(x), 1e-14, "cos", x);
        }
        near(t_atan(x), atan(x), 1e-14, "atan", x);
        near(t_atan2(x, -1.25), atan2(x, -1.25), 1e-14, "atan2(x,-1.25)", x);
        if (fabs(x) <= 1)
            near(t_asin(x), asin(x), 1e-13, "asin", x), near(t_acos(x), acos(x), 1e-13, "acos", x);
        near(t_fmod(x, 0.75), fmod(x, 0.75), 1e-15, "fmod(x,0.75)", x);
        int e1, e2;
        double m1 = t_frexp(x, &e1), m2 = frexp(x, &e2);
        check(m1 == m2 && (x == 0 || e1 == e2), "frexp");
        near(t_ldexp(x, 7), ldexp(x, 7), 0, "ldexp", x);
    }
    near(t_tan(0.3), tan(0.3), 1e-14, "tan", 0.3);
    near(t_pow(2, 10), 1024, 0, "pow(2,10)", 0);
    near(t_pow(10, -3), 1e-3, 1e-15, "pow(10,-3)", 0);
    near(t_pow(-2, 3), -8, 0, "pow(-2,3)", 0);
    near(t_pow(2.5, 1.7), pow(2.5, 1.7), 1e-13, "pow(2.5,1.7)", 0);
    near(t_pow(-2, 0.5), NAN, 0, "pow(-2,0.5)", 0);
    near(t_sqrt(-1), NAN, 0, "sqrt(-1)", 0);
    near(t_log(0), -INFINITY, 0, "log(0)", 0);
    near(t_exp(1000), INFINITY, 0, "exp(1000)", 0);
    near(t_fabsf(-2.5f), 2.5, 0, "fabsf", 0);
    near(t_ceilf(1.2f), 2, 0, "ceilf", 0);
    near(t_sqrtf(2.0f), sqrtf(2.0f), 1e-7, "sqrtf", 2);
}

static void test_time(void)
{
    static const long ts[] = { 0, 86399, 86400, 951782400 /* 2000-02-29 */, 1709251199, 2147483647, -1, -86401 };
    for (unsigned i = 0; i < sizeof ts / sizeof ts[0]; i++) {
        struct t_tm t;
        time_t h = ts[i];
        struct tm *r = gmtime(&h);
        char buf[96];
        t_gmtime_r(&ts[i], &t);
        snprintf(buf, sizeof buf, "gmtime(%ld) = %d-%d-%d %d:%d:%d wd %d yd %d", ts[i], t.tm_year + 1900, t.tm_mon + 1,
                 t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, t.tm_wday, t.tm_yday);
        check(r && t.tm_year == r->tm_year && t.tm_mon == r->tm_mon && t.tm_mday == r->tm_mday &&
              t.tm_hour == r->tm_hour && t.tm_min == r->tm_min && t.tm_sec == r->tm_sec &&
              t.tm_wday == r->tm_wday && t.tm_yday == r->tm_yday, buf);
        snprintf(buf, sizeof buf, "mktime(gmtime(%ld))", ts[i]);
        check(t_mktime(&t) == ts[i], buf);
    }
    struct t_tm n = { 0, 0, 0, 31, 12, 99, 0, 0, 0 };   /* 1999, month 13, day 31 = 2000-01-31 */
    check(t_mktime(&n) == 949276800 && n.tm_mon == 0 && n.tm_year == 100, "mktime normalises the month");
}

int main(void)
{
    test_string();
    test_stdlib();
    test_math();
    test_time();
    printf("%d of %d checks passed\n", checks - fails, checks);
    return fails != 0;
}
