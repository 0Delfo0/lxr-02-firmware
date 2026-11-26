/*
 * After a save fails, is the card still usable? (#7, review item BL7)
 *
 * This build has _FS_LOCK 1 and _FS_REENTRANT 0 (preprocessed from the
 * project's own ffconf.h), so FatFs holds exactly one lock entry: Files[1] in
 * ff.c. f_open() registers the object through inc_lock() (ff.c:3428) and
 * f_close() gives it back through dec_lock() (ff.c:3828). With the single slot
 * taken by a file nobody closed, inc_lock() finds no free entry and returns 0,
 * so the next f_open() of any *other* file fails with FR_TOO_MANY_OPEN_FILES
 * (ff.c:789,3349) and f_unlink()/f_rename() of the stuck file get FR_LOCKED
 * (ff.c:4549,4739). Only f_mount(), which calls clear_lock() (ff.c:3203), or a
 * power cycle clears it - which is what "unusable until reboot" means.
 *
 * The observable is therefore not the failed save. It is what happens *after*:
 * open a brand-new file with plenty of room on the card, and see whether FatFs
 * will have it. That probe is the whole test, and part 1 proves the probe can
 * tell the two states apart before any of it is believed.
 *
 * The free space is swept rather than guessed, and the project folders are
 * removed between runs. preset_saveProject() creates /projNN if it is absent,
 * then /projNNn, chdir's in and writes prj.nfo, so the window where the write
 * is the step that fails is a cluster or two wide and moves with how many
 * directories still have to be made. Leaving the folders behind is what made an
 * earlier version of this sweep miss the defect entirely: the second run needed
 * one mkdir fewer, so prj.nfo fitted and the save failed somewhere harmless.
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
#include "fatfs.h"

static const char *fr(FRESULT r)
{
	switch (r) {
	case FR_OK: return "FR_OK";
	case FR_DISK_ERR: return "FR_DISK_ERR";
	case FR_NO_FILE: return "FR_NO_FILE";
	case FR_NO_PATH: return "FR_NO_PATH";
	case FR_DENIED: return "FR_DENIED";
	case FR_EXIST: return "FR_EXIST";
	case FR_LOCKED: return "FR_LOCKED";
	case FR_TOO_MANY_OPEN_FILES: return "FR_TOO_MANY_OPEN_FILES";
	case FR_NOT_ENOUGH_CORE: return "FR_NOT_ENOUGH_CORE";
	case FR_INVALID_OBJECT: return "FR_INVALID_OBJECT";
	default: return "other";
	}
}

/* Can FatFs still open a file? A fresh name each time, so it cannot collide
   with whatever is stuck, and in the root, which needs no directory lock. */
static FRESULT probe(int n)
{
	char path[20];
	FIL f;
	snprintf(path, sizeof path, "/probe%02d.tst", n);
	FRESULT r = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
	if (r == FR_OK) {
		f_close(&f);
		f_unlink(path);
	}
	return r;
}

/* What a power cycle does to the lock table, without rebooting: f_mount()
   calls clear_lock() (ff.c:3203). Used to make each row of the sweep an
   independent measurement instead of a consequence of the row above. */
static void remount(void)
{
	f_mount(0, SDPath, 0);
	f_mount(&SDFatFS, SDPath, 1);
	f_chdir("/");
}

static void forget_project(void)
{
	f_chdir("/");
	f_unlink("/proj63n/prj.nfo");
	f_unlink("/proj63n/glo.cfg");
	f_rmdir("/proj63n");
	f_rmdir("/proj63o");
	f_unlink("/proj63/prj.nfo");
	f_unlink("/proj63/glo.cfg");
	f_rmdir("/proj63");
	f_chdir("/");
}

