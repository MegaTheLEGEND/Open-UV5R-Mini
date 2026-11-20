/* Equivalence test for a rewritten function (tools/lift/local_rewrite.py): runs the lifted original and the
 * candidate on the same random states and compares what the caller can see.
 *
 * Compared: r0 at return (r1 is scratch in most functions), sp at return, every byte written outside the function's own stack frame (below
 * the entry sp), and the calls made (address, r0..r3, sp). Callees are stubs that return values derived from
 * their arguments, so both versions see the same results. Memory reads come from a fixed pseudo-random fill plus
 * what was written. Built together with the two functions as f_orig and f_new. */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lift.h"

LiftState S;
uint32_t lift_pc, lift_icount;
static jmp_buf jb;
static int stopped;

#define MAPN (1u << 16)
typedef struct { uint32_t a; uint8_t used, v; } Cell;
static Cell map[MAPN];
static uint32_t seed;
static uint32_t entry_sp;

static uint32_t hsh(uint32_t x) { x ^= seed; x *= 0x9E3779B1u; x ^= x >> 15; x *= 0x85EBCA77u; x ^= x >> 13; return x; }
static Cell *cell(uint32_t a, int make)
{
	uint32_t i = hsh(a) & (MAPN - 1);
	while (map[i].used && map[i].a != a) i = (i + 1) & (MAPN - 1);
	if (!map[i].used && make) { map[i].used = 1; map[i].a = a; }
	return map[i].used ? &map[i] : NULL;
}
static uint32_t rdb(uint32_t a) { Cell *c = cell(a, 0); return c ? c->v : (hsh(a * 7u + 1u) & 0xFF); }
static void wrb(uint32_t a, uint32_t v) { Cell *c = cell(a, 1); c->v = (uint8_t)v; }
uint32_t rd8(uint32_t a) { return rdb(a); }
uint32_t rd16(uint32_t a) { return rdb(a) | rdb(a + 1) << 8; }
uint32_t rd32(uint32_t a) { return rd16(a) | rd16(a + 2) << 16; }
void wr8(uint32_t a, uint32_t v) { wrb(a, v); }
void wr16(uint32_t a, uint32_t v) { wrb(a, v); wrb(a + 1, v >> 8); }
void wr32(uint32_t a, uint32_t v) { wr16(a, v); wr16(a + 2, v >> 16); }

typedef struct { uint32_t addr, r[4], sp; } Call;
static Call calls[256];
static int ncalls;
void lift_call(uint32_t addr)
{
	Call *c;
	if (ncalls >= 256) lift_unimpl(addr, "too many calls");
	c = &calls[ncalls++];
	c->addr = addr; memcpy(c->r, S.r, 16); c->sp = S.sp;
	S.r[0] = hsh(addr ^ S.r[0] ^ (S.r[1] << 1));      /* the callee's results: a function of its arguments */
	S.r[1] = hsh(addr + S.r[1]);
	S.r[2] = hsh(addr + 2); S.r[3] = hsh(addr + 3);   /* scratch registers come back clobbered */
}
void lift_unimpl(uint32_t pc, const char *why) { (void)pc; (void)why; stopped = 1; longjmp(jb, 1); }

#include "lift_helpers.inc"

void f_orig(void);
void f_new(void);

typedef struct { uint32_t r0, r1, sp; int ncalls; Call calls[256]; int nw; uint32_t wa[4096]; uint8_t wv[4096]; } Out;
static Out o1, o2;

static void snapshot(Out *o)
{
	o->r0 = S.r[0]; o->r1 = S.r[1]; o->sp = S.sp; o->ncalls = ncalls; memcpy(o->calls, calls, sizeof(calls));
	o->nw = 0;
	for (uint32_t i = 0; i < MAPN && o->nw < 4096; i++)
		if (map[i].used && !(map[i].a < entry_sp && map[i].a >= entry_sp - 0x4000)) {
			o->wa[o->nw] = map[i].a; o->wv[o->nw] = map[i].v; o->nw++;
		}
}

