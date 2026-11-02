/*
 * What does a damaged glo.cfg leave behind? (#90, reported as P11)
 *
 * The globals used to be read straight into activeParameters, so a file that
 * stopped part way through left the settings part from the card and part from
 * the project loaded before it - and preset_loadGlobals() returned 0 while two
 * of its three callers ignored that (presetManager.c, menu.c).
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "lxr_api.h"
#include "ff.h"
#include "Preset/PresetManager.h"
#include "Parameters.h"
#include "menu/menu.h"

#define NUM_GLOBALS (NUM_PARAMS - PAR_BEGINNING_OF_GLOBALS)
#define NUM_LEGACY  (PAR_SYSEX_ENABLE - PAR_BEGINNING_OF_GLOBALS)
#define MARKER 99               /* what the running state holds beforehand */

static int write_glo(const uint8_t *data, UINT len)
{
	FIL f;
	UINT n = 0;
	if (f_open(&f, "glo.cfg", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
		return 0;
	if (f_write(&f, data, len, &n) != FR_OK || n != len) { f_close(&f); return 0; }
	f_close(&f);
	return 1;
}

static void fill_running_state(uint8_t value)
{
	for (int i = PAR_BEGINNING_OF_GLOBALS; i < NUM_PARAMS; i++)
		activeParameters[i] = value;
}

static void show(const char *what)
{
	printf("    %-22s", what);
	for (int i = PAR_BEGINNING_OF_GLOBALS; i < NUM_PARAMS; i++)
		printf(" %u", activeParameters[i]);
	printf("\n");
}

int main(void)
{
	int failures = 0;
	uint8_t file[NUM_GLOBALS];
	uint8_t ret;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(20);

	puts("-- a glo.cfg that stops after 5 bytes");
	for (int i = 0; i < NUM_GLOBALS; i++)
		file[i] = (uint8_t)(11 + i);
	if (!write_glo(file, 5)) { puts("FAIL - cannot write glo.cfg"); return 2; }
	fill_running_state(MARKER);
	show("before the load:");
	ret = preset_loadGlobals();
	show("after the load:");
	printf("    preset_loadGlobals() returned %u\n", ret);
	if (ret != 0) {
		puts("FAIL - a 5-byte file was accepted");
		failures++;
	}
	for (int i = PAR_BEGINNING_OF_GLOBALS; i < NUM_PARAMS; i++)
		if (activeParameters[i] != MARKER) {
			printf("FAIL - parameter %d was overwritten: %u instead of %u\n",
			       i, activeParameters[i], MARKER);
			failures++;
		}

	puts("-- a stock 30-byte glo.cfg: applied, SysEx settings defaulted");
	fill_running_state(MARKER);
	if (!write_glo(file, NUM_LEGACY)) { puts("FAIL - cannot write glo.cfg"); return 2; }
	ret = preset_loadGlobals();
	show("after the load:");
	printf("    returned %u\n", ret);
	if (ret != 1) { puts("FAIL - a stock file was rejected"); failures++; }
	for (int i = 0; i < NUM_LEGACY; i++)
		if (activeParameters[PAR_BEGINNING_OF_GLOBALS + i] != file[i]) {
			printf("FAIL - global %d is %u, the file says %u\n", i,
			       activeParameters[PAR_BEGINNING_OF_GLOBALS + i], file[i]);
			failures++;
		}
	if (activeParameters[PAR_SYSEX_ENABLE] != 1
	    || activeParameters[PAR_SYSEX_DEVICE_ID] != 1
	    || activeParameters[PAR_SYSEX_ECHO] != 0) {
		printf("FAIL - SysEx settings are %u %u %u, expected 1 1 0\n",
		       activeParameters[PAR_SYSEX_ENABLE],
		       activeParameters[PAR_SYSEX_DEVICE_ID],
		       activeParameters[PAR_SYSEX_ECHO]);
		failures++;
	}

	puts("-- a full 33-byte glo.cfg: every byte applied");
	fill_running_state(MARKER);
	file[PAR_SYSEX_ENABLE - PAR_BEGINNING_OF_GLOBALS] = 1;
	file[PAR_SYSEX_DEVICE_ID - PAR_BEGINNING_OF_GLOBALS] = 9;
	file[PAR_SYSEX_ECHO - PAR_BEGINNING_OF_GLOBALS] = 1;
	if (!write_glo(file, NUM_GLOBALS)) { puts("FAIL - cannot write glo.cfg"); return 2; }
	ret = preset_loadGlobals();
	show("after the load:");
	if (ret != 1) { puts("FAIL - a full file was rejected"); failures++; }
	for (int i = 0; i < NUM_GLOBALS; i++)
		if (activeParameters[PAR_BEGINNING_OF_GLOBALS + i] != file[i]) {
			printf("FAIL - global %d is %u, the file says %u\n", i,
			       activeParameters[PAR_BEGINNING_OF_GLOBALS + i], file[i]);
			failures++;
		}

	puts("-- a glo.cfg that is not there at all");
	fill_running_state(MARKER);
	f_unlink("glo.cfg");
	ret = preset_loadGlobals();
	printf("    returned %u\n", ret);
	if (ret != 0) { puts("FAIL - a missing file was accepted"); failures++; }
	for (int i = PAR_BEGINNING_OF_GLOBALS; i < NUM_PARAMS; i++)
		if (activeParameters[i] != MARKER) {
			printf("FAIL - parameter %d was touched: %u\n", i, activeParameters[i]);
			failures++;
		}

	printf(failures ? "\n%d failure(s)\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
