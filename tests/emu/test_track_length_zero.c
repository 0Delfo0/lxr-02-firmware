/*
 * A stored track length of 0 means 16. Do all the readers agree? (#99)
 *
 * Five sites remapped it with their own "if(length == 0) length = 16"; three
 * did not. seq_processFlam() took (step+1) % 0 and length-1 = 255, and
 * seq_addNote() took quantizedStep % 0. On x86 a modulo by zero is SIGFPE, so
 * a build without the fix dies here; on the Cortex-M7 UDIV returns 0 unless
 * DIV_0_TRP is set, which this firmware does not set, so on the device the
 * step number silently becomes 0 and the flam decision is taken from step 255
 * of a 64-step track.
 *
 * 0 is a value to read, not to reject: the Euclidean generator writes it (#23)
 * and .pat files carry it, which is why preset_validLength() leaves it alone.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include "lxr_api.h"
#include "buttonHandler.h"
#include "Soundengine/Sequencer/sequencer.h"

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(3000);

	puts("-- every track of the active pattern gets a stored length of 0");
	for (int trk = 0; trk < NUM_TRACKS; trk++)
		seq_patternSet.length[seq_activePattern][trk] = 0;
	printf("    seq_getTrackLength reports:");
	for (int trk = 0; trk < NUM_TRACKS; trk++)
		printf(" %u", seq_getTrackLength((uint8_t)trk));
	printf("\n");

	puts("-- start the sequencer and let it run, which is where the modulo is");
	lxr_button_click(BUT_START_STOP);
	{
		static float block[256 * 2];
		for (int i = 0; i < 400; i++)
			lxr_render(block, 256, 2);
	}
	printf("    running: %u\n", seq_isRunning());

	puts("-- and call the flam handler directly, the site that divides");
	fflush(stdout);
	seq_processFlam(0);
	puts("    returned");

	puts("-- a recorded note, the other divisor (seq_addNote)");
	seq_addNote(0, 100, 60);
	puts("    returned");

	lxr_button_click(BUT_START_STOP);

	puts("-- the length is still stored as 0: nothing rewrote it");
	for (int trk = 0; trk < NUM_TRACKS; trk++)
		if (seq_patternSet.length[seq_activePattern][trk] != 0) {
			printf("FAIL - track %d now stores %u; 0 has a meaning and must survive\n",
			       trk, seq_patternSet.length[seq_activePattern][trk]);
			failures++;
		}

	printf(failures ? "\n%d failure(s)\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
