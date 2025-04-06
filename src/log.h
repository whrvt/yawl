/*
 * Logging subsystem
 *
 * Copyright (C) 2025 William Horvath
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * See the full license text in the repository LICENSE file.
 */

#pragma once

#include <unistd.h>

#include "result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_NONE = 0,
    LOG_SYSTEM = 1,  /* Messages always attempted to be logged to stdout if in a terminal */
    LOG_ERROR = 2,   /* Critical errors that prevent proper operation */
    LOG_WARNING = 3, /* Non-critical issues that might affect behavior */
    LOG_INFO = 4,    /* Normal operational information */
    LOG_DEBUG = 5,   /* Detailed information for troubleshooting */
    LOG_PROGRESS = 6 /* Terminal-only progress display */
} log_level_t;

/* Initialize the logging subsystem */
RESULT log_init(void); /* Changed return type from int to RESULT */

/* Cleanup logging resources */
void log_cleanup(void);

/* Set the maximum log level to display */
void log_set_level(log_level_t level);

/* Get the current log level */
log_level_t log_get_level(void);

/* Basically isatty() */
bool log_get_terminal_output(void);

/* Core logging function (for internal use) */
void _log_message(log_level_t level, const char *file, int line, const char *format, ...);

/* Core function for logging RESULT values with context */
void _log_result(log_level_t level, const char *file, int line, RESULT result, const char *context);

/* Progress meter display (terminal-only, does not write to log file) */
void log_progress(const char *operation, double percentage, int bytes_done, int bytes_total);

/* Finish progress display with newline */
void log_progress_end(void);

/* Convenience macros that include file and line information */
#define LOG_SYSTEM(...) _log_message(LOG_SYSTEM, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) _log_message(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARNING(...) _log_message(LOG_WARNING, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) _log_message(LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) _log_message(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_RESULT(level, result, context) _log_result(level, __FILE__, __LINE__, result, context)
#define LOG_DEBUG_RESULT(result, context) _log_result(LOG_DEBUG, __FILE__, __LINE__, result, context)

#ifdef __cplusplus
}
#endif
