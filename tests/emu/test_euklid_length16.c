/*
 * Does a 16-step Euclidean track store 16? (#23, S-B1)
 *
 * euklid_transferPattern() used to store a length of 16 as 0, and only the
 * sequencer remapped it. Everything else that reads the field - the display,
 * the .pat file, the SysEx dump - saw 0.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include "lxr_api.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/Sequencer/EuklidGenerator.h"

#define TRK 0
#define PAT 0

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(3000);

	puts("-- a 16-step Euclidean track, 4 steps, no rotation");
	euklid_setLength(TRK, 16);
	euklid_setSteps(TRK, 4, PAT);
	euklid_transferPattern(TRK, PAT);
	lxr_idle_blocks(200);
	printf("    stored length %u, euklid_getLength %u, seq_getTrackLength %u\n",
	       seq_patternSet.length[PAT][TRK], euklid_getLength(TRK),
	       seq_getTrackLength(TRK));
	if (seq_patternSet.length[PAT][TRK] != 16) {
		printf("FAIL - the field holds %u; 16 is what the track is\n",
		       seq_patternSet.length[PAT][TRK]);
		failures++;
	}
	if (seq_getTrackLength(TRK) != 16) {
		printf("FAIL - the display would show %u\n", seq_getTrackLength(TRK));
		failures++;
	}

	puts("-- a length other than 16 is untouched, as it always was");
	euklid_setLength(TRK, 12);
	euklid_setSteps(TRK, 3, PAT);
	euklid_transferPattern(TRK, PAT);
	lxr_idle_blocks(200);
	printf("    stored length %u, seq_getTrackLength %u\n",
	       seq_patternSet.length[PAT][TRK], seq_getTrackLength(TRK));
	if (seq_patternSet.length[PAT][TRK] != 12 || seq_getTrackLength(TRK) != 12) {
		puts("FAIL - 12 did not survive");
		failures++;
	}

	puts("-- a pattern from older firmware, which carries 0");
	seq_patternSet.length[PAT][TRK] = 0;
	printf("    stored 0, seq_getTrackLength reports %u\n", seq_getTrackLength(TRK));
	if (seq_getTrackLength(TRK) != 16) {
		printf("FAIL - the display shows %u for a track the sequencer runs as 16\n",
		       seq_getTrackLength(TRK));
		failures++;
	}
	if (seq_patternSet.length[PAT][TRK] != 0) {
		puts("FAIL - reading the length rewrote the stored byte");
		failures++;
	}

	printf(failures ? "\n%d failure(s)\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
