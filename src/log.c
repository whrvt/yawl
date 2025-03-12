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

#include "log.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static FILE *log_file = NULL;
static log_level_t current_log_level = LOG_INFO;
static int terminal_output = 0;

/* Color codes for terminal output */
#define COLOR_RESET "\033[0m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_GREEN "\033[32m"
#define COLOR_BLUE "\033[34m"

static const char *level_strings[] = {"\0", "ERROR", "WARNING", "INFO", "DEBUG"};
static const char *level_colors[] = {"\0", COLOR_RED, COLOR_YELLOW, COLOR_GREEN, COLOR_BLUE};

/* Parse log level from string */
static log_level_t parse_log_level(const char *level_str) {
    if (!level_str)
        return LOG_INFO;

    char *level_copy = strdup(level_str);
    if (!level_copy)
        return LOG_INFO;

    for (char *p = level_copy; *p; p++)
        *p = tolower(*p);

    log_level_t level = LOG_INFO;
    if (strcmp(level_copy, "none") == 0)
        level = LOG_NONE;
    else if (strcmp(level_copy, "error") == 0)
        level = LOG_ERROR;
    else if (strcmp(level_copy, "warning") == 0)
        level = LOG_WARNING;
    else if (strcmp(level_copy, "info") == 0)
        level = LOG_INFO;
    else if (strcmp(level_copy, "debug") == 0)
        level = LOG_DEBUG;

    free(level_copy);
    return level;
}

RESULT log_init(void) {
    char *log_file_path = NULL;
    terminal_output = isatty(STDOUT_FILENO);

    const char *log_level_env = getenv("YAWL_LOG_LEVEL");
    if (log_level_env)
        log_set_level(parse_log_level(log_level_env));

    const char *log_file_env = getenv("YAWL_LOG_FILE");
    if (log_file_env)
        log_file_path = strdup(log_file_env);
    else
        join_paths(log_file_path, g_yawl_dir, PROG_NAME ".log");

    RETURN_NULL_CHECK(log_file_path, "Failed to allocate memory for log file path");

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
    if (level > current_log_level)
        return;

    va_list args;
    char timestamp[32];
    time_t now = time(NULL);

    /* Create timestamp */
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    /* Get just the filename without the path */
    const char *filename = strrchr(file, '/');
    if (filename)
        filename++; /* Skip the slash */
    else
        filename = file;

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

    if (log_file) {
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
