/*
 * Simple Steam Linux Runtime bootstrapper/launcher program
 *
 * Copyright (C) 2025 William Horvath
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * See the full license text in the repository LICENSE file.
 */

#include "config.h"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <getopt.h>
#include <sys/prctl.h>

#include "apparmor.hpp"
#include "log.hpp"
#include "macros.hpp"
#include "nsenter.hpp"
#include "result.hpp"
#include "update.hpp"
#include "util.hpp"
#include "yawlconfig.hpp"

#include "fmt/compile.h"
#include "fmt/core.h"
#include "fmt/printf.h"

using namespace fmt::literals;

#if YAWL_ARCH_AARCH64
#define RUNTIME_ARCHIVE_EXTRA_SUFFIX "-arm64"
#else
#define RUNTIME_ARCHIVE_EXTRA_SUFFIX
#endif

#define RUNTIME_PREFIX "SteamLinuxRuntime_"
#define RUNTIME_VERSION "sniper"
#define RUNTIME_NAME RUNTIME_PREFIX RUNTIME_VERSION RUNTIME_ARCHIVE_EXTRA_SUFFIX

#define RUNTIME_BASE_URL                                                                                               \
    "https://repo.steampowered.com/steamrt-images-" RUNTIME_VERSION "/snapshots/latest-container-runtime-public-beta"

#define DEFAULT_EXEC_PATH "/usr/bin/wine"
#define CONFIG_EXTENSION ".cfg"

static void __attribute__((__noreturn__)) print_usage() {
    fmt::print(R"_(Usage: {2} [args_for_executable...]
Environment variables:
  YAWL_VERBS       Semicolon-separated list of verbs to control {0} behavior:
                   - 'version'   Just print the version of {0} and exit
                   - 'verify'    Verify the runtime before running (default: only verify after install)
                                 Also can be used to check for runtime updates (will be a separate option in the future)
                   - 'reinstall' Force reinstallation of the runtime
                   - 'help'      Display this help and exit
                   - 'check'     Check for updates to {0} (without downloading/installing)
                   - 'update'    Check for, download, and install available updates
                   - 'exec=PATH' Set the executable to run in the container (default: {1})
                   - 'make_wrapper=NAME' Create a wrapper configuration and symlink
                   - 'config=NAME'       Use a specific configuration file
                   - 'wineserver=PATH'   Set the wineserver executable path when creating a wrapper
                   - 'proton=PATH':      Set the Proton script to run in the container (overrides 'exec=')
                   - 'proton_verb=NAME': Verb to use to run Proton (default: 'run')
                   - 'enter=PID'         Run an executable in the same container as PID

            Examples:
                YAWL_VERBS="make_wrapper=osu;exec=/opt/wine-osu/bin/wine;wineserver=/opt/wine-osu/bin/wineserver" {2}
                YAWL_VERBS="verify;reinstall" {2} winecfg
                YAWL_VERBS="exec=/opt/wine/bin/wine64" {2} winecfg
                YAWL_VERBS="make_wrapper=cool-wine;exec=/opt/wine/bin/wine64" {2}
                YAWL_VERBS="enter=$(pgrep game.exe)" {2} cheatengine.exe

  YAWL_INSTALL_DIR Override the default installation directory of $XDG_DATA_HOME/{0} or $HOME/.local/share/{0}
            Example:
                YAWL_INSTALL_DIR="$HOME/programs/winelauncher" YAWL_VERBS="reinstall" {2}

  YAWL_LOG_LEVEL   Control the verbosity of the logging output. Valid values are:
                   - 'none'     Turn off all logging
                   - 'error'    Show only critical errors that prevent proper operation
                   - 'warn'     Show warnings and errors (default)
                   - 'info'     Show normal operational information and all of the above
                   - 'debug'    Show detailed debugging information and all of the above

  YAWL_LOG_FILE    Specify a custom path for the log file. By default, logs are written to:
                   - Terminal output (only when running interactively)
                   - $YAWL_INSTALL_DIR/{0}.log
)_"_cf,
               PROG_NAME, DEFAULT_EXEC_PATH, program_invocation_short_name);
    exit(0);
}

