/*
 * Do both documented NRPN mute ranges actually mute? (#76)
 *
 * There are two paths and they used to write different bitmasks:
 *
 *   NRPN 200..206 (CC2_MUTE_x)  -> midiParser_ccHandler() -> seq_setMute()
 *                                  -> seq_mutedTracks, which the sequencer
 *                                     and the engine test
 *   NRPN 1000..1006 (NRPN_MUTE_x) -> frontPanel_ccHandler() ->
 *                                  buttonHandler_muteVoice() ->
 *                                  buttonHandler_mutedVoices, which only the
 *                                  voice LEDs are drawn from
 *
 * So the range docs/params.json documented lit the LED and left the voice
 * playing - measured on the device at 0.0 dB against -62.5 dB for the other
 * range. The observable here is seq_isTrackMuted(), which is the gate itself
 * rather than a proxy for it.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include "lxr_api.h"
#include "Soundengine/Sequencer/sequencer.h"

static void nrpn(uint8_t chan, uint16_t nr, uint8_t value)
{
	const uint8_t msg[] = {
		(uint8_t)(0xB0 | chan), 99, (uint8_t)(nr >> 7),
		(uint8_t)(0xB0 | chan), 98, (uint8_t)(nr & 0x7f),
		(uint8_t)(0xB0 | chan),  6, value,
	};
	lxr_midi_in(msg, sizeof msg);
	lxr_idle_blocks(20);
}

static int check(const char *what, uint16_t nr, uint8_t voice, int expect)
{
	nrpn(0, nr, expect ? 1 : 0);
	const int got = seq_isTrackMuted(voice) ? 1 : 0;
	printf("  %-28s voice %d -> seq_isTrackMuted = %d (expected %d)%s\n",
	       what, voice + 1, got, expect, got == expect ? "" : "   FAIL");
	return got == expect;
}

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(400);

	printf("at boot: voice 1 muted = %d (expected 0)\n", seq_isTrackMuted(0) ? 1 : 0);
	if (seq_isTrackMuted(0)) { puts("FAIL - a voice is muted before anything was sent"); return 2; }

	/* 1. the control: the range that always worked. If this fails the NRPN is
	 *    not reaching the firmware at all and nothing below means anything. */
	puts("\n1. NRPN 200..206, the path that already worked");
	if (!check("NRPN 200 = 1", 200, 0, 1)) { puts("   the control failed: the NRPN never arrived"); return 2; }
	failures += !check("NRPN 200 = 0", 200, 0, 0);

	/* 2. the documented range */
	puts("\n2. NRPN 1000..1006, the range docs/params.json documents");
	failures += !check("NRPN 1000 = 1", 1000, 0, 1);
	failures += !check("NRPN 1000 = 0", 1000, 0, 0);

	/* 3. a different voice, so the bit position is checked too and not just
	 *    "something was muted" */
	puts("\n3. a different voice");
	failures += !check("NRPN 1003 = 1", 1003, 3, 1);
	printf("  voice 1 still unmuted  -> %d (expected 0)%s\n",
	       seq_isTrackMuted(0) ? 1 : 0, seq_isTrackMuted(0) ? "   FAIL" : "");
	failures += seq_isTrackMuted(0) ? 1 : 0;
	failures += !check("NRPN 1003 = 0", 1003, 3, 0);

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
