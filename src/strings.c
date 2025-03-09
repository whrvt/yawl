/*
 * Path and string helper functions
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

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void _append_sep_impl(char **result_ptr, const char *separator, int num_paths, ...) {
    va_list args;
    va_start(args, num_paths);

    if (num_paths <= 0) {
        va_end(args);
        return;
    }

    const char *first_path = va_arg(args, const char *);

    if (*result_ptr == NULL || **result_ptr == '\0') {
        free(*result_ptr);
        *result_ptr = strdup(first_path);
    } else {
        size_t current_len = strlen(*result_ptr);
        size_t sep_len = strlen(separator);
        size_t next_len = strlen(first_path);
        size_t new_size = current_len + sep_len + next_len + 1;

        *result_ptr = realloc(*result_ptr, new_size);

        strcat(*result_ptr, separator);
        strcat(*result_ptr, first_path);
    }

    for (int i = 1; i < num_paths; i++) {
        const char *next_path = va_arg(args, const char *);

        size_t current_len = strlen(*result_ptr);
        size_t sep_len = strlen(separator);
        size_t next_len = strlen(next_path);
        size_t new_size = current_len + sep_len + next_len + 1;

        *result_ptr = realloc(*result_ptr, new_size);

        strcat(*result_ptr, separator);
        strcat(*result_ptr, next_path);
    }

    va_end(args);
}
