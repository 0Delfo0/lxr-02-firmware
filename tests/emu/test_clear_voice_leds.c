/*
 * Which LED does led_clearVoiceLeds() keep? (#25, review item U-C1)
 *
 * Byte 0 of the output data holds LED_PAGE_UP in bit 0 and LED_VOICE1..7 in
 * bits 1 to 7, so clearing the voices means keeping bit 0. The mask was 0x80,
 * which keeps bit 7 - LED_VOICE7, HH OPN - and clears PAGE UP with the other
 * six voices.
 *
 * The one caller is COPY/CLEAR pressed outside song mode without SHIFT, which
 * enters copy-track mode (buttonHandler.c:2039), so the test does that.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include "lxr_api.h"
#include "buttonHandler.h"
#include "ledHandler.h"

extern uint8_t dout_outputData[];

static uint64_t leds(void)
{
	return (uint64_t)lxr_get_leds_lo() | ((uint64_t)lxr_get_leds_hi() << 32);
}

static void show(const char *what)
{
	const uint64_t m = leds();
	printf("  %-22s PAGE UP %d   voices", what, (int)((m >> LED_PAGE_UP) & 1));
	for (int v = LED_VOICE1; v <= LED_VOICE7; v++) printf(" %d", (int)((m >> v) & 1));
	printf("   byte0 0x%02x\n", dout_outputData[0]);
}

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(2000);

	/* Light PAGE UP and every voice, so both halves of the mask are visible.
	   PAGE UP is lit by the bar handling in normal use; setting it here keeps
	   the test about the mask rather than about how it got lit. */
	led_setValue(1, LED_PAGE_UP);
	for (int v = LED_VOICE1; v <= LED_VOICE7; v++) led_setValue(1, (uint8_t)v);
	lxr_idle_blocks(20);
	show("before");

	if (!((leds() >> LED_PAGE_UP) & 1)) {
		puts("FAIL - PAGE UP is not lit, so nothing below means anything");
		return 2;
	}

	/* COPY/CLEAR: enters copy-track mode and clears the voice LEDs */
	lxr_button(BUT_COPY, 1); lxr_idle_blocks(8);
	lxr_button(BUT_COPY, 0); lxr_idle_blocks(200);
	show("after COPY/CLEAR");

	const uint64_t m = leds();
	if (!((m >> LED_PAGE_UP) & 1)) {
		puts("\nFAIL - PAGE UP was cleared; it is not a voice LED");
		failures++;
	}
	for (int v = LED_VOICE1; v <= LED_VOICE7; v++) {
		if ((m >> v) & 1) {
			printf("FAIL - voice LED %d is still lit after clearing the voices\n",
			       v - LED_VOICE1 + 1);
			failures++;
		}
	}

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
