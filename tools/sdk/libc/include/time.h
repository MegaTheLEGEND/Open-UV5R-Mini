/* Open-UV5R-Mini small C library (tools/sdk/README.md). Types and UTC conversion only; there is no clock here. */
#ifndef MINILIBC_TIME_H
#define MINILIBC_TIME_H
#include <stddef.h>

typedef long time_t;
typedef long clock_t;
#define CLOCKS_PER_SEC 1000

struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
};

time_t mktime(struct tm *t);
struct tm *gmtime(const time_t *t);
struct tm *gmtime_r(const time_t *t, struct tm *out);
struct tm *localtime(const time_t *t);
struct tm *localtime_r(const time_t *t, struct tm *out);

#endif