int main(void)
{
	int failures = 0, reproduced = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - the emulated device did not boot"); return 2; }
	lxr_idle_blocks(200);
	printf("cluster %u bytes, %llu bytes free\n",
	       card_cluster_bytes(), (unsigned long long)card_free_bytes());

	/* 1. The probe's own positive control. Without this, every
	 *    FR_TOO_MANY_OPEN_FILES below would be an unchecked assumption and
	 *    every FR_OK would be worthless. */
	puts("\n1. does the probe see a held lock entry at all?");
	FRESULT base = probe(0);
	printf("   healthy card                -> %s\n", fr(base));
	FIL held;
	FRESULT ho = f_open(&held, "/held.tst", FA_CREATE_ALWAYS | FA_WRITE);
	FRESULT withHeld = probe(1);
	printf("   one file open and unclosed  -> %s\n", fr(withHeld));
	if (ho == FR_OK) f_close(&held);
	FRESULT afterClose = probe(2);
	printf("   after closing it            -> %s\n", fr(afterClose));
	f_unlink("/held.tst");
	if (base != FR_OK || withHeld != FR_TOO_MANY_OPEN_FILES || afterClose != FR_OK) {
		puts("   FAIL - the probe cannot tell the two states apart; nothing below means anything");
		return 2;
	}
	puts("   the probe works: one unclosed handle blocks the whole card");

	/* 2. The real path, across the window where the prj.nfo write is the step
	 *    that fails. */
	/* The folder half is #129, in the same place and for free: after :2574
	   preset_saveProject() must take /projNNn with it on any failure, which is
	   what preset_abortSdOperation() is for and what three of its exits used
	   to skip. */
	puts("\n2. preset_saveProject(63) with the card nearly full");
	puts("    free left   the save returned        the card afterwards   /proj63n");
	static const uint32_t leave[] = { 0, 1024, 1536, 2048, 2560, 3072, 4096, 5120 };
	for (unsigned i = 0; i < sizeof leave / sizeof leave[0]; i++) {
		forget_project();
		card_remove_filler();
		uint64_t left = card_fill_leaving(leave[i]);
		FRESULT save = preset_saveProject(63);
		/* Give the space back first, so a probe that fails now fails for the
		   lock and not for the room. */
		card_remove_filler();
		FRESULT p = probe(10 + i);
		FILINFO fno;
		const int leftover = (save != FR_OK) && (f_stat("/proj63n", &fno) == FR_OK);
		printf("     %6llu     %-22s   %-22s %s\n",
		       (unsigned long long)left, fr(save), fr(p),
		       leftover ? "LEFT BEHIND" : "gone");
		if (p == FR_TOO_MANY_OPEN_FILES) { reproduced++; failures++; }
		else if (p != FR_OK) { puts("        unexpected probe result"); failures++; }
		if (leftover) {
			puts("        FAIL - a failed save left /proj63n on the card (#129)");
			failures++;
		}
		remount();	/* so the next row measures itself, not this one */
	}

	/* 3. Why it matters: the leak is not one failed save, it is every save
	 *    afterwards. Reached by running the window again and then asking for a
	 *    save with the whole card free. */
	puts("\n3. what the next save gets, after one that leaked");
	forget_project();
	card_remove_filler();
	card_fill_leaving(2048);
	FRESULT first = preset_saveProject(63);
	card_remove_filler();
	forget_project();
	printf("   the save in the window      -> %s\n", fr(first));
	printf("   %llu bytes free now\n", (unsigned long long)card_free_bytes());
	FRESULT second = preset_saveProject(63);
	printf("   the next save, card empty   -> %s\n", fr(second));
	if (second == FR_TOO_MANY_OPEN_FILES) {
		puts("   FAIL - nothing can be saved any more: this is the reboot the issue names");
		failures++;
	} else if (second != FR_OK) {
		printf("   the next save did not succeed either (%s), but not for the lock\n", fr(second));
	} else {
		puts("   the card still works: the failed save gave its lock entry back");
	}
	remount();

	if (reproduced)
		printf("\nFAILURES - %d of %zu runs left a file open: the card stays unusable "
		       "until the next f_mount\n", reproduced, sizeof leave / sizeof leave[0]);
	else
		puts("\nall checks passed - every failed save gave the lock entry back");
	return failures ? 1 : 0;
}
