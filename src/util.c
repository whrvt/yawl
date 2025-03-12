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

#include "log.h"
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
            LOG_WARNING("Invalid characters in path: %s", path);
        else if (ret == WRDE_SYNTAX)
            LOG_WARNING("Syntax error in path: %s", path);

        /* Fall back to the original path */
        return strdup(path);
    }

    /* We should have exactly one expansion result */
    if (p.we_wordc == 1) {
        result = strdup(p.we_wordv[0]);
    } else {
        /* If we get multiple results or none, fall back to the original path */
        LOG_WARNING("Ambiguous path expansion for: %s", path);
        result = strdup(path);
    }

    wordfree(&p);
    return result;
}

static RESULT create_directory_tree(char *path) {
    /* Skip leading slashes */
    char *p = path;
    if (*p == '/')
        p++;

    while (*p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(path, 0755) != 0 && errno != EEXIST) {
                *p = '/';
                return result_from_errno();
            }
            *p = '/';
        }
        p++;
    }

    /* Create the final directory */
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
        return result_from_errno();

    return RESULT_OK;
}

RESULT ensure_dir(const char *path) {
    if (!path)
        return MAKE_RESULT(SEV_ERROR, CAT_FILESYSTEM, E_INVALID_ARG);

    char *expanded_path = expand_path(path);
    RETURN_NULL_CHECK(expanded_path, "Failed to expand path");

    RESULT ret = RESULT_OK;
    struct stat st;
    if (stat(expanded_path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            if (access(expanded_path, W_OK) != 0)
                ret = result_from_errno();
        } else { /* Exists, not a directory */
            errno = ENOTDIR;
            ret = result_from_errno();
        }
    } else {
        /* Directory doesn't exist, create it (recursively) */
        ret = create_directory_tree(expanded_path);
    }

    free(expanded_path);
    return ret;
}

RESULT remove_dir(const char *path) {
    DIR *dir = opendir(path);
    if (!dir)
        return result_from_errno();

    RESULT result = RESULT_OK;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char *full_path = NULL;
        join_paths(full_path, path, entry->d_name);

        struct stat st;
        if (lstat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                RESULT dir_result = remove_dir(full_path);
                if (FAILED(dir_result)) {
                    free(full_path);
                    closedir(dir);
                    return dir_result;
                }
            } else if (unlink(full_path) != 0) {
                RESULT unlink_result = result_from_errno();
                LOG_RESULT(LOG_WARNING, unlink_result, "Failed to remove file");
                result = unlink_result; /* remember the error, but continue */
            }
        }

        free(full_path);
    }

    closedir(dir);

    if (rmdir(path) != 0) {
        RESULT rmdir_result = result_from_errno();
        LOG_RESULT(LOG_ERROR, rmdir_result, "Failed to remove directory");
        return rmdir_result;
    }

    return result;
}

RESULT calculate_sha256(const char *file_path, char *hash_str, size_t hash_str_len) {
    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        RESULT result = result_from_errno();
        LOG_RESULT(LOG_ERROR, result, "Failed to open file for hash calculation");
        return result;
    }

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        RESULT result = MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_OUT_OF_MEMORY);
        LOG_RESULT(LOG_ERROR, result, "Failed to create hash context");
        fclose(fp);
        return result;
    }

    const EVP_MD *md = EVP_sha256();
    if (!md) {
        RESULT result = MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_NOT_SUPPORTED);
        LOG_RESULT(LOG_ERROR, result, "Failed to get SHA256 algorithm");
        EVP_MD_CTX_free(mdctx);
        fclose(fp);
        return result;
    }

    if (EVP_DigestInit_ex(mdctx, md, NULL) != 1) {
        RESULT result = MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_UNKNOWN);
        LOG_RESULT(LOG_ERROR, result, "Failed to initialize hash context");
        EVP_MD_CTX_free(mdctx);
        fclose(fp);
        return result;
    }

    unsigned char buffer[BUFFER_SIZE];
    size_t bytes_read;
    RESULT result = RESULT_OK;

    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
        if (EVP_DigestUpdate(mdctx, buffer, bytes_read) != 1) {
            result = MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_UNKNOWN);
            LOG_RESULT(LOG_ERROR, result, "Failed to update hash context");
            EVP_MD_CTX_free(mdctx);
            fclose(fp);
            return result;
        }
    }

    fclose(fp);

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;

    if (EVP_DigestFinal_ex(mdctx, hash, &hash_len) != 1) {
        result = MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_UNKNOWN);
        LOG_RESULT(LOG_ERROR, result, "Failed to finalize hash");
        EVP_MD_CTX_free(mdctx);
        return result;
    }

    EVP_MD_CTX_free(mdctx);

    /* Convert to hex string */
    for (unsigned int i = 0; i < hash_len; i++)
        snprintf(hash_str + (i * 2), 3, "%02x", hash[i]);

    hash_str[hash_str_len - 1] = '\0';
    return RESULT_OK;
}

