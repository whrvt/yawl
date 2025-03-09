/*
 * Simple Steam Linux Runtime bootstrapper/launcher program
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

#define _GNU_SOURCE
#include <errno.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>

/* This file does not normally exist, but it contains embedded CA certificate
   data generated as part of the curl build process, and we move it here to be
   able to use it for CURLOPT_CAINFO_BLOB */
#include "archive.h"
#include "curl/ca_cert_embed.h"
#include "curl/curl.h"

#include "strings.h"

#define PROG_NAME "yawl"

#define RUNTIME_VERSION "sniper"
#define RUNTIME_BASE_URL                                                                                               \
    "https://repo.steampowered.com/steamrt-images-" RUNTIME_VERSION "/snapshots/latest-container-runtime-public-beta"
#define RUNTIME_PREFIX "SteamLinuxRuntime_"
#define RUNTIME_ARCHIVE_NAME RUNTIME_PREFIX RUNTIME_VERSION ".tar.xz"
#define BUFFER_SIZE 8192

static char *g_top_data_dir;
static char *g_yawl_dir;

static char *setup_data_dir(void) {
    char *result = NULL;
    const char *temp_dir = getenv("XDG_DATA_HOME");
    if (!temp_dir) {
        if (!(temp_dir = getenv("HOME"))) {
            struct passwd *pw = getpwuid(getuid());
            if (!(pw && (temp_dir = pw->pw_dir)))
                return NULL;
        }
        join_paths(result, temp_dir, ".local", "share");
    } else
        result = strdup(temp_dir);

    if (access(result, X_OK) != 0)
        return NULL;

    return result;
}

static int ensure_dir(const char *path) {
    struct stat st;
    if (!stat(path, &st)) {
        if (S_ISDIR(st.st_mode))
            return access(path, W_OK);
        else
            return -1;
    }
    return mkdir(path, 0755);
}

static int download_file(const char *url, const char *output_path) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "curl error: %s\n", curl_easy_strerror(CURLE_FAILED_INIT));
        return -1;
    }

    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        fprintf(stderr, "Error: Couldn't open output_path (%s), errno %d\n", output_path, errno);
        curl_easy_cleanup(curl);
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    /* Copied from curl's `src/tool_operate.c`, use the embedded CA certificate
     * data */
    struct curl_blob blob;
    blob.data = (void *)curl_ca_embed;
    blob.len = strlen((const char *)curl_ca_embed);
    blob.flags = CURL_BLOB_NOCOPY;
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob);

    CURLcode res = curl_easy_perform(curl);

    fclose(fp);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl error: %s\n", curl_easy_strerror(res));
        return -1;
    }

    return 0;
}

static int extract_archive(const char *archive_path, const char *extract_path) {
    struct archive *a;
    struct archive *ext;
    struct archive_entry *entry;
    int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS |
                ARCHIVE_EXTRACT_OWNER;
    int r;

    a = archive_read_new();
    archive_read_support_format_tar(a);
    archive_read_support_filter_xz(a);
    archive_read_support_filter_zstd(a);
    archive_read_support_filter_lzip(a);

    ext = archive_write_disk_new();
    archive_write_disk_set_options(ext, flags);
    archive_write_disk_set_standard_lookup(ext);

    if ((r = archive_read_open_filename(a, archive_path, BUFFER_SIZE))) {
        fprintf(stderr, "Error: Extracting failed (read_open_filename), errno: %d, string: %s\n", archive_errno(a),
                archive_error_string(a));
        return -1;
    }

    char *old_cwd = getcwd(NULL, 0);
    if (chdir(extract_path) != 0) {
        fprintf(stderr, "Error: Extracting failed (chdir), errno: %d\n", errno);
        free(old_cwd);
        return -1;
    }

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        r = archive_write_header(ext, entry);
        if (r != ARCHIVE_OK)
            continue;

        const void *buff;
        size_t size;
        int64_t offset;

        while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK)
            if (archive_write_data_block(ext, buff, size, offset) != ARCHIVE_OK)
                break;
    }

    chdir(old_cwd);
    free(old_cwd);

    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);
    return 0;
}

static int setup_runtime(void) {
    int ret = 0;
    char *archive_path = NULL, *runtime_path = NULL;
    join_paths(archive_path, g_yawl_dir, RUNTIME_ARCHIVE_NAME);
    join_paths(runtime_path, g_yawl_dir, RUNTIME_PREFIX RUNTIME_VERSION);

    struct stat st;
    if (stat(runtime_path, &st) == 0 && S_ISDIR(st.st_mode))
        goto setup_done;

    printf("Downloading Steam Runtime (%s)...\n", RUNTIME_VERSION);
    if ((ret = download_file(RUNTIME_BASE_URL "/" RUNTIME_ARCHIVE_NAME, archive_path)) != 0)
        goto setup_done;

    printf("Extracting runtime...\n");
    ret = extract_archive(archive_path, g_yawl_dir);

    unlink(archive_path);

setup_done:
    free(archive_path);
    free(runtime_path);

    return ret;
}

