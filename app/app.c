/* Open-UV5R-Mini: the radio application: main screen, frequency entry, menu, PTT.
 *
 * Laid out the way the UV-K5 open firmware splits a radio (a radio core in app/radio.c, screens drawn from state,
 * keys turned into actions), written fresh for this radio. Not tried on a radio yet. The cable update mode is kept:
 * PTT-A + '=' at power-on, as stock (app/update_radio.c).
 */
#include <stdbool.h>
#include <stdint.h>
#include "app/app.h"
#include "app/battery.h"
#include "app/calib.h"
#include "app/channels.h"
#include "app/keys.h"
#include "app/keytest.h"
#include "app/radio.h"
#include "app/dualwatch.h"
#include "app/dtmfseq.h"
#include "app/scan.h"
#include "app/vox.h"
#include "app/settings.h"
#include "app/tones.h"
#include "app/update.h"
#include "board.h"
#include "bt/bt.h"
#include "driver/adc.h"
#include "driver/bk4819.h"
#include "driver/extflash.h"
#include "driver/gpio.h"
#include "driver/io.h"
#include "driver/keyboard.h"
#include "driver/st7735s.h"
#include "driver/system.h"
#include "ui/helper.h"

#define WHITE  RGB565(255, 255, 255)
#define BLACK  RGB565(0, 0, 0)
#define YELLOW RGB565(255, 220, 0)
#define GREEN  RGB565(0, 220, 80)
#define RED    RGB565(255, 40, 40)
#define GREY   RGB565(120, 120, 120)
#define BLUE   RGB565(40, 120, 255)

typedef enum { SCREEN_MAIN, SCREEN_MENU } Screen_t;

static Screen_t s_screen;
static bool     s_redraw = true;         /* whole screen */
#define LAMP_STROBE_MS 500u          /* strobe half-period: a guess, the stock rate is not read yet */
static uint8_t  s_lamp_mode;             /* 0 off, 1 on, 2 strobe (stock flashlight key) */
static uint16_t s_lamp_ms;
static char     s_entry[7];              /* digits typed, MHz then kHz */
static uint8_t  s_entry_len;
static uint8_t  s_menu_sel, s_menu_edit;
static int      s_menu_top = -1;
static uint16_t s_off_ticks, s_batt_ticks;
static uint32_t s_light_ms, s_tx_ms;     /* backlight on for, transmitting for */
static bool     s_light_on = true, s_tot_hit;
static int      s_shown_batt = -1, s_shown_scan = -1;
static bool     s_mr;                    /* channel mode */
static int      s_ch = -1;               /* channel slot shown, 0-based */
static char     s_ch_name[13];
static bool     s_xf_ok;                 /* external flash answered */
static bool     s_ptt_down, s_tx_refused;
static uint16_t s_tail_ms;     /* transmit still on after PTT was let go */
static bool     s_eot;         /* the down code is being sent after PTT was let go */

/* PTT-ID: the up (bit 0) or down (bit 1) code if the channel (in VFO mode setting 0x0B) and the ANI block both ask
 * for it (stock 0x01E507B0 / 0x01E508C2: channel byte 13 bit, then the ANI byte 6 bit). Stock does something else when the channel asks and
 * the ANI byte doesn't (a group call path, not followed here). */
static bool ptt_id_start(uint8_t bit)
{
	const uint8_t *codes = bit == 1 ? gStock.up_code : gStock.down_code;

	uint8_t mode = s_mr ? gVfo.ptt_id : gStock.raw[SET_PTT_ID];   /* stock sub_01E3F9D0 */

	if (!(mode & bit) || !(gStock.ani[6] & bit))
		return false;
	return DTMFSEQ_Start(codes, bit == 1 ? DTMFSEQ_WaitMs(gStock.raw[0x0C]) : 0, DTMFSEQ_TimeMs(gStock.ani[7]),
	                     DTMFSEQ_TimeMs(gStock.ani[8]));
}

/* transmit end, the stock order: roger, tail tone, then 400 ms more (sub_01E4DCC2) */
static void tx_release(void)
{
	if (gStock.raw[SET_ROGER])
		RADIO_Roger();
	if (gStock.raw[SET_STE])
		RADIO_TxTail();
	s_tail_ms = RADIO_TX_TAIL_MS;
}

static void ptt_id_tick(void)
{
	int c = DTMFSEQ_Tick(APP_TICK_MS);

	if (c >= 0)
		RADIO_TxTone((uint8_t)c, (gStock.raw[SET_DTMF_MON] & 2u) != 0);
	else if (c == DTMFSEQ_GAP)
		RADIO_TxToneSilence();
	else if (c == DTMFSEQ_DONE) {
		RADIO_TxToneOff();
		if (s_eot) {
			s_eot = false;
			tx_release();
		}
	}
}

/* The two sides, A and B, as stock has them. gVfo, s_mr, s_ch and s_ch_name are the active side's working copy;
 * s_side[] holds both and is brought up to date before the other side is tuned. */
typedef struct {
	Vfo_t vfo;
	bool  mr;
	int   ch;
	char  name[13];
} Side_t;
static Side_t   s_side[2];
static uint8_t  s_active;                /* 0 = A, 1 = B */
static bool     s_peek;                  /* dual watch: tuned to the other side for a moment */
static bool     s_peek_held;             /* ... and it opened */
static uint32_t s_shown_other = 0xFFFFFFFFu;
static void side_save(void);
static bool     s_tscan;                 /* tone scan running */
static uint16_t s_tscan_last, s_tscan_ms;
static uint8_t  s_tscan_same;
static char     s_dtmf[17];              /* DTMF heard on receive */
static uint8_t  s_dtmf_len;
static uint32_t s_dtmf_ms;
static bool     s_dtmf_dirty;
static void     draw_tscan(void);
static void     tscan_start(void);
static void side_switch(void);
static void dw_end(void);

/* what is on the screen now, so only changes are drawn (the LCD is slow to draw) */
static uint32_t s_shown_freq = 0xFFFFFFFFu;
static int      s_shown_state = -1, s_shown_bar = -1, s_shown_status = -1;

