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

#include "config.h"

#include <getopt.h>
#include <stdint.h>
#include <sys/prctl.h>

#include "apparmor.h"
#include "log.h"
#include "util.h"

#define RUNTIME_PREFIX "SteamLinuxRuntime_"
#define RUNTIME_VERSION "sniper"
#define RUNTIME_ARCHIVE_NAME RUNTIME_PREFIX RUNTIME_VERSION ".tar.xz"

#define RUNTIME_BASE_URL                                                                                               \
    "https://repo.steampowered.com/steamrt-images-" RUNTIME_VERSION "/snapshots/latest-container-runtime-public-beta"

#define DEFAULT_EXEC_PATH "/usr/bin/wine"
#define CONFIG_EXTENSION ".cfg"

const char *g_yawl_dir;
const char *g_config_dir;

struct options {
    int version;        /* 1 = return a version string and exit */
    int verify;         /* 0 = no verification (default), 1 = verify */
    int reinstall;      /* 0 = don't reinstall unless needed, 1 = force reinstall */
    int help;           /* 0 = don't show help, 1 = show help and exit */
    char *exec_path;    /* Path to the executable to run (default: /usr/bin/wine) */
    char *make_wrapper; /* Name of the wrapper to create (NULL = don't create) */
    char *config;       /* Name of the config to use (NULL = use argv[0] or default) */
    char *wineserver;   /* Path to the wineserver binary (NULL = don't create wineserver wrapper) */
};

static void print_usage(void) {
    printf("Usage: " PROG_NAME " [args_for_executable...]\n");
    printf("\n");
    printf("Environment variables:\n");
    printf("  YAWL_VERBS       Semicolon-separated list of verbs to control yawl behavior:\n");
    printf("                   - 'version'   Just print the version of yawl and exit\n");
    printf("                   - 'verify'    Verify the runtime before running (default: only verify after install)\n");
    printf("                                 Also can be used to check for runtime updates (will be a separate option "
           "in the future)\n");
    printf("                   - 'reinstall' Force reinstallation of the runtime\n");
    printf("                   - 'help'      Display this help and exit\n");
    printf("                   - 'exec=PATH' Set the executable to run in the container (default: %s)\n",
           DEFAULT_EXEC_PATH);
    printf("                   - 'make_wrapper=NAME' Create a wrapper configuration and symlink\n");
    printf("                   - 'config=NAME' Use a specific configuration file\n");
    printf("                   - 'wineserver=PATH' Set the wineserver executable path when creating a wrapper\n");
    printf(
        "                   Example: "
        "YAWL_VERBS=\"make_wrapper=osu;exec=/opt/wine-osu/bin/wine;wineserver=/opt/wine-osu/bin/wineserver\" " PROG_NAME
        "\n");
    printf("                   Example: YAWL_VERBS=\"verify;reinstall\" " PROG_NAME " winecfg\n");
    printf("                   Example: YAWL_VERBS=\"exec=/opt/wine/bin/wine64\" " PROG_NAME " winecfg\n");
    printf("                   Example: YAWL_VERBS=\"make_wrapper=cool-wine;exec=/opt/wine/bin/wine64\" " PROG_NAME
           "\n\n");
    printf("  YAWL_INSTALL_DIR Override the default installation directory of $XDG_DATA_HOME/" PROG_NAME
           " or $HOME/.local/share/" PROG_NAME "\n");
    printf(
        "                   Example: YAWL_INSTALL_DIR=\"$HOME/programs/winelauncher\" YAWL_VERBS=\"reinstall\" yawl\n");
    printf("\n");
    printf("  YAWL_LOG_LEVEL   Control the verbosity of the logging output. Valid values are:\n");
    printf("                   - 'none'     Turn off all logging\n");
    printf("                   - 'error'    Show only critical errors that prevent proper operation\n");
    printf("                   - 'warning'  Show warnings and errors (default)\n");
    printf("                   - 'info'     Show normal operational information and all of the above\n");
    printf("                   - 'debug'    Show detailed debugging information and all of the above\n");
    printf("\n");
    printf("  YAWL_LOG_FILE    Specify a custom path for the log file. By default, logs are written to:\n");
    printf("                   - Terminal output (only when running interactively)\n");
    printf("                   - $YAWL_INSTALL_DIR/yawl.log\n");
}

