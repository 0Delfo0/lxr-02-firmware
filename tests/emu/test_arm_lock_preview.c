/*
 * When a step is armed, whose lock does the preview show, and on which
 * parameter? (#13, review item U-B2)
 *
 * Holding a step button for BUTTON_TIMEOUT arms it for automation, and
 * buttonHandler_armTimerActionStep() previews the step's locks by writing
 * their values into activeParameters, remembering the originals so the disarm
 * can put them back. Two things about those twenty-five lines
 * (buttonHandler.c:144-165):
 *
 *   - the step is read from seq_subStepPattern[pattern][0][stepNr] - voice
 *     **0**, hardcoded - while the line immediately above sends the panel
 *     menu_getActiveVoice(). So arming a step on voice 4 previews voice 1's
 *     lock;
 *   - the parameter number is taken as param1Nr - 1, with the empty slot
 *     tested as 254. But the stored number is raw: seq_recordAutomation()
 *     writes dest unchanged (sequencer.c:2549), autoNode_setDestination()
 *     reads it unchanged and treats it as a CC below 128 and as CC2 above
 *     (automationNode.c:22-41), and the empty slot is NO_AUTOMATION = 255. So
 *     the preview lands one parameter below the lock's own.
 *
 * The review described neither: it said the lock slots are never freed and run
 * out. They are freed - buttonHandler_disarmTimerActionStep() clears
 * resetLock1 and resetLock2 on the only path through it
 * (buttonHandler.c:241,271) - so that half of U-B2 is not present here.
 *
 * The preview is only visible while the button is held: the release calls
 * buttonHandler_TimerActionOccured(), which disarms and restores. So the
 * reading is taken mid-gesture, and the restore is checked afterwards.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "lxr_api.h"
#include "buttonHandler.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "menu/menu.h"

#define VOICE      3        /* the voice whose lock must be previewed */
#define STEP       3        /* step button 4 -> step index 3 */
#define PAR_VOICE  20       /* the lock on VOICE */
#define VAL_VOICE  100
#define PAR_ZERO   30       /* a different lock on voice 0, same step */
#define VAL_ZERO   50

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(400);

	PatternSet *ps = get_seq_patternSet_p();
	const uint8_t pat = seq_activePattern;

	/* both steps carry a lock, on different parameters with different values,
	   so the readout names which one was used */
	Step *sv = &ps->seq_subStepPattern[pat][VOICE][STEP];
	Step *s0 = &ps->seq_subStepPattern[pat][0][STEP];
	sv->param1Nr = PAR_VOICE; sv->param1Val = VAL_VOICE;
	sv->param2Nr = NO_AUTOMATION;
	s0->param1Nr = PAR_ZERO;  s0->param1Val = VAL_ZERO;
	s0->param2Nr = NO_AUTOMATION;

	/* pick the voice, in voice mode, the way the panel does */
	lxr_button(BUT_VOICE_4, 1); lxr_idle_blocks(8);
	lxr_button(BUT_VOICE_4, 0); lxr_idle_blocks(200);
	printf("active voice %u (expected %d)\n", menu_getActiveVoice(), VOICE);
	if (menu_getActiveVoice() != VOICE) {
		puts("FAIL - could not select the voice; nothing below means anything");
		return 2;
	}

	const uint8_t b_voice   = activeParameters[PAR_VOICE];
	const uint8_t b_voiceM1 = activeParameters[PAR_VOICE - 1];
	const uint8_t b_zero    = activeParameters[PAR_ZERO];
	const uint8_t b_zeroM1  = activeParameters[PAR_ZERO - 1];

	/* hold the step past BUTTON_TIMEOUT (2000 ticks of 250 us = 500 ms) and
	   read while it is still down */
	lxr_button(BUT_SEQ4, 1);
	lxr_idle_blocks(4000);
	const uint8_t a_voice   = activeParameters[PAR_VOICE];
	const uint8_t a_voiceM1 = activeParameters[PAR_VOICE - 1];
	const uint8_t a_zero    = activeParameters[PAR_ZERO];
	const uint8_t a_zeroM1  = activeParameters[PAR_ZERO - 1];
	lxr_button(BUT_SEQ4, 0);
	lxr_idle_blocks(400);

	printf("\n                        before  while armed   expected while armed\n");
	printf("  par %-3d (voice %d lock) %4u    %4u          %u\n",
	       PAR_VOICE, VOICE, b_voice, a_voice, VAL_VOICE);
	printf("  par %-3d                %4u    %4u          %u (unchanged)\n",
	       PAR_VOICE - 1, b_voiceM1, a_voiceM1, b_voiceM1);
	printf("  par %-3d (voice 1 lock) %4u    %4u          %u (unchanged)\n",
	       PAR_ZERO, b_zero, a_zero, b_zero);
	printf("  par %-3d                %4u    %4u          %u (unchanged)\n",
	       PAR_ZERO - 1, b_zeroM1, a_zeroM1, b_zeroM1);

	if (a_voice != VAL_VOICE) {
		printf("\nFAIL - the armed voice's own lock was not previewed: par %d is %u, not %u\n",
		       PAR_VOICE, a_voice, VAL_VOICE);
		failures++;
	}
	if (a_zeroM1 != b_zeroM1) {
		printf("FAIL - par %d changed to %u: that is voice 1's lock value %u written one "
		       "parameter below voice 1's own lock target\n",
		       PAR_ZERO - 1, a_zeroM1, VAL_ZERO);
		failures++;
	}
	if (a_voiceM1 != b_voiceM1) {
		printf("FAIL - par %d changed to %u: the off-by-one, with the right voice\n",
		       PAR_VOICE - 1, a_voiceM1);
		failures++;
	}
	if (a_zero != b_zero) {
		printf("FAIL - par %d changed to %u: voice 1's lock was previewed\n",
		       PAR_ZERO, a_zero);
		failures++;
	}

	/* The restore, which is the half of U-B2 that is not broken: everything
	   must be back where it was. */
	printf("\n  after the release: par %d = %u (was %u), par %d = %u (was %u)\n",
	       PAR_VOICE, activeParameters[PAR_VOICE], b_voice,
	       PAR_ZERO - 1, activeParameters[PAR_ZERO - 1], b_zeroM1);
	if (activeParameters[PAR_VOICE] != b_voice || activeParameters[PAR_ZERO - 1] != b_zeroM1) {
		puts("FAIL - the disarm did not put the originals back");
		failures++;
	}

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