struct options {
    const char *exec_path;    /* Path to the executable to run (default: /usr/bin/wine) */
    const char *make_wrapper; /* Name of the wrapper to create (nullptr = don't create) */
    const char *config;       /* Name of the config to use (nullptr = use argv[0] or default) */
    const char *wineserver;   /* Path to the wineserver binary (nullptr = don't create wineserver wrapper) */
    const char *proton;       /* Path to the proton script */
    const char *proton_verb;  /* Verb to use to run proton (default: run)*/
    unsigned long enterpid;   /* The pid of the namespace we want to run a command in */
    unsigned version : 1;     /* 1 = return a version string and exit */
    unsigned verify : 1;      /* 0 = no verification (default), 1 = verify */
    unsigned reinstall : 1;   /* 0 = don't reinstall unless needed, 1 = force reinstall */
    unsigned help : 1;        /* 0 = don't show help, 1 = show help and exit */
    unsigned check : 1;       /* 1 = check for updates */
    unsigned update : 1;      /* 1 = check for and apply updates */
};

/* Parse a single option string and update the options structure */
static RESULT parse_option(nonnull_charp option, struct options *opts) {
    if (!opts || !option[0])
        return RESULT_OK; /* Skip empty options, not an error */

    if (LCSTRING_EQUALS(option, "version")) {
        opts->version = 1;
    } else if (LCSTRING_EQUALS(option, "verify")) {
        opts->verify = 1;
    } else if (LCSTRING_EQUALS(option, "reinstall")) {
        opts->reinstall = 1;
    } else if (LCSTRING_EQUALS(option, "help")) {
        opts->help = 1;
    } else if (LCSTRING_EQUALS(option, "check")) {
        opts->check = 1;
    } else if (LCSTRING_EQUALS(option, "update")) {
        opts->update = 1;
    } else if (LCSTRING_PREFIX(option, "enter=")) {
        opts->enterpid = str2unum(STRING_AFTER_PREFIX(option, "enter="), 10);
    } else if (LCSTRING_PREFIX(option, "exec=")) {
        opts->exec_path = expand_path(STRING_AFTER_PREFIX(option, "exec="));
        if (!opts->exec_path)
            opts->exec_path = strdup(DEFAULT_EXEC_PATH);
    } else if (LCSTRING_PREFIX(option, "make_wrapper=")) {
        opts->make_wrapper = strdup(STRING_AFTER_PREFIX(option, "make_wrapper="));
    } else if (LCSTRING_PREFIX(option, "config=")) {
        opts->config = strdup(STRING_AFTER_PREFIX(option, "config="));
    } else if (LCSTRING_PREFIX(option, "wineserver=")) {
        opts->wineserver = expand_path(STRING_AFTER_PREFIX(option, "wineserver="));
    } else if (LCSTRING_PREFIX(option, "proton=")) {
        opts->proton = expand_path(STRING_AFTER_PREFIX(option, "proton="));
    } else if (LCSTRING_PREFIX(option, "proton_verb=")) {
        opts->proton_verb = expand_path(STRING_AFTER_PREFIX(option, "proton_verb="));
    } else {
        return MAKE_RESULT(SEV_WARNING, CAT_CONFIG, E_UNKNOWN); /* Unknown option */
    }

    /* proton= takes precedence over exec= */
    if (opts->proton && opts->exec_path && !STRING_EQUALS(opts->exec_path, DEFAULT_EXEC_PATH)) {
        LOG_INFO("Ignoring exec, using proton instead.");
    }

    return RESULT_OK;
}

