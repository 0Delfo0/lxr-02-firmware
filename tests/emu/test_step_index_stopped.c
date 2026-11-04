/*
 * What happens when a voice is triggered with the sequencer stopped? (#82)
 *
 * seq_stepIndex is -1 then, and two things used it as an index anyway:
 * seq_triggerVoice() read the current step's automation and velocity, and
 * seq_recordAutomation() assigned it to a uint8_t - so -1 became 255 and
 * seq_intIsMainStepActive() was asked for bar 15 of 4.
 *
 * The roll is the way in: seq_setRoll() fires the voice through the sequencer
 * when it is stopped, which is also why #77 exposes it over SysEx.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "lxr_api.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Parameters.h"
#include "menu/menu.h"

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(3000);

	printf("-- sequencer stopped: running = %u\n", seq_isRunning());
	if (seq_isRunning()) { puts("FAIL - expected it stopped after boot"); return 2; }

	puts("-- roll voice 0, which triggers it through the sequencer");
	fflush(stdout);
	seq_setRoll(0, 1);
	lxr_idle_blocks(100);
	seq_setRoll(0, 0);
	puts("    returned");

	puts("-- the same for every voice, since each has its own step index");
	for (int v = 0; v < NUM_TRACKS; v++) {
		seq_setRoll((uint8_t)v, 1);
		lxr_idle_blocks(20);
		seq_setRoll((uint8_t)v, 0);
	}
	puts("    returned");

	puts("-- REC armed with the sequencer stopped, then a parameter edit");
	{
		/* The steps are not blank after boot, so the question is what changed,
		   not what is non-zero. */
		static Step before[NUM_TRACKS][NUM_STEPS];
		int changed = 0;
		memcpy(before, seq_patternSet.seq_subStepPattern[seq_activePattern],
		       sizeof before);
		seq_setRecordingMode(1);
		fflush(stdout);
		seq_recordAutomation(0, PAR_VELOD3, 77);
		lxr_idle_blocks(50);
		seq_setRecordingMode(0);
		puts("    returned");

		for (int v = 0; v < NUM_TRACKS; v++)
			for (int s = 0; s < NUM_STEPS; s++)
				if (memcmp(&before[v][s],
					   &seq_patternSet.seq_subStepPattern[seq_activePattern][v][s],
					   sizeof(Step))) {
					printf("    track %d step %d changed\n", v, s);
					changed++;
				}
		printf("    steps changed by the edit: %d\n", changed);
		if (changed) {
			puts("FAIL - a stopped sequencer recorded automation onto a step");
			failures++;
		}
	}

	puts("-- non-regression: with the sequencer running, a roll still triggers");
	{
		static float block[256 * 2];
		seq_setRunning(1);
		for (int i = 0; i < 200; i++)
			lxr_render(block, 256, 2);
		seq_setRoll(0, 1);
		for (int i = 0; i < 100; i++)
			lxr_render(block, 256, 2);
		seq_setRoll(0, 0);
		seq_setRunning(0);
		puts("    returned");
	}

	printf(failures ? "\n%d failure(s)\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
