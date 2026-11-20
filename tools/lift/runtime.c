/* Host runtime for the lifted stock app (tools/lift/lift.py): memory and ROM go back to the caller (Python, the
 * emulator's memory map) through callbacks, so the lifted code runs on exactly the state the emulator runs on. */
#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include "lift.h"

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

LiftState S;
EXPORT uint32_t lift_pc;
EXPORT uint32_t lift_icount;

typedef uint32_t (*rd_cb)(uint32_t addr, uint32_t size);
typedef void (*wr_cb)(uint32_t addr, uint32_t size, uint32_t value);
typedef int (*rom_cb)(uint32_t addr);          /* 0 = handled (state in S), else unknown */

static rd_cb g_rd;
static wr_cb g_wr;
static rom_cb g_rom;
static jmp_buf g_jmp;
static uint32_t g_fault_pc;
static const char *g_fault_why;
static uint64_t g_calls;

EXPORT LiftState *lift_state(void) { return &S; }
EXPORT void lift_set_callbacks(rd_cb rd, wr_cb wr, rom_cb rom) { g_rd = rd; g_wr = wr; g_rom = rom; }
EXPORT uint32_t lift_fault_pc(void) { return g_fault_pc; }
EXPORT const char *lift_fault_why(void) { return g_fault_why; }

/* The host sets lift_stop from a callback to end the run (a longjmp out of a callback would skip its frames). */
EXPORT volatile int lift_stop;
static void check_stop(uint32_t a) { if (lift_stop) lift_unimpl(a, "stopped by the host"); }

uint32_t rd8(uint32_t a) { uint32_t v = g_rd(a, 1) & 0xFFu; check_stop(a); return v; }
uint32_t rd16(uint32_t a) { uint32_t v = g_rd(a, 2) & 0xFFFFu; check_stop(a); return v; }
uint32_t rd32(uint32_t a) { uint32_t v = g_rd(a, 4); check_stop(a); return v; }
void wr8(uint32_t a, uint32_t v) { g_wr(a, 1, v & 0xFFu); check_stop(a); }
void wr16(uint32_t a, uint32_t v) { g_wr(a, 2, v & 0xFFFFu); check_stop(a); }
void wr32(uint32_t a, uint32_t v) { g_wr(a, 4, v); check_stop(a); }

void lift_unimpl(uint32_t pc, const char *why)
{
	g_fault_pc = pc;
	g_fault_why = why;
	longjmp(g_jmp, 1);
}

void lift_dispatch(uint32_t addr, int *found);

void lift_call(uint32_t addr)
{
	int found = 0;
	if (++g_calls > 2000000u)
		lift_unimpl(addr, "too many calls");
	if (addr >= 0x02000000u && addr < 0x03000000u) {
		int rc;
		lift_pc = addr;
		lift_icount++;               /* (the emulator counts a ROM routine as one step) */
		rc = g_rom(addr);
		check_stop(addr);
		if (rc == 0)
			return;
		lift_unimpl(addr, "unknown ROM routine");
	}
	lift_dispatch(addr, &found);
	if (!found)
		lift_unimpl(addr, "call to an address that is not a lifted function");
}

/* Run the lifted function at addr on the state in S. 0 = returned, 1 = stopped (lift_fault_pc/why). */
EXPORT int lift_run(uint32_t addr)
{
	g_calls = 0;
	lift_stop = 0;
	lift_icount = 0;
	if (setjmp(g_jmp))
		return 1;
	lift_call(addr);
	return 0;
}

