/*
 * What does one unreadable .pat do to a project load?
 *
 * Found while trying to write the device test for #89: a project load gives up
 * at the first pattern it cannot read and returns FR_INT_ERR, so every kit,
 * pattern and song after that slot is never loaded and the unit is left with a
 * project that is part the new one and part the old one. This is not something
 * #89 introduced - it is the same before and after that fix, which only decides
 * whether the half-read pattern reaches the live pattern set.
 *
 * It also settles how the #89 defect can and cannot be seen. A pattern file
 * stores the step data of all seven tracks first and the main-step words after
 * them, so a file cut at 1800 bytes never reaches the main steps: the step LEDs
 * show nothing either way, before the fix as well as after. What the old reader
 * did overwrite is the step data of the first three tracks, which is what
 * test_pattern_staging.c counts (228 of 448). So the device cannot show this
 * one on its LEDs, and the host test is the evidence.
 *
 * Usage: test_project_load_abort <path to test-project/PROJ63>
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
#include "Soundengine/Sequencer/sequencer.h"
#include "menu/menu.h"
#include "Parameters.h"

#define PROJ 63
#define PAT_HEADER 14
#define PAT_TRACK_BLOCK (64 * 7 + 64)
#define PAT_MAIN_STEPS (PAT_HEADER + 7 * PAT_TRACK_BLOCK)

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

/* Steps 1,5,9,13 on all seven voices, the same bytes craft-pat.py writes.
   The reference patterns are empty, so without this a half-applied load would
   be invisible. cut > 0 also truncates the file. */
static FRESULT craft_pat(const char *name, FSIZE_t cut)
{
	static uint8_t buf[4096];
	FIL f;
	UINT n = 0;
	FRESULT res = f_open(&f, name, FA_READ | FA_WRITE);

	if (res != FR_OK) {
		/* _FS_RPATH is 1, so there is no f_getcwd to ask */
		printf("    f_open(%s) -> %d (FR_NO_FILE = %d, FR_NO_PATH = %d)\n",
		       name, res, FR_NO_FILE, FR_NO_PATH);
		return res;
	}
	res = f_read(&f, buf, sizeof buf, &n);
	if (res != FR_OK || n < PAT_MAIN_STEPS + 56) { f_close(&f); return FR_INT_ERR; }
	for (int trk = 0; trk < NUM_TRACKS; trk++) {
		const unsigned base = PAT_HEADER + (unsigned)trk * PAT_TRACK_BLOCK;
		for (int i = 0; i < 4; i++) {
			buf[base + (unsigned)(i * 4) * 7] = 0x80 | 100;
			buf[base + (unsigned)(i * 4) * 7 + 1] = 100;
		}
		buf[PAT_MAIN_STEPS + (unsigned)trk * 8] = 0x11;
		buf[PAT_MAIN_STEPS + (unsigned)trk * 8 + 1] = 0x11;
	}
	if ((res = f_lseek(&f, 0)) == FR_OK)
		res = f_write(&f, buf, n, &n);
	if (res == FR_OK && cut) {
		if ((res = f_lseek(&f, cut)) == FR_OK)
			res = f_truncate(&f);
	}
	printf("    %-12s %lu bytes\n", name, (unsigned long)f_size(&f));
	f_close(&f);
	return res;
}

static int voices_with_steps(int pattern)
{
	int n = 0;
	for (int v = 0; v < NUM_TRACKS; v++)
		for (int s = 0; s < 16; s++)
			if (lxr_get_main_step((uint8_t)pattern, (uint8_t)v, (uint8_t)s)) {
				n++;
				break;
			}
	return n;
}

int main(int argc, char **argv)
{
	int failures = 0;
	FRESULT res;

	if (argc < 2) { puts("usage: test_project_load_abort <path to PROJ63>"); return 2; }
	if (!import_project(argv[1])) { puts("FAIL - could not put the project on the card"); return 2; }

	res = preset_loadProject(PROJ, 1);
	printf("-- the intact project loads: %d (FR_OK = %d)\n", res, FR_OK);
	if (res != FR_OK) { puts("FAIL - the reference project does not load"); return 2; }

	puts("-- pattern 0 gets a pattern and stays whole, pattern 1 is cut at 1800");
	if (craft_pat("/proj63/00-REF.PAT", 0) != FR_OK) { puts("FAIL - cannot craft 00"); return 2; }
	if (craft_pat("/proj63/01-REF.PAT", 1800) != FR_OK) { puts("FAIL - cannot craft 01"); return 2; }

	res = preset_loadProject(PROJ, 1);
	printf("-- loading it again: %d (FR_OK = %d, FR_INT_ERR = %d)\n", res, FR_OK, FR_INT_ERR);
	printf("   voices with steps: pattern 0 %d of %d, pattern 1 %d of %d\n",
	       voices_with_steps(0), NUM_TRACKS, voices_with_steps(1), NUM_TRACKS);
	printf("   kit 20 name after the load: [%.8s]\n", savefileNames[20][KIT_NAMES]);

	if (res == FR_OK) {
		puts("FAIL - an unreadable pattern file was reported as a clean load");
		failures++;
	}
	if (voices_with_steps(0) != NUM_TRACKS) {
		puts("FAIL - the control pattern did not load, so nothing here is conclusive");
		failures++;
	}
	if (voices_with_steps(1) != 0) {
		printf("FAIL - %d voices show main steps from a file that stops before"
		       " the main-step words\n", voices_with_steps(1));
		failures++;
	}

	puts("\n-- what the abort costs: the load stops at slot 1, so the kits,");
	puts("   patterns and songs of slots 1-63 keep whatever the previous project");
	puts("   left. The kit 20 name above comes from this project only because the");
	puts("   kit loop runs before the pattern that fails.");

	printf(failures ? "\n%d failure(s)\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