/* ------------------------------------------------------------------------------------------- formatting */
static char *fmt_freq(char *b, uint32_t f)        /* 10 Hz units -> "145.52000" */
{
	uint32_t mhz = f / 100000u, rest = f % 100000u;
	char *p = b;

	if (mhz >= 100)
		*p++ = (char)('0' + mhz / 100);
	else
		*p++ = ' ';
	*p++ = (char)('0' + mhz / 10 % 10);
	*p++ = (char)('0' + mhz % 10);
	*p++ = '.';
	for (uint32_t d = 10000u; d; d /= 10) {
		*p++ = (char)('0' + rest / d % 10);
	}
	*p = 0;
	return b;
}

static char *fmt_step(char *b, uint16_t s)        /* 10 Hz units -> "12.5K", "6.25K", "100K" */
{
	char *p = b, t[6];
	uint32_t whole = s / 100u, frac = s % 100u;
	int n = 0;

	do
		t[n++] = (char)('0' + whole % 10);
	while ((whole /= 10) && n < 5);
	while (n)
		*p++ = t[--n];
	if (frac) {
		*p++ = '.';
		*p++ = (char)('0' + frac / 10);
		if (frac % 10)
			*p++ = (char)('0' + frac % 10);
	}
	*p++ = 'K';
	*p = 0;
	return b;
}

/* ------------------------------------------------------------------------------------------- main screen */
static void draw_main_static(void)
{
	ST7735S_FillRect(0, 0, 128, 128, BLACK);
	UI_PrintString("MENU *BW #LAMP", 0, 120, GREY, BLACK, 1);
	s_shown_batt = s_shown_scan = -1;
}

/* text, then spaces to n characters, so the old text is covered without a clear */
static void pad_print(const char *t, uint8_t x, uint8_t y, uint16_t fg, unsigned n)
{
	char p[24];
	unsigned i = 0;

	for (; i < n && i < sizeof p - 1 && t[i]; i++)
		p[i] = t[i];
	for (; i < n && i < sizeof p - 1; i++)
		p[i] = ' ';
	p[i] = 0;
	UI_PrintString(p, x, y, fg, BLACK, 1);
}

static void draw_status(void)
{
	char b[12];
	int st = gVfo.squelch | gVfo.volume << 4 | gVfo.narrow << 8 | RADIO_IsAm() << 9 | gVfo.step << 10
	         | (s_lamp_mode != 0) << 14 | gVfo.power << 15 | (int)(gVfo.tx_tone ^ gVfo.rx_tone << 3) << 16;

	if (st == s_shown_status && !s_redraw)
		return;
	s_shown_status = st;
	UI_PrintString(RADIO_IsAm() ? "AM" : "FM", 0, 1, BLUE, BLACK, 1);
	UI_PrintString(gVfo.narrow || RADIO_IsAm() ? "N" : "W", 16, 1, BLUE, BLACK, 1);
	UI_PrintString("SQ", 30, 1, WHITE, BLACK, 1);
	UI_PrintString(UI_FormatDec(b, gVfo.squelch, 1), 42, 1, YELLOW, BLACK, 1);
	UI_PrintString("V", 54, 1, WHITE, BLACK, 1);
	UI_PrintString(UI_FormatDec(b, gVfo.volume, 2), 60, 1, YELLOW, BLACK, 1);
	UI_PrintString(gVfo.power == POWER_LOW ? "L" : "H", 76, 1, BLUE, BLACK, 1);
	/* every field is printed over its old text with padding (no clear first: clearing made it flicker) */
	UI_PrintString(s_lamp_mode == 2 ? " STRB" : s_lamp_mode ? " LAMP" : !CALIB_Present() ? "NOCAL" : "     ", 94, 1,
	               s_lamp_mode ? YELLOW : RED, BLACK, 1);
	if (s_mr) {
		UI_PrintString("CH", 0, 55, GREY, BLACK, 1);
		UI_PrintString(s_ch >= 0 ? UI_FormatDec(b, (uint32_t)s_ch + 1, 3) : "---", 14, 55, WHITE, BLACK, 1);
		pad_print(s_ch_name, 36, 55, YELLOW, 15);
	} else {
		UI_PrintString("STEP", 0, 55, GREY, BLACK, 1);
		pad_print(fmt_step(b, RADIO_Steps[gVfo.step]), 30, 55, WHITE, 16);
	}
	UI_PrintString(gVfo.rx_tone ? "R" : " ", 0, 12, GREY, BLACK, 1);
	pad_print(gVfo.rx_tone ? TONE_Format(b, gVfo.rx_tone) : "", 8, 12, WHITE, 8);
	UI_PrintString(gVfo.tx_tone ? "T" : " ", 60, 12, GREY, BLACK, 1);
	pad_print(gVfo.tx_tone ? TONE_Format(b, gVfo.tx_tone) : "", 68, 12, WHITE, 5);
}

static void draw_freq(void)
{
	char b[12];

	if (s_entry_len && s_mr) {
		b[0] = 'C';
		b[1] = 'H';
		b[2] = ' ';
		for (int i = 0; i < 3; i++)
			b[3 + i] = i < s_entry_len ? s_entry[i] : '-';
		b[6] = b[7] = b[8] = ' ';
		b[9] = 0;
		UI_PrintString(b, 10, 26, YELLOW, BLACK, 2);
		s_shown_freq = 0xFFFFFFFFu;
		return;
	}
	if (s_entry_len) {
		for (int i = 0; i < 6; i++)
			b[i < 3 ? i : i + 1] = i < s_entry_len ? s_entry[i] : '-';
		b[3] = '.';
		b[7] = ' ';
		b[8] = ' ';
		b[9] = 0;
		UI_PrintString(b, 10, 26, YELLOW, BLACK, 2);
		s_shown_freq = 0xFFFFFFFFu;
		return;
	}
	{
		uint8_t shown_side = (uint8_t)(s_active ^ s_peek);
		const Side_t *o = &s_side[shown_side ^ 1];
		uint32_t key = gVfo.freq ^ (uint32_t)shown_side << 31;

		if (key != s_shown_freq || s_redraw) {
			s_shown_freq = key;
			UI_PrintString(shown_side ? "B" : "A", 0, 30, s_peek ? GREEN : BLUE, BLACK, 1);
			/* stock display mode, channel mode only: setting 0x0D (A) / 0x0E (B): 0 name, 1 frequency, 2 channel */
			uint8_t dm = gStock.raw[shown_side ? SET_DISP_B : SET_DISP_A];
			if (s_mr && !s_peek && s_ch >= 0 && (dm == 2 || (dm == 0 && s_ch_name[0]))) {
				unsigned i = 0;
				if (dm == 2) {
					b[i++] = 'C'; b[i++] = 'H'; b[i++] = '-';
					char n[8];
					const char *d = UI_FormatDec(n, (uint32_t)s_ch + 1, 3);
					while (*d)
						b[i++] = *d++;
				} else
					for (; i < 8 && s_ch_name[i]; i++)
						b[i] = s_ch_name[i];
				while (i < 9)
					b[i++] = ' ';
				b[i] = 0;
				UI_PrintString(b, 10, 26, WHITE, BLACK, 2);
			} else
				UI_PrintString(fmt_freq(b, gVfo.freq), 10, 26, s_peek ? GREEN : WHITE, BLACK, 2);
		}
		key = gStock.raw[SET_DUAL_WATCH] ? o->vfo.freq ^ (uint32_t)shown_side << 31 : 0;
		if (key != s_shown_other || s_redraw) {
			s_shown_other = key;
			ST7735S_FillRect(0, 44, 128, 9, BLACK);
			if (key) {
				UI_PrintString(shown_side ? "A" : "B", 0, 45, GREY, BLACK, 1);
				if (o->mr && o->name[0])
					UI_PrintString(o->name, 12, 45, GREY, BLACK, 1);
				else
					UI_PrintString(fmt_freq(b, o->vfo.freq), 12, 45, GREY, BLACK, 1);
			}
		}
	}
}

