/* Open-UV5R-Mini: the TX lock. See radio/txlock.h. Frequencies in 10 Hz. */
#include "radio/txlock.h"

typedef struct { uint32_t lo, hi; } Range_t;

/* Amateur bands this radio can transmit on (2 m and 70 cm), by region. ITU: the part common to regions 1-3. */
static const Range_t HAM_ITU[] = { { 14400000u, 14600000u }, { 43000000u, 44000000u } };
#if TXLOCK_REGIONS
static const Range_t HAM_US[] = { { 14400000u, 14800000u }, { 42000000u, 45000000u } };   /* FCC part 97 */
static const Range_t HAM_EU[] = { { 14400000u, 14600000u }, { 43000000u, 44000000u } };   /* CEPT, UK */
static const Range_t HAM_AU[] = { { 14400000u, 14800000u }, { 42000000u, 45000000u } };   /* ACMA LCD */
#endif

static TxLock_t s_mode = TXL_HAM;
static Region_t s_region = TXLOCK_REGIONS ? (TXLOCK_REGION ? (Region_t)TXLOCK_REGION : REGION_DEFAULT) : REGION_ITU;

bool TXLOCK_RegionFixed(void)
{
	return !TXLOCK_REGIONS || TXLOCK_REGION != 0;
}

bool TXLOCK_Available(TxLock_t mode, Region_t region)
{
	switch (mode) {
	case TXL_LOCKED:
	case TXL_HAM:
		return true;
	case TXL_LOCAL:
	case TXL_HAM_LOCAL:
		return TXLOCK_REGIONS && region != REGION_ITU && region < REGION_COUNT;
	case TXL_UNLOCKED:
		return TXLOCK_UNLOCK != 0;
	default:
		return false;
	}
}

void TXLOCK_Set(TxLock_t mode, Region_t region)
{
	if (!TXLOCK_REGIONS)
		region = REGION_ITU;
	else if (TXLOCK_REGION)
		region = (Region_t)TXLOCK_REGION;
	else if (region >= REGION_COUNT)
		region = REGION_DEFAULT;
	s_region = region;
	s_mode = mode < TXL_COUNT && TXLOCK_Available(mode, region) ? mode : TXL_HAM;
}

TxLock_t TXLOCK_Mode(void) { return s_mode; }
Region_t TXLOCK_Region(void) { return s_region; }

const char *TXLOCK_RegionName(Region_t region)
{
	static const char *const N[REGION_COUNT] = { "ITU", "US", "EU/UK", "AU" };
	return region < REGION_COUNT ? N[region] : "?";
}

/* the region's local service, for names and messages */
static const char *local_name(Region_t region)
{
	switch (region) {
	case REGION_US: return "GMRS";
	case REGION_EU: return "PMR446";
	case REGION_AU: return "UHF CB";
	default:        return "-";
	}
}

const char *TXLOCK_ModeName(TxLock_t mode, Region_t region)
{
	switch (mode) {
	case TXL_LOCKED:   return "LOCKED";
	case TXL_HAM:      return "HAM";
	case TXL_LOCAL:    return local_name(region);
	case TXL_HAM_LOCAL:
		switch (region) {
		case REGION_US: return "HAM+GMRS";
		case REGION_EU: return "HAM+PMR446";
		case REGION_AU: return "HAM+UHF CB";
		default:        return "-";
		}
	case TXL_UNLOCKED: return "UNLOCKED";
	default:           return "?";
	}
}

static bool in(const Range_t *r, unsigned n, uint32_t f)
{
	for (unsigned i = 0; i < n; i++)
		if (f >= r[i].lo && f < r[i].hi)
			return true;
	return false;
}

static bool ham(uint32_t f)
{
#if TXLOCK_REGIONS
	switch (s_region) {
	case REGION_US: return in(HAM_US, 2, f);
	case REGION_EU: return in(HAM_EU, 2, f);
	case REGION_AU: return in(HAM_AU, 2, f);
	default:        break;
	}
#endif
	return in(HAM_ITU, 2, f);
}

#if TXLOCK_REGIONS
/* The channel number (0-based) of f on a grid of count channels from base, step apart; or -1. */
static int grid(uint32_t f, uint32_t base, uint32_t step, int count)
{
	if (f < base || (f - base) % step)
		return -1;
	uint32_t n = (f - base) / step;
	return n < (uint32_t)count ? (int)n : -1;
}

/* 0 = a channel of the local service and its rules are kept; "" = not a channel; else why not. */
static const char *local(uint32_t f, bool low_power, bool narrow)
{
	switch (s_region) {
	case REGION_US:      /* GMRS, FCC part 95E */
		if (grid(f, 46256250u, 2500u, 7) >= 0 || grid(f, 46255000u, 2500u, 8) >= 0)   /* 1-7, 15-22 */
			return 0;
		if (grid(f, 46755000u, 2500u, 8) >= 0)                                     /* repeater inputs */
			return 0;
		if (grid(f, 46756250u, 2500u, 7) >= 0) {                                   /* 8-14: 0.5 W, narrow */
			if (!low_power)
				return "GMRS 8-14: LOW PWR";
			if (!narrow)
				return "GMRS 8-14: NARROW";
			return 0;
		}
		return "";
	case REGION_EU:      /* PMR446: 16 channels of 12.5 kHz, 0.5 W */
		if (grid(f, 44600625u, 1250u, 16) < 0)
			return "";
		if (!low_power)
			return "PMR446: LOW PWR";
		if (!narrow)
			return "PMR446: NARROW";
		return 0;
	case REGION_AU: {    /* UHF CB: 80 channels of 12.5 kHz, 476.425-477.4125 MHz. Channels 41-80 sit between the
		                      * first 40, so numbers are not in frequency order: 22 and 23 (476.950, 476.975, data
		                      * only) and their neighbours 61-63 (476.9375, 476.9625, 476.9875, reserved) are refused. */
		if (grid(f, 47642500u, 1250u, 80) < 0)
			return "";
		if (f >= 47693750u && f <= 47698750u)
			return "UHF CB 22/23: DATA";
		if (!narrow)
			return "UHF CB: NARROW";
		return 0;
	}
	default:
		return "";
	}
}
#endif

const char *TXLOCK_Check(uint32_t f, bool low_power, bool narrow)
{
	switch (s_mode) {
	case TXL_LOCKED:
		return "TX LOCKED";
	case TXL_HAM:
		return ham(f) ? 0 : "NO TX: NOT HAM BAND";
#if TXLOCK_REGIONS
	case TXL_LOCAL:
	case TXL_HAM_LOCAL: {
		const char *why;
		if (s_mode == TXL_HAM_LOCAL && ham(f))
			return 0;
		why = local(f, low_power, narrow);
		if (why && !*why) {
			switch (s_region) {
			case REGION_US: return s_mode == TXL_LOCAL ? "NO TX: NOT GMRS CH" : "NO TX: NOT HAM/GMRS";
			case REGION_EU: return s_mode == TXL_LOCAL ? "NO TX: NOT PMR446" : "NO TX: NOT HAM/PMR";
			default:        return s_mode == TXL_LOCAL ? "NO TX: NOT UHF CB" : "NO TX: NOT HAM/CB";
			}
		}
		return why;
	}
#endif
	case TXL_UNLOCKED:
		return TXLOCK_UNLOCK ? 0 : "TX LOCKED";
	default:
		(void)low_power;
		(void)narrow;
		return "TX LOCKED";
	}
}
