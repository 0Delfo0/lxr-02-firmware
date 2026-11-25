/*
 * Is the step interval computed in floating point? (#21, review item S-B5)
 *
 * Both tempo paths wrote (1000*60)/bpm into a float. bpm is a uint16_t and the
 * dividend was an int, so the division truncated before anything became a
 * float. seq_getDeltaTShuffleDelay() is the exported one and is a pure function
 * of the tempo, so the whole range can be checked without hardware.
 *
 * It returns shuffle * dT * 16, so the shuffle has to be set first - with it at
 * zero, the default, the function returns 0 whatever the tempo and the test
 * would say nothing. At full shuffle the truncation in dT comes out multiplied
 * by 16, which is also what it does to the swing on the device.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "lxr_api.h"
#include "Soundengine/Sequencer/sequencer.h"

/* shuffle * dT * 16, with dT the beat period over the 96 PPQ clock times the
   prescaler, all in floating point */
static float expected(uint16_t bpm, float shuffle)
{
	return shuffle * ((1000.f * 60.f) / bpm / 96.f * 4.f) * 16.f;
}

int main(void)
{
	int failures = 0;
	double worst = 0.0;
	uint16_t worst_bpm = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(500);

	/* without this the function returns 0 for every tempo */
	const float shuffle = 1.f;
	seq_setShuffle(shuffle);
	if (seq_getDeltaTShuffleDelay(120) == 0.f) {
		puts("FAIL - the shuffle delay is zero, so nothing below means anything");
		return 2;
	}

	puts("-- a few tempi by hand, shuffle at full, in 0.25 ms ticks of the 4 kHz counter");
	const uint16_t show[] = { 120, 125, 128, 137, 150, 174, 200 };
	for (unsigned i = 0; i < sizeof show / sizeof *show; i++) {
		const uint16_t b = show[i];
		const float got = seq_getDeltaTShuffleDelay(b);
		const float want = expected(b, shuffle);
		const double err = 100.0 * (want - got) / want;
		printf("   %3u bpm: %10.5f  want %10.5f   %+.4f %%\n",
		       b, (double)got, (double)want, err);
	}

	puts("\n-- the whole range, 20 to 300");
	for (uint16_t b = 20; b <= 300; b++) {
		const float got = seq_getDeltaTShuffleDelay(b);
		const float want = expected(b, shuffle);
		const double err = fabs(100.0 * (want - got) / want);
		if (err > worst) { worst = err; worst_bpm = b; }
		if (err > 0.01) failures++;
	}
	printf("   worst error %.4f %% at %u bpm, %d tempi off by more than 0.01 %%\n",
	       worst, worst_bpm, failures);

	puts(failures ? "\nFAILURES - the division is still truncating"
	              : "\nall checks passed - every tempo within 0.01 %");
	return failures ? 1 : 0;
}
