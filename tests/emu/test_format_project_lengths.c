/*
 * What track lengths does the project written at format time have? (#105)
 *
 * sd_formatRoutine() called preset_init(0) - the init sound for kit 0 - and
 * then preset_saveProject(0), which writes seq_patternSet. At that point in
 * main() seq_init() has not run (format at main.c:407, soundengine_init at
 * :494), so the pattern set is still zero and every saved track length was 0
 * instead of 16.
 *
 * The emulator does not run main(), so this cannot drive the format routine
 * itself - that part is established by reading those two line numbers. What it
 * can do is the half that matters for the fix: save a project the way the old
 * code did and the way the new code does, load each back, and read the lengths
 * out of the file.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "lxr_api.h"
#include "Parameters.h"
#include "Preset/PresetManager.h"
#include "Soundengine/Sequencer/sequencer.h"

static void show_lengths(const char *what)
{
	printf("  %-34s", what);
	for (uint8_t t = 0; t < NUM_TRACKS; t++)
		printf(" %2u", seq_patternSet.length[0][t]);
	printf("\n");
}

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(2000);

	puts("-- the old sequence: the pattern set as main() has it at format time");
	memset(&seq_patternSet, 0, sizeof(seq_patternSet));   /* what seq_init has not yet done */
	preset_init(0);
	show_lengths("in RAM before saving");
	sprintf(savefileNames[0][PROJECT_NAMES], "Init");
	preset_saveProject(0);

	memset(&seq_patternSet, 0xAA, sizeof(seq_patternSet));  /* so the load has to supply them */
	preset_loadProject(0, 1);
	show_lengths("loaded back from the card");
	uint8_t zeros = 0;
	for (uint8_t t = 0; t < NUM_TRACKS; t++) if (seq_patternSet.length[0][t] == 0) zeros++;
	printf("   tracks whose stored length is 0: %u of %u\n\n", zeros, NUM_TRACKS);

	puts("-- the new sequence: preset_createProject() first");
	preset_createProject(0);
	show_lengths("in RAM before saving");
	preset_saveProject(0);

	memset(&seq_patternSet, 0xAA, sizeof(seq_patternSet));
	preset_loadProject(0, 1);
	show_lengths("loaded back from the card");

	for (uint8_t t = 0; t < NUM_TRACKS; t++) {
		if (seq_patternSet.length[0][t] != 16) {
			printf("FAIL - track %u came back as %u, not 16\n",
			       t + 1, seq_patternSet.length[0][t]);
			failures++;
		}
	}

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
