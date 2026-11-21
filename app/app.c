/* Open-UV5R-Mini: the radio application: main screen, frequency entry, menu, PTT.
 *
 * Laid out the way the UV-K5 open firmware splits a radio (a radio core in app/radio.c, screens drawn from state,
 * keys turned into actions), written fresh for this radio. Not tried on a radio yet. The cable update mode is kept:
 * PTT-A + '=' at power-on, as stock (app/update_radio.c).
 */
#include <stdbool.h>
#include <stdint.h>
#include "app/app.h"
#include "app/keys.h"
#include "app/keytest.h"
#include "app/radio.h"
#include "app/update.h"
#include "board.h"
#include "bt/bt.h"
#include "driver/bk4819.h"
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
static uint16_t s_off_ticks;
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

static char *fmt_tenths(char *b, uint32_t v)      /* 885 -> "88.5" */
{
	char *p = b, t[8];
	int n = 0;
	uint32_t w = v / 10;

	do
		t[n++] = (char)('0' + w % 10);
	while ((w /= 10) && n < 6);
	while (n)
		*p++ = t[--n];
	*p++ = '.';
	*p++ = (char)('0' + v % 10);
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
}

static void draw_status(void)
{
	char b[12];
	int st = gVfo.squelch | gVfo.volume << 4 | gVfo.narrow << 8 | RADIO_IsAm() << 9 | gVfo.step << 10
	         | IO_LampIsOn() << 14 | gVfo.ctcss_tx << 16;

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
	if (IO_LampIsOn())
		UI_PrintString("LAMP", 100, 1, YELLOW, BLACK, 1);
	UI_PrintString("STEP", 0, 55, GREY, BLACK, 1);
	UI_PrintString(fmt_step(b, RADIO_Steps[gVfo.step]), 30, 55, WHITE, BLACK, 1);
	if (gVfo.ctcss_tx) {
		UI_PrintString("CT", 76, 55, GREY, BLACK, 1);
		UI_PrintString(fmt_tenths(b, RADIO_Ctcss[gVfo.ctcss_tx - 1]), 90, 55, WHITE, BLACK, 1);
	}
}

static void draw_freq(void)
{
	char b[12];

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

static void draw_main(void)
{
	if (s_redraw)
		draw_main_static();
	draw_status();
	draw_freq();
	draw_signal();
}

/* ------------------------------------------------------------------------------------------- menu */
enum { M_SQL, M_VOL, M_STEP, M_BW, M_MOD, M_CTCSS, M_POWER, M_KEYTEST, M_COUNT };
static const char *const MENU_NAMES[M_COUNT] = {
	"SQUELCH", "VOLUME", "STEP", "BANDWIDTH", "MODULATION", "CTCSS TX", "TX POWER", "KEY TEST",
};

static const char *menu_value(int i, char *b)
{
	static const char *const MOD[] = { "AUTO", "FM", "AM" };

	switch (i) {
	case M_SQL:     return UI_FormatDec(b, gVfo.squelch, 1);
	case M_VOL:     return UI_FormatDec(b, gVfo.volume, 2);
	case M_STEP:    return fmt_step(b, RADIO_Steps[gVfo.step]);
	case M_BW:      return gVfo.narrow ? "NARROW" : "WIDE";
	case M_MOD:     return MOD[gVfo.mod];
	case M_CTCSS:   return gVfo.ctcss_tx ? fmt_tenths(b, RADIO_Ctcss[gVfo.ctcss_tx - 1]) : "OFF";
	case M_POWER:   return RADIO_ENABLE_TX ? (gVfo.power ? "HIGH" : "LOW") : "TX OFF";
	default:        return "";
	}
}

static void menu_change(int i, int dir)
{
	switch (i) {
	case M_SQL:   gVfo.squelch = (uint8_t)((gVfo.squelch + 10 + dir) % 10); break;
	case M_VOL:   gVfo.volume = (uint8_t)((gVfo.volume + 16 + dir) % 16); break;
	case M_STEP:  gVfo.step = (uint8_t)((gVfo.step + RADIO_StepCount + dir) % RADIO_StepCount); break;
	case M_BW:    gVfo.narrow = !gVfo.narrow; break;
	case M_MOD:   gVfo.mod = (Modulation_t)((gVfo.mod + 3 + dir) % 3); break;
	case M_CTCSS: gVfo.ctcss_tx = (uint8_t)((gVfo.ctcss_tx + RADIO_CtcssCount + 1 + dir) % (RADIO_CtcssCount + 1)); break;
	case M_POWER: gVfo.power = 0; break;    /* low only until the calibration is understood */
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
	for (int i = 0; i < M_COUNT; i++) {
		uint8_t y = (uint8_t)(16 + 12 * i);
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

static void entry_finish(void)
{
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
		if (s_entry_len == 6)
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
	if (k == KEY_ACT_UP)
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
void APP_Init(void)
{
	BOARD_Init();
	ST7735S_Init();
	KEYBOARD_Init();
	if (UPDATE_Requested())
		UPDATE_Mode();          /* PTT-A + '=' at power-on, as the stock app; does not return */
	BK4819_Init();
	RADIO_Init();
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

	k = key_event();
	if (k != KEY_INVALID && !ptt) {
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
