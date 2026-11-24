/* Open-UV5R-Mini: channel book (ENABLE_APP_CHANNELS), after F4HWN's ChannelBook app.
 *
 * The filled channels in a list ("012 CITY RPTR"; the frequency when a channel has no name), drawn with the menu
 * screen; the selected one's frequency large underneath. UP/DOWN move, a 3-digit number jumps to that channel,
 * MENU goes to the main screen on it (channel mode), EXIT back to the menu. Reads the channels from the external
 * flash as it goes (CHANNEL_Next, CHANNEL_Load), so a sparse list costs a few reads a key. PC-checked only. */
#include "app/apps.h"
#if APP_CHANNELS_ENABLE
#include "app/keys.h"
#include "store/channels.h"
#include "ui/screen_main.h"
#include "ui/screen_menu.h"

static int     s_sel = -1, s_top = -1;   /* channel slots, 0-based */
static int     s_typed, s_typed_n;
static char    s_rows[UI_MENU_ROWS][20];
static char    s_value[12];

static void row_text(char *b, int n, const Channel_t *c)
{
	char *p = b;
	unsigned k = (unsigned)n + 1u;

	*p++ = (char)('0' + k / 100u);
	*p++ = (char)('0' + k / 10u % 10u);
	*p++ = (char)('0' + k % 10u);
	*p++ = ' ';
	if (c->name[0]) {
		for (int i = 0; i < 12 && c->name[i]; i++)
			*p++ = c->name[i];
		*p = 0;
	} else
		APPS_FormatMhz(p, c->rx);
}

static void draw(bool full)
{
	UiMenu_t m;
	int n = s_top, r = 0, sel_row = 0;
	char *p;

	for (int i = 0; i < UI_MENU_ROWS; i++) {
		m.name[i] = m.right[i] = 0;
		m.num[i] = 0;
		m.hidden[i] = false;
	}
	m.title = "CHANNELS";
	m.edit = false;
	m.cursor = -1;
	m.corner_warn = s_typed_n != 0;
	m.top = 0;
	s_value[0] = 0;
	if (s_sel < 0) {
		m.count = 1;
		m.sel = 0;
		m.name[0] = gAppsFlashOk ? "NO CHANNELS" : "NO FLASH";
		m.value = "";
		m.corner[0] = 0;
		UI_MenuDraw(&m, full);
		return;
	}
	/* rows from s_top, at most UI_MENU_ROWS, stopping when the list wraps round */
	while (r < UI_MENU_ROWS && n >= 0) {
		Channel_t c;
		if (!CHANNEL_Load((uint16_t)n, &c))
			break;
		row_text(s_rows[r], n, &c);
		m.name[r] = s_rows[r];
		if (n == s_sel) {
			sel_row = r;
			UI_FormatFreq(s_value, c.rx);
		}
		r++;
		{
			int next = CHANNEL_Next(n, +1);
			if (next <= n)
				break;
			n = next;
		}
	}
	m.count = (uint8_t)r;
	m.sel = (uint8_t)sel_row;
	m.value = s_value;
	p = m.corner;
	if (s_typed_n) {
		p = APPS_PutInt(p, s_typed);
		*p++ = '_';
		*p = 0;
	} else {
		*p++ = 'C'; *p++ = 'H'; *p++ = ' ';
		APPS_PutInt(p, s_sel + 1);
	}
	UI_MenuDraw(&m, full);
}

/* keep s_sel on screen: s_top moves so s_sel is within the rows drawn */
static void follow(void)
{
	int n = s_top, k = 0;

	if (s_top < 0 || s_sel < s_top) {
		s_top = s_sel;
		return;
	}
	while (n >= 0 && n != s_sel && k < UI_MENU_ROWS - 1) {
		int next = CHANNEL_Next(n, +1);
		if (next <= n)
			break;
		n = next;
		k++;
	}
	if (n != s_sel) {                     /* below the last row: scroll so it is the last */
		int t = s_sel;
		for (int i = 0; i < UI_MENU_ROWS - 1; i++) {
			int prev = CHANNEL_Next(t, -1);
			if (prev < 0 || prev >= t)
				break;
			t = prev;
		}
		s_top = t;
	}
}

static void enter(void)
{
	s_typed_n = 0;
	if (!gAppsFlashOk) {
		s_sel = s_top = -1;
		return;
	}
	if (s_sel < 0 || CHANNEL_Next(s_sel - 1, +1) != s_sel)
		s_sel = CHANNEL_Next(-1, +1);
	s_top = -1;
	if (s_sel >= 0)
		follow();
}

static void leave(void) {}

static AppsResult_t key(KEY_Code_t k)
{
	int d = KEY_Digit(k);

	if (k == KEY_ACT_EXIT) {
		if (s_typed_n) {
			s_typed_n = 0;
			draw(false);
			return APPS_STAY;
		}
		return APPS_EXIT;
	}
	if (s_sel < 0)
		return APPS_STAY;
	if (d >= 0) {
		s_typed = s_typed_n ? s_typed * 10 + d : d;
		if (++s_typed_n == 3) {
			s_typed_n = 0;
			if (s_typed >= 1 && s_typed <= (int)CHANNEL_COUNT && CHANNEL_Next(s_typed - 2, +1) == s_typed - 1) {
				s_sel = s_typed - 1;
				follow();
			}
		}
	} else if (k == KEY_ACT_MENU) {
		gAppsChannel = s_sel;
		return APPS_CHANNEL;
	} else if (k == KEY_ACT_UP || k == KEY_ACT_DOWN) {
		int n = CHANNEL_Next(s_sel, k == KEY_ACT_UP ? -1 : +1);
		if (n >= 0) {
			s_sel = n;
			follow();
		}
	}
	draw(false);
	return APPS_STAY;
}

static AppsResult_t tick(bool full)
{
	if (full)
		draw(true);
	return APPS_STAY;
}

const AppDef_t APP_CHANNELS = { "CHANNELS", enter, key, tick, leave };
#endif
