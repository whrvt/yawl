/*
 * Path, string, and miscellaneous helper functions
 *
 * Copyright (C) 2025 William Horvath
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * See the full license text in the repository LICENSE file.
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <wordexp.h>

#include "archive.h"
#include "archive_entry.h"
#include "curl/curl.h"
#include "log.h"
#include "macros.h"
#include "openssl/evp.h"
#include "util.h"

void _append_sep_impl(char *result_ptr[], const char *separator, size_t num_strings, ...) {
    char *old_result = *result_ptr;
    size_t old_len = (old_result != nullptr) ? strlen(old_result) : 0;
    size_t sep_len = strlen(separator);
    size_t total_length = old_len;

    size_t num_separators = num_strings;
    if (old_len == 0 && num_strings > 0)
        num_separators--;

    total_length += num_separators * sep_len;

    va_list args, args_copy;
    va_start(args, num_strings);
    va_copy(args_copy, args);

    for (size_t i = 0; i < num_strings; i++) {
        const char *str = va_arg(args_copy, const char *);
        total_length += strlen(str);
    }
    va_end(args_copy);

    char *new_result = (char *)realloc(old_result, total_length + 1);
    assert(new_result != nullptr); /* don't fail malloc */

    /* early return for the degenerate case (no separator or strings to add) */
    if (old_len == 0 && num_strings == 0) {
        new_result[0] = '\0';
        *result_ptr = new_result;
        va_end(args);
        return;
    }

    /* do the concatenation */
    char *dest = new_result + old_len;
    for (size_t i = 0; i < num_strings; i++) {
        const char *str = va_arg(args, const char *);

        if (i > 0 || old_len > 0) {
            memcpy(dest, separator, sep_len);
            dest += sep_len;
        }

        size_t str_len = strlen(str);
        memcpy(dest, str, str_len);
        dest += str_len;
    }
    *dest = '\0';

    *result_ptr = new_result;
    va_end(args);
}

