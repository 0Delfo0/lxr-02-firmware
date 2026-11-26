/*
 * Does a step released under a modifier still arm itself? (#27, review U-B7)
 *
 * A step press arms a long-press timer (buttonHandler.c:1160) - but only in the
 * "no key modifier pressed" branch, so the order of the gesture is the whole
 * thing. Pressing a step while a modifier is already held arms nothing, and the
 * author's comment at the release site is right about that case. The defect is
 * the other order: press the step alone, so the timer is armed, then reach for
 * SHIFT or LOAD, then let the step go. The release then takes the modifier
 * branch, which returned before cancelling, so buttonHandler_tick() fired
 * afterwards and armed the step for automation with nobody touching anything.
 *
 * BUTTON_TIMEOUT is 2000 ticks of the 4 kHz counter, so half a second, and
 * buttonHandler_tick() runs in the emulator's main loop.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include "lxr_api.h"
#include "buttonHandler.h"

#define STEP BUT_SEQ4
#define STEP_NR 3

/* step down first - that is what arms the timer - then the modifier, then the
   step up. Reversing the first two arms nothing and proves nothing. */
static void press_release_under(int modifier, const char *what)
{
	printf("  %-28s", what);
	lxr_button(STEP, 1); lxr_idle_blocks(20);
	if (modifier >= 0) { lxr_button((unsigned char)modifier, 1); lxr_idle_blocks(20); }
	lxr_button(STEP, 0); lxr_idle_blocks(20);
	if (modifier >= 0) { lxr_button((unsigned char)modifier, 0); }
	/* well past BUTTON_TIMEOUT, with the tick handler running */
	lxr_idle_blocks(4000);
	printf(" armed step after the timeout: %3d\n", buttonHandler_getArmedAutomationStep());
}

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(2000);

	printf("-- nothing armed to begin with: %d (NO_STEP_SELECTED is %d)\n\n",
	       buttonHandler_getArmedAutomationStep(), NO_STEP_SELECTED);

	const struct { int mod; const char *name; } cases[] = {
		{ BUT_MODE3, "LOAD held" },
		{ BUT_MODE4, "SAVE/MENU held" },
		{ BUT_SHIFT, "SHIFT held" },
	};

	for (unsigned i = 0; i < 3; i++) {
		press_release_under(cases[i].mod, cases[i].name);
		const int armed = buttonHandler_getArmedAutomationStep();
		if (armed != NO_STEP_SELECTED) {
			printf("FAIL - %s: the step armed itself (%d)\n", cases[i].name, armed);
			failures++;
			/* put it back so the next case starts clean */
			lxr_button(STEP, 1); lxr_idle_blocks(20); lxr_button(STEP, 0);
			lxr_idle_blocks(100);
		}
	}

	/* Positive control: a deliberate long press must still arm the step, or
	   the fix has removed the feature rather than the defect. */
	puts("\n-- positive control: a step held down past the timeout, no modifier");
	lxr_button(STEP, 1);
	lxr_idle_blocks(4000);
	const int armed_held = buttonHandler_getArmedAutomationStep();
	lxr_button(STEP, 0);
	lxr_idle_blocks(100);
	printf("   armed while held: %d  (expected %d)\n", armed_held, STEP_NR);
	if (armed_held != STEP_NR) {
		puts("FAIL - holding a step no longer arms it");
		failures++;
	}

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
