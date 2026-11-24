/* Open-UV5R-Mini small C library (tools/sdk/README.md). */
#ifndef MINILIBC_ASSERT_H
#define MINILIBC_ASSERT_H

#ifdef NDEBUG
#define assert(x) ((void)0)
#else
void __minilibc_assert(const char *expr, const char *file, int line);
#define assert(x) ((x) ? (void)0 : __minilibc_assert(#x, __FILE__, __LINE__))
#endif
#define static_assert _Static_assert

#endif