char *expand_path(const char *path) {
    if (!path)
        return nullptr;

    /* Fast path for paths that don't need expansion */
    if (!strchr(path, '~') && !strchr(path, '$'))
        return strdup(path);

    wordexp_t p;
    char *result = nullptr;

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

static forceinline RESULT create_directory_tree(char *path) {
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
        /* Directory doesn't exist, create it */
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
    while ((entry = readdir(dir)) != nullptr) {
        if (STRING_EQUALS(entry->d_name, ".") || STRING_EQUALS(entry->d_name, ".."))
            continue;

        char *full_path = nullptr;
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

RESULT calculate_sha256(const char *file_path, char hash_str[65]) {
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

    if (EVP_DigestInit_ex(mdctx, md, nullptr) != 1) {
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

    hash_str[64] = '\0';
    return RESULT_OK;
}

RESULT get_online_slr_sha256sum(const char *file_name, const char *hash_url, char hash_str[65]) {
    char *local_sums_path = nullptr;
    FILE *fp = nullptr;
    char line[200];
    int found = 0;
    RESULT result = RESULT_OK;

    join_paths(local_sums_path, g_yawl_dir, "SHA256SUMS");

    result = download_file(hash_url, local_sums_path, nullptr);
    if (FAILED(result)) {
        LOG_RESULT(LOG_ERROR, result, "Failed to download hash file");
        unlink(local_sums_path);
        free(local_sums_path);
        return result;
    }

    fp = fopen(local_sums_path, "r");
    if (!fp) {
        result = result_from_errno();
        LOG_RESULT(LOG_ERROR, result, "Failed to open downloaded hash file");
        unlink(local_sums_path);
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

        if (STRING_EQUALS(file, file_name)) {
            strncpy(hash_str, line, 64);
            hash_str[64] = '\0';
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

/* This file is just an SSL CA certificate bundle, which we use to make secure requests with curl
 * (with CURLOPT_CAINFO_BLOB), without needing to rely on this data being found by curl/OpenSSL on the host */
static constexpr const unsigned char curl_ca_embed[] = {
#embed "../assets/external/cacert.pem"
};

static int download_progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    const char *filename = (const char *)clientp;
    if (dltotal > 0) {
        double percentage = ((double)dlnow / (double)dltotal) * 100.0;
        log_progress(filename, percentage, (int)dlnow, (int)dltotal);
    }
    return 0; /* continue */
}

RESULT download_file(const char *url, const char *output_path, const char *headers[]) {
    if (!url || !output_path)
        return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_INVALID_ARG);

    CURL *curl = curl_easy_init();
    if (!curl) {
        RESULT result = MAKE_RESULT(SEV_ERROR, CAT_NETWORK, E_CURL);
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

    /* Add optional headers to the req */
    struct curl_slist *header_list = nullptr;
    if (headers) {
        for (const char **header = headers; *header; header++) {
            header_list = curl_slist_append(header_list, *header);
            if (!header_list)
                LOG_WARNING("Failed to append header: %s", *header);
        }
        if (header_list)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullptr);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

    /* Copied from curl's `src/tool_operate.c`, use the embedded CA certificate data */
    struct curl_blob blob;
    blob.data = (void *)curl_ca_embed;
    blob.len = sizeof(curl_ca_embed);
    blob.flags = CURL_BLOB_NOCOPY;
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob);

    /* progress meter */
    const char *filename = nullptr;
    if (log_get_terminal_output()) {
        filename = strrchr(output_path, '/');
        if (filename)
            filename++;
        else
            filename = output_path;
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, download_progress_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, filename);
    }

    CURLcode res = curl_easy_perform(curl);

    log_progress_end();

    fclose(fp);

    if (header_list)
        curl_slist_free_all(header_list);

    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        return MAKE_RESULT(SEV_ERROR, CAT_NETWORK, res);

    return RESULT_OK;
}

/* archive-specific cleanup */
static forceinline void cleanup_archive_r(void *p) {
    struct archive **r = (struct archive **)p;
    if (r && *r) {
        archive_read_free(*r); /* (header) ... archive_read_free will call archive_read_close for you. */
        *r = nullptr;
    }
}

static forceinline void cleanup_archive_w(void *p) {
    struct archive **w = (struct archive **)p;
    if (w && *w) {
        archive_write_close(*w);
        archive_write_free(*w);
        *w = nullptr;
    }
}

#define auto_archive_r [[gnu::cleanup(cleanup_archive_r)]]
#define auto_archive_w [[gnu::cleanup(cleanup_archive_w)]]

RESULT extract_archive(const char *archive_path, const char *extract_path) {
    if (!archive_path || !extract_path)
        return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_INVALID_ARG);

    int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS |
                ARCHIVE_EXTRACT_OWNER;
    RESULT result = RESULT_OK;

    auto_archive_r struct archive *a = archive_read_new();
    if (!a)
        return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_OUT_OF_MEMORY);

    archive_read_support_format_tar(a);
    archive_read_support_filter_xz(a);
    archive_read_support_filter_zstd(a);
    archive_read_support_filter_lzip(a);
    archive_read_support_filter_gzip(a);

    auto_archive_w struct archive *ext = archive_write_disk_new();
    if (!ext)
        return MAKE_RESULT(SEV_ERROR, CAT_GENERAL, E_OUT_OF_MEMORY);

    archive_write_disk_set_options(ext, flags);
    archive_write_disk_set_standard_lookup(ext);

    if (archive_read_open_filename(a, archive_path, BUFFER_SIZE) != ARCHIVE_OK) {
        result = MAKE_RESULT(SEV_ERROR, CAT_FILESYSTEM, E_IO_ERROR);
        LOG_RESULT(LOG_ERROR, result, "Failed to open archive for extraction");
        return result;
    }

    struct archive_entry *entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        char fullpath[BUFFER_SIZE];
        const char *current_path = archive_entry_pathname(entry);

        /* Construct the full path including extraction directory */
        snprintf(fullpath, sizeof(fullpath), "%s/%s", extract_path, current_path);

        /* Update the entry with the full destination path */
        archive_entry_copy_pathname(entry, fullpath);

        if (archive_write_header(ext, entry) != ARCHIVE_OK) {
            LOG_WARNING("Skipping entry, failed to write header: %s", archive_error_string(ext));
            continue;
        }

        const void *buff;
        size_t size;
        int64_t offset;

        while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
            if (archive_write_data_block(ext, buff, size, offset) != ARCHIVE_OK) {
                LOG_WARNING("Write error for %s: %s", fullpath, archive_error_string(ext));
                break;
            }
        }
    }

    return result;
}

RESULT remove_verbs_from_env(const char *verbs_to_remove[], int num_verbs) {
    RESULT result = RESULT_OK;
    const char *yawl_verbs = getenv("YAWL_VERBS");
    if (!yawl_verbs || *yawl_verbs == '\0')
        return MAKE_RESULT(SEV_INFO, CAT_SYSTEM, E_NOT_FOUND);

    autofree char *copy = strdup(yawl_verbs);
    autofree char *new_verbs = nullptr;
    char *token, *saveptr;
    token = strtok_r(copy, ";", &saveptr);

    while (token) {
        /* Trim whitespace */
        while (*token && isspace(*token))
            token++;
        char *end = token + strlen(token) - 1;
        while (end > token && isspace(*end))
            *end-- = '\0';

        /* Check if this verb should be removed */
        int remove_verb = 0;
        for (int i = 0; i < num_verbs; i++) {
            if (LCSTRING_EQUALS(token, verbs_to_remove[i])) {
                remove_verb = 1;
                break;
            }
        }

        /* If not to be removed, add it to the new list */
        if (!remove_verb)
            append_sep(new_verbs, ";", token);

        token = strtok_r(nullptr, ";", &saveptr);
    }

    if (new_verbs && *new_verbs != '\0') {
        setenv("YAWL_VERBS", new_verbs, 1);
    } else {
        unsetenv("YAWL_VERBS");
        result = MAKE_RESULT(SEV_INFO, CAT_SYSTEM, E_NOT_FOUND);
    }

    return result;
}
