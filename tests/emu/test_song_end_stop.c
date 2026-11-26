/*
 * Does a song reaching its end stop like a stop? (#87, review item S-B7)
 *
 * The SONG_END branch called seq_setRunning(0) and nothing else, so no MIDI
 * stop went out and the panel's START/STOP state still said running - which
 * makes the next press of START read as a stop.
 *
 * Both halves are observable here: the MIDI byte through lxr_set_midi_out()
 * and the panel's state through its LED, which is the same bit the next press
 * is decided from.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include "lxr_api.h"
#include "buttonHandler.h"
#include "ledHandler.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/Sequencer/sequencer.h"

#define SONG_END_MARKER MAX_PATTERN      /* 64, as menu.c defines it */

static int saw_start, saw_stop;

static void midi_out(uint8_t b, void *user)
{
	(void)user;
	if (b == MIDI_START) saw_start++;
	if (b == MIDI_STOP)  saw_stop++;
}

static int run_led(void)
{
	const uint64_t m = (uint64_t)lxr_get_leds_lo() | ((uint64_t)lxr_get_leds_hi() << 32);
	return (int)((m >> LED_START_STOP) & 1);
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
	seq_setSongModeEnabled(1);

	lxr_button(BUT_START_STOP, 1); lxr_idle_blocks(8); lxr_button(BUT_START_STOP, 0);
	lxr_idle_blocks(200);
	printf("-- started: MIDI start seen %d, START/STOP led %d, running %d\n",
	       saw_start, run_led(), seq_isRunning());
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
	printf("   START/STOP led now:  %d  (1 = the panel still thinks it is running)\n",
	       run_led());

	if (!saw_stop) {
		puts("FAIL - no MIDI stop: anything synced to our clock keeps running");
		failures++;
	}
	if (run_led()) {
		puts("FAIL - the panel still says running, so the next START reads as a stop");
		failures++;
	}

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
