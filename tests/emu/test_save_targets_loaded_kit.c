/*
 * Which kit slot does a project save write the edit buffer into? (#8)
 *
 * menu_loadedPresetNr[CURRENT_KIT] is the kit being edited.
 * menu_selectedPresetNr[SAVE_TYPE_KIT] is wherever the encoder was last turned
 * in a load or save menu, which is not the same thing and used to be the one
 * the save believed.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include "lxr_api.h"
#include "ff.h"
#include "Preset/PresetManager.h"
#include "menu/menu.h"
#include "Parameters.h"

#define EDITED   5          /* the kit in the edit buffer */
#define SCROLLED 9          /* the slot the encoder was left on */
#define MARKER   77         /* a value neither kit starts with */
#define PARAM    PAR_VELOD3 /* drum 3 amp-EG decay: an ordinary sound parameter */

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(20);

	/* Edit kit 5, then leave the menu cursor on kit 9. */
	preset_loadDrumsetRam(EDITED, 0, 0);
	menu_loadedPresetNr[CURRENT_KIT] = EDITED;
	menu_selectedPresetNr[SAVE_TYPE_KIT] = SCROLLED;
	activeParameters[PARAM] = MARKER;

	printf("editing kit %d, cursor on kit %d, PAR_VELOD3 = %d in the edit buffer\n",
	       EDITED, SCROLLED, activeParameters[PARAM]);
	printf("before the save: kit %d has %d, kit %d has %d\n",
	       EDITED, mainParameters[EDITED][PARAM], SCROLLED, mainParameters[SCROLLED][PARAM]);

	const FRESULT res = preset_saveProject(63);
	printf("save -> %d\n", res);
	printf("after the save:  kit %d has %d, kit %d has %d\n",
	       EDITED, mainParameters[EDITED][PARAM], SCROLLED, mainParameters[SCROLLED][PARAM]);

	if (mainParameters[EDITED][PARAM] != MARKER) {
		printf("FAIL - the edit buffer did not reach kit %d, the one being edited\n", EDITED);
		failures++;
	}
	if (mainParameters[SCROLLED][PARAM] == MARKER) {
		printf("FAIL - the edit buffer was written into kit %d, which was only scrolled to\n", SCROLLED);
		failures++;
	}
	if (!failures)
		printf("the edit buffer went to kit %d and left kit %d alone\n", EDITED, SCROLLED);

	printf("\n%s\n", failures ? "FAILURES" : "done");
	return failures ? 1 : 0;
}
