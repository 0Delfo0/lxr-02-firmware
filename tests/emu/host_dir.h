/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef HOST_DIR_H_
#define HOST_DIR_H_
#define HOST_DIR_NAME_MAX 256
/* Sorted list of the entries of a host directory, dot files excluded.
   Returns the count, or -1 if the directory cannot be read. */
int host_dir_list(const char *path, char names[][HOST_DIR_NAME_MAX], int max);
#endif
