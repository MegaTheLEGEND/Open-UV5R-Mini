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
static char     s_entry[7];              /* digits typed, MHz then kHz */
static uint8_t  s_entry_len;
static uint8_t  s_menu_sel, s_menu_edit;
static int      s_menu_top = -1;
static uint16_t s_off_ticks, s_batt_ticks;
static int      s_shown_batt = -1;
static bool     s_mr;                    /* channel mode */
static int      s_ch = -1;               /* channel slot shown, 0-based */
static char     s_ch_name[13];
static bool     s_xf_ok;                 /* external flash answered */
static bool     s_ptt_down, s_tx_refused;

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
	s_shown_batt = -1;
}

static void draw_status(void)
{
	char b[12];
	int st = gVfo.squelch | gVfo.volume << 4 | gVfo.narrow << 8 | RADIO_IsAm() << 9 | gVfo.step << 10
	         | IO_LampIsOn() << 14 | gVfo.power << 15 | (int)(gVfo.tx_tone ^ gVfo.rx_tone << 3) << 16;

	if (st == s_shown_status && !s_redraw)
		return;
	s_shown_status = st;
	ST7735S_FillRect(0, 0, 128, 10, BLACK);
	ST7735S_FillRect(0, 54, 128, 10, BLACK);
	UI_PrintString(RADIO_IsAm() ? "AM" : "FM", 0, 1, BLUE, BLACK, 1);
	UI_PrintString(gVfo.narrow || RADIO_IsAm() ? "N" : "W", 16, 1, BLUE, BLACK, 1);
	UI_PrintString("SQ", 30, 1, WHITE, BLACK, 1);
	UI_PrintString(UI_FormatDec(b, gVfo.squelch, 1), 42, 1, YELLOW, BLACK, 1);
	UI_PrintString("V", 54, 1, WHITE, BLACK, 1);
	UI_PrintString(UI_FormatDec(b, gVfo.volume, 2), 60, 1, YELLOW, BLACK, 1);
	UI_PrintString(gVfo.power == POWER_LOW ? "L" : "H", 76, 1, BLUE, BLACK, 1);
	if (IO_LampIsOn())
		UI_PrintString("LAMP", 100, 1, YELLOW, BLACK, 1);
	else if (!CALIB_Present())
		UI_PrintString("NOCAL", 94, 1, RED, BLACK, 1);
	if (s_mr) {
		UI_PrintString("CH", 0, 55, GREY, BLACK, 1);
		UI_PrintString(s_ch >= 0 ? UI_FormatDec(b, (uint32_t)s_ch + 1, 3) : "---", 14, 55, WHITE, BLACK, 1);
		UI_PrintString(s_ch_name, 36, 55, YELLOW, BLACK, 1);
	} else {
		UI_PrintString("STEP", 0, 55, GREY, BLACK, 1);
		UI_PrintString(fmt_step(b, RADIO_Steps[gVfo.step]), 30, 55, WHITE, BLACK, 1);
	}
	ST7735S_FillRect(0, 12, 128, 8, BLACK);
	if (gVfo.rx_tone) {
		UI_PrintString("R", 0, 12, GREY, BLACK, 1);
		UI_PrintString(TONE_Format(b, gVfo.rx_tone), 8, 12, WHITE, BLACK, 1);
	}
	if (gVfo.tx_tone) {
		UI_PrintString("T", 60, 12, GREY, BLACK, 1);
		UI_PrintString(TONE_Format(b, gVfo.tx_tone), 68, 12, WHITE, BLACK, 1);
	}
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
	if (gVfo.freq == s_shown_freq && !s_redraw)
		return;
	s_shown_freq = gVfo.freq;
	UI_PrintString(fmt_freq(b, gVfo.freq), 10, 26, WHITE, BLACK, 2);
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

/* Battery, bottom right: volts as the stock voltage screen shows them, red at the lowest calibrated level. */
static void draw_battery(void)
{
	char b[8];
	int t = BATTERY_Tenths(), lvl = BATTERY_Level();

	if (t == s_shown_batt)
		return;
	s_shown_batt = t;
	b[0] = (char)('0' + t / 10 % 10);
	b[1] = '.';
	b[2] = (char)('0' + t % 10);
	b[3] = 'V';
	b[4] = 0;
	UI_PrintString(BATTERY_Raw() ? b : "    ", 104, 120, lvl == 0 ? RED : GREEN, BLACK, 1);
}

static void draw_main(void)
{
	if (s_redraw)
		draw_main_static();
	draw_battery();
	draw_status();
	draw_freq();
	draw_signal();
}

/* ------------------------------------------------------------------------------------------- menu */
enum { M_MEM, M_SQL, M_VOL, M_STEP, M_BW, M_MOD, M_RXTONE, M_TXTONE, M_POWER, M_KEYTEST, M_COUNT };
static const char *const MENU_NAMES[M_COUNT] = {
	"MEMORY", "SQUELCH", "VOLUME", "STEP", "BANDWIDTH", "MODULATION", "RX TONE", "TX TONE", "TX POWER",
	"KEY TEST",
};
#define MENU_ROWS 8                      /* rows on screen; the list scrolls */

static const char *menu_value(int i, char *b)
{
	static const char *const MOD[] = { "AUTO", "FM", "AM" };

	switch (i) {
	case M_MEM:     return s_mr ? "CHANNELS" : "VFO";
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

static void key_main(KEY_Code_t k)
{
	int d = KEY_Digit(k);

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
	} else if (k == KEY_ACT_LAMP)
		IO_Lamp(!IO_LampIsOn());
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
static KEY_Code_t key_event(void)
{
	static KEY_Code_t last = KEY_INVALID;
	static uint16_t held;
	KEY_Code_t k = KEYBOARD_Poll();

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
static void start_from_stock(void)
{
	SETTINGS_Defaults();
	if (!s_xf_ok || !SETTINGS_Load())
		return;
	if (gStock.raw[SET_SQUELCH] <= 9)
		gVfo.squelch = gStock.raw[SET_SQUELCH];
	if (gStock.have_vfo) {
		const StockVfo_t *v = &gStock.vfo[gStock.raw[SET_ACTIVE_SIDE] ? 1 : 0];
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
	if ((gStock.raw[SET_WORK_MODE] >> (gStock.raw[SET_ACTIVE_SIDE] ? 4 : 0) & 0xFu) == 1) {
		int n = CHANNEL_Next(-1, +1);
		if (n >= 0) {
			s_mr = true;
			channel_show(n);
		}
	}
}

void APP_Init(void)
{
	BOARD_Init();
	ST7735S_Init();
	KEYBOARD_Init();
	if (UPDATE_Requested())
		UPDATE_Mode();          /* PTT-A + '=' at power-on, as the stock app; does not return */
	BK4819_Init();
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

	/* PTT */
	ptt = KEYBOARD_IsPttPressed();
	if (ptt && !s_ptt_down) {
		s_tx_refused = !RADIO_StartTx();
		if (s_tx_refused)
			RADIO_Beep(400, 150);
		s_shown_state = -1;
	} else if (!ptt && s_ptt_down) {
		RADIO_EndTx();
		s_tx_refused = false;
		s_shown_state = -1;
	}
	s_ptt_down = ptt;

	RADIO_Tick();

	/* battery about once a second, never while transmitting (the voltage sags) */
	if (++s_batt_ticks >= 1000 / APP_TICK_MS && RADIO_State() != RADIO_TX) {
		s_batt_ticks = 0;
		BATTERY_Set(ADC_Read(ADC_CH_BATTERY, 8));
	}

	k = key_event();
	if (k != KEY_INVALID && !ptt) {
		if (gStock.raw[SET_BEEP])
			RADIO_Beep(1000, 40);
		if (s_screen == SCREEN_MAIN)
			key_main(k);
		else
			key_menu(k);
	}

	if (s_screen == SCREEN_MAIN)
		draw_main();
	else if (s_redraw)
		draw_menu();
	s_redraw = false;
}
