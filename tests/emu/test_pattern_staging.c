/*
 * What does a truncated .pat leave behind, and what does a pattern from older
 * firmware inherit? (#89, reported as P10)
 *
 * Both readers - preset_loadPatternSD() for a .pat and preset_LoadTmpPat() for
 * the autosave - used to read straight into seq_patternSet, so a file that
 * stopped part way through left the pattern half new and half old. And the
 * timing block, which V02 files do not have, was simply not read, so the
 * pattern kept the flams and shifts of whatever was in that slot before.
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
#include "menu/menu.h"

#define SLOT 3
#define STEPS 64
#define SAVED_NOTE 40          /* what goes into the file */
#define LIVE_NOTE  70          /* what the live pattern holds when it is loaded */
#define MARK_FLAM  3           /* a timing marker a V02 file cannot carry */

static int find_pattern_file(char *out, size_t outsz)
{
	DIR dir;
	FILINFO fno;
	char want[4];

	sprintf(want, "%02d-", SLOT);
	if (f_opendir(&dir, ".") != FR_OK)
		return 0;
	while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
		const char *dot = strrchr(fno.fname, '.');
		if (!strncmp(fno.fname, want, 3) && dot && !strcmp(dot, ".PAT")) {
			snprintf(out, outsz, "%s", fno.fname);
			f_closedir(&dir);
			return 1;
		}
	}
	f_closedir(&dir);
	return 0;
}

static void set_pattern(uint8_t note, uint8_t flam)
{
	for (int trk = 0; trk < NUM_TRACKS; trk++)
		for (int stp = 0; stp < STEPS; stp++) {
			seq_patternSet.seq_subStepPattern[SLOT][trk][stp].note = note;
			seq_patternSet.seq_stepTiming[SLOT][trk][stp].flam = flam;
			seq_patternSet.seq_stepTiming[SLOT][trk][stp].shift = 7;
		}
}

/* How many of the 7x64 steps hold each note, and how many timing bytes hold
   the marker. */
static void census(const char *what, int *saved, int *live, int *marked)
{
	*saved = *live = *marked = 0;
	for (int trk = 0; trk < NUM_TRACKS; trk++)
		for (int stp = 0; stp < STEPS; stp++) {
			const uint8_t n = seq_patternSet.seq_subStepPattern[SLOT][trk][stp].note;
			if (n == SAVED_NOTE) (*saved)++;
			else if (n == LIVE_NOTE) (*live)++;
			if (seq_patternSet.seq_stepTiming[SLOT][trk][stp].flam == MARK_FLAM)
				(*marked)++;
		}
	printf("    %-26s %3d steps from the file, %3d from the live pattern, "
	       "%3d timing bytes marked\n", what, *saved, *live, *marked);
}

/* Rewrite the file as a V02 pattern: same step data, no timing blocks and no
   condition block, which is what firmware before V03 wrote. */
