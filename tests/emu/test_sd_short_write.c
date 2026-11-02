/*
 * What does the firmware report when the card fills up in the middle of a
 * write? (issues #3, #69, #72, #73)
 *
 * FatFs reports a full disk from f_write() as FR_OK with a short count. That is
 * the failure that looks like success, and it is the one preset_writeExact()
 * was added for. This runs the volume out of space for real - see card_fill.h
 * for why there is no fault-injection hook - and then asks the save paths what
 * they return.
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
#include "card_fill.h"

static const char *fr(FRESULT r)
{
	switch (r) {
	case FR_OK: return "FR_OK";
	case FR_DISK_ERR: return "FR_DISK_ERR";
	case FR_INT_ERR: return "FR_INT_ERR";
	case FR_NOT_READY: return "FR_NOT_READY";
	case FR_NO_FILE: return "FR_NO_FILE";
	case FR_NO_PATH: return "FR_NO_PATH";
	case FR_DENIED: return "FR_DENIED";
	case FR_EXIST: return "FR_EXIST";
	case FR_WRITE_PROTECTED: return "FR_WRITE_PROTECTED";
	case FR_INVALID_PARAMETER: return "FR_INVALID_PARAMETER";
	default: return "other";
	}
}

/* A raw write of a .pat-sized block, to show the shape of the failure. */
static void show_raw_write(const char *path, UINT size)
{
	static uint8_t buf[4096];
	FIL f;
	UINT bw = 0;
	FRESULT res;

	memset(buf, 0x5A, sizeof buf);
	res = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
	if (res != FR_OK) {
		printf("    open %-14s -> %s\n", path, fr(res));
		return;
	}
	res = f_write(&f, buf, size, &bw);
	printf("    f_write(%u) -> %s, %u bytes written%s\n", size, fr(res), bw,
	       (res == FR_OK && bw != size) ? "   <<< looks like success" : "");
	f_close(&f);
}

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - the emulated device did not boot"); return 2; }
	lxr_idle_blocks(20);
	printf("booted; cluster %u bytes, %llu bytes free\n",
	       card_cluster_bytes(), (unsigned long long)card_free_bytes());

	/* 1. The failure that looks like success, with a few clusters left. */
	printf("\n1. a few kilobytes free, then a .pat-sized write\n");
	printf("   filled, %llu bytes left\n",
	       (unsigned long long)card_fill_leaving(3 * 1024));
	show_raw_write("/probe1.bin", 3831);

	/* 2. Nothing left at all: the all-or-nothing case. */
	printf("\n2. nothing free at all\n");
	printf("   filled, %llu bytes left\n",
	       (unsigned long long)card_fill_leaving(0));
	show_raw_write("/probe2.bin", 3831);

	/* 3. What the firmware's own save paths report. A kit is 261 bytes and a
	 *    pattern 3831, so with a few clusters free the kit fits and the
	 *    pattern does not: the interesting boundary. */
	printf("\n3. the firmware's save paths, with a few clusters free\n");
	card_remove_filler();
	printf("   filler removed, %llu bytes free\n",
	       (unsigned long long)card_free_bytes());
	printf("   filled, %llu bytes left\n",
	       (unsigned long long)card_fill_leaving(4 * 1024));

	FRESULT res = preset_saveDrumsetSD(0, 0);
	printf("    preset_saveDrumsetSD(0)  -> %s\n", fr(res));
	res = preset_savePatternSD(0);
	printf("    preset_savePatternSD(0)  -> %s\n", fr(res));
	res = preset_saveGlobals();
	printf("    preset_saveGlobals()     -> %s\n", fr(res));

	/* Every one of them must say something other than FR_OK: the card is full
	 * and none of these files can have been written whole. */
	if (res == FR_OK) {
		puts("    FAIL - preset_saveGlobals() reported success on a full card");
		failures++;
	}

	printf("\n%s\n", failures ? "FAILURES" : "done");
	return failures ? 1 : 0;
}