static void draw_signal(void)
{
	RadioState_t st = RADIO_State();
	int bar;
	char b[8];

	if ((int)st != s_shown_state || s_redraw) {
		s_shown_state = (int)st;
		ST7735S_FillRect(0, 92, 128, 18, BLACK);
		if (st == RADIO_TX)
			UI_PrintString("TX", 4, 92, RED, BLACK, 2);
		else if (st == RADIO_RX_OPEN)
			UI_PrintString("RX", 4, 92, GREEN, BLACK, 2);
		if (s_tx_refused)
			UI_PrintString("NO TX", 60, 96, RED, BLACK, 1);
	}
	if (st == RADIO_TX)
		return;
	/* RSSI bar: -130 dBm .. -50 dBm across 120 pixels */
	{
		int dbm = (int)RADIO_Rssi() / 2 - 160;
		bar = (dbm + 130) * 3 / 2;
		if (bar < 0)
			bar = 0;
		if (bar > 120)
			bar = 120;
	}
	if (bar == s_shown_bar && !s_redraw)
		return;
	if (s_shown_bar < 0 || s_redraw)
		ST7735S_FillRect(4, 70, 120, 8, RGB565(30, 30, 30));
	if (bar > s_shown_bar || s_shown_bar < 0)
		ST7735S_FillRect(4, 70, (uint8_t)bar, 8, bar > 60 ? GREEN : YELLOW);
	if (bar < s_shown_bar)
		ST7735S_FillRect((uint8_t)(4 + bar), 70, (uint8_t)(s_shown_bar - bar), 8, RGB565(30, 30, 30));
	s_shown_bar = bar;
	{
		int dbm = (int)RADIO_Rssi() / 2 - 160;
		char *p = b;
		if (dbm < 0) {
			*p++ = '-';
			dbm = -dbm;
		}
		UI_FormatDec(p, (uint32_t)dbm, 3);
		UI_PrintString(b, 4, 80, GREY, BLACK, 1);
		UI_PrintString("DBM", 34, 80, GREY, BLACK, 1);
	}
}

/* Battery, bottom right: volts as the stock voltage screen shows them. Low (below the second calibrated threshold):
 * red and blinking, as stock blinks its battery icon. */
static void draw_battery(void)
{
	char b[8];
	int t = BATTERY_Tenths(), low = BATTERY_Low(), blink = low && (s_batt_ticks * APP_TICK_MS) % 1000u < 500u;
	int key = t | low << 8 | blink << 9;

	if (key == s_shown_batt)
		return;
	s_shown_batt = key;
	if (blink) {
		ST7735S_FillRect(104, 120, 24, 8, BLACK);
		return;
	}
	b[0] = (char)('0' + t / 10 % 10);
	b[1] = '.';
	b[2] = (char)('0' + t % 10);
	b[3] = 'V';
	b[4] = 0;
	UI_PrintString(BATTERY_Raw() ? b : "    ", 104, 120, low ? RED : GREEN, BLACK, 1);
}

static void draw_scan(void)
{
	int sc = SCAN_Active() + SCAN_Holding();

	if (sc == s_shown_scan)
		return;
	s_shown_scan = sc;
	if (sc)
		UI_PrintString("SCAN", 100, 12, sc == 2 ? GREEN : YELLOW, BLACK, 1);
	else
		ST7735S_FillRect(100, 12, 28, 8, BLACK);
}

/* DTMF heard: shown above the key help for the stock hang time (setting 0x39: min(n, 7) + 3 s), up to 16 characters
 * as the stock buffer. */
static void dtmf_tick(void)
{
	char c = RADIO_DtmfPoll();
	uint8_t hang = gStock.raw[0x39] > 7 ? 7 : gStock.raw[0x39];

	if (c) {
		if (s_dtmf_len >= 16)
			s_dtmf_len = 0;
		s_dtmf[s_dtmf_len++] = c;
		s_dtmf[s_dtmf_len] = 0;
		s_dtmf_ms = 0;
		s_dtmf_dirty = true;
	} else if (s_dtmf_len && (s_dtmf_ms += APP_TICK_MS) >= (hang + 3u) * 1000u) {
		s_dtmf_len = 0;
		s_dtmf[0] = 0;
		s_dtmf_dirty = true;
	}
}

static void draw_dtmf(void)
{
	if (!s_dtmf_dirty && !s_redraw)
		return;
	s_dtmf_dirty = false;
	ST7735S_FillRect(0, 110, 128, 9, BLACK);
	if (s_dtmf_len) {
		UI_PrintString("DTMF", 0, 110, GREY, BLACK, 1);
		UI_PrintString(s_dtmf, 28, 110, YELLOW, BLACK, 1);
	}
}

static void draw_main(void)
{
	if (s_redraw)
		draw_main_static();
	draw_dtmf();
	draw_battery();
	draw_scan();
	draw_tscan();
	draw_status();
	draw_freq();
	draw_signal();
}