/* Parse a single option string and update the options structure */
static RESULT parse_option(const char *option, struct options *opts) {
    if (!option || !opts || !option[0])
        return RESULT_OK; /* Skip empty options, not an error */

    if (strncmp(option, "version", 7) == 0) {
        opts->version = 1;
    } else if (strncmp(option, "verify", 6) == 0) {
        opts->verify = 1;
    } else if (strncmp(option, "reinstall", 9) == 0) {
        opts->reinstall = 1;
    } else if (strncmp(option, "help", 4) == 0) {
        opts->help = 1;
    } else if (strncmp(option, "exec=", 5) == 0) {
        free(opts->exec_path);

        opts->exec_path = expand_path(option + 5);
        if (!opts->exec_path) {
            LOG_ERROR("Failed to expand exec path: %s", option + 5);
            return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_OUT_OF_MEMORY);
        }
    } else if (strncmp(option, "make_wrapper=", 13) == 0) {
        free(opts->make_wrapper);
        opts->make_wrapper = strdup(option + 13);
        if (!opts->make_wrapper)
            return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_OUT_OF_MEMORY);
    } else if (strncmp(option, "config=", 7) == 0) {
        free(opts->config);
        opts->config = strdup(option + 7);
        if (!opts->config)
            return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_OUT_OF_MEMORY);
    } else if (strncmp(option, "wineserver=", 11) == 0) {
        free(opts->wineserver);

        opts->wineserver = expand_path(option + 11);
        if (!opts->wineserver) {
            LOG_ERROR("Failed to expand wineserver path: %s", option + 11);
            return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_OUT_OF_MEMORY);
        }
    } else {
        return MAKE_RESULT(SEV_WARNING, CAT_CONFIG, E_UNKNOWN); /* Unknown option */
    }

    return RESULT_OK;
}

static RESULT parse_env_options(struct options *opts) {
    opts->version = 0;
    opts->verify = 0;
    opts->reinstall = 0;
    opts->help = 0;
    opts->exec_path = strdup(DEFAULT_EXEC_PATH);
    opts->make_wrapper = NULL;
    opts->config = NULL;
    opts->wineserver = NULL;

    if (!opts->exec_path)
        return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_OUT_OF_MEMORY);

    const char *verbs = getenv("YAWL_VERBS");
    if (!verbs)
        return RESULT_OK;

    char *verbs_copy = strdup(verbs);
    if (!verbs_copy) {
        free(opts->exec_path);
        opts->exec_path = NULL;
        return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_OUT_OF_MEMORY);
    }

    char *token = strtok(verbs_copy, ";");
    RESULT result = RESULT_OK;

    while (token) {
        result = parse_option(token, opts);
        if (FAILED(result)) {
            if (RESULT_CODE(result) != E_UNKNOWN) {
                free(verbs_copy);
                return result;
            }
            LOG_WARNING("Unknown YAWL_VERBS token: %s", token);
            result = RESULT_OK;
        } else if (opts->version || opts->help) {
            LOG_DEBUG("Returning early, got %s token", opts->version ? "version" : "help");
            break;
        }
        token = strtok(NULL, ";");
    }

    free(verbs_copy);
    return RESULT_OK;
}

