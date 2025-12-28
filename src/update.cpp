/*
 * Self-update functionality
 *
 * Copyright (C) 2025 William Horvath
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * See the full license text in the repository LICENSE file.
 */

#include "config.h"

#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#define G_LOG_DOMAIN "json-glib"
#include "json-glib/json-glib.h"

#include "log.hpp"
#include "macros.hpp"
#include "update.hpp"
#include "util.hpp"
#include "yawlconfig.hpp"

#include "fmt/printf.h"

#define GITHUB_API_RELEASES_URL "https://api.github.com/repos/whrvt/" PROG_NAME "/releases/latest"
#define GITHUB_RELEASES_PAGE_URL PACKAGE_URL "/releases/download"
#define UPDATE_USER_AGENT PROG_NAME "-updater/" VERSION

/* json-glib specific cleanup */
static forceinline void cleanup_json_parser(void *p) {
    JsonParser **parser = (JsonParser **)p;
    if (parser && *parser) {
        g_object_unref(*parser);
        *parser = nullptr;
    }
}

static forceinline void cleanup_gerror(void *p) {
    GError **error = (GError **)p;
    if (error && *error) {
        g_error_free(*error);
        *error = nullptr;
    }
}

#define autounref_json [[gnu::cleanup(cleanup_json_parser)]]
#define autofree_gerror [[gnu::cleanup(cleanup_gerror)]]

/* Parse release info and check if an update is available */
static RESULT parse_release_info(const char *json_path, char *tag_name[], char *download_url[]) {
    if (!json_path || !tag_name || !download_url)
        return MAKE_RESULT(SEV_ERROR, CAT_JSON, E_INVALID_ARG);

    *tag_name = nullptr;
    *download_url = nullptr;

    autounref_json JsonParser *parser = json_parser_new();
    autofree_gerror GError *error = nullptr;

    /* Load and parse the JSON file */
    json_parser_load_from_file(parser, json_path, &error);
    if (error) {
        LOG_ERROR("Failed to parse JSON: %s", error->message);
        return MAKE_RESULT(SEV_ERROR, CAT_JSON, E_PARSE_ERROR);
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!root || JSON_NODE_TYPE(root) != JSON_NODE_OBJECT)
        return MAKE_RESULT(SEV_ERROR, CAT_JSON, E_PARSE_ERROR);

    JsonObject *root_obj = json_node_get_object(root);

    /* Get tag name (version) */
    if (json_object_has_member(root_obj, "tag_name")) {
        const char *tag = json_object_get_string_member(root_obj, "tag_name");
        *tag_name = strdup(tag);
    } else {
        return MAKE_RESULT(SEV_ERROR, CAT_JSON, E_NOT_FOUND);
    }

    /* Format the download URL */
    if (*tag_name) {
        /* NOTE: x86_64 binaries are uploaded as just "yawl", aarch64 as "yawl_aarch64".
         *       This is just for backwards compatibility. */
        join_paths(*download_url, GITHUB_RELEASES_PAGE_URL, *tag_name, PROG_NAME_ARCH);
    }

    if (!*download_url)
        return MAKE_RESULT(SEV_ERROR, CAT_JSON, E_OUT_OF_MEMORY);

    return RESULT_OK;
}

/* Mark a file as executable */
static RESULT make_executable(const char *file_path) {
    struct stat st;

    if (stat(file_path, &st) != 0)
        return result_from_errno();

    /* Add executable bits matching read bits */
    mode_t new_mode = st.st_mode;
    if (new_mode & S_IRUSR)
        new_mode |= S_IXUSR;
    if (new_mode & S_IRGRP)
        new_mode |= S_IXGRP;
    if (new_mode & S_IROTH)
        new_mode |= S_IXOTH;

    if (chmod(file_path, new_mode) != 0)
        return result_from_errno();

    return RESULT_OK;
}