uint32_t lift_shl3(uint32_t a, uint32_t n)
{
	/* `<<<`: signed shift left that saturates (the ISA model's shl3) */
	int64_t v = (int32_t)a;
	n &= 0xFF;
	if (!v) return 0;
	if (n > 32) n = 32;
	v *= (int64_t)1 << n;
	if (v > INT32_MAX) return 0x7FFFFFFFu;
	if (v < INT32_MIN) return 0x80000000u;
	return (uint32_t)v;
}
uint32_t lift_sar64sat(uint64_t a, uint32_t n)
{
	int64_t v;
	n &= 0xFF;
	v = (int64_t)a >> (n > 63 ? 63 : n);
	if (v > INT32_MAX) return 0x7FFFFFFFu;
	if (v < INT32_MIN) return 0x80000000u;
	return (uint32_t)v;
}
/* Division as the ISA model: a zero divisor gives 0, quotients truncate toward zero. */
uint32_t lift_divu(uint32_t a, uint32_t b) { return b ? a / b : 0; }
uint32_t lift_divs(uint32_t a, uint32_t b)
{
	if (!b) return 0;
	if (a == 0x80000000u && b == 0xFFFFFFFFu) return a;
	return (uint32_t)((int32_t)a / (int32_t)b);
}
uint64_t lift_div64s(uint64_t a, uint32_t b)
{
	if (!b) return 0;
	if (a == 0x8000000000000000ull && b == 0xFFFFFFFFu) return a;
	return (uint64_t)((int64_t)a / (int64_t)(int32_t)b);
}
uint64_t lift_div64u(uint64_t a, uint32_t b) { return b ? a / b : 0; }
uint32_t lift_clz(uint32_t a) { return a ? (uint32_t)__builtin_clz(a) : 32u; }
uint32_t lift_abs(uint32_t a) { return a == 0x80000000u ? 0x7FFFFFFFu : (int32_t)a < 0 ? (uint32_t)-(int32_t)a : a; }
uint32_t lift_sat16(uint32_t a)
{
	int32_t v = (int32_t)a;
	return (uint32_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}
/* Floats: IEEE binary32, nearest-even (the host's SSE arithmetic), except that any NaN, in or out, is 0x7F800001
 * as the ISA model measured it. */
#define FNAN 0x7F800001u
static int is_nan(uint32_t a) { return (a & 0x7F800000u) == 0x7F800000u && (a & 0x7FFFFFu); }
static float f_of(uint32_t a) { float f; memcpy(&f, &a, 4); return f; }
static uint32_t of_f(float f) { uint32_t a; memcpy(&a, &f, 4); return is_nan(a) ? FNAN : a; }
uint32_t lift_ftoi(uint32_t a)
{
	if ((a & 0x7F800000u) == 0x7F800000u)
		return (a >> 31) ? 0x80000000u : 0x7FFFFFFFu;
	float f = f_of(a);
	if (f >= 2147483648.0f) return 0x7FFFFFFFu;
	if (f < -2147483648.0f) return 0x80000000u;
	return (uint32_t)(int32_t)f;
}
uint32_t lift_itof(uint32_t a) { return of_f((float)(int32_t)a); }
uint32_t lift_fadd(uint32_t a, uint32_t b) { return is_nan(a) || is_nan(b) ? FNAN : of_f(f_of(a) + f_of(b)); }
uint32_t lift_fsub(uint32_t a, uint32_t b) { return is_nan(a) || is_nan(b) ? FNAN : of_f(f_of(a) - f_of(b)); }
uint32_t lift_fmul(uint32_t a, uint32_t b) { return is_nan(a) || is_nan(b) ? FNAN : of_f(f_of(a) * f_of(b)); }
uint32_t lift_fdiv(uint32_t a, uint32_t b) { return is_nan(a) || is_nan(b) ? FNAN : of_f(f_of(a) / f_of(b)); }

uint32_t lift_ror(uint32_t a, uint32_t n) { n &= 31; return n ? (a >> n) | (a << (32 - n)) : a; }
static int32_t cnt6(uint32_t t) { int32_t c = (int32_t)(t & 63); return c >= 32 ? c - 64 : c; }
uint32_t lift_xchg(uint32_t a, uint32_t t)
{
	int32_t c = cnt6(t);
	return c > 0 ? a << c : c < 0 ? a >> -c : a;
}
uint32_t lift_xchg_sat(uint32_t a, uint32_t t)
{
	int32_t c = cnt6(t);
	int64_t v = (int32_t)a;
	v = c >= 0 ? v * ((int64_t)1 << c) : v >> -c;
	return (uint32_t)(v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : v);
}
uint32_t lift_utof(uint32_t a) { return of_f((float)a); }
uint32_t lift_ftou(uint32_t a)
{
	if ((a & 0x7F800000u) == 0x7F800000u)
		return (a >> 31) ? 0 : 0xFFFFFFFFu;
	float f = f_of(a);
	if (f >= 4294967296.0f) return 0xFFFFFFFFu;
	if (f <= -1.0f) return 0;
	return f < 0 ? 0 : (uint32_t)f;
}
uint64_t lift_xchg64_sat(uint64_t a, uint32_t t)
{
	/* pair shift by a signed 7-bit count, saturating to 64 bits signed (semantics/q.py shift 'pr') */
	int32_t c = (int32_t)(t & 127);
	__int128 v = (int64_t)a;
	if (c >= 64) c -= 128;
	v = c >= 0 ? v * ((__int128)1 << c) : v >> -c;
	if (v > INT64_MAX) return (uint64_t)INT64_MAX;
	if (v < INT64_MIN) return (uint64_t)INT64_MIN;
	return (uint64_t)(int64_t)v;
}
uint32_t lift_ftoi_floor(uint32_t a)
{
	if ((a & 0x7F800000u) == 0x7F800000u)
		return (a >> 31) ? 0x80000000u : 0x7FFFFFFFu;
	float f = floorf(f_of(a));
	if (f >= 2147483648.0f) return 0x7FFFFFFFu;
	if (f < -2147483648.0f) return 0x80000000u;
	return (uint32_t)(int32_t)f;
}
/* fmin/fmax as the ISA model: one NaN loses to the number, two NaNs give the second; zeros by sign */
static uint32_t fminmax(uint32_t a, uint32_t b, int mx)
{
	if (is_nan(a)) return b;
	if (is_nan(b)) return a;
	if (!(a & 0x7FFFFFFFu) && !(b & 0x7FFFFFFFu)) return mx ? (a & b) : (a | b);
	float x = f_of(a), y = f_of(b);
	return mx ? (x > y ? a : b) : (x < y ? a : b);
}
uint32_t lift_fmin(uint32_t a, uint32_t b) { return fminmax(a, b, 0); }
uint32_t lift_fmax(uint32_t a, uint32_t b) { return fminmax(a, b, 1); }
uint32_t lift_uextra_r(uint32_t a, uint32_t t)
{
	uint32_t p = t >> 8 & 31, l = t & 31;
	uint32_t v = a >> p;
	return l ? v & ((1u << l) - 1) : 0;
}
uint32_t lift_sextra_r(uint32_t a, uint32_t t)
{
	uint32_t p = t >> 8 & 31, l = t & 31;
	if (!l) return 0;
	return (uint32_t)((int32_t)((a >> p) << (32 - l)) >> (32 - l));
}
uint32_t lift_ssat64(int64_t v) { return (uint32_t)(v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : v); }
uint32_t lift_usat64(int64_t v) { return (uint32_t)(v < 0 ? 0 : v > (int64_t)0xFFFFFFFF ? 0xFFFFFFFFu : v); }
uint32_t lift_shiftr(uint64_t x, uint32_t n, int mode, int arith)
{
	/* pair shift right with rounding and 32-bit saturation (semantics/p.py _shiftr) */
	__int128 xv, q;
	n &= 0xFF;
	if (n > 64) n = 64;
	xv = arith ? (__int128)(int64_t)x : (__int128)x;
	q = xv >> n;
	if (n) {
		__int128 rem2 = 2 * (xv - q * ((__int128)1 << n)), half = (__int128)1 << n;
		if (rem2 > half) q += 1;
		else if (rem2 == half) q += mode == 0 ? 1 : mode == 1 ? (int)(q & 1) : mode == 2 ? (xv >= 0) : (xv < 0);
	}
	if (arith) return (uint32_t)(q > INT32_MAX ? INT32_MAX : q < INT32_MIN ? INT32_MIN : (int64_t)q);
	return (uint32_t)(q > 0xFFFFFFFF ? 0xFFFFFFFFu : (uint64_t)q);
}
/* float compares: IEEE, the u forms also true when unordered (not modelled by pi32v2-isa) */
#define FC(name, expr) int lift_fcmp_##name(uint32_t a, uint32_t b) { float x = f_of(a), y = f_of(b); \
	int un = is_nan(a) || is_nan(b); (void)x; (void)y; return expr; }
FC(gt, !un && x > y)
FC(lt, !un && x < y)
FC(ge, !un && x >= y)
FC(le, !un && x <= y)
FC(eq, !un && x == y)
FC(ugt, un || x > y)
FC(ult, un || x < y)
FC(uge, un || x >= y)
FC(ule, un || x <= y)

/* System hooks. On the host they stop the run (tests); a firmware build implements them for the chip. */
uint64_t lift_copex(uint64_t x, int n) { (void)x; (void)n; lift_unimpl(lift_pc, "copex (no ISA model)"); return 0; }
void lift_swi(int n) { (void)n; lift_unimpl(lift_pc, "swi (software interrupt)"); }
void lift_trigger(void) { }
void lift_sysret(int bank) { (void)bank; }
