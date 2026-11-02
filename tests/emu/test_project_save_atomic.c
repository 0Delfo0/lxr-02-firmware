/*
 * Does a failed project save destroy the project that was already there? (#65)
 *
 * Nothing else runs in this test: a fresh boot, one good save, fill the card,
 * one failing save, count the folder. Mixing it with the other SD cases hid the
 * answer, because on firmware without the atomic save an earlier failing save
 * had already emptied the folder.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include "lxr_api.h"
#include "ff.h"
#include "Preset/PresetManager.h"
#include "card_fill.h"

#define SLOT 63

static int count_files(const char *path)
{
	DIR dir;
	FILINFO fno;
	int n = 0;
	if (f_opendir(&dir, path) != FR_OK)
		return -1;
	while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0])
		n++;
	f_closedir(&dir);
	return n;
}

static void list_proj_folders(void)
{
	DIR dir;
	FILINFO fno;
	if (f_opendir(&dir, "/") != FR_OK) { puts("    cannot open /"); return; }
	printf("    folders in /:");
	while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0])
		if (fno.fattrib & AM_DIR)
			printf(" %s", fno.fname);
	printf("\n");
	f_closedir(&dir);
}

int main(void)
{
	int failures = 0;
	FRESULT res;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(20);
	printf("booted; cluster %u bytes, %llu bytes free\n",
	       card_cluster_bytes(), (unsigned long long)card_free_bytes());

	res = preset_saveProject(SLOT);
	const int before = count_files("/proj63");
	printf("  save with room      -> %d, /proj63 holds %d files\n", res, before);
	list_proj_folders();
	if (res != FR_OK || before <= 0) {
		puts("  FAIL - could not create the project to protect");
		return 1;
	}

	printf("  filled, %llu bytes left\n",
	       (unsigned long long)card_fill_leaving(2 * card_cluster_bytes()));

	res = preset_saveProject(SLOT);
	const int after = count_files("/proj63");
	printf("  save on a full card -> %d, /proj63 holds %d files\n", res, after);
	list_proj_folders();

	if (res == FR_OK) { puts("  FAIL - reported success on a full card"); failures++; }
	if (after != before) {
		printf("  FAIL - the project lost %d of its %d files\n", before - after, before);
		failures++;
	} else {
		puts("  the project that was there survived intact");
	}

	printf("\n%s\n", failures ? "FAILURES" : "done");
	return failures ? 1 : 0;
}
