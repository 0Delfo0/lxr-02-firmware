/*
 * Does a song reaching its end stop like a stop? (#87, review item S-B7)
 *
 * The SONG_END branch called seq_setRunning(0) and nothing else, so no MIDI
 * stop went out and the panel's START/STOP state still said running - which
 * makes the next press of START read as a stop.
 *
 * Both halves are observable here: the MIDI byte through lxr_set_midi_out(),
 * and the panel state through what it decides - one press of START/STOP after
 * the song has ended must start the sequencer, not stop it.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include "lxr_api.h"
#include "buttonHandler.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/Sequencer/sequencer.h"

#define SONG_END_MARKER MAX_PATTERN      /* 64, as menu.c defines it */

/* sequencer.c has the flag but no accessor for it, and adding one to the
   firmware to serve a test would cost flash for nothing. */
extern uint8_t seq_songModeEnabled;

static int saw_start, saw_stop;

static void midi_out(uint8_t b, void *user)
{
	(void)user;
	if (b == MIDI_START) saw_start++;
	if (b == MIDI_STOP)  saw_stop++;
}

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(2000);
	lxr_set_midi_out(midi_out, NULL);

	/* a song of one entry: pattern 0, then the end */
	seq_activeSong[0] = 0;
	seq_activeSong[1] = SONG_END_MARKER;

	/* Song mode is armed by SHIFT + START/STOP and by nothing else
	   (buttonHandler.c, case BUT_START_STOP): the press *without* SHIFT calls
	   seq_setSongModeEnabled(0), so a bare start undoes any setup above it and
	   seq_determineNextPattern() keeps returning the active pattern - the song
	   is never followed and the end marker is never reached. Two earlier
	   attempts at this test and two at the device procedure died on exactly
	   that, each time reading as the fix not working. Hold SHIFT. */
	lxr_button(BUT_SHIFT, 1);
	lxr_button(BUT_START_STOP, 1); lxr_idle_blocks(8); lxr_button(BUT_START_STOP, 0);
	lxr_button(BUT_SHIFT, 0);
	lxr_idle_blocks(200);
	if (!seq_songModeEnabled) {
		puts("FAIL - song mode is not armed, so the song would not be followed");
		return 2;
	}
	printf("-- started: MIDI start seen %d, running %d\n", saw_start, seq_isRunning());
	if (!seq_isRunning()) {
		puts("FAIL - the sequencer did not start, so nothing below means anything");
		return 2;
	}
	saw_stop = 0;

	/* let the song run until it reaches the end marker */
	int blocks = 0;
	while (seq_isRunning() && blocks < 200000) { lxr_idle_blocks(500); blocks += 500; }
	printf("-- the song ended after %d blocks: running %d\n", blocks, seq_isRunning());
	if (seq_isRunning()) {
		puts("FAIL - the song never ended; the test cannot say anything");
		return 2;
	}

	lxr_idle_blocks(200);
	printf("\n   MIDI stop sent:      %s\n", saw_stop ? "yes" : "NO");

	if (!saw_stop) {
		puts("FAIL - no MIDI stop: anything synced to our clock keeps running");
		failures++;
	}

	/* The panel's half of the defect, measured the way it is felt rather than
	   by reading a LED: the panel keeps its own seqRunning
	   (buttonHandler_setRunStopState), and if the song end never told it, the
	   next press of START/STOP is handed 1 - seqRunning = 0 and reads as a
	   stop, so the machine does not start until the button is pressed twice.
	   An earlier version of this test asserted on LED_START_STOP instead; that
	   LED reads 0 even while the sequencer is running, so the assertion could
	   not fire in either direction and half of this test was dead. */
	saw_start = 0;
	lxr_button(BUT_START_STOP, 1); lxr_idle_blocks(8); lxr_button(BUT_START_STOP, 0);
	lxr_idle_blocks(200);
	printf("   one press of START:  running %d, MIDI start seen %d\n",
	       seq_isRunning(), saw_start);
	if (!seq_isRunning()) {
		puts("FAIL - one press of START did not start it: the panel still said running");
		failures++;
	}

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
