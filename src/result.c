/*
 * Error handling subsystem implementation
 *
 * Copyright (C) 2025 William Horvath
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * See the full license text in the repository LICENSE file.
 */

#include <errno.h>
#include <string.h>

#include "curl/curl.h"
#include "result.h"

RESULT result_from_errno(void) {
    if (errno == 0)
        return RESULT_OK;

    switch (errno) {
    case ENOENT:
        return MAKE_RESULT(SEV_ERROR, CAT_FILESYSTEM, E_FILE_NOT_FOUND);
    case EACCES:
    case EPERM:
        return MAKE_RESULT(SEV_ERROR, CAT_FILESYSTEM, E_ACCESS_DENIED);
    case EEXIST:
        return MAKE_RESULT(SEV_ERROR, CAT_FILESYSTEM, E_ALREADY_EXISTS);
    case EINVAL:
        return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_INVALID_ARG);
    case ENOMEM:
        return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_OUT_OF_MEMORY);
    case EIO:
        return MAKE_RESULT(SEV_ERROR, CAT_FILESYSTEM, E_IO_ERROR);
    case EBUSY:
        return MAKE_RESULT(SEV_ERROR, CAT_SYSTEM, E_BUSY);
    case ETIMEDOUT:
        return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_TIMEOUT);
    case ENOSYS:
        return MAKE_RESULT(SEV_ERROR, CAT_SYSTEM, E_NOT_SUPPORTED);
    case ENOTDIR:
        return MAKE_RESULT(SEV_ERROR, CAT_FILESYSTEM, E_NOT_DIR);
    case ECONNREFUSED:
    case ECONNRESET:
    case ENETUNREACH:
    case EHOSTUNREACH:
        return MAKE_RESULT(SEV_ERROR, CAT_NETWORK, E_NETWORK_ERROR);
    default:
        return MAKE_RESULT(SEV_ERROR, CAT_SYSTEM, errno & 0xFFFF);
    }
}

const char *result_to_string(RESULT result) {
    if (SUCCEEDED(result) && result == RESULT_OK)
        return "Success";

    /* Get components of the result */
    int category = RESULT_CATEGORY(result);
    int code = RESULT_CODE(result);

    /* Handle common error codes first */
    switch (code) {
    case E_UNKNOWN:
        return "Unknown error";
    case E_INVALID_ARG:
        return "Invalid argument";
    case E_OUT_OF_MEMORY:
        return "Out of memory";
    case E_TIMEOUT:
        return "Operation timed out";
    case E_BUSY:
        return "Resource busy";
    case E_CANCELED:
        return "Operation canceled";
    case E_NOT_SUPPORTED:
        return "Operation not supported";
    }

    /* Handle category-specific errors */
    switch (category) {
    case CAT_FILESYSTEM:
        switch (code) {
        case E_FILE_NOT_FOUND:
            return "File not found";
        case E_ACCESS_DENIED:
            return "Access denied";
        case E_ALREADY_EXISTS:
            return "File already exists";
        case E_IO_ERROR:
            return "I/O error";
        case E_NOT_FOUND:
            return "Path not found";
        case E_NOT_DIR:
            return "Not a directory";
        default:
            return "Filesystem error";
        }
    case CAT_NETWORK:
        switch (code) {
        case E_CURL:
            return "curl error";
        default:
            return curl_easy_strerror((CURLcode)code);
        }
    case CAT_RUNTIME:
        return "Runtime error";
    case CAT_CONFIG:
        return "Configuration error";
    case CAT_CONTAINER:
        return "Container error";
    case CAT_APPARMOR:
        return "AppArmor error";
    case CAT_JSON:
        switch (code) {
        case E_PARSE_ERROR:
            return "JSON parsing error";
        case E_NOT_FOUND:
            return "JSON data not found";
        default:
            return "JSON error";
        }
    case CAT_SYSTEM:
        /* For system errors, try to map back to errno strings if possible */
        if (code < 256) {
            return strerror(code);
        }
        return "System error";
    }

    return "Unhandled result code error";
}