static RESULT parse_env_options(struct options *opts) {
    const char *verbs = getenv("YAWL_VERBS");
    if (!verbs)
        return RESULT_OK;

    autofree char *verbs_copy = strdup(verbs);
    char *token, *saveptr;
    token = strtok_r(verbs_copy, ";", &saveptr);
    RESULT result = RESULT_OK;

    while (token) {
        result = parse_option(token, opts);
        if (FAILED(result)) {
            if (RESULT_SEVERITY(result) > SEV_WARNING) {
                return result;
            }
            LOG_INFO("Unknown YAWL_VERBS token: %s", token);
            result = RESULT_OK;
        } else if (opts->help) {
            LOG_DEBUG("Returning early, got help token");
            break;
        }
        token = strtok_r(nullptr, ";", &saveptr);
    }

    return RESULT_OK;
}

static RESULT verify_runtime(nonnull_charp runtime_path) {
    autofree char *versions_txt_path = nullptr;
    autofree char *pv_verify_path = nullptr;

    /* First, a lightweight check for VERSIONS.txt (same as the SLR shell script) */
    join_paths(versions_txt_path, runtime_path, "VERSIONS.txt");

    if (access(versions_txt_path, F_OK) != 0) {
        LOG_ERROR("VERSIONS.txt not found. Runtime may be corrupt or incomplete.");
        return MAKE_RESULT(SEV_ERROR, CAT_RUNTIME, E_NOT_FOUND);
    }

    /* Check if pv-verify exists */
    join_paths(pv_verify_path, runtime_path, "pressure-vessel/bin/pv-verify");

    if (!is_exec_file(pv_verify_path)) {
        LOG_ERROR("pv-verify not found. Runtime may be corrupt or incomplete.");
        return MAKE_RESULT(SEV_ERROR, CAT_RUNTIME, E_NOT_FOUND);
    }

    const char *argv[] = {pv_verify_path, "--quiet", nullptr};
    int cmd_ret = execute_program(argv, runtime_path, nullptr, nullptr);

    if (cmd_ret != 0) {
        LOG_ERROR("pv-verify reported verification errors (exit code %d).", cmd_ret);
        /* checking verify.c, it can only return EX_USAGE (64), 1 (failure), or 0 (success)
         * so treat any other error as E_UNKNOWN */
        return MAKE_RESULT(SEV_ERROR, CAT_RUNTIME, cmd_ret == 64 ? E_INVALID_ARG : cmd_ret == 1 ? E_ACCESS_DENIED : E_UNKNOWN);
    }

    autofree char *entry_point = nullptr;
    join_paths(entry_point, config::yawl_dir, RUNTIME_NAME "/_v2-entry-point");

    if (!is_exec_file(entry_point)) {
        LOG_ERROR("Runtime entry point not found: %s", entry_point);
        return MAKE_RESULT(SEV_ERROR, CAT_RUNTIME, E_NOT_FOUND);
    }

    /* Check and fix AppArmor issues if needed */
    RESULT apparmor_result = handle_apparmor(entry_point);
    if (FAILED(apparmor_result)) {
        LOG_WARNING("AppArmor issues detected but couldn't be fully resolved.");
        LOG_WARNING("The program will continue, but may not work correctly.");
    }

    LOG_INFO("Runtime verification completed successfully.");
    return RESULT_OK;
}

static RESULT verify_slr_hash(nonnull_charp archive_path, nonnull_charp hash_url) {
    char expected_hash[65] = {};
    char actual_hash[65] = {};
    RESULT result;

    result = get_online_slr_sha256sum(RUNTIME_NAME ".tar.xz", hash_url, expected_hash);
    if (FAILED(result)) {
        LOG_WARNING("Unexpected error while trying to obtain the hash from the SHA256SUMS file.");
        LOG_WARNING("Attempting to proceed with unverified archive.");
        return RESULT_OK;
    }

    result = calculate_sha256(archive_path, actual_hash);
    LOG_AND_RETURN_IF_FAILED(Level::Error, result, "Could not calculate hash");

    if (!STRING_EQUALS(expected_hash, actual_hash)) {
        LOG_WARNING("Archive hash mismatch, expected: %s got: %s", expected_hash, actual_hash);
        return MAKE_RESULT(SEV_ERROR, CAT_RUNTIME, E_INVALID_ARG);
    }

    return RESULT_OK;
}

