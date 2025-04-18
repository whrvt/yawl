/*
 * Common miscellaneous macros/routines
 *
 * Copyright (C) 2025 William Horvath
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * See the full license text in the repository LICENSE file.
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

#define forceinline __attribute__((always_inline)) inline

/* Cleanup function for pointers allocated with malloc/strdup/etc. */
static forceinline void cleanup_pointer(void *p) {
    void **pp = (void**)p;
    free(*pp);
    *pp = nullptr;
}

/* Cleanup function for FILE pointers */
static forceinline void cleanup_file(void *p) {
    FILE **fp = (FILE **)p;
    if (fp && *fp) {
        fclose(*fp);
        *fp = nullptr;
    }
}

/* Cleanup function that unlinks a file and then frees the path */
static forceinline void cleanup_unlink_and_free(void *p) {
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

#ifdef __cplusplus
}
#endif