/* ------------------------------------------------------------------------------------------- menu */
enum { M_AB, M_DW, M_MEM, M_SCAN, M_SCANMODE, M_SQL, M_VOL, M_STEP, M_BW, M_MOD, M_RXTONE, M_TXTONE, M_TSCAN,
       M_TSCANSAVE, M_POWER, M_KEYTEST, M_COUNT };
static const char *const MENU_NAMES[M_COUNT] = {
	"A/B", "DUAL WATCH", "MEMORY", "SCAN", "SCAN MODE", "SQUELCH", "VOLUME", "STEP", "BANDWIDTH", "MODULATION",
	"RX TONE", "TX TONE", "TONE SCAN", "TSCAN SAVE", "TX POWER", "KEY TEST",
};
#define MENU_ROWS 8                      /* rows on screen; the list scrolls */

static const char *menu_value(int i, char *b)
{
	static const char *const MOD[] = { "AUTO", "FM", "AM" };

	switch (i) {
	case M_AB:      return s_active ? "B" : "A";
	case M_DW:      return gStock.raw[SET_DUAL_WATCH] ? "ON" : "OFF";
	case M_MEM:     return s_mr ? "CHANNELS" : "VFO";
	case M_SCAN:    return "START";
	case M_TSCAN:   return "START";
	case M_TSCANSAVE: {
		static const char *const TS[] = { "RX+TX", "RX", "TX" };
		return TS[gStock.raw[0x2B] <= 2 ? gStock.raw[0x2B] : 0];
	}
	case M_SCANMODE: {
		static const char *const SM[] = { "TIME", "CARRIER", "SEARCH" };
		return SM[gStock.raw[SET_SCAN_MODE] <= 2 ? gStock.raw[SET_SCAN_MODE] : 1];
	}
	case M_SQL:     return UI_FormatDec(b, gVfo.squelch, 1);
	case M_VOL:     return UI_FormatDec(b, gVfo.volume, 2);
	case M_STEP:    return fmt_step(b, RADIO_Steps[gVfo.step]);
	case M_BW:      return gVfo.narrow ? "NARROW" : "WIDE";
	case M_MOD:     return MOD[gVfo.mod];
	case M_RXTONE:  return TONE_Format(b, gVfo.rx_tone);
	case M_TXTONE:  return TONE_Format(b, gVfo.tx_tone);
	case M_POWER:   return gVfo.power == POWER_LOW ? "LOW" : "HIGH";
	default:        return "";
	}
}

static void channel_show(int n);

static void menu_change(int i, int dir)
{
	switch (i) {
	case M_MEM:
		s_mr = s_xf_ok && !s_mr;
		if (s_mr)
			channel_show(CHANNEL_Next(s_ch < 0 ? -1 : s_ch - 1, +1));
		else {
			gVfo.tx_freq = 0;
			gVfo.rx_only = false;
		}
		break;
	case M_AB:
		side_switch();
		return;
	case M_DW:
		gStock.raw[SET_DUAL_WATCH] = !gStock.raw[SET_DUAL_WATCH];
		break;
	case M_TSCANSAVE:
		gStock.raw[0x2B] = (uint8_t)(((gStock.raw[0x2B] <= 2 ? gStock.raw[0x2B] : 0) + 3 + dir) % 3);
		break;
	case M_SCANMODE:
		gStock.raw[SET_SCAN_MODE] = (uint8_t)(((gStock.raw[SET_SCAN_MODE] <= 2 ? gStock.raw[SET_SCAN_MODE] : 1)
		                                       + 3 + dir) % 3);
		break;
	case M_SQL:   gVfo.squelch = (uint8_t)((gVfo.squelch + 10 + dir) % 10); break;
	case M_VOL:   gVfo.volume = (uint8_t)((gVfo.volume + 16 + dir) % 16); break;
	case M_STEP:  gVfo.step = (uint8_t)((gVfo.step + RADIO_StepCount + dir) % RADIO_StepCount); break;
	case M_BW:    gVfo.narrow = !gVfo.narrow; break;
	case M_MOD:   gVfo.mod = (Modulation_t)((gVfo.mod + 3 + dir) % 3); break;
	case M_RXTONE: gVfo.rx_tone = TONE_Step(gVfo.rx_tone, dir); break;
	case M_TXTONE: gVfo.tx_tone = TONE_Step(gVfo.tx_tone, dir); break;
	case M_POWER: gVfo.power = gVfo.power == POWER_LOW ? POWER_HIGH : POWER_LOW; break;
	default: break;
	}
	RADIO_Apply();
}

static void draw_menu(void)
{
	char b[12];

	if (s_redraw) {
		ST7735S_FillRect(0, 0, 128, 128, BLACK);
		UI_PrintString("MENU", 52, 2, YELLOW, BLACK, 1);
		UI_PrintString("UP/DN MENU EXIT", 0, 120, GREY, BLACK, 1);
	}
	int top = s_menu_sel < MENU_ROWS ? 0 : s_menu_sel - MENU_ROWS + 1;

	if (top != s_menu_top) {
		s_menu_top = top;
		ST7735S_FillRect(0, 14, 128, 12 * MENU_ROWS + 2, BLACK);
	}
	for (int i = top; i < M_COUNT && i < top + MENU_ROWS; i++) {
		uint8_t y = (uint8_t)(16 + 12 * (i - top));
		bool sel = i == s_menu_sel;
		uint16_t bg = sel ? RGB565(40, 40, 90) : BLACK;
		ST7735S_FillRect(0, y - 1, 128, 10, bg);
		UI_PrintString(MENU_NAMES[i], 2, y, WHITE, bg, 1);
		UI_PrintString(menu_value(i, b), 76, y, sel && s_menu_edit ? GREEN : YELLOW, bg, 1);
	}
}

/* ------------------------------------------------------------------------------------------- keys */
static void go(Screen_t s)
{
	s_screen = s;
	s_redraw = true;
}

static void freq_step(int dir)
{
	uint32_t st = RADIO_Steps[gVfo.step];
	uint32_t f = gVfo.freq - gVfo.freq % st;

	if (dir > 0)
		f += st;
	else if (f == gVfo.freq)
		f -= st;
	if (f < FREQ_MIN || f > FREQ_MAX) {
		RADIO_Beep(400, 80);
		return;
	}
	gVfo.freq = f;
	RADIO_Apply();
}

