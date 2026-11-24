/* Open-UV5R-Mini small C library: UTC calendar conversion (tools/sdk/README.md). No time zones. */
#include <time.h>

static int leap(long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static const short mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

/* Days from 1970-01-01 to y-m-d (m 1..12), proleptic Gregorian (H. Hinnant's days_from_civil). */
static long days_from_civil(long y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

time_t mktime(struct tm *t)
{
    long y = t->tm_year + 1900L, m = t->tm_mon;
    y += m / 12;
    m %= 12;
    if (m < 0) {
        m += 12;
        y--;
    }
    long days = days_from_civil(y, (int)m + 1, 1) + t->tm_mday - 1;
    time_t s = days * 86400L + t->tm_hour * 3600L + t->tm_min * 60L + t->tm_sec;
    gmtime_r(&s, t);
    return s;
}

struct tm *gmtime_r(const time_t *tp, struct tm *t)
{
    long s = *tp, days = s / 86400, rem = s % 86400;
    if (rem < 0) {
        rem += 86400;
        days--;
    }
    t->tm_hour = (int)(rem / 3600);
    t->tm_min = (int)(rem / 60 % 60);
    t->tm_sec = (int)(rem % 60);
    t->tm_wday = (int)((days % 7 + 11) % 7);          /* 1970-01-01 was a Thursday */
    long y = 1970;
    for (;;) {
        long n = leap(y) ? 366 : 365;
        if (days < 0) {
            y--;
            days += leap(y) ? 366 : 365;
        } else if (days >= n) {
            days -= n;
            y++;
        } else {
            break;
        }
    }
    t->tm_year = (int)(y - 1900);
    t->tm_yday = (int)days;
    int m = 0;
    for (;; m++) {
        int n = mdays[m] + (m == 1 && leap(y));
        if (days < n)
            break;
        days -= n;
    }
    t->tm_mon = m;
    t->tm_mday = (int)days + 1;
    t->tm_isdst = 0;
    return t;
}

struct tm *gmtime(const time_t *t)
{
    static struct tm r;
    return gmtime_r(t, &r);
}

struct tm *localtime_r(const time_t *t, struct tm *out) { return gmtime_r(t, out); }
struct tm *localtime(const time_t *t) { return gmtime(t); }
