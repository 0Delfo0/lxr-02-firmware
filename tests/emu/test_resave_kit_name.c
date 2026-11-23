/*
 * Does saving a project into an occupied slot damage kit 0's name?
 *
 * Reported upstream as PR #13 by jhw: empty_directory() appended each file
 * name to currentFolder[13], and the overflow landed in
 * savefileNames[0][KIT_NAMES], so "Brownie" became ".SND" plus a zero byte and
 * "ie" - and the save then wrote that name to the card, renaming
 * 00-BROWN.SND to 00-_SND.SND.
 *
 * Our tree should be immune twice over: #2 gave empty_directory a 256-byte
 * working buffer, and #65 stopped the save emptying the destination at all -
 * it writes into /projNNn and renames. This test is the confirmation, because
 * two reasons to believe something are not a measurement.
 *
 * Usage: test_resave_kit_name <path to test-project/PROJ63>
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "lxr_api.h"
#include "ff.h"
#include "host_dir.h"
#include "Preset/PresetManager.h"
#include "menu/menu.h"

#define PROJ 63

static int import_project(const char *hostdir)
{
	static char names[256][HOST_DIR_NAME_MAX];
	char path[1024];
	int count, n = 0;

	if (lxr_sd_create(128) != 0) return 0;
	if (lxr_card_import_begin() != 0) return 0;
	sprintf(lxr_card_path_buffer(), "/proj%02d", PROJ);
	if (lxr_card_import_file(0, 1) != 0) return 0;
	count = host_dir_list(hostdir, names, 256);
	if (count < 0) return 0;
	for (int i = 0; i < count; i++) {
		FILE *f;
		long len;
		snprintf(path, sizeof path, "%s/%s", hostdir, names[i]);
		f = fopen(path, "rb");
		if (!f) continue;
		fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
		if (fread(lxr_card_buffer((uint32_t)len), 1, (size_t)len, f) != (size_t)len) {
			fclose(f);
			return 0;
		}
		fclose(f);
		sprintf(lxr_card_path_buffer(), "/proj%02d/%s", PROJ, names[i]);
		if (lxr_card_import_file((uint32_t)len, 0) != 0) return 0;
		n++;
	}
	if (lxr_card_import_end() != 0) return 0;
	if (lxr_boot(1) != 0) return 0;
	lxr_idle_blocks(3000);
	return n;
}

static void show_name(const char *what)
{
	const char *n = savefileNames[0][KIT_NAMES];
	printf("    %-26s [", what);
	for (int i = 0; i < 8; i++) {
		const unsigned char c = (unsigned char)n[i];
		if (c == 0) printf("\\0");
		else if (c >= 0x20 && c <= 0x7E) printf("%c", c);
		else printf("\\x%02X", c);
	}
	printf("]\n");
}

static int count_kit0_files(void)
{
	DIR dj;
	FILINFO fno;
	int n = 0;

	if (f_findfirst(&dj, &fno, "/proj63", "00-*.SND") != FR_OK) { f_closedir(&dj); return -1; }
	printf("    /proj63/00-*.SND ->");
	while (fno.fname[0]) {
		printf(" %s", fno.fname);
		n++;
		if (f_findnext(&dj, &fno) != FR_OK) break;
	}
	f_closedir(&dj);
	printf("  (%d)\n", n);
	return n;
}

int main(int argc, char **argv)
{
	int failures = 0;
	char expected[9];

	if (argc < 2) { puts("usage: test_resave_kit_name <path to PROJ63>"); return 2; }
	if (!import_project(argv[1])) { puts("FAIL - could not put the project on the card"); return 2; }

	printf("-- project 63 loaded: %d\n", preset_loadProject(PROJ, 1));
	show_name("kit 0 name as loaded:");
	memcpy(expected, savefileNames[0][KIT_NAMES], 8);
	expected[8] = 0;

	puts("-- save the project into the slot it already occupies");
	printf("    preset_saveProject(%d) -> %d\n", PROJ, preset_saveProject(PROJ));
	lxr_idle_blocks(600);
	show_name("kit 0 name after:");
	if (memcmp(expected, savefileNames[0][KIT_NAMES], 8)) {
		puts("FAIL - the first kit's name changed during the save");
		failures++;
	}

	puts("-- and the file on the card still carries that name");
	if (count_kit0_files() != 1) {
		puts("FAIL - kit 0 should have exactly one file, named after the kit");
		failures++;
	}

	puts("-- again, twice more, since the defect needed an occupied slot");
	for (int i = 0; i < 2; i++) {
		preset_saveProject(PROJ);
		lxr_idle_blocks(300);
	}
	show_name("kit 0 name after three:");
	if (memcmp(expected, savefileNames[0][KIT_NAMES], 8)) {
		puts("FAIL - the name changed on a later save");
		failures++;
	}
	if (count_kit0_files() != 1) {
		puts("FAIL - more than one file for kit 0");
		failures++;
	}

	printf(failures ? "\n%d failure(s)\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
