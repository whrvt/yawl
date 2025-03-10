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
#include <getopt.h>
#include <pwd.h>
#include <stdint.h>
#include <sys/prctl.h>

/* This file does not normally exist, but it contains embedded CA certificate
   data generated as part of the curl build process, and we move it here to be
   able to use it for CURLOPT_CAINFO_BLOB */
#include "curl/ca_cert_embed.h"

#include "archive.h"
#include "archive_entry.h"
#include "curl/curl.h"

#include "util.h"

#define PROG_NAME "yawl"

#define RUNTIME_VERSION "sniper"
#define RUNTIME_BASE_URL                                                                                               \
    "https://repo.steampowered.com/steamrt-images-" RUNTIME_VERSION "/snapshots/latest-container-runtime-public-beta"
#define RUNTIME_PREFIX "SteamLinuxRuntime_"
#define RUNTIME_ARCHIVE_NAME RUNTIME_PREFIX RUNTIME_VERSION ".tar.xz"
#define RUNTIME_MTREE_NAME "com.valvesoftware.SteamRuntime.Platform-amd64%2Ci386-" RUNTIME_VERSION "-runtime.mtree.gz"

struct options {
    int verify;    /* 0 = no verification (default), 1 = verify */
    int reinstall; /* 0 = don't reinstall unless needed, 1 = force reinstall */
    int help;      /* 0 = don't show help, 1 = show help and exit */
};

static char *g_top_data_dir;
static char *g_yawl_dir;

static void print_usage(void) {
    printf("Usage: " PROG_NAME " [options] [-- wine_args...]\n");
    printf("Options:\n");
    printf("  --verify       Verify the runtime before running (default: only verify after install)\n"
"                            Also can be used to check for runtime updates (updating will be a separate option in the future)\n");
    printf("  --reinstall    Force reinstallation of the runtime\n");
    printf("  --help         Display this help and exit\n");
    printf("\n");
    printf("Notes:\n");
    printf("  When using options, you must separate Wine arguments with --\n");
    printf("  Example: " PROG_NAME " --reinstall -- winecfg --help\n");
    printf("\n");
    printf("  Without options, you can call Wine directly:\n");
    printf("  Example: " PROG_NAME " winecfg --help\n");
    printf("\n");
    printf("Environment variables:\n");
    printf("  WINE_PATH      Path to the top-level folder that contains bin/wine, lib/wine, etc.\n");
    printf("                 Default: /usr\n");
}

static int parse_options(int argc, char *argv[], struct options *opts, int *first_arg_index) {
    static struct option long_options[] = {{"verify", no_argument, 0, 'v'},
                                           {"no-verify", no_argument, 0, 'n'},
                                           {"reinstall", no_argument, 0, 'r'},
                                           {"help", no_argument, 0, 'h'},
                                           {0, 0, 0, 0}};

    /* Set defaults */
    opts->verify = 0;
    opts->reinstall = 0;
    opts->help = 0;

    int option_index = 0;
    int c;
    int has_yawl_options = 0;
    int found_separator = 0;

    optind = 1;

    /* First pass: check if we have a -- separator */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            found_separator = 1;
            break;
        }
    }

    while ((c = getopt_long(argc, argv, "", long_options, &option_index)) != -1) {
        has_yawl_options = 1;

        if (strcmp(argv[optind - 1], "--") == 0)
            break;

        switch (c) {
        case 'v':
            opts->verify = 1;
            break;
        case 'n':
            opts->verify = 0;
            break;
        case 'r':
            opts->reinstall = 1;
            break;
        case 'h':
            opts->help = 1;
            break;
        case '?':
            return -1;
        default:
            fprintf(stderr, "Error: Unknown option code %d\n", c);
            return -1;
        }
    }

    if (has_yawl_options && !found_separator && optind < argc) {
        fprintf(stderr, "Error: When using yawl options, you must separate Wine arguments with --\n");
        fprintf(stderr, "Example: yawl --reinstall -- winecfg --help\n");
        return -1;
    }

    if (found_separator) {
        for (int i = optind; i < argc; i++) {
            if (strcmp(argv[i], "--") == 0) {
                *first_arg_index = i + 1;
                return 0;
            }
        }
    }

    *first_arg_index = optind;
    return 0;
}

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

