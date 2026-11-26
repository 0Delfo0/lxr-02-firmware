/*
 * Are the reserved MIDI CCs recorded as step automation? (#86, review item K-1d)
 *
 * seq_recordAutomation() wrote whatever CC arrived into the step's automation
 * destination. Bank select and the three NRPN CCs are not sound parameters, so
 * a host sending an NRPN while REC is on filled the pattern with destinations
 * that mean nothing.
 *
 * The positive control is the half that matters: a guard that stops those four
 * must not stop the others, or automation recording is gone.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include "lxr_api.h"
#include "buttonHandler.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/Sequencer/sequencer.h"

#define VOICE 0
#define PAT   0

static void send_cc(uint8_t number, uint8_t value)
{
	MidiMsg msg;
	msg.status = (uint8_t)(MIDI_CC | 0);	/* channel 1 */
	msg.data1 = number;
	msg.data2 = value;
	midiParser_parseMidiMessage(msg);
	lxr_idle_blocks(30);
}

static uint8_t dest_of(uint8_t step)
{
	return seq_patternSet.seq_subStepPattern[PAT][VOICE][step].param1Nr;
}

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(2000);

	printf("-- the four reserved CCs, by number: bank %d, data entry %d, fine %d, coarse %d\n",
	       CC_BANK_CHANGE, NRPN_DATA_ENTRY_COARSE, NRPN_FINE, NRPN_COARSE);

	/* every step of voice 0 active, so whatever step the sequencer is on can
	   take a lock, and REC armed with the sequencer running */
	for (uint8_t s = 0; s < 16; s++) seq_setMainStep(PAT, VOICE, s, 1);
	seq_setRecordingMode(1);
	lxr_button(BUT_START_STOP, 1); lxr_idle_blocks(8); lxr_button(BUT_START_STOP, 0);
	lxr_idle_blocks(600);

	/* clear the destinations we are about to watch */
	for (uint8_t s = 0; s < 16; s++)
		seq_patternSet.seq_subStepPattern[PAT][VOICE][s].param1Nr = 0xFF;

	const uint8_t reserved[] = { CC_BANK_CHANGE, NRPN_DATA_ENTRY_COARSE, NRPN_FINE, NRPN_COARSE };
	for (unsigned i = 0; i < 4; i++) {
		send_cc(reserved[i], 64);
		int written = 0;
		for (uint8_t s = 0; s < 16; s++) if (dest_of(s) == reserved[i]) written = 1;
		printf("   CC %3u recorded into a step: %s\n", reserved[i], written ? "YES" : "no");
		if (written) failures++;
	}

	/* positive control: a CC that IS a sound parameter must still record */
	const uint8_t real_cc = 55;	/* PAR_VELOD3, a per-voice sound parameter */
	for (uint8_t s = 0; s < 16; s++)
		seq_patternSet.seq_subStepPattern[PAT][VOICE][s].param1Nr = 0xFF;
	send_cc(real_cc, 100);
	int recorded = 0;
	for (uint8_t s = 0; s < 16; s++) if (dest_of(s) == real_cc) recorded = 1;
	printf("\n   positive control, CC %u recorded: %s\n", real_cc, recorded ? "yes" : "NO");
	if (!recorded) {
		puts("FAIL - automation recording is broken, not filtered");
		failures++;
	}

	lxr_button(BUT_START_STOP, 1); lxr_idle_blocks(8); lxr_button(BUT_START_STOP, 0);
	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
