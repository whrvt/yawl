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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"
#include "log.h"
#include "update.h"
#include "util.h"

#define G_LOG_DOMAIN "json-glib"
#include "json-glib/json-glib.h"

#define GITHUB_API_RELEASES_URL "https://api.github.com/repos/whrvt/" PROG_NAME "/releases/latest"
#define GITHUB_DOWNLOAD_URL_FORMAT PACKAGE_URL "/releases/download/%s/" PROG_NAME
#define UPDATE_USER_AGENT PROG_NAME "-updater/" VERSION

/* temp files */
#define RELEASE_INFO_FILE "latest_release.json"
#define NEW_BINARY_FILE PROG_NAME ".new"
#define BACKUP_SUFFIX ".bak"

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

/* Helper function to download GitHub API data */
static RESULT download_github_release_info(const char *output_path) {
    char user_agent[100];
    snprintf(user_agent, sizeof(user_agent), "User-Agent: %s", UPDATE_USER_AGENT);

    char *headers[] = {"Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28", user_agent, NULL};

    return download_file(GITHUB_API_RELEASES_URL, output_path, headers);
}

/* Parse release info and check if an update is available */
static RESULT parse_release_info(const char *json_path, char **tag_name, char **download_url) {
    if (!json_path || !tag_name || !download_url)
        return MAKE_RESULT(SEV_ERROR, CAT_JSON, E_INVALID_ARG);

    *tag_name = NULL;
    *download_url = NULL;

    JsonParser *parser = json_parser_new();
    GError *error = NULL;

    /* Load and parse the JSON file */
    json_parser_load_from_file(parser, json_path, &error);
    if (error) {
        LOG_ERROR("Failed to parse JSON: %s", error->message);
        g_error_free(error);
        g_object_unref(parser);
        return MAKE_RESULT(SEV_ERROR, CAT_JSON, E_PARSE_ERROR);
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!root || JSON_NODE_TYPE(root) != JSON_NODE_OBJECT) {
        g_object_unref(parser);
        return MAKE_RESULT(SEV_ERROR, CAT_JSON, E_PARSE_ERROR);
    }

    JsonObject *root_obj = json_node_get_object(root);

    /* Get tag name (version) */
    if (json_object_has_member(root_obj, "tag_name")) {
        const char *tag = json_object_get_string_member(root_obj, "tag_name");
        *tag_name = strdup(tag);
    } else {
        g_object_unref(parser);
        return MAKE_RESULT(SEV_ERROR, CAT_JSON, E_NOT_FOUND);
    }

    /* Format the download URL */
    if (*tag_name) {
        *download_url = malloc(strlen(GITHUB_DOWNLOAD_URL_FORMAT) + strlen(*tag_name) + 1);
        if (*download_url) {
            sprintf(*download_url, GITHUB_DOWNLOAD_URL_FORMAT, *tag_name);
        } else {
            free(*tag_name);
            *tag_name = NULL;
            g_object_unref(parser);
            return MAKE_RESULT(SEV_ERROR, CAT_JSON, E_OUT_OF_MEMORY);
        }
    }

    g_object_unref(parser);
    return RESULT_OK;
}

/* Make the downloaded file executable */
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
    FILE *src = NULL, *dst = NULL;
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    RESULT result = RESULT_OK;
    char *actual_dest = NULL;

    src = fopen(source, "rb");
    if (!src)
        return result_from_errno();

    if (use_temp)
        append_sep(actual_dest, "", destination, ".tmp");
    else
        actual_dest = strdup(destination);

    dst = fopen(actual_dest, "wb");
    if (!dst) {
        RESULT err_result = result_from_errno();
        free(actual_dest);
        fclose(src);
        return err_result;
    }

    /* Copy the file contents in chunks */
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, src)) > 0) {
        if (fwrite(buffer, 1, bytes_read, dst) != bytes_read) {
            result = result_from_errno();
            LOG_RESULT(LOG_ERROR, result, "Failed to write to destination file");

            fclose(src);
            fclose(dst);
            unlink(actual_dest);
            free(actual_dest);
            return result;
        }
    }

    if (ferror(src)) {
        result = result_from_errno();
        LOG_RESULT(LOG_ERROR, result, "Failed to read from source file");

        fclose(src);
        fclose(dst);
        unlink(actual_dest);
        free(actual_dest);
        return result;
    }

    /* Ensure all data is written */
    fflush(dst);

    /* Preserve perms */
    struct stat src_stat;
    if (stat(source, &src_stat) == 0)
        fchmod(fileno(dst), src_stat.st_mode);

    fclose(src);
    fclose(dst);

    /* If we're using a temp file, rename it to the actual destination */
    if (use_temp) {
        if (rename(actual_dest, destination) != 0) {
            if (errno == EXDEV) {
                LOG_DEBUG("Cross-device rename detected, trying direct copy");
                result = copy_file_raw(actual_dest, destination, 0);
                unlink(actual_dest);
                free(actual_dest);
                return result;
            } else {
                result = result_from_errno();
                LOG_RESULT(LOG_ERROR, result, "Failed to rename temporary file");
                unlink(actual_dest);
                free(actual_dest);
                return result;
            }
        }
    }

    free(actual_dest);
    return RESULT_OK;
}