static RESULT copy_file_raw(const char *source, const char *destination, int use_temp) {
    autofree char *actual_dest = nullptr;

    if (use_temp)
        append_sep(actual_dest, "", destination, ".tmp");
    else
        actual_dest = strdup(destination);

    /* Open source file (scoped) */
    {
        autoclose FILE *src = fopen(source, "rb");
        if (!src)
            return result_from_errno();

        /* Open destination file in its own scope */
        autoclose FILE *dst = fopen(actual_dest, "wb");
        if (!dst)
            return result_from_errno();

        /* Copy the file contents in chunks */
        char buffer[BUFFER_SIZE];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, src)) > 0) {
            if (fwrite(buffer, 1, bytes_read, dst) != bytes_read) {
                RESULT result = result_from_errno();
                LOG_RESULT(Level::Error, result, "Failed to write to destination file");
                unlink(actual_dest);
                return result;
            }
        }

        if (ferror(src)) {
            RESULT result = result_from_errno();
            LOG_RESULT(Level::Error, result, "Failed to read from source file");
            unlink(actual_dest);
            return result;
        }

        /* Ensure all data is written */
        fflush(dst);

        /* Preserve perms */
        struct stat src_stat;
        if (stat(source, &src_stat) == 0)
            fchmod(fileno(dst), src_stat.st_mode);
    }
    /* Files are now closed */

    /* If we're using a temp file, rename it to the actual destination */
    if (use_temp) {
        if (rename(actual_dest, destination) != 0) {
            if (errno == EXDEV) {
                LOG_DEBUG("Cross-device rename detected, trying direct copy");
                RESULT result = copy_file_raw(actual_dest, destination, 0);
                unlink(actual_dest);
                return result;
            } else {
                RESULT result = result_from_errno();
                LOG_RESULT(Level::Error, result, "Failed to rename temporary file");
                unlink(actual_dest);
                return result;
            }
        }
    }

    return RESULT_OK;
}

/* Fallback for when source and destination are on different filesystems (e.g. ext4->btrfs) */
static RESULT copy_file(const char *source, const char *destination) {
    RESULT result = RESULT_OK;

    autofree char *backup_file = nullptr;
    join_paths(backup_file, config::yawl_dir, PROG_NAME ".bak");

    if (access(destination, F_OK) == 0) {
        if (access(backup_file, F_OK) == 0)
            unlink(backup_file);
        if (rename(destination, backup_file) != 0) {
            if (errno == EXDEV) {
                LOG_DEBUG("Creating backup using copy (cross-device)");
                result = copy_file_raw(destination, backup_file, 0);
                if (FAILED(result)) {
                    LOG_RESULT(Level::Error, result, "Failed to create backup copy");
                    return result;
                }
            } else {
                result = result_from_errno();
                /* too dangerous to try continuing */
                LOG_RESULT(Level::Error, result, "Failed to create backup");
                return result;
            }
        }
    }

    result = copy_file_raw(source, destination, 1);
    if (FAILED(result)) {
        LOG_RESULT(Level::Error, result, "Failed to copy new binary");
        if (access(backup_file, F_OK) == 0) {
            LOG_INFO("Restoring from backup");
            if (rename(backup_file, destination) != 0) {
                if (errno == EXDEV) {
                    RESULT restore_result = copy_file_raw(backup_file, destination, 0);
                    if (FAILED(restore_result))
                        LOG_RESULT(Level::Error, restore_result, "Failed to restore from backup");
                }
            }
        }
        return result;
    }

    LOG_DEBUG("Backup created at %s", backup_file);
    return RESULT_OK;
}

/* This is required for musl, there's no wrapper for renameat2 like glibc */
#define RENAME_NOREPLACE (1 << 0)
#define RENAME_EXCHANGE (1 << 1)
#define RENAME_WHITEOUT (1 << 2)
#ifndef HAVE_RENAMEAT2
#include <syscall.h>
#if defined(HAVE_RENAMEAT) && defined(SYS_renameat2)
static inline long renameat2(int oldfd, const char *oldname, int newfd, const char *newname, unsigned flags) {
    if (!flags)
        return syscall(SYS_renameat, oldfd, oldname, newfd, newname);
    return syscall(SYS_renameat2, oldfd, oldname, newfd, newname, flags);
}
#else
static inline long renameat2(int, const char *, int, const char *, unsigned) {
    errno = ENOSYS;
    return -1;
}
#endif
#endif

/* Replace the current binary with the new one */
static RESULT replace_binary(const char *new_binary, const char *current_binary) {
    struct stat src_stat, dst_stat;

    /* Need to manually copy if new/current are on different filesystems */
    if (stat(new_binary, &src_stat) != 0 || stat(current_binary, &dst_stat) != 0)
        return result_from_errno();

    if (src_stat.st_dev == dst_stat.st_dev) {
        if (!renameat2(AT_FDCWD, new_binary, AT_FDCWD, current_binary, RENAME_EXCHANGE))
            return RESULT_OK;
        if (errno != ENOSYS && errno != EINVAL && errno != ENOTTY)
            LOG_DEBUG_RESULT(result_from_errno(), "renameat2 failed");

        /* Fallback method: Create a backup and use rename */
        autofree char *backup_file = nullptr;
        append_sep(backup_file, "", current_binary, ".bak");

        /* Step 1: Backup the current binary */
        if (access(backup_file, F_OK) == 0)
            if (unlink(backup_file) != 0)
                return result_from_errno();

        if (link(current_binary, backup_file) != 0)
            return result_from_errno();

        /* Step 2: Replace the current binary with the new one */
        if (rename(new_binary, current_binary) != 0) {
            RESULT result = result_from_errno();
            LOG_ERROR("Failed to replace binary, restoring from backup");
            unlink(current_binary);
            rename(backup_file, current_binary);
            return result;
        }

        LOG_DEBUG("Backup created at %s", backup_file);
        return RESULT_OK;
    }
    LOG_DEBUG("Files on different filesystems, using copy method");
    return copy_file(new_binary, current_binary);
}

