/* src/radio/ook.c on durations from a file (one signed number of us a line or space separated), for checking the
 * decoder on real captures (tools/sdr_ook.py feeds it an SDR's). Prints the key line or "none". */
#include <stdio.h>
#include <stdlib.h>
#include "radio/ook.h"

static int32_t d[200000];

int main(int argc, char **argv)
{
	FILE *f = argc > 1 ? fopen(argv[1], "r") : stdin;
	unsigned n = 0;
	long v;
	OokCode_t c;

	if (!f)
		return 2;
	while (n < sizeof d / sizeof d[0] && fscanf(f, "%ld", &v) == 1)
		d[n++] = (int32_t)v;
	OOK_Princeton(d, n, &c);
	if (c.ok)
		printf("princeton key %06X te %u frames %u other %u (of %u durations)\n", (unsigned)c.key, c.te, c.frames,
		       c.other, n);
	else
		printf("none (%u durations)\n", n);
	return 0;
}
