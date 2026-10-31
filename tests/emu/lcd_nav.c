/* Press buttons on the emulated panel and print the LCD after each step, so a
 * navigation path can be written down from what the device actually shows.
 *
 *   lcd_nav voice1 select3 pot2=127 shift+step12 step4 ...
 *
 * Names follow the printed panel: voice1..7 = DRUM 1..HH OPN, select1..8 =
 * OSC AEG MOD FM TRANS FILTER LFO MIX, mode1..4 = VOICE/STEP PERF/EUKLID LOAD
 * SAVE/MENU, step1..16, shift, enc+N / enc-N, encbtn, pot1..4=value.
 * SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lxr_api.h"
#include "buttonHandler.h"
#include "Soundengine/Sequencer/sequencer.h"

static const int step_but[16] = {
	BUT_SEQ1, BUT_SEQ2, BUT_SEQ3, BUT_SEQ4, BUT_SEQ5, BUT_SEQ6, BUT_SEQ7, BUT_SEQ8,
	BUT_SEQ9, BUT_SEQ10, BUT_SEQ11, BUT_SEQ12, BUT_SEQ13, BUT_SEQ14, BUT_SEQ15, BUT_SEQ16,
};
static const int voice_but[7] = { BUT_VOICE_1, BUT_VOICE_2, BUT_VOICE_3, BUT_VOICE_4,
                                  BUT_VOICE_5, BUT_VOICE_6, BUT_VOICE_7 };
static const int select_but[8] = { BUT_SELECT1, BUT_SELECT2, BUT_SELECT3, BUT_SELECT4,
                                   BUT_SELECT5, BUT_SELECT6, BUT_SELECT7, BUT_SELECT8 };
static const int mode_but[4] = { BUT_MODE1, BUT_MODE2, BUT_MODE3, BUT_MODE4 };

static void show(const char *what)
{
	const char *l = lxr_get_lcd();
	printf("  %-22s |%.16s|%.16s|   lengths", what, l, l + 16);
	for (int t = 0; t < 7; t++) printf(" %u", seq_getTrackLength((unsigned char)t));
	printf("\n");
}

static int press(const char *tok)   /* returns the button, or -1 */
{
	int n;
	if (sscanf(tok, "step%d", &n) == 1 && n >= 1 && n <= 16) return step_but[n - 1];
	if (sscanf(tok, "voice%d", &n) == 1 && n >= 1 && n <= 7) return voice_but[n - 1];
	if (sscanf(tok, "select%d", &n) == 1 && n >= 1 && n <= 8) return select_but[n - 1];
	if (sscanf(tok, "mode%d", &n) == 1 && n >= 1 && n <= 4) return mode_but[n - 1];
	if (!strcmp(tok, "shift")) return BUT_SHIFT;
	if (!strcmp(tok, "start")) return BUT_START_STOP;
	if (!strcmp(tok, "rec")) return BUT_REC;
	if (!strcmp(tok, "copy")) return BUT_COPY;
	if (!strcmp(tok, "pageup")) return BUT_PAGE_UP;
	if (!strcmp(tok, "pagedown")) return BUT_PAGE_DOWN;
	return -1;
}

int main(int argc, char **argv)
{
	if (lxr_init(1, 128) != 0) { printf("boot failed\n"); return 2; }
	lxr_idle_blocks(3000);
	show("after boot");

	for (int a = 1; a < argc; a++) {
		char *tok = argv[a];
		int n, v;
		if (sscanf(tok, "pot%d=%d", &n, &v) == 2) {
			lxr_pot_turn((unsigned char)(n - 1), v);
		} else if (sscanf(tok, "enc%d", &v) == 1) {
			lxr_encoder_turn(v);
		} else if (!strcmp(tok, "encbtn")) {
			lxr_encoder_button(1); lxr_idle_blocks(8); lxr_encoder_button(0);
		} else if (strchr(tok, '+')) {          /* modifier+button, held together */
			char a1[32], a2[32];
			sscanf(tok, "%31[^+]+%31s", a1, a2);
			const int m = press(a1), b = press(a2);
			if (m < 0 || b < 0) { printf("  ?? %s\n", tok); continue; }
			lxr_button((unsigned char)m, 1); lxr_idle_blocks(8);
			lxr_button((unsigned char)b, 1); lxr_idle_blocks(8);
			lxr_button((unsigned char)b, 0); lxr_idle_blocks(8);
			lxr_button((unsigned char)m, 0);
		} else {
			const int b = press(tok);
			if (b < 0) { printf("  ?? %s\n", tok); continue; }
			lxr_button_click((unsigned char)b);
		}
		lxr_idle_blocks(300);
		show(tok);
	}
	return 0;
}
