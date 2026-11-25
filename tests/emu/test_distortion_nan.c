/*
 * Once a NaN reaches the RAT distortion's state, does anything clear it? (#15)
 *
 * diode() reads table_1n914[257], one element past the table. In a device
 * build that word is the four-byte alignment gap at _eccm, which the startup
 * code neither copies nor zeroes, so it holds whatever the DTCM held at
 * power-on. At full scale the interpolation fraction is 0, so a finite value
 * there is harmless - the lerp returns table_1n914[256] exactly - and a NaN or
 * infinity bit pattern makes the result NaN. A random word has an all-ones
 * exponent once in 256.
 *
 * That makes the failure impossible to produce on demand, which is why this
 * test injects the NaN into the state the poisoned sample would land in and
 * asks the question that decides the severity: does it ever go away?
 *
 * This test asserts the state, not the audio: the rendered block is not the
 * place the effect's result reaches here, and reporting a level from it would
 * imply a measurement this does not make.
 *
 * Both states are self-referential and unguarded - lp3_state through
 * "state += coff * (signal - state)" and lastOut through the feedback at the
 * top of the loop - and Distortion_init() is reached only from
 * main.c -> soundengine_init -> mixer_init -> FxBus_init, once, at startup.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "lxr_api.h"
#include "Soundengine/FxBus.h"
#include "Soundengine/DSPAudio/FX/Distortion.h"

#define FRAMES 256

int main(void)
{
	static float buf[FRAMES * 2];
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(2000);

	/* RAT distortion on the FX bus, driven hard. Set on the struct rather
	   than through the parameter path: this test is about the state, and the
	   parameter route adds nothing it needs. */
	fxBus.fxType = FX_DISTORTION;
	fxBus.distortion.distortionType = DISTORTION_RAT;
	fxBus.distortion.drive = 0.9f;
	fxBus.distortion.p1 = 0.5f;
	fxBus.distortion.p2 = 0.5f;
	fxBus.distortion.p3 = 1.0f;

	lxr_render(buf, FRAMES, 2);
	printf("-- before: lp3_state %g, lastOut %g - both finite\n",
	       (double)fxBus.distortion.lp3_state[0], (double)fxBus.distortion.lastOut[0]);
	if (!isfinite(fxBus.distortion.lp3_state[0])) {
		puts("FAIL - the state is already not finite, so nothing below means anything");
		return 2;
	}

	/* one poisoned sample, exactly where a NaN from the table read lands */
	fxBus.distortion.lp3_state[0] = NAN;
	puts("-- a single NaN injected into lp3_state[0]");

	for (int block = 1; block <= 200; block++) {
		lxr_render(buf, FRAMES, 2);
		if (block == 1 || block == 10 || block == 200)
			printf("   after %3d blocks: lp3_state %g, lastOut %g\n",
			       block, (double)fxBus.distortion.lp3_state[0],
			       (double)fxBus.distortion.lastOut[0]);
	}

	const int latched = isnan(fxBus.distortion.lp3_state[0]);
	printf("\n   200 blocks = %.0f ms of audio at 48 kHz\n", 200.0 * FRAMES / 48.0);
	if (latched) {
		puts("   LATCHED - nothing in the signal path clears it; only Distortion_init(),");
		puts("             which runs once at startup, so this needs a power cycle.");
	} else {
		puts("FAIL - the state recovered, so the premise of this test is wrong");
		failures++;
	}

	/* And the other half: with the fix in diode() the table read cannot be
	   the source, because the index never leaves the table. */
	extern const float table_1n914[257];
	extern float diode(float in);
	printf("\n-- diode() at the rails, where the index is exactly 0 and exactly 256\n");
	printf("   diode(-1) = %.9f   table_1n914[0]   = %.9f\n",
	       (double)diode(-1.f), (double)table_1n914[0]);
	printf("   diode(+1) = %.9f   table_1n914[256] = %.9f\n",
	       (double)diode(1.f), (double)table_1n914[256]);
	if (diode(1.f) != table_1n914[256]) {
		puts("FAIL - the curve's last point is not the table's last entry");
		failures++;
	}
	if (diode(-1.f) != table_1n914[0]) {
		puts("FAIL - the curve's first point is not the table's first entry");
		failures++;
	}

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
