/*
 * Fill the emulated SD card to a chosen amount of free space, so a save runs
 * into a genuinely full card instead of a simulated one.
 *
 * Why there is no fault-injection hook here: FatFs reports a full disk from
 * f_write() as FR_OK with a short count, and it does that when it cannot
 * allocate the next cluster - a decision taken inside FatFs, not by the disk
 * driver. A hook in the RAM disk could only make disk_write fail, which FatFs
 * turns into FR_DISK_ERR, a different failure that any call site checking the
 * return value already catches. The condition worth testing is the one that
 * looks like success, and the way to reach it is to run the volume out of
 * space for real.
 *
 * Measured on this card: 1024-byte clusters (2 sectors), and filling it made
 * f_write(32768) return FR_OK with 3072 bytes written. So leaving a few
 * kilobytes free puts the next multi-kilobyte file - a .pat is 3831 bytes -
 * squarely in the partial-write case.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef CARD_FILL_H_
#define CARD_FILL_H_

#include <stdint.h>
#include <string.h>
#include "ff.h"

#define CARD_FILLER_PATH "/filler.bin"

/* Free bytes on the volume, or 0 if it cannot be read. */
static inline uint64_t card_free_bytes(void)
{
	DWORD nclst;
	FATFS *fs;
	if (f_getfree("", &nclst, &fs) != FR_OK)
		return 0;
	return (uint64_t)nclst * fs->csize * 512u;
}

/* Cluster size in bytes, which is the granularity everything below works at. */
static inline uint32_t card_cluster_bytes(void)
{
	DWORD nclst;
	FATFS *fs;
	if (f_getfree("", &nclst, &fs) != FR_OK)
		return 0;
	return (uint32_t)fs->csize * 512u;
}

/* Write a filler file until at most `leave` bytes are free. Returns the free
 * bytes actually left, which is a whole number of clusters and so may be less
 * than asked for. */
static inline uint64_t card_fill_leaving(uint64_t leave)
{
	static uint8_t block[32768];
	FIL f;
	UINT bw;

	memset(block, 0xA5, sizeof block);
	if (f_open(&f, CARD_FILLER_PATH, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
		return card_free_bytes();

	while (card_free_bytes() > leave) {
		uint64_t over = card_free_bytes() - leave;
		UINT want = over > sizeof block ? (UINT)sizeof block : (UINT)over;
		if (want == 0)
			break;
		if (f_write(&f, block, want, &bw) != FR_OK || bw != want)
			break;	/* ran out earlier than the arithmetic expected */
	}
	f_close(&f);
	return card_free_bytes();
}

/* Give the space back, so one test can run several cases. */
static inline FRESULT card_remove_filler(void)
{
	return f_unlink(CARD_FILLER_PATH);
}

#endif /* CARD_FILL_H_ */