static const char *setup_prog_dir(void) {
    char *result = NULL;
    struct passwd *pw;

    const char *temp_dir = getenv("YAWL_INSTALL_DIR");
    if (temp_dir)
        result = expand_path(temp_dir);
    else if ((temp_dir = getenv("XDG_DATA_HOME")))
        join_paths(result, temp_dir, PROG_NAME);
    else if ((temp_dir = getenv("HOME")) || ((pw = getpwuid(getuid())) && (temp_dir = pw->pw_dir)))
        join_paths(result, temp_dir, ".local/share/" PROG_NAME);

    RESULT ensure_result = ensure_dir(result);
    if (FAILED(ensure_result)) {
        fprintf(stderr, "Error: Failed to create or access program directory: %s\n", result_to_string(ensure_result));
        free(result);
        result = NULL;
    }

    return result;
}

static const char *setup_config_dir(void) {
    char *result = NULL;
    join_paths(result, g_yawl_dir, CONFIG_DIR);

    RESULT ensure_result = ensure_dir(result);
    if (FAILED(ensure_result)) {
        fprintf(stderr, "Error: Failed to create or access config directory: %s\n", result_to_string(ensure_result));
        free(result);
        result = NULL;
    }

    return result;
}

static RESULT verify_runtime(const char *runtime_path) {
    char *versions_txt_path = NULL;
    char *pv_verify_path = NULL;
    RESULT result;

    /* First, a lightweight check for VERSIONS.txt (same as the SLR shell script) */
    join_paths(versions_txt_path, runtime_path, "VERSIONS.txt");
    RETURN_NULL_CHECK(versions_txt_path, "Failed to allocate memory for VERSIONS.txt path");

    if (access(versions_txt_path, F_OK) != 0) {
        LOG_ERROR("VERSIONS.txt not found. Runtime may be corrupt or incomplete.");
        free(versions_txt_path);
        return MAKE_RESULT(SEV_ERROR, CAT_RUNTIME, E_NOT_FOUND);
    }
    free(versions_txt_path);

    /* Check if pv-verify exists */
    join_paths(pv_verify_path, runtime_path, "pressure-vessel/bin/pv-verify");
    RETURN_NULL_CHECK(pv_verify_path, "Failed to allocate memory for pv-verify path");

    if (access(pv_verify_path, X_OK) != 0) {
        LOG_ERROR("pv-verify not found. Runtime may be corrupt or incomplete.");
        free(pv_verify_path);
        return MAKE_RESULT(SEV_ERROR, CAT_RUNTIME, E_NOT_FOUND);
    }

    char *cmd = NULL;
    append_sep(cmd, " ", pv_verify_path, "--quiet");
    RETURN_NULL_CHECK(cmd, "Failed to allocate memory for command string");

    char *old_cwd = getcwd(NULL, 0);
    if (!old_cwd) {
        result = MAKE_RESULT(SEV_ERROR, CAT_FILESYSTEM, E_OUT_OF_MEMORY);
        LOG_RESULT(LOG_ERROR, result, "Failed to get current working directory");
        free(pv_verify_path);
        free(cmd);
        return result;
    }

    if (chdir(runtime_path) != 0) {
        result = result_from_errno();
        LOG_RESULT(LOG_ERROR, result, "Failed to change to runtime directory");
        free(old_cwd);
        free(pv_verify_path);
        free(cmd);
        return result;
    }

    /* Run pv-verify */
    int cmd_ret = system(cmd);

    /* Restore directory */
    if (chdir(old_cwd) != 0) {
        LOG_WARNING("Failed to restore directory: %s", strerror(errno));
    }
    free(old_cwd);
    free(cmd);

    if (cmd_ret != 0) {
        LOG_ERROR("pv-verify reported verification errors (exit code %d).", WEXITSTATUS(cmd_ret));
        free(pv_verify_path);
        return MAKE_RESULT(SEV_ERROR, CAT_RUNTIME, E_INVALID_ARG);
    }

    char *entry_point = NULL;
    join_paths(entry_point, g_yawl_dir, RUNTIME_PREFIX RUNTIME_VERSION "/_v2-entry-point");
    RETURN_NULL_CHECK(entry_point, "Failed to allocate memory for entry point path");

    if (access(entry_point, X_OK) != 0) {
        LOG_ERROR("Runtime entry point not found: %s", entry_point);
        free(entry_point);
        return MAKE_RESULT(SEV_ERROR, CAT_RUNTIME, E_NOT_FOUND);
    }

    /* Check and fix AppArmor issues if needed */
    RESULT apparmor_result = handle_apparmor(entry_point);
    if (FAILED(apparmor_result)) {
        LOG_WARNING("AppArmor issues detected but couldn't be fully resolved.");
        LOG_WARNING("The program will continue, but may not work correctly.");
    }

    LOG_INFO("Runtime verification completed successfully.");
    free(pv_verify_path);
    free(entry_point);
    return RESULT_OK;
}