/* Parse version string and return a comparable integer */
static int parse_version(const char *version) {
    if (!version || *version == '\0')
        return -1;

    if (*version == 'v')
        version++;

    int major = 0, minor = 0, patch = 0;
    if (sscanf(version, "%d.%d.%d", &major, &minor, &patch) < 1)
        return -1;

    return (major * 10000) + (minor * 100) + patch;
}

static RESULT check_for_updates(void) {
    autofree char *release_file = nullptr;
    autofree char *tag_name = nullptr;
    autofree char *download_url = nullptr;
    RESULT result;
    const char *headers[] = {"Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28",
                             "User-Agent: " UPDATE_USER_AGENT, nullptr};

    LOG_INFO("Checking for updates...");

    /* Download release information */
    join_paths(release_file, config::yawl_dir, "latest_release.json");
    result = download_file(GITHUB_API_RELEASES_URL, release_file, headers);
    if (FAILED(result)) {
        LOG_RESULT(Level::Error, result, "Failed to download release information");
        return result;
    }

    /* Parse release information */
    result = parse_release_info(release_file, &tag_name, &download_url);
    if (FAILED(result)) {
        unlink(release_file);
        return result;
    }

    /* Compare versions */
    int current_version = parse_version(VERSION);
    int latest_version = parse_version(tag_name);

    if (latest_version <= current_version) {
        LOG_INFO("You are already running the latest version (%s).", VERSION);
        unlink(release_file);
        return RESULT_OK;
    }

    LOG_INFO("Update available: %s -> %s", "v" VERSION, tag_name);

    /* Save download URL for later use */
    autoclose FILE *fp = fopen(release_file, "w");
    if (fp)
        fmt::fprintf(fp, "%s", download_url);

    return MAKE_RESULT(SEV_INFO, CAT_GENERAL, E_UPDATE_AVAILABLE);
}

static RESULT perform_update(void) {
    autofree_del char *release_file = nullptr;
    autofree_del char *temp_binary = nullptr;
    autofree char *self_path = nullptr;
    autofree char *download_dir = nullptr;
    char download_url[1024] = {};
    RESULT result;

    /* Get the download URL from the saved file */
    join_paths(release_file, config::yawl_dir, "latest_release.json");

    autoclose FILE *fp = fopen(release_file, "r");
    if (!fp)
        return result_from_errno();

    if (!fgets(download_url, sizeof(download_url), fp))
        return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_PARSE_ERROR);

    /* Get current executable path */
    self_path = realpath("/proc/self/exe", nullptr);
    if (!self_path)
        return result_from_errno();

    /* Try to use same directory as executable (scoped) */
    {
        autofree char *temp_dir = strdup(self_path);
        if (temp_dir) {
            char *last_slash = strrchr(temp_dir, '/');
            if (last_slash) {
                *last_slash = '\0'; /* Truncate to get directory */

                /* Check if directory is writable */
                if (access(temp_dir, W_OK) == 0) {
                    LOG_DEBUG("Using executable directory for download: %s", temp_dir);
                    download_dir = temp_dir;
                    temp_dir = nullptr; /* Transfer ownership, don't free */
                    join_paths(temp_binary, download_dir, PROG_NAME ".new");
                }
            }
        }
    }

    /* Use yawl_dir if exec dir is unwritable */
    if (!download_dir) {
        LOG_DEBUG("Using yawl directory for download: %s", config::yawl_dir);
        join_paths(temp_binary, config::yawl_dir, PROG_NAME ".new");
    }

    LOG_INFO("Downloading update from %s", download_url, temp_binary);
    result = download_file(download_url, temp_binary, nullptr);
    if (FAILED(result)) {
        LOG_RESULT(Level::Error, result, "Failed to download update");
        return result;
    }

    result = make_executable(temp_binary);
    if (FAILED(result)) {
        LOG_RESULT(Level::Error, result, "Failed to set executable permissions");
        return result;
    }

    LOG_INFO("Installing update...");
    result = replace_binary(temp_binary, self_path);

    if (SUCCEEDED(result))
        result = MAKE_RESULT(SEV_INFO, CAT_RUNTIME, E_UPDATE_PERFORMED);

    return result;
}

/* Handle all update operations based on command line flags */
RESULT handle_updates(int check_only, int do_update) {
    RESULT result = RESULT_OK;

    if (check_only || do_update)
        result = check_for_updates();

    if (do_update && RESULT_CODE(result) == E_UPDATE_AVAILABLE)
        result = perform_update();

    return result;
}
