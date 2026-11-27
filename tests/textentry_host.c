/* Open-UV5R-Mini: tests for the typing service (src/ui/textentry.c), run on the PC by tests/textentry_host.py. */
#include <stdio.h>
#include <string.h>
#include "ui/textentry.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static TextEntry_t t;
static uint32_t now;

static TextResult_t press(KEY_Code_t k, uint32_t gap)
{
	now += gap;
	return TEXT_Key(&t, k, now);
}

int main(void)
{
	char b[12];
	char line[24];

	/* letters first: 2 2 = B, a pause, 2 = A; 7 x4 = S; another key moves on at once */
	b[0] = 0;
	TEXT_Start(&t, b, 9, TEXT_CALL);
	CHECK(gTextActive && !strcmp(TEXT_ModeName(&t), "ABC"));
	press(KEY_2, 0); press(KEY_2, 300);
	CHECK(!strcmp(b, "B"));
	press(KEY_2, 1500);
	CHECK(!strcmp(b, "BA"));
	press(KEY_7, 100); press(KEY_7, 100); press(KEY_7, 100); press(KEY_7, 100);
	CHECK(!strcmp(b, "BAS"));
	press(KEY_7, 100);                   /* the 5th press: the digit */
	CHECK(!strcmp(b, "BA7"));
	press(KEY_7, 100);                   /* round again */
	CHECK(!strcmp(b, "BAP"));
	press(KEY_3, 100);
	CHECK(!strcmp(b, "BAPD"));

	/* F: numbers; F again: letters */
	press(KEY_HASH, 100);
	CHECK(!strcmp(TEXT_ModeName(&t), "123"));
	press(KEY_1, 100); press(KEY_1, 100);
	CHECK(!strcmp(b, "BAPD11"));
	press(KEY_HASH, 100);
	press(KEY_1, 100);                   /* a call sign: of key 1's marks only '-' */
	CHECK(!strcmp(b, "BAPD11-"));
	press(KEY_1, 100);
	CHECK(!strcmp(b, "BAPD11-") == 0 && !strcmp(b, "BAPD111"));
	press(KEY_0, 2000);                  /* no space in a call sign: 0 */
	CHECK(!strcmp(b, "BAPD1110"));

	/* * deletes; a full field takes no more */
	press(KEY_STAR, 100);
	CHECK(!strcmp(b, "BAPD111"));
	press(KEY_4, 2000); press(KEY_5, 100); press(KEY_6, 100);
	CHECK(t.len == 9 && !strcmp(b, "BAPD111GJ"));
	TEXT_Line(&t, line, 6);
	CHECK(!strcmp(line, "D111GJ"));        /* full: no cursor */
	press(KEY_STAR, 100);
	TEXT_Line(&t, line, 6);
	CHECK(!strcmp(line, "D111G_"));

	/* UP / DOWN go through what the field allows */
	press(KEY_UP, 100);
	CHECK(b[t.len - 1] == 'H');
	press(KEY_DOWN, 100); press(KEY_DOWN, 100);
	CHECK(b[t.len - 1] == 'F');

	/* MENU done, EXIT cancel */
	CHECK(press(KEY_MENU, 100) == TEXT_DONE && !gTextActive);
	TEXT_Start(&t, b, 9, TEXT_CALL);
	CHECK(t.len == 8 && gTextActive && !strcmp(TEXT_ModeName(&t), "ABC"));
	CHECK(press(KEY_EXIT, 100) == TEXT_CANCEL && !gTextActive);

	/* a name: a space on 0, marks on 1 */
	strcpy(b, "");
	TEXT_Start(&t, b, 11, TEXT_NAME);
	press(KEY_4, 100); press(KEY_4, 100); press(KEY_0, 2000); press(KEY_1, 2000); press(KEY_1, 100);
	CHECK(!strcmp(b, "H -"));
	press(KEY_UP, 100);                  /* after '-' comes '.' */
	CHECK(!strcmp(b, "H ."));
	TEXT_Line(&t, line, 21);
	CHECK(!strcmp(line, "H ._"));

	/* free text: punctuation on 1 */
	strcpy(b, "");
	TEXT_Start(&t, b, 11, TEXT_FREE);
	press(KEY_1, 100); press(KEY_1, 100); press(KEY_1, 100); press(KEY_1, 100);
	CHECK(!strcmp(b, ","));
	press(KEY_1, 100);
	CHECK(!strcmp(b, "?"));
	press(KEY_UP, 2000);                 /* an empty field: UP starts at the first character */
	press(KEY_STAR, 100); press(KEY_STAR, 100); press(KEY_STAR, 100);
	CHECK(t.len == 0 && b[0] == 0);
	press(KEY_UP, 100);
	CHECK(!strcmp(b, " "));
	TEXT_End();

	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