/* Fallback for when source and destination are on different filesystems (e.g. ext4->btrfs) */
static RESULT copy_file(const char *source, const char *destination) {
    RESULT result = RESULT_OK;

    char *backup_file = NULL;
    join_paths(backup_file, g_yawl_dir, PROG_NAME BACKUP_SUFFIX);

    if (access(destination, F_OK) == 0) {
        if (access(backup_file, F_OK) == 0)
            unlink(backup_file);
        if (rename(destination, backup_file) != 0) {
            if (errno == EXDEV) {
                LOG_DEBUG("Creating backup using copy (cross-device)");
                result = copy_file_raw(destination, backup_file, 0);
                if (FAILED(result)) {
                    LOG_RESULT(LOG_ERROR, result, "Failed to create backup copy");
                    free(backup_file);
                    return result;
                }
            } else {
                result = result_from_errno();
                /* too dangerous to try continuing */
                LOG_RESULT(LOG_ERROR, result, "Failed to create backup");
                free(backup_file);
                return result;
            }
        }
    }

    result = copy_file_raw(source, destination, 1);
    if (FAILED(result)) {
        LOG_RESULT(LOG_ERROR, result, "Failed to copy new binary");

        if (access(backup_file, F_OK) == 0) {
            LOG_INFO("Restoring from backup");
            if (rename(backup_file, destination) != 0) {
                if (errno == EXDEV) {
                    RESULT restore_result = copy_file_raw(backup_file, destination, 0);
                    if (FAILED(restore_result)) {
                        LOG_RESULT(LOG_ERROR, restore_result, "Failed to restore from backup");
                    }
                }
            }
        }

        free(backup_file);
        return result;
    }

    LOG_DEBUG("Backup created at %s", backup_file);
    free(backup_file);

    return RESULT_OK;
}

/* Replace the current binary with the new one */
static RESULT replace_binary(const char *new_binary, const char *current_binary) {
    struct stat src_stat, dst_stat;

    /* Need to manually copy if new/current are on different filesystems */
    if (stat(new_binary, &src_stat) != 0 || stat(current_binary, &dst_stat) != 0)
        return result_from_errno();

    if (src_stat.st_dev == dst_stat.st_dev) {
#ifdef _renameat2
        int result;
        result = _renameat2(AT_FDCWD, new_binary, AT_FDCWD, current_binary, RENAME_EXCHANGE);
        if (result == 0)
            return RESULT_OK;
        if (errno != ENOSYS && errno != EINVAL && errno != ENOTTY)
            LOG_DEBUG_RESULT(result_from_errno(), "renameat2 failed");
#endif
        /* Fallback method: Create a backup and use rename */
        char *backup_file = NULL;
        append_sep(backup_file, "", current_binary, BACKUP_SUFFIX);

        /* Step 1: Backup the current binary */
        if (access(backup_file, F_OK) == 0) {
            if (unlink(backup_file) != 0) {
                RESULT result = result_from_errno();
                free(backup_file);
                return result;
            }
        }

        if (link(current_binary, backup_file) != 0) {
            RESULT result = result_from_errno();
            free(backup_file);
            return result;
        }

        /* Step 2: Replace the current binary with the new one */
        if (rename(new_binary, current_binary) != 0) {
            RESULT result = result_from_errno();
            LOG_ERROR("Failed to replace binary, restoring from backup");
            unlink(current_binary);
            rename(backup_file, current_binary);
            free(backup_file);
            return result;
        }

        LOG_DEBUG("Backup created at %s", backup_file);
        free(backup_file);
        return RESULT_OK;
    }
    LOG_DEBUG("Files on different filesystems, using copy method");
    return copy_file(new_binary, current_binary);
}

