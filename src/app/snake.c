/* Open-UV5R-Mini: a snake game (ENABLE_APP_SNAKE), our own, for the games corner F4HWN's firmware has.
 *
 * 21 x 19 cells. 2/UP, 8/DOWN, 4, 6 steer; each food makes the snake one longer and the game a little faster.
 * Hitting a wall or itself ends the game; MENU starts again, EXIT goes back to the menu. Receive keeps working
 * underneath. */
#include "app/apps.h"
#if APP_SNAKE_ENABLE
#include "app/app.h"
#include "app/keys.h"
#include "ui/screen_apps.h"
#include "ui/theme.h"

#define SNAKE_MAX  (UI_GAME_W * UI_GAME_H)

static uint8_t  s_x[SNAKE_MAX], s_y[SNAKE_MAX];   /* a ring: s_head is the head, s_len cells back from it */
static uint16_t s_head, s_len;
static int8_t   s_dx, s_dy, s_ndx, s_ndy;
static uint8_t  s_fx, s_fy;
static uint16_t s_ms, s_every, s_score;
static bool     s_over;
static uint32_t s_rand;

static uint32_t rnd(void)
{
	s_rand = s_rand * 1103515245u + 12345u;
	return s_rand >> 16;
}

static bool on_snake(uint8_t x, uint8_t y)
{
	for (uint16_t i = 0; i < s_len; i++) {
		uint16_t k = (uint16_t)((s_head + SNAKE_MAX - i) % SNAKE_MAX);
		if (s_x[k] == x && s_y[k] == y)
			return true;
	}
	return false;
}

static void place_food(void)
{
	do {
		s_fx = (uint8_t)(rnd() % UI_GAME_W);
		s_fy = (uint8_t)(rnd() % UI_GAME_H);
	} while (on_snake(s_fx, s_fy) && s_len < SNAKE_MAX - 1);
	UI_GameCell(s_fx, s_fy, COLOR_TX);
}

static void score(void)
{
	char t[12], *p = t;
	*p++ = 'S'; *p++ = 'C'; *p++ = 'O'; *p++ = 'R'; *p++ = 'E'; *p++ = ' ';
	APPS_PutInt(p, s_score);
	UI_GameScore(t);
}

static void start(void)
{
	UI_GameFrame("SNAKE");
	s_len = 3;
	s_head = 2;
	for (uint16_t i = 0; i < 3; i++) {
		s_x[i] = (uint8_t)(8 + i);
		s_y[i] = UI_GAME_H / 2;
		UI_GameCell(s_x[i], s_y[i], i == 2 ? COLOR_ACCENT : COLOR_RX);
	}
	s_dx = s_ndx = 1;
	s_dy = s_ndy = 0;
	s_ms = 0;
	s_every = 160;
	s_score = 0;
	s_over = false;
	score();
	place_food();
}

static void enter(void)
{
	s_rand ^= gAppsUptimeMs * 2654435761u + 1u;
	s_over = true;                       /* drawn by the first tick */
}

static void leave(void) {}

static AppsResult_t key(KEY_Code_t k)
{
	if (k == KEY_ACT_EXIT)
		return APPS_EXIT;
	if (s_over) {
		if (k == KEY_ACT_MENU)
			start();
		return APPS_STAY;
	}
	/* a turn takes effect on the next step; no turning straight back */
	if ((k == KEY_2 || k == KEY_ACT_UP) && s_dy == 0)
		s_ndx = 0, s_ndy = -1;
	else if ((k == KEY_8 || k == KEY_ACT_DOWN) && s_dy == 0)
		s_ndx = 0, s_ndy = 1;
	else if (k == KEY_4 && s_dx == 0)
		s_ndx = -1, s_ndy = 0;
	else if (k == KEY_6 && s_dx == 0)
		s_ndx = 1, s_ndy = 0;
	return APPS_STAY;
}

static AppsResult_t tick(bool full)
{
	int nx, ny;
	uint16_t tail;

	if (full) {
		start();
		return APPS_STAY;
	}
	if (s_over || (s_ms = (uint16_t)(s_ms + APP_TICK_MS)) < s_every)
		return APPS_STAY;
	s_ms = 0;
	s_dx = s_ndx;
	s_dy = s_ndy;
	nx = s_x[s_head] + s_dx;
	ny = s_y[s_head] + s_dy;
	if (nx < 0 || ny < 0 || nx >= UI_GAME_W || ny >= UI_GAME_H || on_snake((uint8_t)nx, (uint8_t)ny)) {
		s_over = true;
		UI_GameScore("OVER MENU:GO");
		return APPS_STAY;
	}
	UI_GameCell(s_x[s_head], s_y[s_head], COLOR_RX);
	s_head = (uint16_t)((s_head + 1u) % SNAKE_MAX);
	s_x[s_head] = (uint8_t)nx;
	s_y[s_head] = (uint8_t)ny;
	UI_GameCell((uint8_t)nx, (uint8_t)ny, COLOR_ACCENT);
	if (nx == s_fx && ny == s_fy) {
		s_len++;
		s_score++;
		if (s_every > 70)
			s_every = (uint16_t)(s_every - 5u);
		score();
		place_food();
	} else {
		tail = (uint16_t)((s_head + SNAKE_MAX - s_len) % SNAKE_MAX);
		UI_GameCell(s_x[tail], s_y[tail], COLOR_BG);
	}
	return APPS_STAY;
}

const AppDef_t APP_SNAKE = { "SNAKE", enter, key, tick, leave, false, false, "2 4 6 8 STEER\nUP/DN   UP / DOWN\nMENU    PLAY AGAIN\nEXIT    LEAVE" };
#endif
