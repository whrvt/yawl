/*
 * Logging subsystem implementation
 *
 * Copyright (C) 2025 William Horvath
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * See the full license text in the repository LICENSE file.
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define G_LOG_DOMAIN "libnotify"
#include "libnotify/notify.h"

#include "log.hpp"
#include "util.hpp"
#include "yawlconfig.hpp"

static FILE *log_file = nullptr;
static log_level_t current_log_level = LOG_INFO;
static bool terminal_output = false;
static gboolean notify_initialized = FALSE;

/* Color codes for terminal output */
#define COLOR_RESET "\033[0m"
#define COLOR_SYSTEM "\033[36m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_GREEN "\033[32m"
#define COLOR_BLUE "\033[34m"
#define COLOR_CYAN "\033[36m"

static constexpr const char *const level_strings[] = {"\0", "SYSTEM", "ERROR", "WARN", "INFO", "DEBUG", "DOWN"};
static constexpr const char *const level_colors[] = {"\0",        COLOR_SYSTEM, COLOR_RED, COLOR_YELLOW,
                                                     COLOR_GREEN, COLOR_BLUE,   COLOR_CYAN};

static_assert(sizeof(level_strings) == sizeof(level_colors), "each log level string should have a corresponding color");

/* Parse log level from string */
static log_level_t parse_log_level(const char *level_str) {
    if (!level_str ||
        (strlen(level_str) > (sizeof("error") - 1UL))) /* longest error level string (not including "SYSTEM" since
                                                          that's reserved for always-shown notifications) */
        return LOG_INFO;

    log_level_t level = LOG_INFO;
    if (LCSTRING_EQUALS(level_str, "none"))
        level = LOG_NONE;
    else if (LCSTRING_EQUALS(level_str, "error"))
        level = LOG_ERROR;
    else if (LCSTRING_EQUALS(level_str, "warn"))
        level = LOG_WARNING;
    else if (LCSTRING_EQUALS(level_str, "info"))
        level = LOG_INFO;
    else if (LCSTRING_EQUALS(level_str, "debug"))
        level = LOG_DEBUG;

    return level;
}

RESULT log_init(void) {
    char *log_file_path = nullptr;
    terminal_output = !!isatty(STDOUT_FILENO);

    /* From the ctime(3) docs:
     * According to POSIX.1, localtime() is required to behave as though tzset(3)
     * was called, while localtime_r() does not have this requirement.
     * For portable code, tzset(3) should be called before localtime_r(). */
    tzset();

    const char *log_level_env = getenv("YAWL_LOG_LEVEL");
    if (log_level_env)
        log_set_level(parse_log_level(log_level_env));

    notify_initialized = notify_init(PROG_NAME);

    if (current_log_level == LOG_NONE)
        return MAKE_RESULT(SEV_SUCCESS, CAT_CONFIG, E_CANCELED);

    const char *log_file_env = getenv("YAWL_LOG_FILE");
    if (log_file_env)
        log_file_path = strdup(log_file_env);
    else
        join_paths(log_file_path, config::yawl_dir, PROG_NAME ".log");

    if (log_file_path || !terminal_output) {
        log_file = fopen(log_file_path, "a");
        if (!log_file) {
            /* Fall back to stderr if file can't be opened */
            fprintf(stderr, "Failed to open log file: %s\n", strerror(errno));
            free(log_file_path);

            RESULT result = result_from_errno();
            LOG_RESULT(LOG_ERROR, result, "Failed to open log file");
            return result;
        }

        time_t now = time(nullptr);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_info);

        fprintf(log_file, "=== Log session started at %s ===\n", time_str);
        fflush(log_file);
    }

    free(log_file_path);
    return RESULT_OK;
}

void log_cleanup(void) {
    if (log_file) {
        /* Write session end marker */
        time_t now = time(nullptr);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_info);

        fprintf(log_file, "=== Log session ended at %s ===\n\n", time_str);
        fflush(log_file);

        fclose(log_file);
        log_file = nullptr;
    }
}

void log_set_level(log_level_t level) {
    if (level >= LOG_NONE && level <= LOG_DEBUG)
        current_log_level = level;
}

log_level_t log_get_level(void) { return current_log_level; }

bool log_get_terminal_output(void) { return terminal_output; }

