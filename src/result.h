/*
 * Error handling subsystem
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

#pragma once

#include <stdint.h>

/*
 * RESULT bit layout:
 * 31    - Success/Failure flag (0 = success, 1 = failure)
 * 30-27 - Severity (0 = success, 1 = info, 2 = warning, 3 = error)
 * 26-16 - Error category/facility (0 = general, 1 = system, 2 = runtime, etc.)
 * 15-0  - Error code (specific to category)
 */
typedef int32_t RESULT;

#define SUCCEEDED(result) (((RESULT)(result)) >= 0)
#define FAILED(result) (((RESULT)(result)) < 0)

/* Result creation macros */
#define MAKE_RESULT(sev, cat, code)                                                                                    \
    ((RESULT)(((sev) >= SEV_WARNING ? 0x80000000 : 0) | ((unsigned)(sev) << 27) | ((unsigned)(cat) << 16) |            \
              ((unsigned)(code) & 0xFFFF)))

/* Success result */
#define RESULT_OK ((RESULT)0)

/* Error categories */
#define CAT_GENERAL 0
#define CAT_SYSTEM 1
#define CAT_FILESYSTEM 2
#define CAT_NETWORK 3
#define CAT_RUNTIME 4
#define CAT_CONFIG 5
#define CAT_CONTAINER 6
#define CAT_APPARMOR 7
#define CAT_JSON 8

/* Severity levels */
#define SEV_SUCCESS 0
#define SEV_INFO 1
#define SEV_WARNING 2
#define SEV_ERROR 3

/* Common error codes */
#define E_UNKNOWN 1
#define E_INVALID_ARG 2
#define E_OUT_OF_MEMORY 3
#define E_FILE_NOT_FOUND 4
#define E_ACCESS_DENIED 5
#define E_ALREADY_EXISTS 6
#define E_NOT_SUPPORTED 7
#define E_IO_ERROR 8
#define E_TIMEOUT 9
#define E_NOT_READY 10
#define E_NOT_FOUND 11
#define E_CANCELED 12
#define E_BUSY 13
#define E_NETWORK_ERROR 14
#define E_PARSE_ERROR 15
#define E_NOT_DIR 16
#define E_UPDATE_AVAILABLE 100
#define E_UPDATE_PERFORMED 101
#define E_CURL 404

/* Extract components from a RESULT */
#define RESULT_SEVERITY(result) (((result) >> 27) & 0xF)
#define RESULT_CATEGORY(result) (((result) >> 16) & 0x7FF)
#define RESULT_CODE(result) ((result) & 0xFFFF)

/* Convert from errno to RESULT */
RESULT result_from_errno(void);

/* Get a string description of a RESULT */
const char *result_to_string(RESULT result);

/* Return immediately if result failed */
#define RETURN_IF_FAILED(result)                                                                                       \
    do {                                                                                                               \
        RESULT _r = (result);                                                                                          \
        if (FAILED(_r))                                                                                                \
            return _r;                                                                                                 \
    } while (0)

/* Log and return if result failed */
#define LOG_AND_RETURN_IF_FAILED(level, result, message)                                                               \
    do {                                                                                                               \
        RESULT _r = (result);                                                                                          \
        if (FAILED(_r)) {                                                                                              \
            LOG_RESULT(level, _r, message);                                                                            \
            return _r;                                                                                                 \
        }                                                                                                              \
    } while (0)

/* (unused) Propagate error code but use a different category */
/* #define PROPAGATE_ERROR_CAT(result, new_cat)                                                                           \
    (FAILED(result) ? MAKE_RESULT(RESULT_SEVERITY(result), new_cat, RESULT_CODE(result)) : (result))
 */