/* Show channel slot n (0-based): its frequencies, tones, power and width go into the VFO. */
static void channel_show(int n)
{
	Channel_t ch;

	if (n < 0 || !CHANNEL_Load((uint16_t)n, &ch)) {
		RADIO_Beep(400, 120);
		return;
	}
	s_ch = n;
	for (int i = 0; i < 13; i++)
		s_ch_name[i] = ch.name[i];
	gVfo.freq = ch.rx;
	gVfo.tx_freq = ch.tx;
	gVfo.rx_only = ch.tx == 0;
	gVfo.rx_tone = ch.rx_tone;
	gVfo.tx_tone = ch.tx_tone;
	gVfo.power = ch.low_power ? POWER_LOW : POWER_HIGH;
	gVfo.ptt_id = ch.ptt_id;
	gVfo.narrow = ch.narrow;
	RADIO_Apply();
	s_shown_status = -1;
}

static void entry_finish(void)
{
	if (s_mr) {
		uint32_t n = 0;
		for (int i = 0; i < s_entry_len; i++)
			n = n * 10 + (uint32_t)(s_entry[i] - '0');
		s_entry_len = 0;
		s_redraw = true;
		channel_show(n >= 1 && n <= CHANNEL_COUNT ? (int)n - 1 : -1);
		return;
	}
	uint32_t khz = 0;

	for (int i = 0; i < 6; i++)
		khz = khz * 10 + (uint32_t)(i < s_entry_len ? s_entry[i] - '0' : 0);
	s_entry_len = 0;
	if (khz * 100u < FREQ_MIN || khz * 100u > FREQ_MAX) {
		RADIO_Beep(400, 120);
		s_redraw = true;
		return;
	}
	gVfo.freq = khz * 100u;
	RADIO_Apply();
	s_redraw = true;
}

/* ------------------------------------------------------------------------------------------- tone scan
 * As the stock tone scan: the chip's sub-audio scan, read every 100 ms; a result read twice in a row is kept, in
 * the receive tone, the transmit tone or both by stock setting 0x2B (0 both, 1 receive, 2 transmit). */
static void tscan_start(void)
{
	SCAN_Stop();
	gVfo.rx_tone = 0;                    /* no tone squelch while looking */
	RADIO_Apply();
	BK4819_CssScanStart();
	s_tscan = true;
	s_tscan_last = s_tscan_ms = 0;
	s_tscan_same = 0;
	s_entry_len = 0;
}

static void tscan_stop(void)
{
	if (!s_tscan)
		return;
	s_tscan = false;
	RADIO_Apply();                       /* REG_51 back to the VFO's own tone */
	s_shown_status = -1;
}

static void tscan_tick(void)
{
	uint32_t v;
	uint16_t t = 0;

	if (!s_tscan || (s_tscan_ms += APP_TICK_MS) < 100u)
		return;
	s_tscan_ms = 0;
	switch (BK4819_CssScanRead(&v)) {
	case BK4819_TONE_CTCSS: t = TONE_FromCtcss(v); break;
	case BK4819_TONE_DCS_N: t = TONE_FromDcsWord(v); break;
	default: break;
	}
	if (!t) {
		s_tscan_same = 0;
		return;
	}
	s_tscan_same = t == s_tscan_last ? (uint8_t)(s_tscan_same + 1) : 1;
	s_tscan_last = t;
	if (s_tscan_same < 2)
		return;
	if (gStock.raw[0x2B] != 2)
		gVfo.rx_tone = t;
	if (gStock.raw[0x2B] != 1)
		gVfo.tx_tone = t;
	tscan_stop();
	RADIO_Beep(1200, 120);
}

static void draw_tscan(void)
{
	char b[8];

	if (!s_tscan)
		return;
	UI_PrintString("TONE SCAN", 0, 12, YELLOW, BLACK, 1);
	UI_PrintString(s_tscan_last ? TONE_Format(b, s_tscan_last) : "     ", 60, 12, WHITE, BLACK, 1);
}

/* ------------------------------------------------------------------------------------------- scanning */
/* The stock VFO scan range, whole MHz (setting bytes 0x2C-0x2F); stock defaults 400-470. */
static void scan_range(uint32_t *lo, uint32_t *hi)
{
	uint32_t l = (uint32_t)(gStock.raw[0x2C] | gStock.raw[0x2D] << 8), h = (uint32_t)(gStock.raw[0x2E] | gStock.raw[0x2F] << 8);

	if (!l || h <= l || h > FREQ_MAX / 100000u) {
		l = 400;
		h = 470;
	}
	*lo = l * 100000u;
	*hi = h * 100000u;
}

static void scan_hop(void)
{
	if (s_mr) {
		int n = s_ch;
		for (unsigned i = 0; i < CHANNEL_COUNT; i++) {
			Channel_t ch;
			n = CHANNEL_Next(n, SCAN_Dir());
			if (n < 0)
				break;
			if (CHANNEL_Load((uint16_t)n, &ch) && ch.scan) {
				channel_show(n);
				return;
			}
		}
		SCAN_Stop();                     /* no channel to scan */
		RADIO_Beep(400, 150);
		return;
	}
	uint32_t lo, hi, st = RADIO_Steps[gVfo.step], f;
	scan_range(&lo, &hi);
	f = gVfo.freq - gVfo.freq % st;
	f = SCAN_Dir() > 0 ? f + st : f - st;
	if (f < lo || f > hi || gVfo.freq < lo || gVfo.freq > hi)
		f = SCAN_Dir() > 0 ? lo : hi;
	gVfo.freq = f;
	RADIO_Apply();
}

static void scan_start(int dir)
{
	uint8_t m = gStock.raw[SET_SCAN_MODE];

	SCAN_Start(dir, (ScanResume_t)(m <= 2 ? m : 1), APP_TICK_MS);
	s_entry_len = 0;
}

