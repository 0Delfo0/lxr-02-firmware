/*
 * Does the pitch envelope produce NaN with EG Slope 0 and Pitch Decay 127?
 *
 * DecayEg_calcSlopeValue() shapes the pitch envelope with
 *
 *     (1+slope)*val/(1+slope*fabsf(val))
 *
 * EG Slope 0 gives slope = -1 in DecayEg_setSlope(): amount is -1, and
 * 2*-1/(1-(-1)) is -1. The numerator is then 0 for every val, which is the
 * intended "no pitch sweep". But Pitch Decay 127 gives a decay step of 0 in
 * DecayEg_calcTime() - (1-((1+TIME_K)*1/(1+TIME_K))) * 2 is 0 - so after a
 * trigger the envelope stays at val = 1, and the denominator is 1 + (-1)*1 = 0
 * as well. 0/0 is NaN, and it goes into the oscillator's pitch.
 *
 * Reported by jhw as erica-synths/lxr-desktop-app#18, which is the second of
 * the two cases in Decay.c; the first, EG Slope 127, is our #6 and is fixed.
 *
 * The sweep is there because the window is narrow in one argument and wide in
 * the other: only slope 0 breaks, at any decay that leaves val above zero.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "Soundengine/DSPAudio/Decay.h"

static int probe(uint8_t slopeParam, uint8_t decayParam, int verbose)
{
	DecayEg eg;
	DecayEg_init(&eg);
	DecayEg_setSlope(&eg, slopeParam);
	DecayEg_setDecay(&eg, decayParam);
	DecayEg_trigger(&eg);
	int bad = 0;
	float first = 0.f;
	for (int i = 0; i < 64; i++) {
		const float out = DecayEg_calc(&eg);
		if (!isfinite(out)) { if (!bad) first = out; bad++; }
	}
	if (verbose)
		printf("   slope %3u, decay %3u -> %2d of 64 samples not finite%s\n",
		       slopeParam, decayParam, bad, bad ? (isnan(first) ? "  (NaN)" : "  (inf)") : "");
	return bad;
}

int main(void)
{
	int failures = 0;

	/* 1. the control: the settings a player uses every day must be finite, or
	 *    a guard that returned 0 for everything would pass the rest */
	puts("1. ordinary settings");
	int ok = 1;
	for (unsigned s = 1; s < 127; s += 25)
		for (unsigned d = 0; d < 127; d += 25)
			if (probe((uint8_t)s, (uint8_t)d, 0)) { ok = 0; }
	printf("   slope 1..126 against decay 0..126: %s\n", ok ? "all finite" : "SOMETHING IS NOT FINITE");
	if (!ok) { puts("   FAIL - the control failed"); return 2; }
	/* and the shape has to survive: slope 64 must still bend the envelope */
	{
		DecayEg eg; DecayEg_init(&eg); DecayEg_setSlope(&eg, 64);
		DecayEg_setDecay(&eg, 64); DecayEg_trigger(&eg);
		const float a = DecayEg_calc(&eg);
		printf("   slope 64, decay 64, first sample = %.4f (must be non-zero)\n", (double)a);
		if (!(a > 0.0001f)) { puts("   FAIL - the envelope is flat: the fix went too far"); failures++; }
	}

	/* 1b. slope 0 is "no pitch sweep", and it must stay exactly that. The fix
	 *     belongs in DecayEg_calcSlopeValue() and not in DecayEg_setSlope():
	 *     clamping the amount away from -1, which is what our #6 fix does at
	 *     the other end, would give slope 0 a small sweep at every decay value
	 *     and move every kit that uses it (#93). Guarding the division changes
	 *     one output and no other. */
	{
		DecayEg eg; DecayEg_init(&eg); DecayEg_setSlope(&eg, 0);
		DecayEg_setDecay(&eg, 64); DecayEg_trigger(&eg);
		const float a = DecayEg_calc(&eg);
		printf("   slope 0, decay 64, first sample = %.8f (must be exactly 0)\n", (double)a);
		if (a != 0.0f) { puts("   FAIL - slope 0 no longer means no sweep"); failures++; }
	}

	/* 2. the reported pair */
	puts("\n2. EG Slope 0");
	failures += probe(0, 127, 1) ? 1 : 0;

	/* 3. how wide is it? slope 0 against every decay */
	puts("\n3. slope 0 against the decay range");
	int broken = 0;
	for (unsigned d = 0; d <= 127; d++)
		if (probe(0, (uint8_t)d, 0)) broken++;
	printf("   slope 0: %d of the 128 decay values give a non-finite envelope\n", broken);
	failures += broken ? 1 : 0;

	/* 4. and slope 127, which is #6 and must stay fixed */
	puts("\n4. EG Slope 127, the case already fixed (#6)");
	failures += probe(127, 127, 1) ? 1 : 0;

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
