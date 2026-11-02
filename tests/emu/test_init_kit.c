/*
 * Does preset_init() fill the whole kit? (#24, reported as P9)
 *
 * initKit is 8 name bytes followed by the parameters, so parameter n is
 * initKit[8+n]. The copy loop used to run i from 8 to END_OF_SOUND_PARAMETERS
 * while writing to para[i-8], which filled parameters 0-238 and left 239-246 -
 * PAR_FX_P2 to PAR_MORPH_KIT_DEST - holding the previous kit's values.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include "lxr_api.h"
#include "Preset/PresetManager.h"
#include "Preset/InitKit.h"
#include "Parameters.h"
#include "menu/menu.h"

#define SLOT 5
#define MARKER 123          /* a value the init kit does not contain */

static const struct { int id; const char *name; } tail[] = {
	{ PAR_FX_P2,          "PAR_FX_P2" },
	{ PAR_FX_P3,          "PAR_FX_P3" },
	{ PAR_FX_P4,          "PAR_FX_P4" },
	{ PAR_FX_RATIO,       "PAR_FX_RATIO" },
	{ PAR_FX_WAVE,        "PAR_FX_WAVE" },
	{ PAR_FX_REVERB_TYPE, "PAR_FX_REVERB_TYPE" },
	{ PAR_FX_DELAY_TYPE,  "PAR_FX_DELAY_TYPE" },
	{ PAR_MORPH_KIT_DEST, "PAR_MORPH_KIT_DEST" },
};

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(20);

	printf("END_OF_SOUND_PARAMETERS = %d, INIT_KIT_SIZE = %d, so the init kit\n"
	       "carries parameters 0-%d and %d has no initial value of its own\n",
	       END_OF_SOUND_PARAMETERS, INIT_KIT_SIZE, INIT_KIT_SIZE - 9,
	       INIT_KIT_SIZE - 8);

	/* A kit in slot 5 with a marker in every parameter. */
	for (int i = 0; i < END_OF_SOUND_PARAMETERS; i++)
		mainParameters[SLOT][i] = MARKER;

	puts("-- preset_init() over a kit whose every parameter is 123");
	preset_init(SLOT);

	for (unsigned t = 0; t < sizeof tail / sizeof tail[0]; t++) {
		const uint8_t got = mainParameters[SLOT][tail[t].id];
		const uint8_t want = tail[t].id < INIT_KIT_SIZE - 8
				? initKit[8 + tail[t].id] : 0;
		printf("    %-20s (%d) = %-3u  init kit says %u\n",
		       tail[t].name, tail[t].id, got, want);
		if (got != want) {
			printf("FAIL - %s kept %u from the previous kit\n",
			       tail[t].name, got);
			failures++;
		}
	}

	puts("-- non-regression: every other parameter matches the init kit");
	for (int i = 0; i < INIT_KIT_SIZE - 8; i++)
		if (mainParameters[SLOT][i] != initKit[8 + i]) {
			printf("FAIL - parameter %d is %u, the init kit says %u\n",
			       i, mainParameters[SLOT][i], initKit[8 + i]);
			failures++;
			break;      /* one is enough to show it */
		}
	if (!failures)
		puts("    all 246 parameters in the array match");

	printf(failures ? "\n%d failure(s)\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