static void key_main(KEY_Code_t k)
{
	int d = KEY_Digit(k);

	if (s_tscan) {                       /* any key ends the tone scan */
		tscan_stop();
		return;
	}
	if (SCAN_Active()) {                 /* any key ends the scan; UP/DOWN turn it round */
		if (k == KEY_ACT_UP || k == KEY_ACT_DOWN)
			scan_start(k == KEY_ACT_UP ? +1 : -1);
		else
			SCAN_Stop();
		return;
	}

	if (d >= 0) {
		if (s_entry_len < 6)
			s_entry[s_entry_len++] = (char)('0' + d);
		if (s_entry_len == (s_mr ? 3 : 6))
			entry_finish();
		return;
	}
	if (s_entry_len) {
		if (k == KEY_ACT_EXIT) {
			s_entry_len = 0;
			s_redraw = true;
		} else if (k == KEY_ACT_MENU)
			entry_finish();
		return;
	}
	if ((k == KEY_ACT_UP || k == KEY_ACT_DOWN) && s_mr)
		channel_show(CHANNEL_Next(s_ch, k == KEY_ACT_UP ? +1 : -1));
	else if (k == KEY_ACT_UP)
		freq_step(+1);
	else if (k == KEY_ACT_DOWN)
		freq_step(-1);
	else if (k == KEY_ACT_MENU) {
		s_menu_edit = 0;
		go(SCREEN_MENU);
	} else if (k == KEY_ACT_BW) {
		gVfo.narrow = !gVfo.narrow;
		RADIO_Apply();
	} else if (k == KEY_ACT_LAMP) {
		/* stock 0x01E537C8: the flashlight key steps off, on, strobe (RAM 0x198C0+2831, mod 3) */
		s_lamp_mode = (uint8_t)((s_lamp_mode + 1u) % 3u);
		s_lamp_ms = 0;
		IO_Lamp(s_lamp_mode != 0);
	}
	else if (k == KEY_ACT_AB || k == KEY_ACT_VM) {
		menu_change(k == KEY_ACT_AB ? M_AB : M_MEM, 0);
		s_redraw = true;
	}
}

static void key_menu(KEY_Code_t k)
{
	if (k == KEY_ACT_EXIT) {
		if (s_menu_edit)
			s_menu_edit = 0;
		else
			go(SCREEN_MAIN);
	} else if (k == KEY_ACT_MENU) {
		if (s_menu_sel == M_KEYTEST)
			KEYTEST_Run();
		if (s_menu_sel == M_SCAN) {
			scan_start(+1);
			go(SCREEN_MAIN);
			return;
		}
		if (s_menu_sel == M_TSCAN) {
			tscan_start();
			go(SCREEN_MAIN);
			return;
		}
		s_menu_edit = !s_menu_edit;
	} else if (k == KEY_ACT_UP || k == KEY_ACT_DOWN) {
		int dir = k == KEY_ACT_UP ? -1 : 1;
		if (s_menu_edit)
			menu_change(s_menu_sel, -dir);
		else
			s_menu_sel = (uint8_t)((s_menu_sel + M_COUNT + dir) % M_COUNT);
	}
	draw_menu();
}

/* press events, with repeat for UP/DOWN after 0.5 s */
static KEY_Code_t s_key_held = KEY_INVALID;

static KEY_Code_t key_event(void)
{
	static KEY_Code_t last = KEY_INVALID;
	static uint16_t held;
	KEY_Code_t k = KEYBOARD_Poll();

	s_key_held = k;
	if (k != last) {
		last = k;
		held = 0;
		return k;
	}
	if (k == KEY_ACT_UP || k == KEY_ACT_DOWN) {
		if (++held >= 50 && held % 10 == 0)
			return k;
	}
	return KEY_INVALID;
}

/* ------------------------------------------------------------------------------------------- entry points */
/* Start where the stock app left off: its squelch and beep setting, the active side's VFO, and channel mode if
 * that side was in it (app/settings.h; read only, nothing is written back). The channel number each side was on
 * isn't known yet, so channel mode starts at the first filled slot. */
static void start_side(uint8_t side);

static void start_from_stock(void)
{
	SETTINGS_Defaults();
	s_active = 0;
	side_save();                            /* without stock settings both sides start as the default VFO */
	s_side[1] = s_side[0];
	if (!s_xf_ok || !SETTINGS_Load())
		return;
	if (gStock.raw[SET_SQUELCH] <= 9)
		gVfo.squelch = gStock.raw[SET_SQUELCH];
	s_side[0].vfo = s_side[1].vfo = gVfo;
	s_side[0].ch = s_side[1].ch = -1;
	for (uint8_t side = 0; side < 2; side++) {
		s_active = side;
		start_side(side);
		side_save();
	}
	s_active = gStock.raw[SET_ACTIVE_SIDE] ? 1 : 0;
	gVfo = s_side[s_active].vfo;
	s_mr = s_side[s_active].mr;
	s_ch = s_side[s_active].ch;
	for (int i = 0; i < 13; i++)
		s_ch_name[i] = s_side[s_active].name[i];
	RADIO_Apply();
}

/* One side from the stock VFO log and work mode, into the working copy. */
static void start_side(uint8_t side)
{
	gVfo = s_side[side].vfo;
	s_mr = false;
	s_ch = -1;
	s_ch_name[0] = 0;
	if (gStock.have_vfo) {
		const StockVfo_t *v = &gStock.vfo[side];
		gVfo.freq = v->freq;
		gVfo.rx_tone = v->rx_tone;
		gVfo.tx_tone = v->tx_tone;
		gVfo.power = v->power ? POWER_LOW : POWER_HIGH;
		gVfo.narrow = v->narrow;
		gVfo.step = v->step < RADIO_StepCount ? v->step : 4;
		if (v->shift && v->offset)
			gVfo.tx_freq = v->shift > 0 ? v->freq + v->offset : v->freq - v->offset;
	}
	RADIO_Apply();
	if ((gStock.raw[SET_WORK_MODE] >> (side ? 4 : 0) & 0xFu) == 1) {
		int n = CHANNEL_Next(-1, +1);
		if (n >= 0) {
			s_mr = true;
			channel_show(n);
		}
	}
}

/* ------------------------------------------------------------------------------------------- A/B and dual watch */
static void side_save(void)
{
	Side_t *s = &s_side[s_active];

	s->vfo = gVfo;
	s->mr = s_mr;
	s->ch = s_ch;
	for (int i = 0; i < 13; i++)
		s->name[i] = s_ch_name[i];
}

static void side_load(uint8_t side)
{
	const Side_t *s = &s_side[side];
	uint8_t squelch = gVfo.squelch, volume = gVfo.volume;

	s_active = side;
	gVfo = s->vfo;
	gVfo.squelch = squelch;                 /* one squelch and volume for both sides, as stock */
	gVfo.volume = volume;
	s_mr = s->mr;
	s_ch = s->ch;
	for (int i = 0; i < 13; i++)
		s_ch_name[i] = s->name[i];
	RADIO_Apply();
	s_redraw = true;
	s_shown_status = -1;
}

