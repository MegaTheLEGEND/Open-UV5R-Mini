/* Open-UV5R-Mini small C library: math (tools/sdk/README.md). Straightforward range reduction and series; good to
 * about 1e-15 relative for normal inputs. Not tuned for speed: the chip has no double-precision hardware. */
#include <math.h>
#include <stdint.h>

typedef union { double d; uint64_t u; } bits;

double fabs(double x)
{
    bits b = { x };
    b.u &= ~(1ull << 63);
    return b.d;
}

double trunc(double x)
{
    bits b = { x };
    int e = (int)((b.u >> 52) & 0x7FF) - 1023;
    if (e >= 52)
        return x;                                   /* already whole, or inf / nan */
    if (e < 0) {
        b.u &= 1ull << 63;                          /* |x| < 1: signed zero */
        return b.d;
    }
    b.u &= ~((1ull << (52 - e)) - 1);
    return b.d;
}

double floor(double x)
{
    double t = trunc(x);
    return (t > x) ? t - 1 : t;
}

double ceil(double x)
{
    double t = trunc(x);
    return (t < x) ? t + 1 : t;
}

double round(double x)
{
    return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5);
}

double fmod(double x, double y)
{
    if (y == 0 || isinf(x) || isnan(x) || isnan(y))
        return NAN;
    if (isinf(y))
        return x;
    double r = fabs(x), a = fabs(y);
    while (r >= a) {                                /* long division by doubling: exact */
        double d = a;
        while (d * 2 <= r && !isinf(d * 2))
            d *= 2;
        r -= d;
    }
    return x < 0 ? -r : r;
}

double modf(double x, double *ip)
{
    double t = trunc(x);
    *ip = t;
    return isinf(x) ? 0 * x : x - t;
}

double frexp(double x, int *e)
{
    bits b = { x };
    int ex = (int)((b.u >> 52) & 0x7FF);
    if (x == 0 || ex == 0x7FF) {
        *e = 0;
        return x;
    }
    if (ex == 0) {                                  /* subnormal: scale up first */
        x *= 18014398509481984.0;                   /* 2^54 */
        b.d = x;
        ex = (int)((b.u >> 52) & 0x7FF) - 54;
    }
    *e = ex - 1022;
    b.u = (b.u & ~(0x7FFull << 52)) | (1022ull << 52);
    return b.d;
}

double ldexp(double x, int e)
{
    while (e > 1000) {
        x *= 0x1p1000;
        e -= 1000;
    }
    while (e < -1000) {
        x *= 0x1p-1000;
        e += 1000;
    }
    bits b = { 0 };
    b.u = (uint64_t)(e + 1023) << 52;
    return x * b.d;
}

double sqrt(double x)
{
    if (x < 0 || isnan(x))
        return NAN;
    if (x == 0 || isinf(x))
        return x;
    int e;
    double m = frexp(x, &e);                        /* x = m * 2^e, m in [0.5, 1) */
    if (e & 1) {
        m *= 2;
        e--;
    }
    double r = 0.41731 + 0.59016 * m;               /* first guess on [0.5, 2) */
    for (int i = 0; i < 5; i++)
        r = 0.5 * (r + m / r);
    return ldexp(r, e / 2);
}

double exp(double x)
{
    if (isnan(x))
        return x;
    if (x > 709.8)
        return HUGE_VAL;
    if (x < -745.2)
        return 0;
    int k = (int)floor(x / M_LN2 + 0.5);            /* x = k ln2 + r, |r| <= ln2/2 */
    double r = x - k * 0.693147180369123816490 - k * 1.90821492927058770002e-10;
    double t = 1, s = 1;
    for (int i = 1; i < 20; i++) {
        t *= r / i;
        s += t;
    }
    return ldexp(s, k);
}

double log(double x)
{
    if (x < 0 || isnan(x))
        return NAN;
    if (x == 0)
        return -HUGE_VAL;
    if (isinf(x))
        return x;
    int e;
    double m = frexp(x, &e);                        /* m in [0.5, 1) */
    if (m < M_SQRT2 / 2) {
        m *= 2;
        e--;
    }
    double z = (m - 1) / (m + 1), z2 = z * z, t = z, s = 0;   /* log m = 2 atanh z, |z| < 0.172 */
    for (int i = 1; i < 40; i += 2) {
        s += t / i;
        t *= z2;
    }
    return 2 * s + e * M_LN2;
}

