/*
 * Path, string, and miscellaneous helper functions
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

#include <wordexp.h>

#include "openssl/evp.h"

#include "util.h"

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

int ensure_dir(const char *path) {
    struct stat st;
    if (!stat(path, &st)) {
        if (S_ISDIR(st.st_mode))
            return access(path, W_OK);
        else
            return -1;
    }
    return mkdir(path, 0755);
}

int remove_dir(const char *path) {
    DIR *dir = opendir(path);
    if (!dir)
        return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char *full_path = NULL;
        join_paths(full_path, path, entry->d_name);

        struct stat st;
        if (lstat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                remove_dir(full_path);
            } else
                unlink(full_path);
        }

        free(full_path);
    }

    closedir(dir);
    return rmdir(path);
}

int calculate_sha256(const char *file_path, char *hash_str, size_t hash_str_len) {
    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Failed to open file for hash calculation: %s\n", strerror(errno));
        return -1;
    }

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        fprintf(stderr, "Error: Failed to create hash context\n");
        fclose(fp);
        return -1;
    }

    const EVP_MD *md = EVP_sha256();
    if (!md) {
        fprintf(stderr, "Error: Failed to get SHA256 algorithm\n");
        EVP_MD_CTX_free(mdctx);
        fclose(fp);
        return -1;
    }

    if (EVP_DigestInit_ex(mdctx, md, NULL) != 1) {
        fprintf(stderr, "Error: Failed to initialize hash context\n");
        EVP_MD_CTX_free(mdctx);
        fclose(fp);
        return -1;
    }

    unsigned char buffer[BUFFER_SIZE];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
        if (EVP_DigestUpdate(mdctx, buffer, bytes_read) != 1) {
            fprintf(stderr, "Error: Failed to update hash context\n");
            EVP_MD_CTX_free(mdctx);
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;

    if (EVP_DigestFinal_ex(mdctx, hash, &hash_len) != 1) {
        fprintf(stderr, "Error: Failed to finalize hash\n");
        EVP_MD_CTX_free(mdctx);
        return -1;
    }

    EVP_MD_CTX_free(mdctx);

    /* Convert to hex string */
    for (unsigned int i = 0; i < hash_len; i++)
        snprintf(hash_str + (i * 2), 3, "%02x", hash[i]);

    hash_str[hash_str_len - 1] = '\0';
    return 0;
}

char *expand_path(const char *path) {
    if (!path)
        return NULL;

    /* Fast path for paths that don't need expansion */
    if (!strchr(path, '~') && !strchr(path, '$'))
        return strdup(path);

    wordexp_t p;
    char *result = NULL;

    /* Use wordexp to handle path expansion like the shell would */
    int ret = wordexp(path, &p, WRDE_NOCMD);
    if (ret != 0) {
        /* Handle specific error cases */
        if (ret == WRDE_BADCHAR)
            fprintf(stderr, "Warning: Invalid characters in path: %s\n", path);
        else if (ret == WRDE_SYNTAX)
            fprintf(stderr, "Warning: Syntax error in path: %s\n", path);

        /* Fall back to the original path */
        return strdup(path);
    }

    /* We should have exactly one expansion result */
    if (p.we_wordc == 1) {
        result = strdup(p.we_wordv[0]);
    } else {
        /* If we get multiple results or none, fall back to the original path */
        fprintf(stderr, "Warning: Ambiguous path expansion for: %s\n", path);
        result = strdup(path);
    }

    wordfree(&p);
    return result;
}
