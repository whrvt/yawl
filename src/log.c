/*
 * Logging subsystem implementation
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wextra-semi"
#define G_LOG_DOMAIN "libnotify"
#include "libnotify/notify.h"
#pragma clang diagnostic pop

#include "log.h"
#include "util.h"

static FILE *log_file = NULL;
static log_level_t current_log_level = LOG_INFO;
static int terminal_output = 0;
static gboolean notify_initialized = FALSE;

/* Color codes for terminal output */
#define COLOR_RESET "\033[0m"
#define COLOR_SYSTEM "\033[36m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_GREEN "\033[32m"
#define COLOR_BLUE "\033[34m"

static const char *level_strings[] = {"\0", "SYSTEM", "ERROR", "WARNING", "INFO", "DEBUG"};
static const char *level_colors[] = {"\0", COLOR_SYSTEM, COLOR_RED, COLOR_YELLOW, COLOR_GREEN, COLOR_BLUE};

/* Parse log level from string */
static log_level_t parse_log_level(const char *level_str) {
    if (!level_str || (strlen(level_str) > (sizeof("warning") - 1UL))) /* longest error level string */
        return LOG_INFO;

    log_level_t level = LOG_INFO;
    if (LCSTRING_EQUALS(level_str, "none"))
        level = LOG_NONE;
    else if (LCSTRING_EQUALS(level_str, "error"))
        level = LOG_ERROR;
    else if (LCSTRING_EQUALS(level_str, "warning"))
        level = LOG_WARNING;
    else if (LCSTRING_EQUALS(level_str, "info"))
        level = LOG_INFO;
    else if (LCSTRING_EQUALS(level_str, "debug"))
        level = LOG_DEBUG;

    return level;
}

RESULT log_init(void) {
    char *log_file_path = NULL;
    terminal_output = isatty(STDOUT_FILENO);

    const char *log_level_env = getenv("YAWL_LOG_LEVEL");
    if (log_level_env)
        log_set_level(parse_log_level(log_level_env));

    notify_initialized = notify_init("yawl");

    if (current_log_level == LOG_NONE)
        return MAKE_RESULT(SEV_SUCCESS, CAT_CONFIG, E_CANCELED);

    const char *log_file_env = getenv("YAWL_LOG_FILE");
    if (log_file_env)
        log_file_path = strdup(log_file_env);
    else
        join_paths(log_file_path, g_yawl_dir, PROG_NAME ".log");

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

        time_t now = time(NULL);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

        fprintf(log_file, "=== Log session started at %s ===\n", time_str);
        fflush(log_file);
    }

    free(log_file_path);
    return RESULT_OK;
}

void log_cleanup(void) {
    if (log_file) {
        /* Write session end marker */
        time_t now = time(NULL);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

        fprintf(log_file, "=== Log session ended at %s ===\n\n", time_str);
        fflush(log_file);

        fclose(log_file);
        log_file = NULL;
    }
}

void log_set_level(log_level_t level) {
    if (level >= LOG_NONE && level <= LOG_DEBUG)
        current_log_level = level;
}

log_level_t log_get_level(void) { return current_log_level; }

void _log_message(log_level_t level, const char *file, int line, const char *format, ...) {
    if (level > current_log_level && level != LOG_SYSTEM)
        return;

    va_list args;

    if (level == LOG_SYSTEM && notify_initialized) {
        NotifyNotification *notif;
        char *message;

        va_start(args, format);
        vasprintf(&message, format, args);
        va_end(args);

        notif = notify_notification_new("yawl", message, "dialog-information");

        notify_notification_set_urgency(notif, NOTIFY_URGENCY_CRITICAL);
        notify_notification_set_timeout(notif, 30000); /* 30 seconds */

        GError *error = NULL;
        if (!notify_notification_show(notif, &error)) {
            /* failed, whatever? */
        }

        g_object_unref(G_OBJECT(notif));
        free(message);
    }

    char timestamp[32];
    time_t now = time(NULL);

    /* Create timestamp */
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    /* Output to terminal if appropriate */
    if (terminal_output) {
        FILE *output = (level <= LOG_WARNING) ? stderr : stdout;

        /* Print with colors if it's a terminal */
        fprintf(output, "%s[%s]%s %s: ", level_colors[level], level_strings[level], COLOR_RESET, timestamp);

        va_start(args, format);
        vfprintf(output, format, args);
        va_end(args);

        fprintf(output, "\n");
    }

    if (log_file && level != LOG_SYSTEM) {
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

    if (level == LOG_DEBUG && FAILED(result)) {
        int severity = RESULT_SEVERITY(result);
        int category = RESULT_CATEGORY(result);
        int code = RESULT_CODE(result);

        _log_message(LOG_DEBUG, file, line, "  Details: Severity=%d, Category=%d, Code=0x%04X", severity, category,
                     code);
    }
}
