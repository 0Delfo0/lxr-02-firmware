/*
 * Does the song sentinel escape into the page cursor? (#120)
 *
 * seq_songPositionPointer is 255 before the first song step, on purpose, so
 * that nextStep() increments it to 0. The sequencer copies it into
 * menu_songMenuPosition on every pattern change, and that cursor indexes
 * seq_activeSong - which is a uint8_t* into seq_songData[64][64], so index 255
 * from slot s is slot s+3 offset 63. From the default slot that is inside the
 * same object, so a sanitizer says nothing and the damage is another project's
 * song list.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include "lxr_api.h"
#include "menu/menu.h"
#include "Soundengine/Sequencer/sequencer.h"

extern uint8_t seq_songPositionPointer;
/* neither of these is declared in a header */
extern void seq_setPendingPatternActive(void);
extern void menu_handleSongMenu(int8_t inc, uint8_t btnClicked);

/* So the same test links against a build without the fix, where the sequencer
   assigns the cursor directly. The real one clamps; this one does what the old
   code did. */
__attribute__((weak)) void menu_setSongPosition(uint8_t pos)
{
	menu_songMenuPosition = pos;
}

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(2000);

	printf("-- where index 255 of the active song actually lands\n");
	printf("   &seq_activeSong[255]   = %p\n", (void *)&seq_activeSong[255]);
	printf("   &seq_songData[3][63]   = %p   %s\n", (void *)&seq_songData[3][63],
	       (&seq_activeSong[255] == &seq_songData[3][63]) ? "same byte" : "DIFFERENT");
	printf("   so with slot 0 active it is inside seq_songData: no sanitizer report,\n");
	printf("   and it is project 3's last song entry.\n\n");

	puts("-- arm song mode while stopped, which sets the sentinel");
	seq_setSongModeEnabled(1);
	printf("   seq_songPositionPointer = %u\n", seq_songPositionPointer);
	if (seq_songPositionPointer != 255) {
		puts("FAIL - the sentinel is not 255, so this test proves nothing");
		return 2;
	}

	puts("-- a pattern change, which is what copies it into the page cursor");
	menu_setSongPosition(7);                 /* a known good value first */
	seq_setPendingPatternActive();
	printf("   menu_songMenuPosition = %u\n", menu_songMenuPosition);

	if (menu_songMenuPosition >= MAX_SONG_LENGTH) {
		printf("FAIL - the cursor holds %u, which indexes seq_activeSong past its 64 entries\n",
		       menu_songMenuPosition);
		failures++;
	} else {
		printf("   in range: 0..%d\n", MAX_SONG_LENGTH - 1);
	}

	puts("\n-- and the cursor's own navigation repairs an out-of-range value");
	menu_songMenuPosition = 255;             /* as the old copy would leave it */
	menu_handleSongMenu(-1, 0);              /* turning down used to give 254 */
	printf("   after turning down from 255: %u\n", menu_songMenuPosition);
	if (menu_songMenuPosition >= MAX_SONG_LENGTH) {
		puts("FAIL - turning down left the cursor out of range");
		failures++;
	}

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
