/*
 * What happens to the old file when a renamed slot is saved? (#88, P4)
 *
 * A slot's file name carries the first five characters of the slot name, so
 * saving a renamed slot writes NN-NEWNAME.ext and used to leave NN-OLDNAME.ext
 * next to it. The loader takes the first NN-*.ext that f_findfirst() returns,
 * so the slot can come back with its previous contents.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "lxr_api.h"
#include "ff.h"
#include "Preset/PresetManager.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Parameters.h"
#include "menu/menu.h"

#define SLOT 7
#define OLD_MARK 11
#define NEW_MARK 22
#define PARAM PAR_VELOD3        /* an ordinary sound parameter */

static void set_name(int category, const char *name)
{
	memset(savefileNames[SLOT][category], 0, 8);
	memcpy(savefileNames[SLOT][category], name, strlen(name) > 8 ? 8 : strlen(name));
}

static int count_and_list(const char *pattern)
{
	DIR dj;
	FILINFO fno;
	int n = 0;

	if (f_findfirst(&dj, &fno, "", pattern) != FR_OK) { f_closedir(&dj); return -1; }
	printf("    %s ->", pattern);
	while (fno.fname[0]) {
		printf(" %s", fno.fname);
		n++;
		if (f_findnext(&dj, &fno) != FR_OK)
			break;
	}
	f_closedir(&dj);
	printf("  (%d file%s)\n", n, n == 1 ? "" : "s");
	return n;
}

int main(void)
{
	int failures = 0;
	int n;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(20);

	puts("-- a kit saved as BADON, renamed to GOOD and saved again");
	set_name(KIT_NAMES, "BADON");
	mainParameters[SLOT][PARAM] = OLD_MARK;
	if (preset_saveDrumsetSD(SLOT, 0) != FR_OK) { puts("FAIL - cannot save"); return 2; }
	count_and_list("07-*.SND");

	set_name(KIT_NAMES, "GOOD");
	mainParameters[SLOT][PARAM] = NEW_MARK;
	if (preset_saveDrumsetSD(SLOT, 0) != FR_OK) { puts("FAIL - cannot save"); return 2; }
	n = count_and_list("07-*.SND");
	if (n != 1) {
		printf("FAIL - the slot has %d files, one of them stale\n", n);
		failures++;
	}

	puts("-- which one does a load find?");
	mainParameters[SLOT][PARAM] = 0;
	if (preset_loadDrumsetSD(SLOT, 0) != FR_OK) { puts("FAIL - cannot load"); return 2; }
	printf("    PAR_VELOD3 after the load: %u (saved as GOOD: %u, as BADON: %u)\n",
	       mainParameters[SLOT][PARAM], NEW_MARK, OLD_MARK);
	if (mainParameters[SLOT][PARAM] != NEW_MARK) {
		puts("FAIL - the load returned the kit saved under the old name");
		failures++;
	}

	puts("-- the same for a pattern");
	set_name(PATTERN_NAMES, "BADON");
	if (preset_savePatternSD(SLOT) != FR_OK) { puts("FAIL - cannot save"); return 2; }
	set_name(PATTERN_NAMES, "GOOD");
	if (preset_savePatternSD(SLOT) != FR_OK) { puts("FAIL - cannot save"); return 2; }
	n = count_and_list("07-*.PAT");
	if (n != 1) { printf("FAIL - %d pattern files for one slot\n", n); failures++; }

	puts("-- and for a song");
	set_name(SONG_NAMES, "BADON");
	if (preset_saveSongSD(SLOT) != FR_OK) { puts("FAIL - cannot save"); return 2; }
	set_name(SONG_NAMES, "GOOD");
	if (preset_saveSongSD(SLOT) != FR_OK) { puts("FAIL - cannot save"); return 2; }
	n = count_and_list("07-*.SNG");
	if (n != 1) { printf("FAIL - %d song files for one slot\n", n); failures++; }

	puts("-- non-regression: saving twice under the same name keeps one file");
	set_name(KIT_NAMES, "GOOD");
	if (preset_saveDrumsetSD(SLOT, 0) != FR_OK) { puts("FAIL - cannot save"); return 2; }
	n = count_and_list("07-*.SND");
	if (n != 1) { printf("FAIL - %d kit files after re-saving\n", n); failures++; }
	mainParameters[SLOT][PARAM] = 0;
	if (preset_loadDrumsetSD(SLOT, 0) != FR_OK || mainParameters[SLOT][PARAM] != NEW_MARK) {
		printf("FAIL - the kit no longer loads: PAR_VELOD3 = %u\n",
		       mainParameters[SLOT][PARAM]);
		failures++;
	}

	printf(failures ? "\n%d failure(s)\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
