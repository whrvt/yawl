/*
 * Common miscellaneous macros/routines
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Cleanup function for pointers allocated with malloc/strdup/etc. */
[[gnu::always_inline]] static inline void cleanup_pointer(void *p) {
    void **pp = (void**)p;
    free(*pp);
    *pp = nullptr;
}

/* Cleanup function for FILE pointers */
[[gnu::always_inline]] static inline void cleanup_file(void *p) {
    FILE **fp = (FILE **)p;
    if (fp && *fp) {
        fclose(*fp);
        *fp = nullptr;
    }
}

/* Cleanup function that unlinks a file and then frees the path */
[[gnu::always_inline]] static inline void cleanup_unlink_and_free(void *p) {
    char **path = (char **)p;
    if (path && *path)
        unlink(*path);
    free(*path);
    *path = nullptr;
}

#define autofree [[gnu::cleanup(cleanup_pointer)]]
#define autoclose [[gnu::cleanup(cleanup_file)]]
#define autofree_del [[gnu::cleanup(cleanup_unlink_and_free)]]

#define nonnull_charp [[gnu::nonnull]] const char *_Nonnull
