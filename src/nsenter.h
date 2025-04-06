/*
 * nsenter(1) - command-line interface for setns(2)
 *
 * Source from the https://github.com/util-linux/util-linux repository,
 * adapted for use internally with yawl.
 *
 * Copyright (C) 2012-2023 Eric Biederman <ebiederm@xmission.com>
 * Copyright (C) 2025 William Horvath
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * See the full license text in the repository LICENSE file.
 */

#ifdef __cplusplus
extern "C" {
#endif

int do_nsenter(int argc, char *argv[], unsigned long pid_to_enter);

/* Convert a string to an unsigned long in the specified base */
unsigned long str2unum(const char *str, int base);

#ifdef __cplusplus
}
#endif
