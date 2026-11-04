/*
 * What does the display do with a value outside a name table? (#84, U-A2/U-D)
 *
 * Every name table keeps its entry count in its first element, and
 * getMenuItemNameForValue() indexed the table without consulting it. A value
 * can be out of range without any file or panel gesture being at fault: a MIDI
 * CC, an NRPN, or a kit written by older firmware. The renderer then reads
 * past the table and puts whatever it finds on the LCD.
 *
 * Every parameter is set to 127 and then every menu page is walked. The
 * assertion is mechanical: a name table read past its end yields arbitrary
 * bytes, so every character the LCD shows must be printable ASCII.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "lxr_api.h"
#include "buttonHandler.h"
#include "Parameters.h"
#include "menu/menu.h"

static int failures, pages, bad_chars;

static const int voice_but[7] = { BUT_VOICE_1, BUT_VOICE_2, BUT_VOICE_3, BUT_VOICE_4,
                                  BUT_VOICE_5, BUT_VOICE_6, BUT_VOICE_7 };
static const int select_but[8] = { BUT_SELECT1, BUT_SELECT2, BUT_SELECT3, BUT_SELECT4,
                                   BUT_SELECT5, BUT_SELECT6, BUT_SELECT7, BUT_SELECT8 };
static const int step_but[16] = {
	BUT_SEQ1, BUT_SEQ2, BUT_SEQ3, BUT_SEQ4, BUT_SEQ5, BUT_SEQ6, BUT_SEQ7, BUT_SEQ8,
	BUT_SEQ9, BUT_SEQ10, BUT_SEQ11, BUT_SEQ12, BUT_SEQ13, BUT_SEQ14, BUT_SEQ15, BUT_SEQ16,
};

/* Every character on both rows has to be printable: that is what distinguishes
   a name from a table read past its end. */
static void check_lcd(const char *what)
{
	const char *l = lxr_get_lcd();
	int bad = 0;
	char shown[33];

	for (int i = 0; i < 32; i++) {
		const unsigned char c = (unsigned char)l[i];
		shown[i] = (c >= 0x20 && c <= 0x7E) ? (char)c : '?';
		if (c < 0x20 || c > 0x7E)
			bad++;
	}
	shown[32] = 0;
	pages++;
	{
		const uint8_t sub = menu_getSubPage();
		if (sub >= NUM_SUB_PAGES) {
			printf("    %-22s sub-page index %u, table has %d\n", what, sub, NUM_SUB_PAGES);
			failures++;
		}
	}
	if (bad) {
		printf("    %-22s |%.16s|%.16s|  %d byte(s) not printable\n",
		       what, shown, shown + 16, bad);
		bad_chars += bad;
		failures++;
	}
}

static void click(int but) { lxr_button_click((uint8_t)but); lxr_idle_blocks(200); }
static void hold(int mod, int but)
{
	lxr_button((uint8_t)mod, 1); lxr_idle_blocks(8);
	lxr_button((uint8_t)but, 1); lxr_idle_blocks(8);
	lxr_button((uint8_t)but, 0); lxr_idle_blocks(8);
	lxr_button((uint8_t)mod, 0); lxr_idle_blocks(200);
}

int main(void)
{
	char label[32];

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(3000);

	printf("-- setting all %d sound parameters to 127\n", lxr_num_sound_params());
	for (int i = 0; i < lxr_num_sound_params(); i++)
		lxr_set_param(i, 127);
	lxr_idle_blocks(600);

	puts("-- the eight sub-pages of each of the seven voices, both screens");
	for (int v = 0; v < 7; v++) {
		click(voice_but[v]);
		for (int sel = 0; sel < 8; sel++) {
			click(select_but[sel]);
			sprintf(label, "voice %d select %d", v + 1, sel + 1);
			check_lcd(label);
			click(select_but[sel]);          /* same button again: 2nd screen */
			sprintf(label, "voice %d select %d'", v + 1, sel + 1);
			check_lcd(label);
		}
	}

	puts("-- the shifted step pages: FX, CONFIG, LAST STEP, PATTERN");
	for (int i = 0; i < 4; i++) {
		static const int which[4] = { 14, 11, 13, 15 };   /* FX, CONFIG, LAST STEP, PATTERN */
		static const char *name[4] = { "FX", "CONFIG", "LAST STEP", "PATTERN" };
		for (int page = 0; page < 3; page++) {
			hold(BUT_SHIFT, step_but[which[i]]);
			sprintf(label, "%s page %d", name[i], page + 1);
			check_lcd(label);
		}
	}

	puts("-- the step edit pages, where flam, shift and condition are rendered");
	click(BUT_MODE1);
	for (int s = 0; s < 4; s++) {
		click(step_but[s]);
		for (int sel = 0; sel < 8; sel++) {
			click(select_but[sel]);
			sprintf(label, "step %d select %d", s + 1, sel + 1);
			check_lcd(label);
		}
	}

	printf("\n%d pages rendered, %d non-printable bytes\n", pages, bad_chars);
	printf(failures ? "%d page(s) showed bytes from past a name table\n"
			: "every page printable\n", failures);
	return failures ? 1 : 0;
}
