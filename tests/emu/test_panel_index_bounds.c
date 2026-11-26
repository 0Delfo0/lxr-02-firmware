/*
 * Does a 7-bit byte from the panel reach an array index unchecked? (#31, SW16)
 *
 * CortexParser.c keeps three selections - shown pattern, active track, active
 * step - each assigned straight from cortex_midiMsg.data2, and nineteen live
 * accesses to seq_subStepPattern and seq_stepTiming are indexed by them, six
 * of them writes. The bytes arrive over the panel UART, so 0 to 127, against
 * NUM_PATTERN 64, NUM_TRACKS 7 and NUM_STEPS 64.
 *
 * The test speaks the wire format rather than calling anything internal:
 * frontParser_parseUartData() takes one byte at a time, a status byte is one
 * with the high bit set, and the two bytes after it are data1 and data2
 * (CortexParser.c, frontParser_parseUartData). That is the boundary the issue
 * is about, so it is the boundary to push on.
 *
 * Part 1 is the control, and it is the half that matters most here: a guard
 * that refuses everything would pass every out-of-range check below while
 * breaking the panel. So a legitimate selection and a legitimate write have to
 * land in the right cell first.
 *
 * Built with ASan, the out-of-range writes in parts 2 and 3 abort the process
 * on an unfixed tree - seq_patternSet is a global, and [100][100][100] of
 * [64][7][64] is far outside it. That abort is the result; the parts are
 * ordered so the output shows how far it got.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "lxr_api.h"
#include "Soundengine/MIDI/CortexParser.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/Sequencer/sequencer.h"

static void panel(uint8_t status, uint8_t d1, uint8_t d2)
{
	frontParser_parseUartData(status);
	frontParser_parseUartData(d1);
	frontParser_parseUartData(d2);
	lxr_idle_blocks(4);
}

static void seq_cc(uint8_t cmd, uint8_t value) { panel(FRONT_SEQ_CC, cmd, value); }

/* The active step has no select command of its own: it is set as a side effect
   of the panel asking for that step's parameters, at the end of
   FRONT_SEQ_REQUEST_STEP_PARAMS. FRONT_SEQ_SELECT_ACTIVE_STEP is a message the
   Cortex sends *to* the panel, not one it receives - an earlier version of this
   test used it and the step never moved. */
static void select(uint8_t pattern, uint8_t track, uint8_t step)
{
	seq_cc(FRONT_SEQ_SET_SHOWN_PATTERN, pattern);
	seq_cc(FRONT_SEQ_SET_ACTIVE_TRACK, track);
	seq_cc(FRONT_SEQ_REQUEST_STEP_PARAMS, step);
}

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(400);

	/* 1. the control: a legal selection and a legal write must work */
	puts("1. a legal selection and a legal write");
	select(2, 3, 5);
	seq_cc(FRONT_SEQ_PROB, 42);
	const uint8_t legal = seq_patternSet.seq_subStepPattern[2][3][5].prob;
	printf("   pattern 2, track 3, step 5: prob = %u (expected 42)\n", legal);
	if (legal != 42) {
		puts("   FAIL - the panel can no longer write a step: a guard refuses legal traffic");
		return 2;
	}

	/* 2. an out-of-range selection must be refused, so the write that follows
	 *    still lands on the selection that was legal */
	puts("\n2. an out-of-range selection, then the same write");
	seq_patternSet.seq_subStepPattern[2][3][5].prob = 7;
	select(100, 100, 100);
	/* every write that is indexed by the three selections, so the report set
	   below is the whole inventory and not the part one message happens to
	   reach */
	seq_cc(FRONT_SEQ_PROB, 99);
	seq_cc(FRONT_SEQ_FLAM, 99);
	seq_cc(FRONT_SEQ_SHIFT, 99);
	seq_cc(FRONT_SEQ_STEP_CONDITION, 1);
	const uint8_t kept = seq_patternSet.seq_subStepPattern[2][3][5].prob;
	printf("   pattern 100, track 100, step 100 selected, then prob 99\n");
	printf("   pattern 2, track 3, step 5: prob = %u\n", kept);
	if (kept == 99)
		puts("   the selections were refused and the previous one still stands");
	else {
		printf("   FAIL - prob is %u: the write went somewhere else, which is where "
		       "those indices point\n", kept);
		failures++;
	}

	/* 3. the step number of a value write is its own byte, data1 */
	puts("\n3. FRONT_SET_P1_VAL with step 100");
	select(2, 3, 5);
	seq_patternSet.seq_subStepPattern[2][3][63].param1Val = 11;
	panel(FRONT_SET_P1_VAL, 100, 77);
	panel(FRONT_SET_P2_VAL, 100, 77);
	panel(FRONT_SET_P1_VAL, 63, 77);
	const uint8_t at63 = seq_patternSet.seq_subStepPattern[2][3][63].param1Val;
	printf("   step 63 (legal) param1Val = %u (expected 77)\n", at63);
	if (at63 != 77) {
		puts("   FAIL - a legal step write was refused");
		failures++;
	}

	/* 4. the readback request indexes with its own byte too */
	puts("\n4. a step-params request for step 100");
	seq_cc(FRONT_SEQ_REQUEST_STEP_PARAMS, 100);
	seq_cc(FRONT_SEQ_REQUEST_STEP_PARAMS, 5);
	puts("   survived; under ASan an unbounded read of the array would have been reported");

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
