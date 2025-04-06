/*
 * Shared header for helper functions
 *
 * Copyright (C) 2025 William Horvath
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * See the full license text in the repository LICENSE file.
 */

#pragma once

#include <dirent.h>
#include <pwd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "result.h"

#ifdef __cplusplus
extern "C" {
#endif

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

void _append_sep_impl(char *result_ptr[], const char *separator, int num_strings, ...);

#define COUNT_JOIN_ARGS(...) (sizeof((const char *[]){__VA_ARGS__}) / sizeof(const char *))

/* Join strings with a `sep` separator into the first argument (`result`) */
#define append_sep(result, sep, ...)                                                                                   \
    _append_sep_impl(&(result), sep, COUNT_JOIN_ARGS(__VA_ARGS__) __VA_OPT__(, ) __VA_ARGS__)

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
RESULT calculate_sha256(const char *file_path, char hash_str[65]);

/* Find the hash for file_name (e.g. SteamLinuxRuntime_sniper.tar.xz) from a SHA256SUMS hash_url
 * (i.e. ...snapshots/latest-container-runtime-public-beta/SHA256SUMS)
 * Returns RESULT_OK on success, error RESULT on failure */
RESULT get_online_slr_sha256sum(const char *file_name, const char *hash_url, char hash_str[65]);

/* Expands shell paths like ~ to their full equivalents (using wordexp)
 * Returns a newly allocated string that must be freed by the caller
 * Returns nullptr on failure */
char *expand_path(const char *path);

/* A helper to extract an archive from `archive_path` to `extract_path` with libarchive
 * Returns RESULT_OK on success, error RESULT on failure */
RESULT extract_archive(const char *archive_path, const char *extract_path);

/* A helper to download a file from `url` to `output_path` with libcurl
 * Returns RESULT_OK on success, error RESULT on failure
 * headers: nullptr-terminated array of strings for HTTP headers (can be nullptr)
 */
RESULT download_file(const char *url, const char *output_path, const char *headers[]);

/* Is the file a real executable file? */
static inline bool is_exec_file(const char *path) {
    struct stat file_stat;
    if (stat(path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode) || (file_stat.st_mode & S_IXUSR) == 0)
        return false;
    return true;
}

/* Remove specified verbs from YAWL_VERBS environment variable */
RESULT remove_verbs_from_env(const char *verbs_to_remove[], int num_verbs);

/* The global installation path, set at startup in main() */
extern const char *g_yawl_dir;
/* The global configuration path, set at startup in main() */
extern const char *g_config_dir;

#ifdef __cplusplus
}
#endif