static char *build_library_paths(const char *wine_top_path) {
    const char *system_paths[] = {"/lib", "/lib32", "/lib64", "/usr/lib", "/usr/lib32", "/usr/lib64", NULL};
    char *wine_lib64_path = NULL, *wine_lib32_path = NULL, *wine_lib_path = NULL;

    join_paths(wine_lib64_path, wine_top_path, "lib64");
    join_paths(wine_lib32_path, wine_top_path, "lib32");
    join_paths(wine_lib_path, wine_top_path, "lib");

    char *result = NULL;

    const char *orig_path = getenv("LD_LIBRARY_PATH");
    if (orig_path)
        append_sep(result, ":", orig_path);

    append_sep(result, ":", wine_lib64_path, wine_lib32_path, wine_lib_path);

    for (const char **path = system_paths; *path; path++) {
        if (access(*path, F_OK) == 0)
            append_sep(result, ":", *path);
    }

    free(wine_lib64_path);
    free(wine_lib32_path);
    free(wine_lib_path);

    return result;
}

/* required for ancient Debian/Ubuntu */
static char *build_mesa_paths(void) {
    const char *mesa_paths[] = {"/usr/lib/i386-linux-gnu/dri",
                                "/usr/lib/x86_64-linux-gnu/dri",
                                "/usr/lib/dri",
                                "/usr/lib32/dri",
                                "/usr/lib64/dri",
                                NULL};

    char *result = NULL;
    for (const char **path = mesa_paths; *path; path++) {
        if (access(*path, F_OK) == 0)
            append_sep(result, ":", *path);
    }

    return result;
}

static char *find_wine_binary(const char *wine_path) {
    const char *binaries[] = {"wine64", "wine", NULL};
    char *result = NULL;

    for (const char **bin = binaries; *bin; bin++) {
        char *path = NULL;
        join_paths(path, wine_path, "bin", *bin);
        if (access(path, X_OK) == 0) {
            result = path;
            break;
        }
        free(path);
    }

    return result;
}

int main(int argc, char *argv[]) {
    if (geteuid() == 0) {
        fprintf(stderr, "Error: This program should not be run as root. Exiting.\n");
        return 1;
    }

    g_top_data_dir = setup_data_dir();
    if (!g_top_data_dir) {
        fprintf(stderr, "Error: Failed to get a path to a usable data directory. Exiting.\n");
        return 1;
    }

    join_paths(g_yawl_dir, g_top_data_dir, PROG_NAME);
    if (ensure_dir(g_yawl_dir) != 0) {
        fprintf(stderr, "Error: The program directory (%s) is unusable. Exiting.\n", g_yawl_dir);
        return 1;
    }

    if (setup_runtime() != 0) {
        fprintf(stderr, "Error: Failed setting up the runtime. Exiting.\n");
        return 1;
    }

    const char *wine_path = getenv("WINE_PATH");
    if (!wine_path)
        wine_path = "/usr";

    char *wine_bin = find_wine_binary(wine_path);
    if (!wine_bin) {
        fprintf(stderr, "Error: No wine binary found in %s/bin. Exiting.\n", wine_path);
        return 1;
    }

    char *entry_point = NULL;
    join_paths(entry_point, g_yawl_dir, RUNTIME_PREFIX RUNTIME_VERSION "/_v2-entry-point");
    if (access(entry_point, X_OK) != 0) {
        fprintf(stderr, "Error: Runtime entry point not found: %s\n", entry_point);
        free(entry_point);
        free(wine_bin);
        return 1;
    }

    char **new_argv = calloc(argc + 4, sizeof(char *));
    new_argv[0] = entry_point;
    new_argv[1] = "--verb=waitforexitandrun";
    new_argv[2] = "--";
    new_argv[3] = wine_bin;

    for (int i = 1; i < argc; i++) {
        new_argv[i + 3] = argv[i];
    }

    char *lib_paths = build_library_paths(wine_path);
    if (lib_paths) {
        setenv("LD_LIBRARY_PATH", lib_paths, 1);
        free(lib_paths);
    }

    char *mesa_paths = build_mesa_paths();
    if (mesa_paths) {
        setenv("LIBGL_DRIVERS_PATH", mesa_paths, 1);
        free(mesa_paths);
    }

    if (prctl(PR_SET_CHILD_SUBREAPER, 1UL, 0UL, 0UL, 0UL) == -1)
        fprintf(stderr, "Warning: Failed to set child subreaper status, errno: %d\n", errno);

    execv(entry_point, new_argv);
    perror("Failed to execute runtime"); /* Shouldn't reach here. */

    return 1;
}