static RESULT setup_runtime(const struct options *opts) {
    /* Reinstall obviously implies verify */
    RESULT ret = RESULT_OK;
    int install = opts->reinstall, verify = (opts->verify || opts->reinstall);
    autofree char *archive_path = nullptr;
    autofree char *runtime_path = nullptr;
    struct stat st;

    join_paths(archive_path, config::yawl_dir, RUNTIME_NAME ".tar.xz");
    join_paths(runtime_path, config::yawl_dir, RUNTIME_NAME);

    if (!(stat(runtime_path, &st) == 0 && S_ISDIR(st.st_mode))) {
        LOG_INFO("Installing runtime...");
        install = 1;
    } else if (install) {
        LOG_INFO("Reinstalling runtime...");
        RESULT remove_result = remove_dir(runtime_path);
        if (FAILED(remove_result))
            LOG_RESULT(Level::Warning, remove_result, "Failed to remove existing runtime directory");
        unlink(archive_path);
    } else if (verify) {
        LOG_INFO("Verifying existing runtime folder integrity...");
        ret = verify_runtime(runtime_path);
        if (FAILED(ret)) {
            int code = RESULT_CODE(ret);
            if (code == E_UNKNOWN || code == E_INVALID_ARG) {
                /* don't remove/delete anything */
                LOG_DEBUG("Got an unknown error (%d) while verifying runtime! Stopping.", code);
                ret = RESULT_FAIL;
                return ret;
            }
            RESULT remove_result = remove_dir(runtime_path);
            if (FAILED(remove_result))
                LOG_RESULT(Level::Warning, remove_result, "Failed to remove corrupt runtime directory");
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
                    LOG_RESULT(Level::Warning, remove_result, "Failed to remove runtime directory");
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
                success = download_file(RUNTIME_BASE_URL "/" RUNTIME_NAME ".tar.xz", archive_path, nullptr);
                if (FAILED(success)) {
                    LOG_RESULT(Level::Error, success, "Failed to download runtime");
                    unlink(archive_path);
                    continue;
                }
            }

            LOG_INFO("Extracting runtime...");
            success = extract_archive(archive_path, config::yawl_dir);
            if (FAILED(success)) {
                LOG_RESULT(Level::Error, success, "Failed to extract runtime");
                unlink(archive_path);
                continue;
            }

            LOG_INFO("Verifying runtime folder integrity...");
            success = verify_runtime(runtime_path);
            if (FAILED(success)) {
                int code = RESULT_CODE(success);
                if (code == E_UNKNOWN || code == E_INVALID_ARG) {
                    /* don't remove/delete anything */
                    LOG_DEBUG("Got an unknown error (%d) while verifying runtime! Stopping.", code);
                    ret = RESULT_FAIL;
                    return ret;
                }
                LOG_RESULT(Level::Error, success, "Runtime verification failed");
                continue;
            }
        } while (1);
        ret = success;
    }

    return ret;
}

static char *get_top_libdir(nonnull_charp exec_path) {
    char *dirname = strdup(exec_path);

    char *last_slash = strrchr(dirname, '/');
    if (last_slash)
        *last_slash = '\0';

    last_slash = strrchr(dirname, '/');
    if (last_slash && STRING_EQUALS(last_slash, "/bin")) {
        *last_slash = '\0';
    } else {
        free(dirname);
        return nullptr;
    }
    return dirname;
}