static void side_switch(void)
{
	dw_end();
	side_save();
	side_load(s_active ^ 1u);
}

/* Tune the other side for a moment. Only the radio settings are swapped in; the screen keeps the active side
 * until the other side opens. */
static void dw_begin(void)
{
	Vfo_t keep = gVfo;

	side_save();
	gVfo = s_side[s_active ^ 1u].vfo;
	gVfo.squelch = keep.squelch;          /* one squelch and volume for both, as stock */
	gVfo.volume = keep.volume;
	RADIO_Apply();
	s_peek = true;
	s_peek_held = false;
}

static void dw_end(void)
{
	if (!s_peek)
		return;
	s_peek = s_peek_held = false;
	DW_Reset();
	gVfo = s_side[s_active].vfo;
	RADIO_Apply();
	s_shown_freq = 0xFFFFFFFFu;
	s_shown_status = -1;
}

static void dw_tick(bool busy)
{
	switch (DW_Tick(gStock.raw[SET_DUAL_WATCH] != 0, busy, RADIO_State() == RADIO_RX_OPEN, APP_TICK_MS)) {
	case DW_BEGIN:
		dw_begin();
		break;
	case DW_END:
		dw_end();
		break;
	case DW_HELD:
		s_peek_held = true;
		s_shown_freq = 0xFFFFFFFFu;
		break;
	default:
		break;
	}
}

#if SETTINGS_ENABLE_SAVE
/* ------------------------------------------------------------------------------------------- saving
 * Only with SETTINGS_ENABLE_SAVE (app/settings.h). 3 s after the last key, the state goes back into the stock
 * layout and both logs are written (an unchanged log is not written). A side in channel mode keeps its stock VFO
 * record: only the work-mode bit says it is in channel mode (the channel number is not known to us yet). */
static uint32_t s_save_ms;
static bool     s_save_due;

static void save_tick(bool busy)
{
	if (!s_save_due || busy || (s_save_ms += APP_TICK_MS) < 3000u)
		return;
	s_save_due = false;
	side_save();
	gStock.raw[SET_SQUELCH] = gVfo.squelch;
	gStock.raw[SET_ACTIVE_SIDE] = s_active;
	gStock.raw[SET_WORK_MODE] = (uint8_t)((s_side[0].mr ? 0x01u : 0u) | (s_side[1].mr ? 0x10u : 0u));
	for (int i = 0; i < 2; i++) {
		const Vfo_t *v = &s_side[i].vfo;
		StockVfo_t *sv = &gStock.vfo[i];
		if (s_side[i].mr)
			continue;
		sv->freq = v->freq;
		sv->rx_tone = v->rx_tone;
		sv->tx_tone = v->tx_tone;
		sv->power = v->power == POWER_LOW;
		sv->narrow = v->narrow;
		sv->step = v->step;
		if (v->tx_freq && v->tx_freq != v->freq) {
			sv->shift = v->tx_freq > v->freq ? 1 : -1;
			sv->offset = v->tx_freq > v->freq ? v->tx_freq - v->freq : v->freq - v->tx_freq;
		} else {
			sv->shift = 0;
			sv->offset = 0;
		}
	}
	if (!s_xf_ok || SETTINGS_SaveSettings() || (gStock.have_vfo && SETTINGS_SaveVfos()))
		RADIO_Beep(300, 400);        /* not saved */
}

static void save_soon(void)
{
	s_save_due = true;
	s_save_ms = 0;
}
#endif

void APP_Init(void)
{
	BOARD_Init();
	ST7735S_Init();
	KEYBOARD_Init();
	if (UPDATE_Requested())
		UPDATE_Mode();          /* PTT-A + '=' at power-on, as the stock app; does not return */
	BK4819_Init();
	/* a top-row key (wired to GND) held at power-on, PTT not held: the key test */
	if (!KEYBOARD_IsPttPressed()) {
		KEY_Code_t k0 = KEYBOARD_Poll();
		if (k0 == KEY_DIRECT0 || k0 == KEY_VM || k0 == KEY_AB || k0 == KEY_LAMP)
			KEYTEST_Run();
	}
	EXTFLASH_Init();
	s_xf_ok = EXTFLASH_ReadId() == EXTFLASH_ID;
	if (s_xf_ok) {
		static uint8_t cal[CALIB_LEN];
		EXTFLASH_Read(CALIB_ADDR, cal, sizeof cal);
		CALIB_Set(cal);
	}
	ADC_Init();
	BATTERY_Set(ADC_Read(ADC_CH_BATTERY, 8));
	RADIO_Init();
	start_from_stock();
	BT_Init(0);
	go(SCREEN_MAIN);
}

/* Backlight as the stock setting 0x03: 0 always on, 1-4 = 5/10/15/20 s after the last key, PTT or signal. */
static void light_wake(void)
{
	s_light_ms = 0;
	if (!s_light_on) {
		s_light_on = true;
		ST7735S_SetBacklight(true);
	}
}

static void light_tick(void)
{
	uint8_t bl = gStock.raw[SET_BACKLIGHT];

	if (!bl || bl > 4 || !s_light_on)
		return;
	if ((s_light_ms += APP_TICK_MS) >= 5000u * bl) {
		s_light_on = false;
		ST7735S_SetBacklight(false);
	}
}

/* Transmit refusals from the stock settings: busy lockout in VFO mode (0x0F) refuses PTT while a signal is
 * heard; the time-out timer (0x05, n x 15 s) ends a long transmission until PTT is let go. */
static bool tx_busy_locked(void)
{
	return !s_mr && gStock.raw[SET_BCL] && RADIO_State() == RADIO_RX_OPEN && gVfo.squelch;
}

static void tot_tick(void)
{
	uint8_t tot = gStock.raw[SET_TOT];

	if (RADIO_State() != RADIO_TX) {
		s_tx_ms = 0;
		return;
	}
	if (tot && tot <= 12 && (s_tx_ms += APP_TICK_MS) >= 15000u * tot) {
		RADIO_EndTx();
		s_tot_hit = s_tx_refused = true;
		RADIO_Beep(400, 300);
		s_shown_state = -1;
	}
}

/* A PTT pin counts as changed only after PTT_STABLE_MS of the same reading (it bounced on a radio). */
#define PTT_STABLE_MS 40u
static bool ptt_debounced(bool raw, unsigned i)
{
	static bool    state[2];
	static uint8_t ms[2];

	if (raw == state[i])
		ms[i] = 0;
	else if ((ms[i] += APP_TICK_MS) >= PTT_STABLE_MS) {
		state[i] = raw;
		ms[i] = 0;
	}
	return state[i];
}

