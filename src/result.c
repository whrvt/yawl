/*
 * Error handling subsystem implementation
 *
 * Copyright (C) 2025 William Horvath
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * See the full license text in the repository LICENSE file.
 */

#include <errno.h>
#include <stdio.h>
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

static const char *generic_code_to_string(const char *prefix, int rescode) {
    static char out[64] = {0};
    /* Handle other error codes */
    if (strlen(prefix) < sizeof(out))
    {
        switch (rescode) {
        case E_UNKNOWN:
            sprintf(out, "%s: %s", prefix, "Unknown error");
            break;
        case E_INVALID_ARG:
            sprintf(out, "%s: %s", prefix, "Invalid argument");
            break;
        case E_OUT_OF_MEMORY:
            sprintf(out, "%s: %s", prefix, "Out of memory");
            break;
        case E_TIMEOUT:
            sprintf(out, "%s: %s", prefix, "Operation timed out");
            break;
        case E_BUSY:
            sprintf(out, "%s: %s", prefix, "Resource busy");
            break;
        case E_CANCELED:
            sprintf(out, "%s: %s", prefix, "Operation canceled");
            break;
        case E_NOT_SUPPORTED:
            sprintf(out, "%s: %s", prefix, "Operation not supported");
            break;
        default:
            sprintf(out, "%s: %s (%s)", prefix, "Unhandled result code error", rescode < 256 ? strerror(rescode) : "-");
        }
        return out;
    }
    return prefix;
}

const char *result_to_string(RESULT result) {
    if (SUCCEEDED(result) && result == RESULT_OK)
        return "Success";

    /* Get components of the result */
    int category = RESULT_CATEGORY(result);
    int code = RESULT_CODE(result);

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
            return generic_code_to_string("Filesystem error", code);
        }
    case CAT_NETWORK:
        switch (code) {
        case E_CURL:
            return "curl error";
        default:
            return curl_easy_strerror((CURLcode)code);
        }
    case CAT_RUNTIME:
        return generic_code_to_string("Runtime error", code);
    case CAT_CONFIG:
        return generic_code_to_string("Configuration error", code);
    case CAT_CONTAINER:
        return generic_code_to_string("Container error", code);
    case CAT_APPARMOR:
        return generic_code_to_string("AppArmor error", code);
    case CAT_JSON:
        switch (code) {
        case E_PARSE_ERROR:
            return "JSON parsing error";
        case E_NOT_FOUND:
            return "JSON data not found";
        default:
            return generic_code_to_string("JSON error", code);
        }
    case CAT_SYSTEM:
        return generic_code_to_string("System error", code);
    }
    return generic_code_to_string("Unknown error", code);
}
