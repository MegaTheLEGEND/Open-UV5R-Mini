/* Host test for src/radio/ook.c (run by tests/ook_host.py): PT2262/EV1527 frames made up with jitter, the pulses
 * stretched and the gaps shrunk (as a slow RSSI reading would), noise before and between, several presses. */
#include <stdio.h>
#include <stdlib.h>
#include "radio/ook.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static int32_t d[4096];
static unsigned n;

static int32_t jit(int32_t v, int pct)
{
	return v + (int32_t)(v * ((rand() % (2 * pct + 1)) - pct) / 100);
}

static void put(int32_t v)
{
	if (n && ((d[n - 1] > 0) == (v > 0)))   /* same sign: merge */
		d[n - 1] += v;
	else
		d[n++] = v;
}

static void frames(uint32_t key, int te, int reps, int stretch)
{
	for (int r = 0; r < reps; r++) {
		put(jit(te, 10) + stretch);
		put(-jit(31 * te, 5) + stretch);
		for (int b = 23; b >= 0; b--) {
			int one = key >> b & 1;
			put(jit(one ? 3 * te : te, 10) + stretch);
			put(-jit(one ? te : 3 * te, 10) + stretch);
		}
	}
	put(te);                             /* the last sync */
	put(-jit(31 * te, 5));
}

static void noise(int k)
{
	for (int i = 0; i < k; i++) {
		put(20 + rand() % 200);
		put(-(20 + rand() % 3000));
	}
}

int main(void)
{
	OokCode_t c;
	srand(7);

	n = 0;
	noise(30);
	frames(0x95D5D4, 400, 6, 0);
	noise(10);
	OOK_Princeton(d, n, &c);
	printf("clean: ok %d key %06X te %u frames %u other %u\n", c.ok, (unsigned)c.key, c.te, c.frames, c.other);
	CHECK(c.ok && c.key == 0x95D5D4 && c.frames >= 5 && c.te > 370 && c.te < 430, "clean");

	n = 0;
	noise(5);
	frames(0x0A1B2C, 320, 5, 90);        /* pulses 90 us long, gaps 90 us short */
	OOK_Princeton(d, n, &c);
	printf("stretched: ok %d key %06X te %u frames %u\n", c.ok, (unsigned)c.key, c.te, c.frames);
	CHECK(c.ok && c.key == 0x0A1B2C && c.frames >= 4, "stretched");

	n = 0;
	frames(0xFFFFFF, 500, 3, 0);         /* all ones */
	noise(3);
	frames(0x000001, 500, 4, 0);         /* another button: more frames, so it wins */
	OOK_Princeton(d, n, &c);
	printf("two keys: ok %d key %06X frames %u other %u\n", c.ok, (unsigned)c.key, c.frames, c.other);
	CHECK(c.ok && c.key == 0x000001 && c.frames >= 3 && c.other >= 2, "two keys");

	n = 0;
	noise(200);
	OOK_Princeton(d, n, &c);
	CHECK(!c.ok, "noise only gave %06X", (unsigned)c.key);

	printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
	return fails ? 1 : 0;
}
