/*
 * What does the display show for a note name? (#123 and #126)
 *
 * Two defects in setNoteName(), fixed separately. #123: the octave character
 * was 48 + num/12, so notes 120 to 127 were octave 10 and printed ':'. #126:
 * the octaves counted from note 0 = C 0, one higher than every host, where
 * note 0 is C-1 and 60 is C4.
 *
 * setNoteName() is static, so this goes through the display: PAR_MIDI_NOTE1 is
 * a DTYPE_NOTE_NAME parameter on DRUM 1's MIX page, second screen, which reads
 * "OUT len ch  nte" and puts the name in columns 12 to 14 of the second line.
 *
 * The lowercase letters are not a typo: the field is exactly three characters,
 * so C#-1 does not fit and a lowercase letter carries the sharp in octave -1.
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

#define NOTE_COL 12

static void press(int but)
{
	lxr_button((unsigned char)but, 1); lxr_idle_blocks(8);
	lxr_button((unsigned char)but, 0); lxr_idle_blocks(200);
}

int main(void)
{
	int failures = 0;
	struct { int note; const char *want; const char *why; } cases[] = {
		/* note 0 is not in the table: on the voice note parameters 0 means
		   "any", and the display shows that instead of a name (menu.c:1732) */
		{   1, "c-1", "C#-1: lowercase carries the sharp, three columns only" },
		{   3, "d-1", "D#-1" },
		{   6, "f-1", "F#-1" },
		{  10, "a-1", "A#-1" },
		{  11, "B-1", "a natural keeps its capital" },
		{  12, "C 0", "the first octave with room for a digit" },
		{  60, "C 4", "middle C, as a DAW shows it" },
		{  61, "C#4", "a sharp above octave -1 is unchanged" },
		{ 119, "B 8", "" },
		{ 120, "C 9", "octave 10 used to print ':' here (#123)" },
		{ 127, "G 9", "the top note" },
	};

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(2000);

	press(BUT_VOICE_1);
	press(BUT_SELECT8);          /* MIX */
	press(BUT_SELECT8);          /* its second screen */
	printf("-- page |%.16s|%.16s|\n\n", lxr_get_lcd(), lxr_get_lcd() + 16);

	for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
		lxr_set_param(PAR_MIDI_NOTE1, cases[i].note);
		lxr_idle_blocks(300);
		const char *got = lxr_get_lcd() + 16 + NOTE_COL;
		const int ok = memcmp(got, cases[i].want, 3) == 0;
		printf("   note %3d -> |%.3s|  want |%s|  %-8s %s\n", cases[i].note, got,
		       cases[i].want, ok ? "ok" : "MISMATCH", cases[i].why);
		if (!ok) failures++;
	}

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