static RESULT check_for_updates(void) {
    char *release_file = NULL;
    char *tag_name = NULL;
    char *download_url = NULL;
    RESULT result;

    LOG_INFO("Checking for updates...");

    /* Download release information */
    join_paths(release_file, g_yawl_dir, RELEASE_INFO_FILE);
    result = download_github_release_info(release_file);
    if (FAILED(result)) {
        LOG_RESULT(LOG_ERROR, result, "Failed to download release information");
        free(release_file);
        return result;
    }

    /* Parse release information */
    result = parse_release_info(release_file, &tag_name, &download_url);
    if (FAILED(result)) {
        unlink(release_file);
        free(release_file);
        return result;
    }

    /* Compare versions */
    int current_version = parse_version(VERSION);
    int latest_version = parse_version(tag_name);

    if (latest_version <= current_version) {
        LOG_INFO("You are already running the latest version (%s).", VERSION);
        unlink(release_file);
        free(release_file);
        free(tag_name);
        free(download_url);
        return RESULT_OK;
    }

    LOG_INFO("Update available: %s -> %s", "v" VERSION, tag_name);

    /* Save download URL for later use */
    FILE *fp = fopen(release_file, "w");
    if (fp) {
        fprintf(fp, "%s", download_url);
        fclose(fp);
    }

    free(tag_name);
    free(download_url);
    free(release_file);

    return MAKE_RESULT(SEV_INFO, CAT_GENERAL, E_UPDATE_AVAILABLE);
}

static RESULT perform_update(void) {
    char *release_file = NULL;
    char download_url[1024] = {0};
    char *temp_binary = NULL;
    char *self_path = NULL;
    char *download_dir = NULL;
    RESULT result;

    /* Get the download URL from the saved file */
    join_paths(release_file, g_yawl_dir, RELEASE_INFO_FILE);

    FILE *fp = fopen(release_file, "r");
    if (!fp) {
        result = result_from_errno();
        free(release_file);
        return result;
    }

    if (!fgets(download_url, sizeof(download_url), fp)) {
        fclose(fp);
        free(release_file);
        return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_PARSE_ERROR);
    }

    fclose(fp);

    /* Get current executable path */
    self_path = realpath("/proc/self/exe", NULL);
    if (!self_path) {
        free(release_file);
        return result_from_errno();
    }

    /* First try: Same directory as executable */
    download_dir = strdup(self_path);

    char *last_slash = strrchr(download_dir, '/');
    if (last_slash) {
        *last_slash = '\0'; /* Truncate to get directory */

        /* Check if directory is writable */
        if (access(download_dir, W_OK) == 0) {
            LOG_DEBUG("Using executable directory for download: %s", download_dir);
            join_paths(temp_binary, download_dir, NEW_BINARY_FILE);
        } else {
            /* Not writable, fallback to yawl_dir */
            free(download_dir);
            download_dir = NULL;
        }
    } else {
        /* Shouldn't happen with realpath, but just in case */
        free(download_dir);
        download_dir = NULL;
    }

    /* Use yawl_dir if exec dir is unwritable */
    if (!download_dir) {
        LOG_DEBUG("Using yawl directory for download: %s", g_yawl_dir);
        join_paths(temp_binary, g_yawl_dir, NEW_BINARY_FILE);
    }

    LOG_INFO("Downloading update from %s", download_url, temp_binary);
    result = download_file(download_url, temp_binary, NULL);
    if (FAILED(result)) {
        LOG_RESULT(LOG_ERROR, result, "Failed to download update");
        goto cleanup_update;
    }

    result = make_executable(temp_binary);
    if (FAILED(result)) {
        LOG_RESULT(LOG_ERROR, result, "Failed to set executable permissions");
        goto cleanup_update;
    }

    LOG_INFO("Installing update...");
    result = replace_binary(temp_binary, self_path);
    if (SUCCEEDED(result))
        result = MAKE_RESULT(SEV_INFO, CAT_RUNTIME, E_UPDATE_PERFORMED);

cleanup_update:
    unlink(release_file);
    unlink(temp_binary);
    free(release_file);
    free(self_path);
    free(download_dir);
    free(temp_binary);

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