static char *build_library_paths(nonnull_charp exec_path) {
    autofree char *top_libdir = nullptr;
    char *result = nullptr;
    struct stat st;

    const char *orig_path = getenv("LD_LIBRARY_PATH");
    if (orig_path)
        result = strdup(orig_path);

    top_libdir = get_top_libdir(exec_path);

    /* append_sep with "" as separator just acts like concatenation */
    if (top_libdir && stat(top_libdir, &st) == 0 && S_ISDIR(st.st_mode))
        append_sep(result, "", orig_path ? ":" : "", top_libdir, "/lib64:", top_libdir, "/lib32:", top_libdir, "/lib");

#ifdef YAWL_ARCH_AARCH64
    if (stat("/usr/aarch64-linux-gnu/lib", &st) == 0 && S_ISDIR(st.st_mode))
        append_sep(result, "", result ? ":" : "", "/usr/aarch64-linux-gnu/lib");
#endif

    return result;
}

/* required for ancient Debian/Ubuntu */
static char *build_mesa_paths(void) {
    const char *mesa_paths[] = {"/usr/lib/i386-linux-gnu/dri",
                                "/usr/i386-linux-gnu/lib/dri",
                                "/usr/i386-linux-gnu/lib32/dri",
                                "/usr/lib/x86_64-linux-gnu/dri",
                                "/usr/x86_64-linux-gnu/lib/dri",
                                "/usr/x86_64-linux-gnu/lib64/dri",
                                "/usr/lib/aarch64-linux-gnu/dri",
                                "/usr/aarch64-linux-gnu/lib/dri",
                                "/usr/lib/dri",
                                "/usr/lib32/dri",
                                "/usr/lib64/dri",
                                nullptr};
    char *result = nullptr;

    const char *orig_path = getenv("LIBGL_DRIVERS_PATH");
    if (orig_path)
        result = strdup(orig_path);

    for (const char **path = mesa_paths; *path; path++) {
        if (access(*path, F_OK) == 0)
            append_sep(result, ":", *path);
    }

    return result;
}

/* Create a configuration file with the current options */
static RESULT create_config_file(nonnull_charp config_name, const struct options *opts) {
    autofree char *config_path = nullptr;
    autoclose FILE *fp = nullptr;
    RESULT result = RESULT_OK;

    /* Build the config file path */
    join_paths(config_path, config::config_dir, config_name);
    append_sep(config_path, "", CONFIG_EXTENSION);

    /* Open the config file */
    fp = fopen(config_path, "w");
    if (!fp) {
        result = result_from_errno();
        LOG_RESULT(Level::Error, result, "Failed to create config file");
        return result;
    }

    /* Write the current configuration */
    /* TODO: maybe support adding PATHs and other env vars */
    if (opts->proton)
        fmt::fprintf(fp, "proton=%s\n", opts->proton);
    else if (opts->exec_path && !STRING_EQUALS(opts->exec_path, DEFAULT_EXEC_PATH))
        fmt::fprintf(fp, "exec=%s\n", opts->exec_path);

    LOG_INFO("Created configuration file: %s", config_path);

    return result;
}

/* Create a symlink to the current binary with the suffix */
static RESULT create_symlink(nonnull_charp config_name) {
    autofree char *exec_path = nullptr;
    autofree char *exec_dir = nullptr;
    autofree char *symlink_path = nullptr;
    RESULT result = RESULT_OK;

    /* Get the full path to the current executable */
    exec_path = realpath("/proc/self/exe", nullptr);
    if (!exec_path) {
        result = result_from_errno();
        LOG_RESULT(Level::Error, result, "Failed to get executable path");
        return result;
    }

    /* Create a copy of exec_path to extract the directory */
    exec_dir = strdup(exec_path);

    char *last_slash = strrchr(exec_dir, '/');
    if (last_slash)
        *last_slash = '\0';

    /* Build the symlink path */
    join_paths(symlink_path, exec_dir, program_invocation_short_name);
    append_sep(symlink_path, "-", config_name);

    if (access(symlink_path, F_OK) == 0) {
        LOG_DEBUG("Symlink already exists: %s", symlink_path);
        unlink(symlink_path);
    }

    /* Create the symlink */
    if (symlink(exec_path, symlink_path) != 0) {
        result = result_from_errno();
        LOG_RESULT(Level::Error, result, "Failed to create symlink");
    } else {
        LOG_INFO("Created symlink: %s -> %s", symlink_path, exec_path);
    }

    return result;
}

