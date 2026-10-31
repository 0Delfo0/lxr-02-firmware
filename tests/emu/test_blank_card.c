/* What the firmware does with a freshly formatted, empty FAT32 card: what it
 * creates at boot, and what a project save adds.
 * SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include "lxr_api.h"
#include "Preset/PresetManager.h"
#include "ff.h"

static void dump_card(const char *when)
{
	const int n = lxr_card_scan();
	printf("\n--- card after %s: %d entries ---\n", when, n);
	for (int i = 0; i < n && i < 40; i++)
		printf("  %-28s %s %u bytes\n", lxr_card_entry_path(i),
		       lxr_card_entry_is_dir(i) ? "DIR " : "file", lxr_card_entry_size(i));
	if (n > 40) printf("  ... %d more\n", n - 40);
}

int main(void)
{
	if (lxr_init(1, 128) != 0) { printf("boot failed\n"); return 2; }
	lxr_idle_blocks(64);
	printf("lcd after boot: \"%.16s\" / \"%.16s\"\n", lxr_get_lcd(), lxr_get_lcd() + 16);
	dump_card("boot on a blank card");

	lxr_idle_blocks(2000);
	printf("\nlcd after running a while: \"%.16s\" / \"%.16s\"\n", lxr_get_lcd(), lxr_get_lcd() + 16);
	dump_card("running for a while");

	const FRESULT r = preset_saveProject(0);
	lxr_idle_blocks(64);
	printf("\npreset_saveProject(0) returned %d\n", (int)r);
	dump_card("one project save");
	return 0;
}
