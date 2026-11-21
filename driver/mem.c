/* Open-UV5R-Mini: memcpy and memset, which the compiler calls for struct copies and clears (we build freestanding,
 * with no C library). Plain byte loops; nothing here is speed-critical. */
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	while (n--)
		*d++ = *s++;
	return dst;
}

void *memset(void *dst, int c, size_t n)
{
	unsigned char *d = dst;

	while (n--)
		*d++ = (unsigned char)c;
	return dst;
}
