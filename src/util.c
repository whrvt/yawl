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

/* This file does not normally exist, but it contains embedded CA certificate
   data generated as part of the curl build process, and we move it here to be
   able to use it for CURLOPT_CAINFO_BLOB */
#include "curl/ca_cert_embed.h"

#include "archive.h"
#include "archive_entry.h"
#include "curl/curl.h"

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

static int create_directory_tree(char *path) {
    /* Skip leading slashes */
    char *p = path;
    if (*p == '/')
        p++;

    while (*p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(path, 0755) != 0 && errno != EEXIST) {
                *p = '/';
                return -1;
            }
            *p = '/';
        }
        p++;
    }

    /* Create the final directory */
    return (mkdir(path, 0755) != 0 && errno != EEXIST) ? -1 : 0;
}

int ensure_dir(const char *path) {
    if (!path)
        return -1;

    char *expanded_path = expand_path(path);
    if (!expanded_path)
        return -1;

    int ret = -1;
    struct stat st;
    if (stat(expanded_path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            ret = access(expanded_path, W_OK);
            goto ensure_done;
        } else {
            /* Path exists but is not a directory */
            errno = ENOTDIR;
            goto ensure_done;
        }
    }

    /* Directory doesn't exist, create it (recursively) */
    ret = create_directory_tree(expanded_path);

ensure_done:
    free(expanded_path);
    return ret;
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

char *get_base_name(const char *path) {
    char *path_copy = strdup(path);
    if (!path_copy)
        return NULL;

    char *last_slash = strrchr(path_copy, '/');
    char *base_name = strdup(last_slash ? last_slash + 1 : path_copy);
    free(path_copy);

    return base_name;
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

int get_online_slr_hash(const char *file_name, const char *hash_url, char *hash_str, size_t hash_str_len) {
    char *local_sums_path = NULL;
    FILE *fp = NULL;
    char line[200];
    int found = 0;

    join_paths(local_sums_path, get_yawl_dir(), "SHA256SUMS");

    if (download_file(hash_url, local_sums_path) != 0) {
        free(local_sums_path);
        return -1;
    }

    fp = fopen(local_sums_path, "r");
    if (!fp) {
        free(local_sums_path);
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        /* Format is "hash *filename" */
        char *hash_end = strchr(line, ' ');
        if (!hash_end)
            continue;

        *hash_end = '\0';
        char *file = hash_end + 2; /* Skip " *" */

        char *newline = strchr(file, '\n');
        if (newline)
            *newline = '\0';

        if (strcmp(file, file_name) == 0) {
            strncpy(hash_str, line, hash_str_len - 1);
            hash_str[hash_str_len - 1] = '\0';
            found = 1;
            break;
        }
    }

    fclose(fp);
    free(local_sums_path);

    return found ? 0 : -1;
}

int download_file(const char *url, const char *output_path) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "curl error: %s\n", curl_easy_strerror(CURLE_FAILED_INIT));
        return -1;
    }

    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        fprintf(stderr, "Error: Couldn't open output_path (%s): %s\n", output_path, strerror(errno));
        curl_easy_cleanup(curl);
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    /* Copied from curl's `src/tool_operate.c`, use the embedded CA certificate data */
    struct curl_blob blob;
    blob.data = (void *)curl_ca_embed;
    blob.len = strlen((const char *)curl_ca_embed);
    blob.flags = CURL_BLOB_NOCOPY;
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob);

    CURLcode res = curl_easy_perform(curl);

    fclose(fp);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "Failed to download %s, curl error: %s\n", url, curl_easy_strerror(res));
        return -1;
    }

    return 0;
}

int extract_archive(const char *archive_path, const char *extract_path) {
    struct archive *a;
    struct archive *ext;
    struct archive_entry *entry;
    int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS |
                ARCHIVE_EXTRACT_OWNER;
    int r;

    a = archive_read_new();
    archive_read_support_format_tar(a);
    archive_read_support_filter_xz(a);
    archive_read_support_filter_zstd(a);
    archive_read_support_filter_lzip(a);
    archive_read_support_filter_gzip(a);

    ext = archive_write_disk_new();
    archive_write_disk_set_options(ext, flags);
    archive_write_disk_set_standard_lookup(ext);

    if ((r = archive_read_open_filename(a, archive_path, BUFFER_SIZE))) {
        fprintf(stderr, "Error: Extracting failed (read_open_filename): %s\n", archive_error_string(a));
        return -1;
    }

    char *old_cwd = getcwd(NULL, 0);
    if (chdir(extract_path) != 0) {
        fprintf(stderr, "Error: Extracting failed (chdir): %s\n", strerror(errno));
        free(old_cwd);
        return -1;
    }

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        r = archive_write_header(ext, entry);
        if (r != ARCHIVE_OK)
            continue;

        const void *buff;
        size_t size;
        int64_t offset;

        while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK)
            if (archive_write_data_block(ext, buff, size, offset) != ARCHIVE_OK)
                break;
    }

    chdir(old_cwd);
    free(old_cwd);

    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);
    return 0;
}