static int run(void (*f)(void), uint32_t s, Out *o)
{
	uint32_t i;
	memset(map, 0, sizeof(map)); ncalls = 0; stopped = 0; seed = s;
	for (i = 0; i < 16; i++) S.r[i] = hsh(i * 77u + 5u);
	switch (s % 4) {      /* small values too: lengths, indexes, flags */
	case 0: S.r[0] &= 0xFF; S.r[1] &= 0xFF; S.r[2] &= 0x1F; break;
	case 1: S.r[1] &= 0xFFFF; S.r[2] &= 0xFF; break;
	case 2: S.r[0] = 0x8000 + (S.r[0] & 0x3FFC); S.r[1] = 0x9000 + (S.r[1] & 0x3FFC); S.r[2] &= 0x3F; break;
	default: break;
	}
	S.sp = entry_sp = 0x20000; S.rets = 0x01E00000u; S.flags = 0; S.icfg = 0;
	if (setjmp(jb)) return -1;
	f();
	snapshot(o);
	return 0;
}

static int cmp_w(const void *a, const void *b) { (void)a; (void)b; return 0; }

int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 300, bad = 0, ran = 0;
	(void)cmp_w;
	for (int k = 1; k <= n && bad < 3; k++) {
		if (run(f_orig, (uint32_t)k, &o1)) continue;           /* the original stops on this input: skip it */
		ran++;
		if (run(f_new, (uint32_t)k, &o2)) { printf("FAIL input %d: the rewrite stopped (unimplemented path)\n", k); bad++; continue; }
		if (o1.r0 != o2.r0) { printf("FAIL input %d: r0/r1 %08x %08x, original %08x %08x\n", k, o2.r0, o2.r1, o1.r0, o1.r1); bad++; continue; }
		if (o1.sp != o2.sp) { printf("FAIL input %d: sp %08x, original %08x\n", k, o2.sp, o1.sp); bad++; continue; }
		if (o1.ncalls != o2.ncalls) { printf("FAIL input %d: %d calls, original %d\n", k, o2.ncalls, o1.ncalls); bad++; continue; }
		for (int c = 0; c < o1.ncalls; c++) {
			Call *a = &o1.calls[c], *b = &o2.calls[c];
			int nargs = 4;
			if (a->addr != b->addr || a->sp != b->sp || memcmp(a->r, b->r, 4 * nargs)) {
				printf("FAIL input %d: call %d to %08x(%08x,%08x,%08x,%08x) sp %08x, original %08x(%08x,%08x,%08x,%08x) sp %08x\n",
				       k, c, b->addr, b->r[0], b->r[1], b->r[2], b->r[3], b->sp, a->addr, a->r[0], a->r[1], a->r[2], a->r[3], a->sp);
				bad++; break;
			}
		}
		if (bad) continue;
		/* memory: same set of bytes written outside the frame, same values (map order is the same: same seed) */
		{
			int i = 0, j = 0, diff = 0;
			/* both lists come from the same hash table walk; compare as sets */
			for (i = 0; i < o1.nw && !diff; i++) {
				for (j = 0; j < o2.nw && o2.wa[j] != o1.wa[i]; j++) ;
				if (j == o2.nw || o2.wv[j] != o1.wv[i]) {
					printf("FAIL input %d: byte %08x = %s%02x, original %02x\n", k, o1.wa[i], j == o2.nw ? "(not written) " : "",
					       j == o2.nw ? 0 : o2.wv[j], o1.wv[i]);
					diff = 1;
				}
			}
			if (!diff && o1.nw != o2.nw) { printf("FAIL input %d: wrote %d bytes, original %d\n", k, o2.nw, o1.nw); diff = 1; }
			bad += diff;
		}
	}
	if (!bad) printf("PASS %d inputs\n", ran);
	return bad ? 1 : 0;
}
