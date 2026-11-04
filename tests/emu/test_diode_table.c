/*
 * Does the RAT diode table get read past its end? (#15)
 *
 * diode() maps a sample clamped to [-1, 1] onto index = (in+1) * 128, so the
 * index reaches 256, and the interpolation reads index+1 - element 257 of
 * const float table_1n914[257], whose last valid index is 256.
 *
 * The values are the same either way, and that is worth knowing rather than
 * glossing: at index exactly 256 frac is 0, so the lerp multiplies the
 * out-of-bounds value by zero. The defect is undefined behaviour and a read of
 * unrelated const data, not a wrong sample - which is why the engine render is
 * bit-identical and why the sanitizer report is the evidence. The checks below
 * are the non-regression: the clamp must keep every value exactly as it was.
 *
 * Distortion.c cannot be built by tests/host: Distortion.h pulls in globals.h
 * and so stm32f7xx.h, which is what #29 is about. The emulator compiles it with
 * its own shim, so the sweep lives here instead.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <math.h>
#include "Soundengine/DSPAudio/FX/Distortion.h"

extern const float table_1n914[257];
float diode(float in);          /* not in Distortion.h */

int main(void)
{
	int failures = 0;

	puts("-- sweeping in across [-1.5, 1.5], which includes both rails");
	{
		float last = diode(-1.5f);
		int jumps = 0;
		for (int i = -15000; i <= 15000; i++) {
			const float in = (float)i / 10000.0f;
			const float out = diode(in);
			if (!isfinite(out)) {
				printf("FAIL - diode(%g) is not finite\n", (double)in);
				failures++;
				break;
			}
			/* the table is monotonic and smooth, so a step larger than one
			   table interval means a value from outside the table got in */
			if (fabsf(out - last) > 0.2f)
				jumps++;
			last = out;
		}
		printf("    30001 samples, %d discontinuities larger than 0.2\n", jumps);
		if (jumps) {
			puts("FAIL - the curve jumps: a value from outside the table");
			failures++;
		}
	}

	puts("-- the endpoints, where the index is exactly 0 and exactly 256");
	{
		const float lo = diode(-1.0f), hi = diode(1.0f);
		printf("    diode(-1) = %.9g, table_1n914[0]   = %.9g\n",
		       (double)lo, (double)table_1n914[0]);
		printf("    diode(+1) = %.9g, table_1n914[256] = %.9g\n",
		       (double)hi, (double)table_1n914[256]);
		if (lo != table_1n914[0]) {
			puts("FAIL - the bottom rail does not return the first table entry");
			failures++;
		}
		/* This is the point of clamping the index before frac is computed:
		   at index 256 frac becomes 1.0, so the lerp returns entry 256 -
		   the value the out-of-bounds read was reaching for. */
		if (hi != table_1n914[256]) {
			puts("FAIL - the top rail does not return the last table entry");
			failures++;
		}
	}

	puts("-- past the rails, where the clamp inside diode() takes over");
	{
		const float a = diode(1.0f), b = diode(4.0f), c = diode(1e9f);
		printf("    diode(1) = %.9g, diode(4) = %.9g, diode(1e9) = %.9g\n",
		       (double)a, (double)b, (double)c);
		if (a != b || b != c) {
			puts("FAIL - inputs above the rail do not all give the same value");
			failures++;
		}
	}

	printf(failures ? "\n%d failure(s)\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
