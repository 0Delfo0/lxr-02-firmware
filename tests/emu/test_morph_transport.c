/*
 * Does a kit send dispatch the MIDI transport slots as sound parameters? (#103)
 *
 * preset_sendDrumsetParametersImmediate() walks the whole sound-parameter range
 * and sends every entry as a CC. Four of those slots hold MIDI transport state
 * rather than a sound parameter: PAR_NRPN_DATA_ENTRY_COARSE (5),
 * PAR_NRPN_FINE (97), PAR_NRPN_COARSE (98) and PAR_BANK_CHANGE (127), which
 * reach the CC handler as CC 6, 98, 99 and 0 - the same four CCs #86 lists from
 * the recording side.
 *
 * The symptom the report to Erica names, voice 1's filter drive moving, cannot
 * happen in this firmware: USE_FILTER_DRIVE is 0 (config.h:45) and the handler
 * is behind that #if. CC 0 reaches an empty case here as well. What is live is
 * CC 6: midiParser_nrpnHandler() applies the value to whichever NRPN address
 * was selected last, so a host that used an NRPN mute and then changes kit gets
 * the swept value written into that mute.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include "lxr_api.h"
#include "Parameters.h"
#include "Preset/PresetManager.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/Sequencer/sequencer.h"

extern uint8_t activeParameters[];

static void cc(uint8_t number, uint8_t value)
{
	MidiMsg msg;
	msg.status = MIDI_CC;
	msg.data1 = number;
	msg.data2 = value;
	midiParser_ccHandler(msg, 1, 0, 0.f);
}

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(2000);

	/* A host selects the NRPN that mutes voice 1 (CC2_MUTE_1 = 200) and sets
	   it to 0: unmuted, and the NRPN address is now 200. */
	cc(99, 200 >> 7);
	cc(98, 200 & 0x7f);
	cc(6, 0);
	lxr_idle_blocks(50);
	printf("-- host selected NRPN %d (CC2_MUTE_1) and set it to 0\n", CC2_MUTE_1);
	printf("   voice 1 muted: %u\n", seq_isTrackMuted(0));
	if (seq_isTrackMuted(0)) {
		puts("FAIL - voice 1 is already muted, so nothing below means anything");
		return 2;
	}

	/* Now a kit arrives. The slot holding the last NRPN data entry is part of
	   the sweep, and it carries a 1. */
	activeParameters[PAR_NRPN_DATA_ENTRY_COARSE] = 1;
	puts("\n-- a kit send, with 1 left in the NRPN data entry slot");
	preset_sendDrumsetParametersImmediate();
	lxr_idle_blocks(50);

	const uint8_t muted = seq_isTrackMuted(0);
	printf("   voice 1 muted: %u\n", muted);

	if (muted) {
		puts("\nFAIL - the kit send muted voice 1 through the NRPN slot");
		failures++;
	} else {
		puts("\n   still unmuted: the reserved slots were not dispatched");
	}

	printf("%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
