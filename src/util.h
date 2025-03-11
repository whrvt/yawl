/*
 * Shared header for helper functions
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

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PROG_NAME "yawl"

#define BUFFER_SIZE 8192

void _append_sep_impl(char **result_ptr, const char *separator, int num_paths, ...);

#define COUNT_JOIN_ARGS(...) (sizeof((const char *[]){__VA_ARGS__}) / sizeof(const char *))

/* Join strings with a `sep` separator into the first argument (`result`) */
#define append_sep(result, sep, ...)                                                                                   \
    do {                                                                                                               \
        _append_sep_impl(&(result), sep, COUNT_JOIN_ARGS(__VA_ARGS__), __VA_ARGS__);                                   \
    } while (0)

/* Join paths with a `/` separator into the first argument (`result`) */
#define join_paths(result, ...) append_sep(result, "/", __VA_ARGS__)

/* Ensure a directory exists and is writable, creating it if necessary
 * Will create parent directories as needed (like mkdir -p)
 * Returns 0 on success, -1 on error */
int ensure_dir(const char *path);

/* Does what it looks like */
int remove_dir(const char *path);

/* Calculates a sha256sum for a file and puts it in `hash_str` */
int calculate_sha256(const char *file_path, char *hash_str, size_t hash_str_len);

/* Expands shell paths like ~ to their full equivalents (using wordexp)
 * Returns a newly allocated string that must be freed by the caller
 * Returns NULL on failure */
char *expand_path(const char *path);

/* A helper to extract an archive from `archive_path` to `extract_path` with libarchive */
int extract_archive(const char *archive_path, const char *extract_path);

/* A helper to download a file from `url` to `output_path` with libcurl */
int download_file(const char *url, const char *output_path);

/* Extract the base name from a given executable path */
char *get_base_name(const char *path);

/* Helper to find the shared directory for the yawl installation */
const char *get_yawl_dir(void);
