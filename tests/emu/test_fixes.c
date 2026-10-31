/* Preliminary checks of the four fixes on branches 2, 3, 5 and 6 of
 * gitlab.com:0Delfo0/lxr-02-firmware, run against the firmware sources in the
 * emulator (bg1eym's emu branch): the same C code, with the hardware layer
 * replaced and the SD card as a RAM disk.
 *
 * It is not a substitute for the hardware test. Float to int casts here wrap
 * instead of saturating when built with gcc, and the device runs -ffast-math.
 * Logic - sequencer, menus, preset and FatFs paths - behaves the same.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "lxr_api.h"
#include "Parameters.h"
#include "buttonHandler.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/DSPAudio/Decay.h"
#include "Soundengine/MIDI/MidiVoiceControl.h"
#include "Preset/PresetManager.h"
#include "ff.h"

static int fails;
static void check(int ok, const char *name, const char *detail)
{
	printf("%-4s %s%s%s\n", ok ? "ok" : "FAIL", name, detail[0] ? " - " : "", detail);
	if (!ok) fails++;
}

/* ---- issue #6: pitch EG slope at 127 ------------------------------------- */
static void test_slope_is_finite(void)
{
	DecayEg eg;
	char detail[96] = "";
	int bad = -1;
	DecayEg_init(&eg);
	for (int d = 0; d <= 127; d++) {
		DecayEg_setSlope(&eg, (unsigned char)d);
		if (!isfinite(eg.slope)) { bad = d; break; }
	}
	if (bad >= 0) snprintf(detail, sizeof detail, "slope is not finite at data2 = %d", bad);
	check(bad < 0, "#6 DecayEg_setSlope finite over 0..127", detail);
}

/* The audible consequence: the same voice at a sane slope and at the maximum,
 * compared. A healthy voice differs by a few dB, not by 30. */
static double peak_with_slope(int slope)
{
	static float buf[64 * 2];
	double peak = 0;

	lxr_set_param(PAR_PITCH_SLOPE1, slope);
	lxr_idle_blocks(8);
	voiceControl_noteOn(0, 60, 127);
	for (int b = 0; b < 200; b++) {
		lxr_render(buf, 64, 2);
		for (int i = 0; i < 64 * 2; i++) {
			if (isnan(buf[i])) return -1;
			const double a = fabs((double)buf[i]);
			if (a > peak) peak = a;
		}
	}
	voiceControl_noteOff(0);
	lxr_idle_blocks(8);
	return peak;
}

static void test_voice_sounds_with_max_slope(void)
{
	char detail[128];
	const double ref = peak_with_slope(64);
	const double max = peak_with_slope(127);

	snprintf(detail, sizeof detail, "peak %.4f at slope 64, %.4f at slope 127 (%.1f dB down)",
	         ref, max, (ref > 0 && max > 0) ? 20.0 * log10(max / ref) : -99.0);
	check(max > ref / 4.0, "#6 drum 1 is not silenced by slope 127", detail);
}

/* ---- issue #5: SHIFT + step 12 must not rewrite the track lengths -------- */
static void test_global_menu_keeps_track_lengths(void)
{
	unsigned char before[7], after[7];
	char detail[160];
	int same = 1;

	for (int t = 0; t < 7; t++) seq_setTrackLength((unsigned char)t, (unsigned char)(8 + t));
	lxr_idle_blocks(8);
	for (int t = 0; t < 7; t++) before[t] = seq_getTrackLength((unsigned char)t);

	lxr_button(BUT_SHIFT, 1);
	lxr_button_click(BUT_SEQ12);
	lxr_button(BUT_SHIFT, 0);
	lxr_idle_blocks(32);
	lxr_button_click(BUT_SEQ4);      /* one ordinary step press afterwards */
	lxr_idle_blocks(32);

	for (int t = 0; t < 7; t++) {
		after[t] = seq_getTrackLength((unsigned char)t);
		if (after[t] != before[t]) same = 0;
	}
	snprintf(detail, sizeof detail,
	         "lengths %u,%u,%u,%u,%u,%u,%u -> %u,%u,%u,%u,%u,%u,%u, lcd \"%.32s\"",
	         before[0], before[1], before[2], before[3], before[4], before[5], before[6],
	         after[0], after[1], after[2], after[3], after[4], after[5], after[6],
	         lxr_get_lcd());
	check(same, "#5 track lengths survive SHIFT + step 12", detail);
}

/* ---- issues #2 and #3: saving a project twice ----------------------------
 * The second save takes the branch that empties the folder first, which is
 * where empty_directory() builds "/projNN/<8.3 name>" into a 13-byte buffer.
 * Under ASan the overflow is reported; without it, the canary below catches
 * the damage to savefileNames, which sits immediately before currentFolder. */
static void test_project_save_twice(void)
{
	char detail[160];
	char saved[NUM_KIT_SLOTS][NUM_SAVEFILE_NAMES][8];
	FRESULT r1, r2;

	memcpy(saved, savefileNames, sizeof saved);
	r1 = preset_saveProject(1);
	lxr_idle_blocks(8);
	r2 = preset_saveProject(1);
	lxr_idle_blocks(8);

	snprintf(detail, sizeof detail, "first save %d, second save %d", (int)r1, (int)r2);
	check(r1 == FR_OK && r2 == FR_OK, "#3 both project saves report success", detail);

	snprintf(detail, sizeof detail, "savefileNames %s",
	         memcmp(saved, savefileNames, sizeof saved) ? "was modified" : "intact");
	check(memcmp(saved, savefileNames, sizeof saved) == 0,
	      "#2 the kit name table is not corrupted by a project save", detail);
}

int main(void)
{
	if (lxr_init(1, 128) != 0) { printf("FAIL - the emulated device did not boot\n"); return 2; }
	lxr_idle_blocks(64);
	printf("booted, lcd \"%.32s\"\n\n", lxr_get_lcd());

	test_slope_is_finite();
	test_global_menu_keeps_track_lengths();
	test_project_save_twice();
	test_voice_sounds_with_max_slope();

	printf("\n%s (%d failed)\n", fails ? "FAILED" : "all checks passed", fails);
	return fails ? 1 : 0;
}