static int make_v02(const char *name)
{
	static uint8_t body[8 + 6 + NUM_TRACKS * (STEPS * 7) + 56 + 1 + NUM_TRACKS + 1];
	FIL f;
	UINT n = 0;
	uint8_t *out = body;

	if (f_open(&f, name, FA_READ) != FR_OK)
		return 0;
	/* 8 name bytes, then the version tag we replace */
	if (f_read(&f, out, 8, &n) != FR_OK || n != 8) { f_close(&f); return 0; }
	out += 8;
	if (f_lseek(&f, 14) != FR_OK) { f_close(&f); return 0; }
	memcpy(out, "PatV02", 6);
	out += 6;
	for (int trk = 0; trk < NUM_TRACKS; trk++) {
		if (f_read(&f, out, STEPS * 7, &n) != FR_OK || n != STEPS * 7) {
			f_close(&f);
			return 0;
		}
		out += STEPS * 7;
		if (f_lseek(&f, f_tell(&f) + STEPS) != FR_OK) { f_close(&f); return 0; }
	}
	/* main steps, shuffle, lengths, kit number: the tail a V02 file keeps */
	if (f_read(&f, out, 56 + 1 + NUM_TRACKS + 1, &n) != FR_OK
	    || n != 56 + 1 + NUM_TRACKS + 1) {
		f_close(&f);
		return 0;
	}
	out += n;
	f_close(&f);

	if (f_open(&f, name, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
		return 0;
	if (f_write(&f, body, (UINT)(out - body), &n) != FR_OK
	    || n != (UINT)(out - body)) {
		f_close(&f);
		return 0;
	}
	f_close(&f);
	printf("    rewrote %s as a V02 pattern, %u bytes\n", name, n);
	return 1;
}

static int truncate_file(const char *name, FSIZE_t at)
{
	FIL f;
	if (f_open(&f, name, FA_READ | FA_WRITE) != FR_OK)
		return 0;
	if (f_lseek(&f, at) != FR_OK || f_truncate(&f) != FR_OK) { f_close(&f); return 0; }
	printf("    truncated %s to %lu bytes\n", name, (unsigned long)f_size(&f));
	f_close(&f);
	return 1;
}

int main(void)
{
	int failures = 0;
	int saved, live, marked;
	char name[16];
	FRESULT res;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(20);

	/* A pattern worth recognising, saved to the card. */
	set_pattern(SAVED_NOTE, 0);
	if (preset_savePatternSD(SLOT) != FR_OK) { puts("FAIL - cannot save"); return 2; }
	if (!find_pattern_file(name, sizeof name)) { puts("FAIL - no pattern file"); return 2; }

	puts("-- a .pat truncated half way through the step data");
	if (!truncate_file(name, 1800)) { puts("FAIL - cannot truncate"); return 2; }
	set_pattern(LIVE_NOTE, MARK_FLAM);
	census("before the load:", &saved, &live, &marked);
	res = preset_loadPatternSD(SLOT);
	printf("    preset_loadPatternSD() returned %d (FR_OK = %d)\n", res, FR_OK);
	census("after the load:", &saved, &live, &marked);
	if (res == FR_OK) { puts("FAIL - a truncated file was accepted"); failures++; }
	if (saved != 0) {
		printf("FAIL - %d steps came from the truncated file\n", saved);
		failures++;
	}
	if (live != NUM_TRACKS * STEPS) {
		printf("FAIL - only %d of %d steps survived\n", live, NUM_TRACKS * STEPS);
		failures++;
	}

	puts("-- a V02 pattern, which has no timing block");
	set_pattern(SAVED_NOTE, 0);
	if (preset_savePatternSD(SLOT) != FR_OK) { puts("FAIL - cannot re-save"); return 2; }
	if (!find_pattern_file(name, sizeof name)) { puts("FAIL - no pattern file"); return 2; }
	if (!make_v02(name)) { puts("FAIL - cannot build a V02 file"); return 2; }
	set_pattern(LIVE_NOTE, MARK_FLAM);
	census("before the load:", &saved, &live, &marked);
	res = preset_loadPatternSD(SLOT);
	printf("    preset_loadPatternSD() returned %d\n", res);
	census("after the load:", &saved, &live, &marked);
	if (res != FR_OK) { puts("FAIL - a V02 file was rejected"); failures++; }
	if (saved != NUM_TRACKS * STEPS) {
		printf("FAIL - %d of %d steps came from the file\n", saved, NUM_TRACKS * STEPS);
		failures++;
	}
	if (marked != 0) {
		printf("FAIL - %d timing bytes were inherited from the previous pattern\n",
		       marked);
		failures++;
	}

	puts("-- non-regression: a full V04 pattern round trip");
	set_pattern(SAVED_NOTE, MARK_FLAM);
	if (preset_savePatternSD(SLOT) != FR_OK) { puts("FAIL - cannot save"); return 2; }
	set_pattern(LIVE_NOTE, 0);
	res = preset_loadPatternSD(SLOT);
	census("after the load:", &saved, &live, &marked);
	if (res != FR_OK) { puts("FAIL - a V04 file was rejected"); failures++; }
	if (saved != NUM_TRACKS * STEPS || marked != NUM_TRACKS * STEPS) {
		puts("FAIL - the round trip did not come back whole");
		failures++;
	}

	printf(failures ? "\n%d failure(s)\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