void APP_Tick(void)
{
	KEY_Code_t k;
	bool ptt;

	SYSTEM_WatchdogClear();

	/* power switch: PB11 high = off (stock sub_01E4C978); act after 0.2 s */
	if (GPIO_CheckBit(PIN_POWER_SENSE)) {
		if (++s_off_ticks >= 200 / APP_TICK_MS) {
			RADIO_EndTx();
			IO_Amp(0xFF, false);
			BOARD_PowerOff();
		}
	} else
		s_off_ticks = 0;

	/* PTT; PTT-B transmits on side B (it becomes the active side) */
	ptt = ptt_debounced(KEYBOARD_IsPttPressed(), 0);
	if (ptt_debounced(KEYBOARD_IsPtt2Pressed(), 1)) {
		if (!s_ptt_down && s_active != 1)
			side_switch();
		ptt = true;
	}
	if (ptt) {
		dw_end();
		tscan_stop();
	}
	if (ptt && !s_ptt_down && (s_tail_ms || s_eot)) {   /* pressed again during the tail: start over */
		s_tail_ms = 0;
		s_eot = false;
		DTMFSEQ_Stop();
		RADIO_EndTx();
	}
	if (ptt && !s_ptt_down) {
		light_wake();
		s_tot_hit = false;
		s_tx_refused = tx_busy_locked() || !RADIO_StartTx();
		if (s_tx_refused)
			RADIO_Beep(400, 150);
		else
			ptt_id_start(1);
		s_shown_state = -1;
	} else if (!ptt && s_ptt_down) {
		if (RADIO_State() == RADIO_TX) {
			DTMFSEQ_Stop();                /* an up code still going is cut short */
			RADIO_TxToneOff();
			s_eot = ptt_id_start(2);
			if (!s_eot)
				tx_release();
		}
		s_tx_refused = s_tot_hit = false;
		s_shown_state = -1;
	}
	s_ptt_down = ptt;

	if (RADIO_State() != RADIO_TX) {
		DTMFSEQ_Stop();
		s_eot = false;
	} else
		ptt_id_tick();
	if (s_tail_ms && (s_tail_ms = s_tail_ms > APP_TICK_MS ? (uint16_t)(s_tail_ms - APP_TICK_MS) : 0) == 0) {
		RADIO_EndTx();
		/* stock 0x01E4FF94: setting 0x15 x 100 ms with the receive audio held off */
		RADIO_MuteRx((uint16_t)((gStock.raw[SET_RPT_CLEAR] < 30 ? gStock.raw[SET_RPT_CLEAR] : 30) * 100u));
	}
	RADIO_Tick();
	tot_tick();
	tscan_tick();
	dtmf_tick();
#if RADIO_ENABLE_TX
	/* VOX (stock setting 0x3A), not while PTT is held, scanning or in a menu */
	if (gStock.raw[0x3A] && !ptt && !SCAN_Active() && !s_tscan && s_screen == SCREEN_MAIN) {
		uint16_t a = ADC_Read(ADC_CH_PA1, 1);
		VoxAction_t v = VOX_Tick(a == 0xFFFFu ? 0 : (uint8_t)(a >> 2), gStock.raw[0x02], gStock.raw[0x20],
		                         RADIO_State() == RADIO_TX, APP_TICK_MS);
		if (v == VOX_START && !tx_busy_locked()) {
			dw_end();
			RADIO_StartTx();
		} else if (v == VOX_STOP)
			RADIO_EndTx();
	}
#endif
	dw_tick(ptt || s_tscan || SCAN_Active() || s_entry_len || s_screen != SCREEN_MAIN);
	if (RADIO_State() == RADIO_RX_OPEN)
		light_wake();
	else
		light_tick();
	if (ptt)
		SCAN_Stop();
	else if (SCAN_Tick(RADIO_State() == RADIO_RX_OPEN) == SCAN_HOP)
		scan_hop();

	/* battery about once a second, never while transmitting (the voltage sags) */
	if (++s_batt_ticks >= 1000 / APP_TICK_MS && RADIO_State() != RADIO_TX) {
		s_batt_ticks = 0;
		BATTERY_Set(ADC_Read(ADC_CH_BATTERY, 8));
	}

	k = key_event();
	/* while transmitting, a held digit, * or # sends its DTMF pair (stock 0x01E5457E / 0x01E5072C) */
	if (RADIO_State() == RADIO_TX && DTMFSEQ_Busy())
		k = KEY_INVALID;                   /* keys wait while a PTT-ID is sent */
	else if (RADIO_State() == RADIO_TX) {
		int d = KEY_Digit(s_key_held);

		if (s_key_held == KEY_STAR)
			d = 14;
		else if (s_key_held == KEY_HASH)
			d = 15;
		if (d >= 0) {
			if (k != KEY_INVALID)
				RADIO_TxTone((uint8_t)d, gStock.raw[SET_DTMF_MON] & 1u);
			k = KEY_INVALID;
		} else if (RADIO_TxToneOn())
			RADIO_TxToneOff();
	}
	if (k != KEY_INVALID && !s_light_on) {       /* the first press only lights the screen */
		light_wake();
		k = KEY_INVALID;
	}
	if (k != KEY_INVALID)
		light_wake();
	if (k != KEY_INVALID)
		dw_end();                          /* keys act on the active side */
#if SETTINGS_ENABLE_SAVE
	if (k != KEY_INVALID)
		save_soon();
	save_tick(ptt || s_peek || s_tscan || SCAN_Active() || s_entry_len);
#endif
	if (s_lamp_mode == 2 && (s_lamp_ms += APP_TICK_MS) >= LAMP_STROBE_MS) {
		s_lamp_ms = 0;
		IO_Lamp(!IO_LampIsOn());
	}
	if (k != KEY_INVALID && !ptt) {
		if (gStock.raw[SET_BEEP])
			RADIO_Beep(1000, 40);
		if (s_screen == SCREEN_MAIN)
			key_main(k);
		else
			key_menu(k);
	}

	if (s_screen == SCREEN_MAIN) {
		if (!s_peek || s_peek_held)
			draw_main();
	} else if (s_redraw)
		draw_menu();
	s_redraw = false;
}
