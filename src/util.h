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
#include <pwd.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

#include "result.h"

#define PROG_NAME "yawl"
#define CONFIG_DIR "configs"

#define BUFFER_SIZE 8192

/* case sensitive */
#define STRING_EQUALS(string1, string2) (strcmp(string1, string2) == 0)
/* case sensitive */
#define STRING_PREFIX(string, prefix) (strncmp(string, prefix, sizeof(prefix) - 1UL) == 0)
/* lowercase string equals (case insensitive) */
#define LCSTRING_EQUALS(string1, string2) (strcasecmp(string1, string2) == 0)
/* lowercase string prefix (case insensitive) */
#define LCSTRING_PREFIX(string, prefix) (strncasecmp(string, prefix, sizeof(prefix) - 1UL) == 0)

#define STRING_AFTER_PREFIX(string, prefix) (string + (sizeof(prefix) - 1UL))

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
 * Returns RESULT_OK on success, error RESULT on failure */
RESULT ensure_dir(const char *path);

/* Removes a directory and all its contents recursively
 * Returns RESULT_OK on success, error RESULT on failure */
RESULT remove_dir(const char *path);

/* Calculates a sha256sum for a file and puts it in `hash_str`
 * Returns RESULT_OK on success, error RESULT on failure */
RESULT calculate_sha256(const char *file_path, char hash_str[static 65]);

/* Find the hash for file_name (e.g. SteamLinuxRuntime_sniper.tar.xz) from a SHA256SUMS hash_url
 * (i.e. ...snapshots/latest-container-runtime-public-beta/SHA256SUMS)
 * Returns RESULT_OK on success, error RESULT on failure */
RESULT get_online_slr_sha256sum(const char *file_name, const char *hash_url, char hash_str[static 65]);

/* Expands shell paths like ~ to their full equivalents (using wordexp)
 * Returns a newly allocated string that must be freed by the caller
 * Returns NULL on failure */
char *expand_path(const char *path);

/* A helper to extract an archive from `archive_path` to `extract_path` with libarchive
 * Returns RESULT_OK on success, error RESULT on failure */
RESULT extract_archive(const char *archive_path, const char *extract_path);

/* A helper to download a file from `url` to `output_path` with libcurl
 * Returns RESULT_OK on success, error RESULT on failure
 * headers: NULL-terminated array of strings for HTTP headers (can be NULL)
 */
RESULT download_file(const char *url, const char *output_path, char **headers);

/* Extract the base name from a given executable path (allocates) */
static inline char *get_base_name(const char *path) {
    char *path_copy = strdup(path);
    if (!path_copy)
        return NULL;

    char *last_slash = strrchr(path_copy, '/');
    char *base_name = strdup(last_slash ? last_slash + 1 : path_copy);
    free(path_copy);

    return base_name;
}

/* Remove specified verbs from YAWL_VERBS environment variable */
RESULT remove_verbs_from_env(const char *verbs_to_remove[], int num_verbs);

/* The global installation path, set at startup in main() */
extern const char *g_yawl_dir;
/* The global configuration path, set at startup in main() */
extern const char *g_config_dir;