RESULT get_online_slr_hash(const char *file_name, const char *hash_url, char *hash_str, size_t hash_str_len) {
    char *local_sums_path = NULL;
    FILE *fp = NULL;
    char line[200];
    int found = 0;
    RESULT result = RESULT_OK;

    join_paths(local_sums_path, g_yawl_dir, "SHA256SUMS");
    RETURN_NULL_CHECK(local_sums_path, "Failed to allocate memory for hash file path");

    result = download_file(hash_url, local_sums_path);
    if (FAILED(result)) {
        LOG_RESULT(LOG_ERROR, result, "Failed to download hash file");
        free(local_sums_path);
        return result;
    }

    fp = fopen(local_sums_path, "r");
    if (!fp) {
        result = result_from_errno();
        LOG_RESULT(LOG_ERROR, result, "Failed to open downloaded hash file");
        free(local_sums_path);
        return result;
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

    if (!found)
        return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_NOT_FOUND);

    return RESULT_OK;
}

RESULT download_file(const char *url, const char *output_path) {
    if (!url || !output_path)
        return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_INVALID_ARG);

    CURL *curl = curl_easy_init();
    if (!curl) {
        RESULT result = MAKE_RESULT(SEV_ERROR, CAT_NETWORK, E_UNKNOWN);
        LOG_RESULT(LOG_ERROR, result, "Failed to initialize curl");
        return result;
    }

    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        RESULT result = result_from_errno();
        LOG_RESULT(LOG_ERROR, result, "Failed to open output file for download");
        curl_easy_cleanup(curl);
        return result;
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
        RESULT result = MAKE_RESULT(SEV_ERROR, CAT_NETWORK, res);
        LOG_RESULT(LOG_ERROR, result, "Download failed");
        return result;
    }

    return RESULT_OK;
}

RESULT extract_archive(const char *archive_path, const char *extract_path) {
    if (!archive_path || !extract_path)
        return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_INVALID_ARG);

    struct archive *a;
    struct archive *ext;
    struct archive_entry *entry;
    int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS |
                ARCHIVE_EXTRACT_OWNER;
    RESULT result = RESULT_OK;

    a = archive_read_new();
    if (!a)
        return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_OUT_OF_MEMORY);

    archive_read_support_format_tar(a);
    archive_read_support_filter_xz(a);
    archive_read_support_filter_zstd(a);
    archive_read_support_filter_lzip(a);
    archive_read_support_filter_gzip(a);

    ext = archive_write_disk_new();
    if (!ext) {
        archive_read_free(a);
        return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_OUT_OF_MEMORY);
    }

    archive_write_disk_set_options(ext, flags);
    archive_write_disk_set_standard_lookup(ext);

    if (archive_read_open_filename(a, archive_path, BUFFER_SIZE) != ARCHIVE_OK) {
        result = MAKE_RESULT(SEV_ERROR, CAT_FILESYSTEM, E_IO_ERROR);
        LOG_RESULT(LOG_ERROR, result, "Failed to open archive for extraction");
        goto cleanup;
    }

    char *old_cwd = getcwd(NULL, 0);
    if (!old_cwd) {
        result = result_from_errno();
        LOG_RESULT(LOG_ERROR, result, "Failed to get current working directory");
        goto cleanup;
    }

    if (chdir(extract_path) != 0) {
        result = result_from_errno();
        LOG_RESULT(LOG_ERROR, result, "Failed to change to extraction directory");
        free(old_cwd);
        goto cleanup;
    }

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (archive_write_header(ext, entry) != ARCHIVE_OK) {
            LOG_WARNING("Skipping entry, failed to write header: %s", archive_error_string(ext));
            continue;
        }

        const void *buff;
        size_t size;
        int64_t offset;

        while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
            if (archive_write_data_block(ext, buff, size, offset) != ARCHIVE_OK) {
                LOG_WARNING("Write error for %s: %s", archive_entry_pathname(entry), archive_error_string(ext));
                break;
            }
        }
    }

    if (chdir(old_cwd) != 0) {
        LOG_WARNING("Failed to restore working directory: %s", strerror(errno));
    }

    free(old_cwd);

cleanup:
    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);
    return result;
}