static RESULT verify_slr_hash(const char *archive_path, const char *hash_url) {
    char expected_hash[65] = {0};
    char actual_hash[65] = {0};
    RESULT result;

    result = get_online_slr_hash(RUNTIME_ARCHIVE_NAME, hash_url, expected_hash, sizeof(expected_hash));
    if (FAILED(result)) {
        LOG_WARNING("Unexpected error while trying to obtain the hash from the SHA256SUMS file.");
        LOG_WARNING("Attempting to proceed with unverified archive.");
        return RESULT_OK;
    }

    result = calculate_sha256(archive_path, actual_hash, sizeof(actual_hash));
    LOG_AND_RETURN_IF_FAILED(LOG_ERROR, result, "Could not calculate hash");

    if (strcmp(expected_hash, actual_hash) != 0) {
        LOG_WARNING("Archive hash mismatch.");
        return MAKE_RESULT(SEV_ERROR, CAT_RUNTIME, E_INVALID_ARG);
    }

    return RESULT_OK;
}

static RESULT setup_runtime(const struct options *opts) {
    /* Reinstall obviously implies verify */
    RESULT ret = RESULT_OK;
    int install = opts->reinstall, verify = (opts->verify || opts->reinstall);
    char *archive_path = NULL, *runtime_path = NULL;
    struct stat st;

    join_paths(archive_path, g_yawl_dir, RUNTIME_ARCHIVE_NAME);
    RETURN_NULL_CHECK(archive_path, "Failed to allocate memory for archive path");

    join_paths(runtime_path, g_yawl_dir, RUNTIME_PREFIX RUNTIME_VERSION);
    RETURN_NULL_CHECK(runtime_path, "Failed to allocate memory for runtime path");

    if (!(stat(runtime_path, &st) == 0 && S_ISDIR(st.st_mode))) {
        LOG_INFO("Installing runtime...");
        install = 1;
    } else if (install) {
        LOG_INFO("Reinstalling runtime...");
        RESULT remove_result = remove_dir(runtime_path);
        if (FAILED(remove_result))
            LOG_RESULT(LOG_WARNING, remove_result, "Failed to remove existing runtime directory");
        unlink(archive_path);
    } else if (verify) {
        LOG_INFO("Verifying existing runtime folder integrity...");
        ret = verify_runtime(runtime_path);
        if (FAILED(ret)) {
            RESULT remove_result = remove_dir(runtime_path);
            if (FAILED(remove_result))
                LOG_RESULT(LOG_WARNING, remove_result, "Failed to remove corrupt runtime directory");
            LOG_INFO("Reinstalling corrupt runtime folder...");
            install = 1;
        }
        /* else we'll skip reinstallation if verification succeeded. */
    }

    /* Needs to be reinstalled because of: option, failed verification, or fresh install */
    if (install) {
        int attempt = 0;
        RESULT success = MAKE_RESULT(SEV_ERROR, CAT_RUNTIME, E_UNKNOWN);

        do {
            if (SUCCEEDED(success))
                break;
            if (++attempt > 2) {
                LOG_ERROR("Runtime verification failed after retrying.");
                break;
            }
            if (attempt == 2) {
                LOG_WARNING("Previous attempt failed, trying one more time...");
                RESULT remove_result = remove_dir(runtime_path);
                if (FAILED(remove_result)) {
                    LOG_RESULT(LOG_WARNING, remove_result, "Failed to remove runtime directory");
                }
                unlink(archive_path);
            }

            int download = 0;
            if (!(stat(archive_path, &st) == 0 && S_ISREG(st.st_mode))) {
                LOG_INFO("Downloading Steam Runtime (%s)...", RUNTIME_VERSION);
                download = 1;
            } else {
                /* TODO: should factor this out to be used as a separate update check */
                LOG_INFO("Verifying existing runtime archive integrity...");
                if (FAILED(verify_slr_hash(archive_path, RUNTIME_BASE_URL "/SHA256SUMS"))) {
                    download = 1;
                    unlink(archive_path);
                    LOG_INFO("Re-downloading Steam Runtime (%s)...", RUNTIME_VERSION);
                }
            }

            if (download) {
                success = download_file(RUNTIME_BASE_URL "/" RUNTIME_ARCHIVE_NAME, archive_path);
                if (FAILED(success)) {
                    LOG_RESULT(LOG_ERROR, success, "Failed to download runtime");
                    continue;
                }
            }

            LOG_INFO("Extracting runtime...");
            success = extract_archive(archive_path, g_yawl_dir);
            if (FAILED(success)) {
                LOG_RESULT(LOG_ERROR, success, "Failed to extract runtime");
                continue;
            }

            LOG_INFO("Verifying runtime folder integrity...");
            success = verify_runtime(runtime_path);
            if (FAILED(success)) {
                LOG_RESULT(LOG_ERROR, success, "Runtime verification failed");
                continue;
            }
        } while (1);
        ret = success;
    }

    free(archive_path);
    free(runtime_path);

    return ret;
}

