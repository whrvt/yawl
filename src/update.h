/*
 * Self-update functionality
 *
 * Copyright (C) 2025 William Horvath
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#pragma once

#include "config.h"
#include "result.h"

/* (private for now) Check if a new version is available and print information about it
 * Returns RESULT_OK if no update is available or if the update check was successful
 * Returns error RESULT on failure
 */
/* RESULT check_for_updates(void); */

/* (private for now) Download and apply available update
 * Returns RESULT_OK on success, error RESULT on failure
 */
/* RESULT perform_update(void); */

/* Handle update operations based on command line options
 * check_only: 1 = just check for updates, 0 = don't check
 * do_update: 1 = check and apply updates, 0 = don't update
 * Returns RESULT_OK on success, error RESULT on failure
 */
RESULT handle_updates(int check_only, int do_update);

#ifdef HAVE_RENAMEAT2
#define _renameat2 renameat2
#else
#include <unistd.h>
#include <syscall.h>
#if defined(HAVE_RENAMEAT) && defined(SYS_renameat2)
#define RENAME_NOREPLACE (1 << 0)
#define RENAME_EXCHANGE (1 << 1)
#define RENAME_WHITEOUT (1 << 2)
/* This is required for musl, there's no wrapper for renameat2 like glibc */
static inline int __renameat2(int oldfd, const char *old, int newfd, const char *new, unsigned flags) {
    if (!flags)
        return syscall(SYS_renameat, oldfd, old, newfd, new);
    return syscall(SYS_renameat2, oldfd, old, newfd, new, flags);
}

#define _renameat2 __renameat2
#else
#define _renameat2
#endif
#endif