/* Create a wineserver wrapper configuration and symlink. Useful for winetricks, as it can find wineserver from
 * `${WINE}server`. */
static RESULT create_wineserver_wrapper(nonnull_charp base_name, nonnull_charp wineserver_path) {
    autofree char *server_config_name = nullptr;
    struct options wineserver_opts = {};
    wineserver_opts.exec_path = wineserver_path;
    RESULT result = RESULT_OK;

    /* Create the config name: append "server" to the base name */
    server_config_name = strdup(base_name);
    append_sep(server_config_name, "", "server");

    result = create_config_file(server_config_name, &wineserver_opts);
    RETURN_IF_FAILED(result);

    result = create_symlink(server_config_name);
    RETURN_IF_FAILED(result);

    LOG_INFO("Created wineserver wrapper: %s-%s", program_invocation_short_name, server_config_name);

    return result;
}

/* Create a wrapper configuration and symlink */
static RESULT create_wrapper(nonnull_charp wrapper_name, const struct options *opts) {
    RESULT result;

    result = create_config_file(wrapper_name, opts);
    RETURN_IF_FAILED(result);

    result = create_symlink(wrapper_name);
    RETURN_IF_FAILED(result);

    if (opts->wineserver) {
        result = create_wineserver_wrapper(wrapper_name, opts->wineserver);
        if (FAILED(result))
            LOG_WARNING("Failed to create wineserver wrapper. Continuing with main wrapper only.");
    }

    return RESULT_OK;
}

/* Find the config to load, prioritizing YAWL_VERBS 'config=' over symlink name
 * Does not allocate, returns a pointer to the g_argv after '-' or the opts.config verb */
static inline const char *get_config_name(const struct options *opts) {
    static const char *wrapper_name = nullptr;
    if (opts->config) {
        wrapper_name = opts->config;
        return wrapper_name;
    }

    const char *temp = strchr(program_invocation_short_name, '-');
    if (temp)
        wrapper_name = temp + 1;

    return wrapper_name;
}

/* Load a configuration from a file, overrides opts passed in from env var */
static RESULT load_config(nonnull_charp config_name, struct options *opts) {
    autofree char *config_path = nullptr;
    autoclose FILE *fp = nullptr;
    char line[BUFFER_SIZE];
    RESULT result = RESULT_OK;

    /* First, try using the name directly as a path */
    if (access(config_name, F_OK) == 0) {
        config_path = strdup(config_name);
    } else {
        /* Build the config file path in the standard location */
        join_paths(config_path, config::config_dir, config_name);

        /* Add extension if not already present */
        if (!strstr(config_name, CONFIG_EXTENSION))
            append_sep(config_path, "", CONFIG_EXTENSION);

        /* Check if the file exists */
        if (access(config_path, F_OK) != 0) {
            LOG_ERROR("Config file not found: %s", config_path);
            return MAKE_RESULT(SEV_ERROR, CAT_CONFIG, E_NOT_FOUND);
        }
    }

    /* Open the config file */
    fp = fopen(config_path, "r");
    if (!fp) {
        result = result_from_errno();
        LOG_RESULT(Level::Error, result, "Failed to open config file");
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
            if (RESULT_SEVERITY(option_result) > SEV_WARNING) {
                result = option_result;
                break;
            }
            LOG_INFO("Unknown configuration option: %s", line);
        }
    }

    LOG_DEBUG("Loaded configuration from: %s", config_path);
    return result;
}