static char *get_top_libdir(const char *exec_path) {
    char *dirname = strdup(exec_path);
    if (!dirname)
        return NULL;

    char *last_slash = strrchr(dirname, '/');
    if (last_slash)
        *last_slash = '\0';

    last_slash = strrchr(dirname, '/');
    if (last_slash && strcmp(last_slash, "/bin") == 0) {
        *last_slash = '\0';
        return dirname;
    } else {
        free(dirname);
        return NULL;
    }
}

static char *build_library_paths(const char *exec_path) {
    char *top_libdir = get_top_libdir(exec_path);
    char *lib64_path = NULL, *lib32_path = NULL, *lib_path = NULL;
    char *result = NULL;

    const char *orig_path = getenv("LD_LIBRARY_PATH");
    if (orig_path)
        result = strdup(orig_path);

    if (top_libdir) {
        join_paths(lib64_path, top_libdir, "lib64");
        join_paths(lib32_path, top_libdir, "lib32");
        join_paths(lib_path, top_libdir, "lib");

        append_sep(result, ":", lib64_path, lib32_path, lib_path);

        free(lib64_path);
        free(lib32_path);
        free(lib_path);
        free(top_libdir);
    }
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

/* Config name to load, either from YAWL_VERBS="config=" or from argv[0] (e.g., "yawl-foo-bar" -> "foo-bar")
 * (allocates) */
static char *get_config_name(const char *argv0, const struct options *opts) {
    if (opts->config)
        return strdup(opts->config);

    char *base_name = get_base_name(argv0);
    if (!base_name)
        return NULL;

    char *dash = strchr(base_name, '-');
    if (!dash) {
        free(base_name);
        return NULL;
    }

    char *config_name = strdup(dash + 1);
    free(base_name);

    return config_name;
}

/* Create a configuration file with the current options */
static RESULT create_config_file(const char *config_name, const struct options *opts) {
    char *config_path = NULL;
    FILE *fp = NULL;
    RESULT result = RESULT_OK;

    /* Build the config file path */
    join_paths(config_path, g_config_dir, config_name);
    RETURN_NULL_CHECK(config_path, "Failed to allocate memory for config path");

    append_sep(config_path, "", CONFIG_EXTENSION);

    /* Open the config file */
    fp = fopen(config_path, "w");
    if (!fp) {
        result = result_from_errno();
        LOG_RESULT(LOG_ERROR, result, "Failed to create config file");
        free(config_path);
        return result;
    }

    /* Write the current configuration */
    /* TODO: maybe support adding PATHs and other env vars */
    if (opts->exec_path && strcmp(opts->exec_path, DEFAULT_EXEC_PATH) != 0)
        fprintf(fp, "exec=%s\n", opts->exec_path);

    fclose(fp);
    LOG_INFO("Created configuration file: %s", config_path);
    free(config_path);

    return result;
}

/* Create a symlink to the current binary with the suffix */
static RESULT create_symlink(const char *config_name) {
    char *exec_path = NULL;
    char *exec_dir = NULL;
    char *base_name = NULL;
    char *symlink_path = NULL;
    RESULT result = RESULT_OK;

    /* Get the full path to the current executable */
    exec_path = realpath("/proc/self/exe", NULL);
    if (!exec_path) {
        result = result_from_errno();
        LOG_RESULT(LOG_ERROR, result, "Failed to get executable path");
        return result;
    }

    /* Extract the base name and directory */
    base_name = get_base_name(exec_path);
    RETURN_NULL_CHECK(base_name, "Failed to extract base name from executable path");

    /* Create a copy of exec_path to extract the directory */
    exec_dir = strdup(exec_path);
    if (!exec_dir) {
        free(exec_path);
        free(base_name);
        return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_OUT_OF_MEMORY);
    }

    char *last_slash = strrchr(exec_dir, '/');
    if (last_slash)
        *last_slash = '\0';

    /* Build the symlink path */
    join_paths(symlink_path, exec_dir, base_name);
    RETURN_NULL_CHECK(symlink_path, "Failed to allocate memory for symlink base path");

    append_sep(symlink_path, "-", config_name);

    /* Create the symlink */
    if (access(symlink_path, F_OK) == 0) {
        LOG_WARNING("Symlink already exists: %s", symlink_path);
        unlink(symlink_path);
    }

    if (symlink(exec_path, symlink_path) != 0) {
        result = result_from_errno();
        LOG_RESULT(LOG_ERROR, result, "Failed to create symlink");
    } else {
        LOG_INFO("Created symlink: %s -> %s", symlink_path, exec_path);
    }

    free(exec_path);
    free(exec_dir);
    free(base_name);
    free(symlink_path);

    return result;
}