double log10(double x) { return log(x) / M_LN10; }
double log2(double x) { return log(x) / M_LN2; }

double pow(double x, double y)
{
    if (y == 0)
        return 1;
    if (isnan(x) || isnan(y))
        return NAN;
    double yi = trunc(y);
    if (y == yi && fabs(y) < 1u << 30) {            /* whole power: repeated squaring, exact where it can be */
        unsigned long n = (unsigned long)fabs(y);
        double r = 1, b = x;
        while (n) {
            if (n & 1)
                r *= b;
            b *= b;
            n >>= 1;
        }
        return y < 0 ? 1 / r : r;
    }
    if (x < 0)
        return NAN;
    if (x == 0)
        return y > 0 ? 0 : HUGE_VAL;
    return exp(y * log(x));
}

/* sin and cos of |r| <= pi/4 */
static double sin_k(double r)
{
    double r2 = r * r, t = r, s = r;
    for (int i = 2; i < 24; i += 2) {
        t *= -r2 / (i * (i + 1));
        s += t;
    }
    return s;
}

static double cos_k(double r)
{
    double r2 = r * r, t = 1, s = 1;
    for (int i = 1; i < 24; i += 2) {
        t *= -r2 / (i * (i + 1));
        s += t;
    }
    return s;
}

/* x = n pi/2 + r; returns n mod 4. Two-part pi/2 keeps r accurate for |x| up to about 1e6. */
static int reduce(double x, double *r)
{
    double n = floor(x / M_PI_2 + 0.5);
    *r = x - n * 1.57079632673412561417 - n * 6.07710050650619224932e-11;
    return (int)((long long)n & 3);
}

double sin(double x)
{
    if (isinf(x) || isnan(x))
        return NAN;
    double r;
    switch (reduce(x, &r)) {
    case 0: return sin_k(r);
    case 1: return cos_k(r);
    case 2: return -sin_k(r);
    default: return -cos_k(r);
    }
}

double cos(double x)
{
    if (isinf(x) || isnan(x))
        return NAN;
    double r;
    switch (reduce(x, &r)) {
    case 0: return cos_k(r);
    case 1: return -sin_k(r);
    case 2: return -cos_k(r);
    default: return sin_k(r);
    }
}

double tan(double x) { return sin(x) / cos(x); }

double atan(double x)
{
    if (isnan(x))
        return x;
    int neg = x < 0, inv = 0;
    if (neg)
        x = -x;
    if (x > 1) {
        x = 1 / x;
        inv = 1;
    }
    double off = 0;
    if (x > 0.2679491924311228) {                   /* tan(pi/12): shift by pi/6 */
        x = (x * 1.7320508075688772 - 1) / (x + 1.7320508075688772);
        off = M_PI / 6;
    }
    double x2 = x * x, t = x, s = 0;
    for (int i = 1; i < 40; i += 2) {
        s += t / i;
        t *= -x2;
    }
    s += off;
    if (inv)
        s = M_PI_2 - s;
    return neg ? -s : s;
}

double atan2(double y, double x)
{
    if (isnan(x) || isnan(y))
        return NAN;
    if (x == 0) {
        if (y == 0)
            return 0;
        return y > 0 ? M_PI_2 : -M_PI_2;
    }
    double a = atan(y / x);
    if (x > 0)
        return a;
    return y >= 0 ? a + M_PI : a - M_PI;
}

double asin(double x)
{
    if (x < -1 || x > 1)
        return NAN;
    return atan2(x, sqrt((1 - x) * (1 + x)));
}

double acos(double x)
{
    if (x < -1 || x > 1)
        return NAN;
    return atan2(sqrt((1 - x) * (1 + x)), x);
}

float fabsf(float x) { return (float)fabs(x); }
float floorf(float x) { return (float)floor(x); }
float ceilf(float x) { return (float)ceil(x); }
float roundf(float x) { return (float)round(x); }
float fmodf(float x, float y) { return (float)fmod(x, y); }
float sqrtf(float x) { return (float)sqrt(x); }
float expf(float x) { return (float)exp(x); }
float logf(float x) { return (float)log(x); }
float log10f(float x) { return (float)log10(x); }
float powf(float x, float y) { return (float)pow(x, y); }
float sinf(float x) { return (float)sin(x); }
float cosf(float x) { return (float)cos(x); }
float tanf(float x) { return (float)tan(x); }
float atanf(float x) { return (float)atan(x); }
float atan2f(float y, float x) { return (float)atan2(y, x); }
