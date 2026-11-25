/*
 * What does the display show for the top MIDI octave? (#123)
 *
 * setNoteName() derives the octave character as 48 + num/12, and 48 is '0', so
 * notes 120 to 127 are octave 10 and print ':'. The function is static, so
 * this goes through the display instead: PAR_MIDI_NOTE1 is a DTYPE_NOTE_NAME
 * parameter on DRUM 1's MIX page, second screen.
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

static const char *lcd_line2(void) { return lxr_get_lcd() + 16; }

static void press(int but)
{
	lxr_button((unsigned char)but, 1); lxr_idle_blocks(8);
	lxr_button((unsigned char)but, 0); lxr_idle_blocks(200);
}

int main(void)
{
	int failures = 0;
	const int notes[] = { 60, 108, 119, 120, 127 };

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(2000);

	press(BUT_VOICE_1);
	press(BUT_SELECT8);          /* MIX */
	press(BUT_SELECT8);          /* its second screen, where the note lives */
	printf("-- page |%.16s|%.16s|\n", lxr_get_lcd(), lcd_line2());

	for (unsigned i = 0; i < sizeof notes / sizeof *notes; i++) {
		lxr_set_param(PAR_MIDI_NOTE1, notes[i]);
		lxr_idle_blocks(300);
		const char *l = lcd_line2();
		printf("   note %3d -> |%.16s|", notes[i], l);
		if (strchr(l, ':')) {
			printf("   <- ':' where the octave digit belongs");
			failures++;
		}
		printf("\n");
	}

	printf("\n%s\n", failures ? "FAILURES - the top octave is not a digit"
	                          : "all checks passed - no ':' at any note");
	return failures ? 1 : 0;
}