/* Create a wineserver wrapper configuration and symlink. Useful for winetricks, as it can find wineserver from
 * `${WINE}server`. */
static RESULT create_wineserver_wrapper(const char *config_name, const char *wineserver_path) {
    struct options server_opts;
    char *server_config_name = NULL;
    RESULT result = RESULT_OK;

    /* Initialize options for the wineserver */
    server_opts.verify = 0;
    server_opts.reinstall = 0;
    server_opts.help = 0;
    server_opts.exec_path = strdup(wineserver_path);
    RETURN_NULL_CHECK(server_opts.exec_path, "Failed to allocate memory for wineserver path");

    server_opts.make_wrapper = NULL;
    server_opts.config = NULL;
    server_opts.wineserver = NULL;

    /* Create the config name: append "server" to the base name */
    server_config_name = strdup(config_name);
    if (!server_config_name) {
        free(server_opts.exec_path);
        return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_OUT_OF_MEMORY);
    }
    append_sep(server_config_name, "", "server");

    /* Create the wineserver config file */
    result = create_config_file(server_config_name, &server_opts);
    LOG_AND_RETURN_IF_FAILED(LOG_ERROR, result, "Failed to create wineserver config file");

    /* Create the wineserver symlink */
    result = create_symlink(server_config_name);
    LOG_AND_RETURN_IF_FAILED(LOG_ERROR, result, "Failed to create wineserver symlink");

    LOG_INFO("Created wineserver wrapper: yawl-%s", server_config_name);

    free(server_opts.exec_path);
    free(server_config_name);
    return result;
}

