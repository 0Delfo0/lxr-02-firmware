/*
 * Host-side directory listing, in its own translation unit because FatFs and
 * <dirent.h> both define DIR.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "host_dir.h"

static int by_name(const void *a, const void *b)
{
	return strcmp((const char *)a, (const char *)b);
}

int host_dir_list(const char *path, char names[][HOST_DIR_NAME_MAX], int max)
{
	DIR *d = opendir(path);
	struct dirent *e;
	int n = 0;

	if (!d)
		return -1;
	while ((e = readdir(d)) && n < max) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(names[n], HOST_DIR_NAME_MAX, "%s", e->d_name);
		n++;
	}
	closedir(d);
	qsort(names, (size_t)n, HOST_DIR_NAME_MAX, by_name);
	return n;
}
