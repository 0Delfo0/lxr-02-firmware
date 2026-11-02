/*
 * Are the numbers read off the card range checked before they are used as
 * indices? (#91, reported as K-2 and P8)
 *
 * Three fields, all written by whoever last touched the card:
 *   - seven track lengths at the end of a .pat body,
 *   - the pattern's kit number, the byte after them,
 *   - the project number in /proj.idx.
 *
 * The consequence of an unchecked track length is in seq_processFlam()
 * (sequencer.c:1146): on step 0 it takes prevStep = length-1 and hands that
 * step number to seq_getShiftFlamPattern(), which asks
 * seq_intIsMainStepActive() for bar 199/16 = 12 of
 * seq_mainSteps[4][64][7] (sequencer.c:2169). The last section plays the
 * loaded pattern, which is where a build without the range check produces
 * that report.
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
#include "buttonHandler.h"

#define SLOT 3                 /* the pattern slot used for the file */
#define COND_BYTES SEQ_CONDITION_BYTES

/* The lengths a damaged file might carry. 0 is legal - the sequencer reads it
   as 16 (sequencer.c:1413) - so the check must leave it alone. */
static const uint8_t poison[NUM_TRACKS] = { 200, 0, 255, 64, 65, 100, 16 };
static const uint8_t expected[NUM_TRACKS] = { 64, 0, 64, 64, 64, 64, 16 };
static const uint8_t long_only[NUM_TRACKS] = { 200, 200, 200, 200, 200, 200, 200 };
#define POISON_KIT 200

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

/* Overwrite the seven length bytes and the kit byte in place. They sit
   immediately before the 168-byte trig condition block, in the order the
   reader takes them (presetManager.c:2105-2121). */
static int poison_pattern_file(const char *name, const uint8_t *lengths)
{
	FIL f;
	UINT n = 0;
	uint8_t buf[NUM_TRACKS + 1];
	FSIZE_t at;

	if (f_open(&f, name, FA_READ | FA_WRITE) != FR_OK)
		return 0;
	if (f_size(&f) < COND_BYTES + sizeof buf) { f_close(&f); return 0; }
	at = f_size(&f) - COND_BYTES - sizeof buf;

	memcpy(buf, lengths, NUM_TRACKS);
	buf[NUM_TRACKS] = POISON_KIT;
	if (f_lseek(&f, at) != FR_OK) { f_close(&f); return 0; }
	if (f_write(&f, buf, sizeof buf, &n) != FR_OK || n != sizeof buf) {
		f_close(&f);
		return 0;
	}
	f_close(&f);
	printf("    patched %s at offset %lu: lengths", name, (unsigned long)at);
	for (int i = 0; i < NUM_TRACKS; i++)
		printf(" %u", lengths[i]);
	printf(", kit %u\n", POISON_KIT);
	return 1;
}

