/*
 * Can the panel alone arm an automation step past the end of a track? (#83)
 *
 * SHIFT + a held select button used to arm
 * buttonHandler_selectedStep * 8 + partNr - the v1 sub-step formula. With a
 * step selected in the upper half of a bar that is 64 to 127, and the armed
 * number is read straight back out of seq_subStepPattern[...][64] by
 * buttonHandler_armTimerActionStep (buttonHandler.c:138-154), four reads past
 * the end of the track - and the bytes it finds there are then used as a
 * parameter number and a value, so a live parameter is overwritten with
 * whatever happened to be in memory. The same number also reaches
 * seq_recordAutomation() (sequencer.c:2474) as a write index.
 *
 * The readout is that last part: the test snapshots every parameter, performs
 * the gesture, and compares. A parameter that changes was named by bytes read
 * from past the end of the track.
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
#include "menu/menu.h"

#define SEL_STEP  15            /* step button 16: selectedStep = 15 */
#define PART       2            /* select button 3 */
#define ARMED     (SEL_STEP * 8 + PART)         /* 122, the old formula */
#define VICTIM_TRACK (ARMED / 64)               /* 1 */
#define VICTIM_STEP  (ARMED % 64)               /* 58 */

static void hold(int mod, int but, int blocks)
{
	lxr_button((uint8_t)mod, 1); lxr_idle_blocks(8);
	lxr_button((uint8_t)but, 1); lxr_idle_blocks(blocks);
	lxr_button((uint8_t)but, 0); lxr_idle_blocks(8);
	lxr_button((uint8_t)mod, 0); lxr_idle_blocks(300);
}

static void show(const char *what)
{
	const Step *v = &seq_patternSet.seq_subStepPattern[seq_activePattern][VICTIM_TRACK][VICTIM_STEP];
	const Step *t = &seq_patternSet.seq_subStepPattern[seq_activePattern][0][SEL_STEP];
	printf("    %-26s track %d step %d: param1Nr %u param1Val %u | track 0 step %d:"
	       " param1Nr %u param1Val %u\n", what, VICTIM_TRACK, VICTIM_STEP,
	       v->param1Nr, v->param1Val, SEL_STEP, t->param1Nr, t->param1Val);
}

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(3000);

	memset(&seq_patternSet.seq_subStepPattern[seq_activePattern], 0,
	       sizeof(Step) * NUM_TRACKS * NUM_STEPS);

	printf("-- select step %d, then SHIFT + select button %d held past the 500 ms timer\n",
	       SEL_STEP + 1, PART + 1);
	lxr_button_click(BUT_MODE1);          /* VOICE/STEP: step buttons select */
	lxr_idle_blocks(300);
	lxr_button_click(BUT_SEQ16);          /* selectedStep = 15 */
	lxr_idle_blocks(300);
	lxr_button_click(BUT_VOICE_1);        /* back to voice mode, track 0 */
	lxr_idle_blocks(300);
	show("before:");

	{
		static uint8_t before[NUM_PARAMS], after[NUM_PARAMS];
		int changed = 0;
		memcpy(before, activeParameters, sizeof before);
		hold(BUT_SHIFT, BUT_SELECT3, 4000);   /* BUTTON_TIMEOUT = 2000 ticks = 500 ms */
		memcpy(after, activeParameters, sizeof after);
		for (int i = 0; i < NUM_PARAMS; i++)
			if (before[i] != after[i]) {
				printf("    parameter %d changed: %u -> %u\n", i, before[i], after[i]);
				changed++;
			}
		printf("    parameters changed by the gesture: %d\n", changed);
		if (changed) {
			puts("FAIL - the gesture wrote into a parameter it was never pointed at:");
			puts("       the number came from past the end of the track");
			failures++;
		}
	}

	puts("-- now turn a pot, which is what records automation on the armed step");
	lxr_pot_turn(0, 100);
	lxr_idle_blocks(600);
	show("after the pot turn:");

	{
		const Step *v = &seq_patternSet.seq_subStepPattern[seq_activePattern]
				[VICTIM_TRACK][VICTIM_STEP];
		if (v->param1Nr || v->param1Val) {
			printf("FAIL - step %d of track %d was written: the gesture armed step %d"
			       " of a %d-step track\n", VICTIM_STEP, VICTIM_TRACK, ARMED, NUM_STEPS);
			failures++;
		}
		puts("    (UBSan reports the four reads past the track at");
		puts("     buttonHandler.c:138-154 in a build without the fix)");
	}

	puts("-- non-regression: the ordinary way of arming a step still records");
	memset(&seq_patternSet.seq_subStepPattern[seq_activePattern], 0,
	       sizeof(Step) * NUM_TRACKS * NUM_STEPS);
	lxr_button(BUT_SEQ4, 1);              /* hold step 4: the documented gesture */
	lxr_idle_blocks(4000);
	lxr_pot_turn(0, 110);
	lxr_idle_blocks(600);
	lxr_button(BUT_SEQ4, 0);
	lxr_idle_blocks(300);
	{
		const Step *s = &seq_patternSet.seq_subStepPattern[seq_activePattern][0][3];
		printf("    track 0 step 3: param1Nr %u param1Val %u\n", s->param1Nr, s->param1Val);
		if (!s->param1Nr && !s->param1Val) {
			puts("FAIL - holding a step button no longer records automation");
			failures++;
		}
	}

	printf(failures ? "\n%d failure(s)\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