void _log_message(log_level_t level, const char *file, int line, const char *format, ...) {
    if (level > current_log_level && level != LOG_SYSTEM)
        return;

    va_list args;

    if (level == LOG_SYSTEM && notify_initialized) {
        NotifyNotification *notif;
        char *message = nullptr;

        va_start(args, format);
        assert(!vasprintf(&message, format, args)); // glibc compatibility
        va_end(args);

        notif = notify_notification_new(PROG_NAME, message, "dialog-information");

        notify_notification_set_urgency(notif, NOTIFY_URGENCY_CRITICAL);
        notify_notification_set_timeout(notif, 30000); /* 30 seconds */

        GError *error = nullptr;
        if (!notify_notification_show(notif, &error)) {
            /* failed, whatever? */
        }

        g_object_unref(G_OBJECT(notif));
        free(message);
    }

    /* Output to terminal if appropriate */
    if (terminal_output) {
        FILE *output = (level <= LOG_WARNING) ? stderr : stdout;

        fprintf(output, "%s[%s]%s ", level_colors[level], level_strings[level], COLOR_RESET);

        va_start(args, format);
        vfprintf(output, format, args);
        va_end(args);

        fprintf(output, "\n");
    }

    if (log_file && level != LOG_SYSTEM && level != LOG_PROGRESS) {
        char timestamp[32];
        time_t now = time(nullptr);

        /* Create timestamp */
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_info);

        /* Get just the filename without the path */
        const char *filename = strrchr(file, '/');
        if (filename)
            filename++; /* Skip the slash */
        else
            filename = file;

        fprintf(log_file, "[%s] %s %s:%d: ", level_strings[level], timestamp, filename, line);

        va_start(args, format);
        vfprintf(log_file, format, args);
        va_end(args);

        fprintf(log_file, "\n");
        fflush(log_file);
    }
}

void _log_result(log_level_t level, const char *file, int line, RESULT result, const char *context) {
    if (SUCCEEDED(result) && level < LOG_DEBUG)
        return;
    if (level > current_log_level)
        return;

    const char *result_str = result_to_string(result);

    /* Provide context */
    if (context && context[0] != '\0')
        _log_message(level, file, line, "%s: %s (0x%08X)", context, result_str, (unsigned)result);
    else
        _log_message(level, file, line, "Result: %s (0x%08X)", result_str, (unsigned)result);

    if (current_log_level == LOG_DEBUG) {
        const int severity = RESULT_SEVERITY(result);
        const int category = RESULT_CATEGORY(result);
        const int code = RESULT_CODE(result);

        _log_message(LOG_DEBUG, file, line, "  Details: Severity=%d, Category=%d, Code=0x%04X", severity, category,
                     code);
    }
}

static time_t last_progress_update = 0;

/* Progress printer for curl downloads */
void log_progress(const char *operation, double percentage, int bytes_done, int bytes_total) {
    if (!terminal_output)
        return;

    /* Limit update frequency to 1hz */
    time_t now = time(nullptr);
    if (now - last_progress_update < 1 && bytes_done < bytes_total && bytes_done > 0)
        return;

    last_progress_update = now;

    /* Determine bar width (assuming 80 chars terminal width, leaving space for other info) */
    int bar_width = 30;
    int filled_width = (int)((percentage / 100.0) * bar_width);

    fprintf(stdout, "\r%s[%s]%s ", level_colors[LOG_PROGRESS], level_strings[LOG_PROGRESS], COLOR_RESET);
    fprintf(stdout, "%-20.20s [", operation);

    for (int i = 0; i < bar_width; i++) {
        if (i < filled_width)
            fprintf(stdout, "=");
        else if (i == filled_width)
            fprintf(stdout, ">");
        else
            fprintf(stdout, " ");
    }

    if (bytes_total > 0) {
        const char *units[] = {"B", "KB", "MB", "GB"};
        int unit_idx = 0;
        double size_now = bytes_done;
        double size_total = bytes_total;

        while (size_total >= 1024 && unit_idx < 3) {
            size_now /= 1024;
            size_total /= 1024;
            unit_idx++;
        }
        fprintf(stdout, "] %3d%% (%.1f/%.1f %s)", (int)percentage, size_now, size_total, units[unit_idx]);
    } else {
        fprintf(stdout, "] %3d%%", (int)percentage);
    }

    fflush(stdout);
}

void log_progress_end(void) {
    if (terminal_output && last_progress_update > 0) {
        fprintf(stdout, "\n");
        fflush(stdout);
        last_progress_update = 0;
    }
}