int main(void)
{
	int failures = 0;
	char name[16];
	static float block[256 * 2];
	uint8_t projNr = 0;
	FIL f;
	UINT n = 0;
	uint8_t byte;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(20);

	puts("-- track lengths and the kit number, from a .pat file");
	if (preset_savePatternSD(SLOT) != FR_OK) { puts("FAIL - cannot save a pattern"); return 2; }
	if (!find_pattern_file(name, sizeof name)) { puts("FAIL - no pattern file"); return 2; }
	if (!poison_pattern_file(name, poison)) { puts("FAIL - cannot patch the file"); return 2; }

	if (preset_loadPatternSD(SLOT) != FR_OK) { puts("FAIL - cannot load the pattern"); return 2; }
	printf("    after the load: lengths");
	for (int trk = 0; trk < NUM_TRACKS; trk++)
		printf(" %u", seq_patternSet.length[SLOT][trk]);
	printf(", kit %u\n", seq_patternSet.kitNr[SLOT]);

	for (int trk = 0; trk < NUM_TRACKS; trk++)
		if (seq_patternSet.length[SLOT][trk] != expected[trk]) {
			printf("FAIL - track %d: %u in the file, %u stored, expected %u\n",
			       trk, poison[trk], seq_patternSet.length[SLOT][trk], expected[trk]);
			failures++;
		}
	if (seq_patternSet.kitNr[SLOT] >= NUM_KIT_SLOTS) {
		printf("FAIL - kit number %u is past mainParameters[%d]\n",
		       seq_patternSet.kitNr[SLOT], NUM_KIT_SLOTS);
		failures++;
	}

	puts("-- the project number, from /proj.idx");
	byte = 200;
	if (f_open(&f, "/proj.idx", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
		puts("FAIL - cannot write /proj.idx");
		return 2;
	}
	f_write(&f, &byte, 1, &n);
	f_close(&f);
	if (preset_loadLastUsedProjectNrFromSD(&projNr) != FR_OK) {
		puts("FAIL - cannot read /proj.idx");
		return 2;
	}
	printf("    /proj.idx holds %u, loaded as %u\n", byte, projNr);
	if (projNr >= NUM_KIT_SLOTS) {
		printf("FAIL - project number %u is past savefileNames[%d]\n",
		       projNr, NUM_KIT_SLOTS);
		failures++;
	}

	puts("-- playing the loaded pattern: seq_processFlam() reads length-1");
	/* All seven lengths at 200 this time. A length of 0 is a different
	   defect: it reaches the same line as a modulo by zero, which the range
	   check must not paper over because the Euclidean generator writes 0 at
	   runtime as well. It has an issue of its own. */
	if (!poison_pattern_file(name, long_only)) { puts("FAIL - cannot patch the file"); return 2; }
	if (preset_loadPatternSD(SLOT) != FR_OK) { puts("FAIL - cannot load the pattern"); return 2; }
	lxr_button_click(BUT_START_STOP);
	/* Only as far as the start itself: step 0 is the index that takes the
	   length-1 path, and it is where the sequencer begins. */
	for (int i = 0; i < 400 && !seq_isRunning(); i++)
		lxr_render(block, 256, 2);
	seq_activePattern = SLOT;    /* the start reset it to 0 */
	printf("    running %u, active pattern %u, lengths", seq_isRunning(), seq_activePattern);
	for (int trk = 0; trk < NUM_TRACKS; trk++)
		printf(" %u", seq_patternSet.length[seq_activePattern][trk]);
	printf("\n    calling seq_processFlam(0): step 0 takes the length-1 path\n");
	fflush(stdout);
	seq_processFlam(0);
	puts("    returned");
	lxr_button_click(BUT_START_STOP);
	for (int i = 0; i < 20; i++)
		lxr_render(block, 256, 2);

	puts("-- non-regression: a pattern saved and reloaded untouched");
	for (int trk = 0; trk < NUM_TRACKS; trk++)
		seq_patternSet.length[SLOT][trk] = (uint8_t)(trk == 0 ? 16 : 32);
	seq_patternSet.kitNr[SLOT] = 7;
	if (preset_savePatternSD(SLOT) != FR_OK) { puts("FAIL - cannot re-save"); return 2; }
	memset(seq_patternSet.length[SLOT], 0, NUM_TRACKS);
	seq_patternSet.kitNr[SLOT] = 0;
	if (preset_loadPatternSD(SLOT) != FR_OK) { puts("FAIL - cannot re-load"); return 2; }
	printf("    lengths");
	for (int trk = 0; trk < NUM_TRACKS; trk++)
		printf(" %u", seq_patternSet.length[SLOT][trk]);
	printf(", kit %u\n", seq_patternSet.kitNr[SLOT]);
	for (int trk = 0; trk < NUM_TRACKS; trk++)
		if (seq_patternSet.length[SLOT][trk] != (trk == 0 ? 16 : 32)) {
			printf("FAIL - track %d came back as %u\n", trk,
			       seq_patternSet.length[SLOT][trk]);
			failures++;
		}
	if (seq_patternSet.kitNr[SLOT] != 7) {
		printf("FAIL - kit number came back as %u\n", seq_patternSet.kitNr[SLOT]);
		failures++;
	}

	printf(failures ? "\n%d failure(s)\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