static int download_file(const char *url, const char *output_path) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "curl error: %s\n", curl_easy_strerror(CURLE_FAILED_INIT));
        return -1;
    }

    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        fprintf(stderr, "Error: Couldn't open output_path (%s): %s\n", output_path, strerror(errno));
        curl_easy_cleanup(curl);
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    /* Copied from curl's `src/tool_operate.c`, use the embedded CA certificate data */
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
    archive_read_support_filter_gzip(a);

    ext = archive_write_disk_new();
    archive_write_disk_set_options(ext, flags);
    archive_write_disk_set_standard_lookup(ext);

    if ((r = archive_read_open_filename(a, archive_path, BUFFER_SIZE))) {
        fprintf(stderr, "Error: Extracting failed (read_open_filename): %s\n", archive_error_string(a));
        return -1;
    }

    char *old_cwd = getcwd(NULL, 0);
    if (chdir(extract_path) != 0) {
        fprintf(stderr, "Error: Extracting failed (chdir): %s\n", strerror(errno));
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

static int verify_runtime(const char *runtime_path) {
    char *versions_txt_path = NULL;
    char *pv_verify_path = NULL;

    /* First, a lightweight check for VERSIONS.txt (same as the SLR shell script) */
    join_paths(versions_txt_path, runtime_path, "VERSIONS.txt");
    if (access(versions_txt_path, F_OK) != 0) {
        fprintf(stderr, "Error: VERSIONS.txt not found. Runtime may be corrupt or incomplete.\n");
        free(versions_txt_path);
        return -1;
    }
    free(versions_txt_path);

    /* Check if pv-verify exists */
    join_paths(pv_verify_path, runtime_path, "pressure-vessel/bin/pv-verify");
    if (access(pv_verify_path, X_OK) != 0) {
        fprintf(stderr, "Error: pv-verify not found. Runtime may be corrupt or incomplete.\n");
        free(pv_verify_path);
        return -1;
    }

    printf("Using pv-verify for runtime verification...\n");

    char *cmd = NULL;
    append_sep(cmd, " ", pv_verify_path, "--quiet");

    char *old_cwd = getcwd(NULL, 0);
    if (chdir(runtime_path) != 0) {
        fprintf(stderr, "Error: Failed to change to runtime directory: %s\n", strerror(errno));
        free(old_cwd);
        free(pv_verify_path);
        free(cmd);
        return -1;
    }

    /* Run pv-verify */
    int cmd_ret = system(cmd);

    /* Restore directory */
    chdir(old_cwd);
    free(old_cwd);
    free(cmd);

    if (cmd_ret != 0) {
        fprintf(stderr, "Error: pv-verify reported verification errors (exit code %d).\n", WEXITSTATUS(cmd_ret));
        free(pv_verify_path);
        return -1;
    }

    printf("Runtime verification completed successfully.\n");
    free(pv_verify_path);
    return 0;
}

static int get_hash_from_sha256sums(const char *file_name, char *hash_str, size_t hash_str_len) {
    char *url = NULL;
    char *sums_path = NULL;
    FILE *fp = NULL;
    char line[200];
    int found = 0;

    join_paths(url, RUNTIME_BASE_URL, "SHA256SUMS");
    join_paths(sums_path, g_yawl_dir, "SHA256SUMS");

    if (download_file(url, sums_path) != 0) {
        fprintf(stderr, "Error: Failed to download SHA256SUMS file\n");
        free(url);
        free(sums_path);
        return -1;
    }

    fp = fopen(sums_path, "r");
    if (!fp) {
        fprintf(stderr, "Error: Failed to open SHA256SUMS file\n");
        free(url);
        free(sums_path);
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        /* Format is "hash *filename" */
        char *hash_end = strchr(line, ' ');
        if (!hash_end)
            continue;

        *hash_end = '\0';
        char *file = hash_end + 2; /* Skip " *" */

        char *newline = strchr(file, '\n');
        if (newline)
            *newline = '\0';

        if (strcmp(file, file_name) == 0) {
            strncpy(hash_str, line, hash_str_len - 1);
            hash_str[hash_str_len - 1] = '\0';
            found = 1;
            break;
        }
    }

    fclose(fp);
    free(url);
    free(sums_path);

    return found ? 0 : -1;
}

static int setup_runtime(const struct options *opts) {
    int ret = 0, need_extraction = 0, need_verification = 0;
    char *archive_path = NULL, *runtime_path = NULL;
    struct stat st;

    join_paths(archive_path, g_yawl_dir, RUNTIME_ARCHIVE_NAME);
    join_paths(runtime_path, g_yawl_dir, RUNTIME_PREFIX RUNTIME_VERSION);

    /* Determine if we need to extract and/or verify the runtime */
    if (opts->reinstall) {
        need_extraction = 1;
        need_verification = opts->verify;
    } else if (stat(runtime_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        /* Runtime doesn't exist, we need to extract */
        need_extraction = 1;
        /* Always verify a fresh installation */
        need_verification = 1;
    } else if (opts->verify) {
        /* Runtime exists but verification is explicitly requested */
        printf("Verifying existing runtime integrity...\n");
        ret = verify_runtime(runtime_path);
        if (ret != 0) {
            fprintf(stderr, "Warning: Runtime verification failed. Reinstalling...\n");
            remove_dir(runtime_path);
            need_extraction = 1;
            need_verification = 1;
        }
    }

    /* If we don't need to extract, we're done */
    if (!need_extraction)
        goto setup_done;

    /* Check if we need to download the archive */
    int archive_exists = (stat(archive_path, &st) == 0 && S_ISREG(st.st_mode));

    if (!archive_exists || opts->reinstall) {
        printf("Downloading Steam Runtime (%s)...\n", RUNTIME_VERSION);
        if ((ret = download_file(RUNTIME_BASE_URL "/" RUNTIME_ARCHIVE_NAME, archive_path)) != 0)
            goto setup_done;
        archive_exists = 1;
    }

    /* Always verify the archive hash before extraction */
    if (archive_exists) {
        printf("Verifying local runtime archive integrity...\n");
        char expected_hash[65] = {0};
        char actual_hash[65] = {0};

        if (get_hash_from_sha256sums(RUNTIME_ARCHIVE_NAME, expected_hash, sizeof(expected_hash)) != 0) {
            fprintf(stderr, "Warning: Could not get expected hash. Continuing anyway.\n");
        } else if (calculate_sha256(archive_path, actual_hash, sizeof(actual_hash)) != 0) {
            fprintf(stderr, "Warning: Could not calculate hash. Re-downloading archive.\n");
            if ((ret = download_file(RUNTIME_BASE_URL "/" RUNTIME_ARCHIVE_NAME, archive_path)) != 0)
                goto setup_done;
        } else if (strcmp(expected_hash, actual_hash) != 0) {
            fprintf(stderr, "Warning: Archive hash mismatch. Re-downloading.\n");
            if ((ret = download_file(RUNTIME_BASE_URL "/" RUNTIME_ARCHIVE_NAME, archive_path)) != 0)
                goto setup_done;
        } else {
            printf("Archive hash verified.\n");
        }
    }

    /* Extract the runtime */
    printf("Extracting runtime...\n");
    ret = extract_archive(archive_path, g_yawl_dir);
    if (ret != 0) {
        fprintf(stderr, "Error: Failed to extract runtime\n");
        goto setup_done;
    }

    /* Verify the runtime if needed */
    if (need_verification) {
        printf("Verifying runtime integrity...\n");
        ret = verify_runtime(runtime_path);
        if (ret != 0) {
            fprintf(stderr, "Warning: Runtime verification failed after extraction.\n");

            /* One retry - redownload and extract again */
            if (!opts->reinstall) {
                fprintf(stderr, "Attempting one more time with a fresh download...\n");
                remove_dir(runtime_path);
                unlink(archive_path);

                printf("Downloading Steam Runtime (%s)...\n", RUNTIME_VERSION);
                if ((ret = download_file(RUNTIME_BASE_URL "/" RUNTIME_ARCHIVE_NAME, archive_path)) != 0)
                    goto setup_done;

                printf("Extracting runtime...\n");
                ret = extract_archive(archive_path, g_yawl_dir);
                if (ret != 0) {
                    fprintf(stderr, "Error: Failed to extract runtime on retry\n");
                    goto setup_done;
                }

                printf("Verifying runtime integrity (final attempt)...\n");
                ret = verify_runtime(runtime_path);
                if (ret != 0) {
                    fprintf(stderr, "Error: Runtime verification failed after retrying. Continuing anyway...\n");
                    ret = 0;
                }
            } else {
                /* If we're already doing a reinstall, don't retry again */
                fprintf(stderr, "Continuing despite verification failure...\n");
                ret = 0;
            }
        }
    }

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
    struct options opts;
    int first_arg_index = 1;

    if (geteuid() == 0) {
        fprintf(stderr, "Error: This program should not be run as root. Exiting.\n");
        return 1;
    }

    if (parse_options(argc, argv, &opts, &first_arg_index) != 0)
        return 1;

    if (opts.help) {
        print_usage();
        return 0;
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

    if (setup_runtime(&opts) != 0) {
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

    /* Skip our options to get to wine args */
    int wine_argc = argc - first_arg_index + 4;
    char **new_argv = calloc(wine_argc + 1, sizeof(char *));
    new_argv[0] = entry_point;
    new_argv[1] = "--verb=waitforexitandrun";
    new_argv[2] = "--";
    new_argv[3] = wine_bin;

    /* Copy the wine args */
    for (int i = first_arg_index; i < argc; i++) {
        new_argv[i - first_arg_index + 4] = argv[i];
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
        fprintf(stderr, "Warning: Failed to set child subreaper status: %s\n", strerror(errno));

    execv(entry_point, new_argv);
    perror("Failed to execute runtime"); /* Shouldn't reach here. */

    return 1;
}