/* Note that we don't *really* care about freeing things from main(), since that's handled
   either when execv() is called or when the process exits. */
int main(int argc, char *argv[]) {
    if (geteuid() == 0) {
        fmt::fprintf(stderr, "This program should not be run as root. Exiting.\n");
        return 1;
    }

    /* Setup global directories first */
    if (FAILED(config::setup_prog_dir())) {
        fmt::fprintf(stderr, "The program directory is unusable\n");
        return 1;
    }

    if (FAILED(config::setup_config_dir())) {
        fmt::fprintf(stderr, "The configuration directory is unusable\n");
        return 1;
    }

    RESULT result;

    result = log_init();
    if (FAILED(result) && (RESULT_CODE(result) != E_CANCELED))
        fmt::fprintf(stderr, "Warning: Failed to initialize logging to file: %s\n", result_to_string(result));

    LOG_DEBUG(PROG_NAME " directories initialized - yawl_dir: %s, config_dir: %s", config::yawl_dir,
              config::config_dir);

    struct options opts = {};
    opts.exec_path = DEFAULT_EXEC_PATH;

    result = parse_env_options(&opts);
    LOG_AND_RETURN_IF_FAILED(Level::Error, result, "Failed to parse options");

    if (opts.help) {
        print_usage();
        return 0;
    }

    if (opts.check || opts.update) {
        /* Remove update verbs from env */
        const char *verbs_to_remove[] = {"update", "check"};
        RESULT remove_result = remove_verbs_from_env(verbs_to_remove, 2);

        RESULT update_result = handle_updates(opts.check, opts.update);
        if (FAILED(update_result)) {
            LOG_RESULT(Level::Warning, update_result, "Update unsuccessful");
            LOG_DEBUG_RESULT(update_result, "May have hit rate limit");
        } else if (RESULT_CODE(update_result) == E_UPDATE_PERFORMED) {
            LOG_INFO("Update installed.");
            /* Restart if there are verbs remaining to be processed. */
            if (RESULT_CODE(remove_result) != E_NOT_FOUND) {
                LOG_INFO("Additional verbs supplied, restarting...");
                execv(argv[0], argv);
                LOG_ERROR("Failed to restart: %s", strerror(errno));
            }
        }
        if (RESULT_CODE(remove_result) == E_NOT_FOUND) {
            LOG_DEBUG("Exiting now, no more verbs to process.");
            return RESULT_CODE(update_result);
        }
    }

    if (opts.version) {
        fmt::printf(VERSION "\n");
        return 0;
    }

    /* Handle make_wrapper option */
    if (opts.make_wrapper) {
        LOG_DEBUG("Making wrapper %s", opts.make_wrapper);
        if ((!opts.exec_path || STRING_EQUALS(opts.exec_path, DEFAULT_EXEC_PATH)) && !opts.proton) {
            LOG_WARNING(
                "You need to pass an exec= or proton= verb to create a wrapper. Use YAWL_VERBS=\"help\" for examples.");
            return 0;
        }
        result = create_wrapper(opts.make_wrapper, &opts);
        LOG_AND_RETURN_IF_FAILED(Level::Error, result, "Failed to create wrapper configuration");

        /* Exit after creating the wrapper if no other arguments */
        if (argc <= 1) {
            LOG_INFO("Wrapper created successfully. Use %s-%s to run with this configuration.",
                     program_invocation_short_name, opts.make_wrapper);
            return 0;
        }
    }

    const char *config_name = get_config_name(&opts);
    if (config_name) {
        result = load_config(config_name, &opts);
        if (FAILED(result))
            LOG_WARNING("Failed to load configuration. Continuing with defaults.");
    }

    if (opts.proton) {
        opts.exec_path = opts.proton;

        /* Set Steam compatibility variables (allow existing env to override) */
        setenv("STEAM_COMPAT_CLIENT_INSTALL_PATH", opts.exec_path, 0);
        setenv("STEAM_COMPAT_SESSION_ID", PROG_NAME "-default", 0);
        setenv("STEAM_COMPAT_APP_ID", PROG_NAME "-default", 0);
        setenv("UMU_ID", PROG_NAME "-default", 0); /* For compatibility with Proton */

        /* Create default compat data path if none specified */
        const char *wineprefix = getenv("WINEPREFIX");
        if (wineprefix) {
            /* Make sure Wineprefix exists beforehand */
            ensure_dir(wineprefix);
            setenv("STEAM_COMPAT_DATA_PATH", wineprefix, 1);
        } else {
            /* Look for appid and if not default, create the corresponding prefix for it */
            char *appid = getenv("STEAM_COMPAT_APP_ID");
            char *prefix_path = nullptr;

            if (!STRING_EQUALS(appid, PROG_NAME "-default"))
                join_paths(prefix_path, config::yawl_dir, "prefixes", appid);
            else
                join_paths(prefix_path, config::yawl_dir, "prefixes", PROG_NAME "-default");

            ensure_dir(prefix_path);
            setenv("STEAM_COMPAT_DATA_PATH", prefix_path, 1);
        }
    }

    if (opts.enterpid) {
        do_nsenter(argc, argv, opts.enterpid);
        /* Should not reach here if do_nsenter succeeded */
        return 1;
    }

    /* Set up library paths based on the executable path */
    char *lib_paths = build_library_paths(opts.exec_path);
    if (lib_paths) {
        setenv("LD_LIBRARY_PATH", lib_paths, 1);
        free(lib_paths);
    }

    result = setup_runtime(&opts);
    LOG_AND_RETURN_IF_FAILED(Level::Error, result, "Failed setting up the runtime");

    if (!is_exec_file(opts.exec_path)) {
        LOG_ERROR("Executable not found or not executable: %s", opts.exec_path);
        return 1;
    }

    char *entry_point = nullptr;
    join_paths(entry_point, config::yawl_dir, RUNTIME_NAME "/_v2-entry-point");
    if (!is_exec_file(entry_point)) {
        LOG_ERROR("Runtime entry point not found: %s", entry_point);
        return 1;
    }

    int extra_args = opts.proton ? 5 : 4;
    char **new_argv = (char **)calloc(argc + extra_args, sizeof(char *));
    new_argv[0] = entry_point;
    new_argv[1] = (char *)"--verb=waitforexitandrun";
    new_argv[2] = (char *)"--";
    new_argv[3] = (char *)opts.exec_path;

    int args_sum = 3;
    if (opts.proton) {
        if (opts.proton_verb)
            new_argv[4] = (char *)opts.proton_verb;
        else
            new_argv[4] = (char *)"run";
        args_sum++;
    }

    for (int i = 1; i < argc; i++) {
        new_argv[i + args_sum] = argv[i];
    }

    char *mesa_paths = build_mesa_paths();
    if (mesa_paths) {
        setenv("LIBGL_DRIVERS_PATH", mesa_paths, 1);
        free(mesa_paths);
    }

    /* TODO: factor and allow setting paths from config */
    if (opts.exec_path && !STRING_EQUALS(opts.exec_path, DEFAULT_EXEC_PATH)) {
        char *exec_dir = strdup(opts.exec_path);
        if (exec_dir) {
            char *last_slash = strrchr(exec_dir, '/');
            if (last_slash) {
                *last_slash = '\0';

                const char *orig_path = getenv("PATH");
                char *new_path = nullptr;

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

    /* TODO: this doesn't do anything, because we execv immediately after
     * need to implement "daemon mode" for this to be effective */
    if (prctl(PR_SET_CHILD_SUBREAPER, 1UL) == -1)
        LOG_WARNING("Failed to set child subreaper status: %s", strerror(errno));

    log_cleanup();

    execv(entry_point, new_argv);
    perror("Failed to execute runtime"); /* Shouldn't reach here. */

    return 1;
}
