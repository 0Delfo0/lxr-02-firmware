/*
 * Which kit does MIDI CC 0 select? (#107, aligning with upstream dca0a44)
 *
 * A CC value is 0-127 and there are NUM_KIT_SLOTS kits. #74 refused anything
 * above the last kit; the official firmware wraps from dca0a44 on, and this
 * fork follows it so that a host written against the instrument everyone else
 * has behaves the same here.
 *
 * The observable: the edit buffer after the message. The kits are given
 * different values in one parameter first, because the reference project's 64
 * kits are identical and "the kit changed" is otherwise invisible - the same
 * reason the device test of #74 needed a marker.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include "lxr_api.h"
#include "Preset/PresetManager.h"
#include "Parameters.h"
#include "menu/menu.h"

#define MARK PAR_VELOD3        /* an ordinary sound parameter, id 54 */
#define UNTOUCHED 77           /* what the edit buffer holds before each message */

/* kit n gets 100 + n, so the value says which kit was loaded */
static uint8_t expected_for(int kit) { return (uint8_t)(100 + kit); }

static int check(uint8_t value, int expect_kit)
{
	activeParameters[MARK] = UNTOUCHED;
	{
		uint8_t cc[3] = { 0xB0, 0x00, value };
		lxr_midi_in(cc, 3);
		lxr_idle_blocks(400);
	}
	{
		const uint8_t got = activeParameters[MARK];
		const int ok = expect_kit < 0 ? got == UNTOUCHED : got == expected_for(expect_kit);
		/* The edit buffer holding the kit is only half of it: a project save
		   writes the buffer back into menu_loadedPresetNr[CURRENT_KIT], so if
		   that still names the previous kit the save overwrites the wrong slot
		   and the sound of the kit that was there is lost (#109). */
		const int loaded_ok = expect_kit < 0 ? 1 :
		                      menu_loadedPresetNr[CURRENT_KIT] == expect_kit;
		printf("    CC 0 = %-3u -> PAR_VELOD3 %-3u, selectedPresetNr %-2u, loadedPresetNr %-2u   %s\n",
		       value, got, menu_selectedPresetNr[SAVE_TYPE_KIT],
		       menu_loadedPresetNr[CURRENT_KIT],
		       expect_kit < 0 ? "(expected: untouched)" :
		       !ok ? "<<< not the expected kit" :
		       !loaded_ok ? "<<< loaded-kit number did not follow" : "");
		if (!ok && expect_kit >= 0)
			printf("FAIL - CC 0 = %u should load kit %d (%u), got %u\n",
			       value, expect_kit, expected_for(expect_kit), got);
		if (!loaded_ok)
			printf("FAIL - CC 0 = %u loaded kit %d but loadedPresetNr[CURRENT_KIT] is %u:"
			       " a project save would write the edit buffer into slot %u\n",
			       value, expect_kit, menu_loadedPresetNr[CURRENT_KIT],
			       menu_loadedPresetNr[CURRENT_KIT]);
		return (ok ? 0 : 1) + (loaded_ok ? 0 : 1);
	}
}

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(3000);

	puts("-- give every kit a different value in PAR_VELOD3, so the kit is identifiable");
	for (int k = 0; k < NUM_KIT_SLOTS; k++)
		mainParameters[k][MARK] = expected_for(k);

	puts("-- the half that must not change: 0 to 63");
	failures += check(0, 0);
	failures += check(5, 5);
	failures += check(63, 63);

	puts("-- the half this issue changes: 64 to 127 now wrap");
	failures += check(64, 0);
	failures += check(100, 100 % NUM_KIT_SLOTS);
	failures += check(127, 127 % NUM_KIT_SLOTS);

	puts("-- and the kit number the save path would use follows the wrap");
	if (menu_selectedPresetNr[SAVE_TYPE_KIT] >= NUM_KIT_SLOTS) {
		printf("FAIL - selectedPresetNr is %u, past the last kit\n",
		       menu_selectedPresetNr[SAVE_TYPE_KIT]);
		failures++;
	}

	printf(failures ? "\n%d failure(s)\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
