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

#pragma once

void _append_sep_impl(char **result_ptr, const char *separator, int num_paths, ...);

#define COUNT_JOIN_ARGS(...) (sizeof((const char *[]){__VA_ARGS__}) / sizeof(const char *))

/* Join strings with a `sep` separator into the first argument (`result`) */
#define append_sep(result, sep, ...)                                                                                   \
    do {                                                                                                               \
        _append_sep_impl(&(result), sep, COUNT_JOIN_ARGS(__VA_ARGS__), __VA_ARGS__);                                   \
    } while (0)

/* Join paths with a `/` separator into the first argument (`result`) */
#define join_paths(result, ...) append_sep(result, "/", __VA_ARGS__)
