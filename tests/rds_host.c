/* Host test harness for src/radio/rds.c: raw FM samples (int32, little-endian) from a file, 512 at a time through
 * the interrupt half, the main-loop half after each block (as the radio does every 10 ms, more often here), then
 * what was decoded. Run by tests/rds_host.py. */
#include <stdio.h>
#include <stdlib.h>
#include "radio/rds.h"

int main(int argc, char **argv)
{
	static int32_t blk[512];
	FILE *f;
	unsigned long nblk = 0, first_sync = 0;

	if (argc < 3) {
		fprintf(stderr, "usage: rds_host samples.bin fs\n");
		return 2;
	}
	f = fopen(argv[1], "rb");
	if (!f)
		return 2;
	RDS_Reset((uint32_t)strtoul(argv[2], 0, 10));
	while (fread(blk, sizeof blk[0], 512, f) == 512) {
		RDS_FeBlock(blk, 512);
		RDS_Poll();
		nblk++;
		if (!first_sync && RDS_Info()->sync)
			first_sync = nblk;
	}
	fclose(f);
	const RdsInfo_t *r = RDS_Info();
	printf("pilot %d sync %d first_sync_block %lu pi %04X groups %u ok %u bad %u\n", r->pilot, r->sync, first_sync,
	       r->pi, (unsigned)r->groups, (unsigned)r->blocks_ok, (unsigned)r->blocks_bad);
	printf("ps [%s]\n", r->ps);
	printf("rt [%s]\n", r->rt);
	return 0;
}
