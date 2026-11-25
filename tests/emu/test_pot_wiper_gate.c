/*
 * Does the firmware see a pot that moves on wiper b alone? (#125)
 *
 * calcPosition() takes the angle from wiper b and uses a only to tell the two
 * half turns apart, but adc_checkPots() used to gate on wiper a alone - and
 * never wrote adc_buf_old[i*2+1] at all. Movement that only changed b was
 * therefore held back, which is why this drives adc_buf directly instead of
 * going through the emulator's pot model: that model alternates wiper a on
 * every step precisely to get past the old gate.
 *
 * Expected: coa follows the sweep. Against the old gate the value does not
 * move at all.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "lxr_api.h"
#include "IO/adcPots.h"

extern uint16_t adc_buf_old[];

static char line[33];
static const char *lcd(void)
{
	const char *l = lxr_get_lcd();
	memcpy(line, l, 32); line[32] = 0;
	return line;
}

int main(void)
{
	if (lxr_init(1, 128) != 0) { puts("boot failed"); return 2; }
	lxr_idle_blocks(3000);
	printf("  page        |%.16s|%.16s|\n", lcd(), lcd() + 16);

	/* wiper a parked where the emulator rests it, b at mid travel */
	const uint16_t a = 0xE00 << 4;
	uint16_t b = 2048;
	adc_buf[0] = a; adc_buf[1] = (uint16_t)(b << 4);
	adc_buf_old[0] = a; adc_buf_old[1] = (uint16_t)(b << 4);
	lxr_idle_blocks(400);
	printf("  start       |%.16s|%.16s|   a %#06x b %u\n", lcd(), lcd() + 16, a >> 4, b);

	char before[33]; memcpy(before, lcd(), 33);

	/* turn the pot: b sweeps, a never moves */
	for (int step = 1; step <= 6; step++) {
		b = (uint16_t)(2048 + step * 200);
		adc_buf[1] = (uint16_t)(b << 4);
		lxr_idle_blocks(400);
		printf("  b -> %-6u |%.16s|%.16s|%s\n", b, lcd(), lcd() + 16,
		       memcmp(before, lcd(), 32) ? "   CHANGED" : "");
	}

	const int changed = memcmp(before, lcd(), 32) != 0;
	printf("\n  wiper b moved by %+d of 4095, wiper a never moved.\n", 6 * 200);
	puts(changed ? "  SEEN     - the gate follows both wipers"
	             : "  NOT SEEN - the pot is dead on this axis");
	return changed ? 0 : 1;
}
