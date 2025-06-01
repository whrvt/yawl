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

#include "result.hpp"

enum class Level : uint8_t {
    None = 0,
    System = 1,  /* Messages always attempted to be logged to stdout if in a terminal */
    Error = 2,   /* Critical errors that prevent proper operation */
    Warning = 3, /* Non-critical issues that might affect behavior */
    Info = 4,    /* Normal operational information */
    Debug = 5,   /* Detailed information for troubleshooting */
    Progress = 6 /* Terminal-only progress display */
};

/* Initialize the logging subsystem */
RESULT log_init(void); /* Changed return type from int to RESULT */

/* Cleanup logging resources */
void log_cleanup(void);

/* Set the maximum log level to display */
void log_set_level(Level level);

/* Get the current log level */
Level log_get_level(void);

/* Basically isatty() */
bool log_get_terminal_output(void);

/* Core logging function (for internal use) */
void _log_message(Level level, const char *file, int line, const char *format, ...);

/* Core function for logging RESULT values with context */
void _log_result(Level level, const char *file, int line, RESULT result, const char *context);

/* Progress meter display (terminal-only, does not write to log file) */
void log_progress(const char *operation, double percentage, int bytes_done, int bytes_total);

/* Finish progress display with newline */
void log_progress_end(void);

/* Convenience macros that include file and line information */
#define LOG_SYSTEM(...) _log_message(Level::System, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) _log_message(Level::Error, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARNING(...) _log_message(Level::Warning, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) _log_message(Level::Info, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) _log_message(Level::Debug, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_RESULT(level, result, context) _log_result(level, __FILE__, __LINE__, result, context)
#define LOG_DEBUG_RESULT(result, context) _log_result(Level::Debug, __FILE__, __LINE__, result, context)