/* Create a wrapper configuration and symlink */
static RESULT create_wrapper(const char *config_name, const struct options *opts) {
    RESULT result;

    result = create_config_file(config_name, opts);
    RETURN_IF_FAILED(result);

    result = create_symlink(config_name);
    RETURN_IF_FAILED(result);

    if (opts->wineserver) {
        result = create_wineserver_wrapper(config_name, opts->wineserver);
        if (FAILED(result))
            LOG_WARNING("Failed to create wineserver wrapper. Continuing with main wrapper only.");
    }

    return RESULT_OK;
}

/* Load a configuration from a file, overrides opts passed in from env var */
static RESULT load_config(const char *config_name, struct options *opts) {
    char *config_path = NULL;
    FILE *fp = NULL;
    char line[BUFFER_SIZE];
    RESULT result = RESULT_OK;

    /* First, try using the name directly as a path */
    if (access(config_name, F_OK) == 0) {
        config_path = strdup(config_name);
        RETURN_NULL_CHECK(config_path, "Failed to allocate memory for config path");
    } else {
        /* Build the config file path in the standard location */
        join_paths(config_path, g_config_dir, config_name);
        RETURN_NULL_CHECK(config_path, "Failed to allocate memory for config path");

        /* Add extension if not already present */
        if (!strstr(config_name, CONFIG_EXTENSION)) {
            append_sep(config_path, "", CONFIG_EXTENSION);
            RETURN_NULL_CHECK(config_path, "Failed to append extension to config path");
        }

        /* Check if the file exists */
        if (access(config_path, F_OK) != 0) {
            LOG_ERROR("Config file not found: %s", config_path);
            free(config_path);
            return MAKE_RESULT(SEV_ERROR, CAT_CONFIG, E_NOT_FOUND);
        }
    }

    /* Open the config file */
    fp = fopen(config_path, "r");
    if (!fp) {
        result = result_from_errno();
        LOG_RESULT(LOG_ERROR, result, "Failed to open config file");
        free(config_path);
        return result;
    }

    /* Read the configuration */
    while (fgets(line, sizeof(line), fp)) {
        /* Remove trailing newline */
        char *newline = strchr(line, '\n');
        if (newline)
            *newline = '\0';

        /* Skip empty lines */
        if (line[0] == '\0')
            continue;

        RESULT option_result = parse_option(line, opts);
        if (FAILED(option_result)) {
            if (RESULT_CODE(option_result) != E_UNKNOWN) {
                result = option_result;
                break;
            }
            LOG_WARNING("Unknown configuration option: %s", line);
        }
    }

    fclose(fp);
    LOG_DEBUG("Loaded configuration from: %s", config_path);
    free(config_path);

    return result;
}

/* Note that we don't care about freeing things from main() since that's handled
   either when execv() is called or when the process exits due to an error. */
int main(int argc, char *argv[]) {
    struct options opts;
    RESULT result;

    if (geteuid() == 0) {
        fprintf(stderr, "This program should not be run as root. Exiting.\n");
        return 1;
    }

    /* Setup global directories first */
    if (!(g_yawl_dir = setup_prog_dir())) {
        fprintf(stderr, "The program directory is unusable\n");
        return 1;
    }

    if (!(g_config_dir = setup_config_dir())) {
        fprintf(stderr, "The configuration directory is unusable\n");
        return 1;
    }

    result = log_init();
    if (FAILED(result))
        fprintf(stderr, "Warning: Failed to initialize logging to file: %s\n", result_to_string(result));

    LOG_DEBUG("yawl directories initialized - g_yawl_dir: %s, g_config_dir: %s", g_yawl_dir, g_config_dir);

    result = parse_env_options(&opts);
    LOG_AND_RETURN_IF_FAILED(LOG_ERROR, result, "Failed to parse options");

    if (opts.version) {
        printf(VERSION "\n");
        return 0;
    }

    if (opts.help) {
        print_usage();
        return 0;
    }

    /* Handle make_wrapper option */
    if (opts.make_wrapper) {
        result = create_wrapper(opts.make_wrapper, &opts);
        LOG_AND_RETURN_IF_FAILED(LOG_ERROR, result, "Failed to create wrapper configuration");

        /* Exit after creating the wrapper if no other arguments */
        if (argc <= 1) {
            LOG_INFO("Wrapper created successfully. Use %s-%s to run with this configuration.", get_base_name(argv[0]),
                     opts.make_wrapper);
            return 0;
        }
    }

    char *config_name = get_config_name(argv[0], &opts);

    if (config_name) {
        result = load_config(config_name, &opts);
        if (FAILED(result))
            LOG_WARNING("Failed to load configuration. Continuing with defaults.");
    }
    free(config_name);

    result = setup_runtime(&opts);
    LOG_AND_RETURN_IF_FAILED(LOG_ERROR, result, "Failed setting up the runtime");

    if (access(opts.exec_path, X_OK) != 0) {
        LOG_ERROR("Executable not found or not executable: %s", opts.exec_path);
        return 1;
    }

    char *entry_point = NULL;
    join_paths(entry_point, g_yawl_dir, RUNTIME_PREFIX RUNTIME_VERSION "/_v2-entry-point");
    if (access(entry_point, X_OK) != 0) {
        LOG_ERROR("Runtime entry point not found: %s", entry_point);
        return 1;
    }

    char **new_argv = calloc(argc + 4, sizeof(char *));
    RETURN_NULL_CHECK(new_argv, "Failed to allocate memory for command string");
    new_argv[0] = entry_point;
    new_argv[1] = "--verb=waitforexitandrun";
    new_argv[2] = "--";
    new_argv[3] = opts.exec_path;

    for (int i = 1; i < argc; i++) {
        new_argv[i + 3] = argv[i];
    }

    /* Set up library paths based on the executable path */
    char *lib_paths = build_library_paths(opts.exec_path);
    if (lib_paths) {
        setenv("LD_LIBRARY_PATH", lib_paths, 1);
        free(lib_paths);
    }

    char *mesa_paths = build_mesa_paths();
    if (mesa_paths) {
        setenv("LIBGL_DRIVERS_PATH", mesa_paths, 1);
        free(mesa_paths);
    }

    /* TODO: factor and allow setting paths from config */
    if (opts.exec_path && strcmp(opts.exec_path, DEFAULT_EXEC_PATH) != 0) {
        char *exec_dir = strdup(opts.exec_path);
        if (exec_dir) {
            char *last_slash = strrchr(exec_dir, '/');
            if (last_slash) {
                *last_slash = '\0';

                const char *orig_path = getenv("PATH");
                char *new_path = NULL;

                if (orig_path)
                    append_sep(new_path, ":", exec_dir, orig_path);
                else
                    new_path = strdup(exec_dir);

                if (new_path) {
                    setenv("PATH", new_path, 1);
                    free(new_path);
                }
            }
            free(exec_dir);
        }
    }

    if (prctl(PR_SET_CHILD_SUBREAPER, 1UL, 0UL, 0UL, 0UL) == -1)
        LOG_WARNING("Failed to set child subreaper status: %s", strerror(errno));

    log_cleanup();

    execv(entry_point, new_argv);
    perror("Failed to execute runtime"); /* Shouldn't reach here. */

    return 1;
}
