/*
 * Simple SLR bootstrapper program
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

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <archive.h>
#include <pwd.h>

#define PROG_NAME "yawl"

#define RUNTIME_VERSION "sniper"
#define RUNTIME_BASE_URL "https://repo.steampowered.com/steamrt-images-" RUNTIME_VERSION "/snapshots/latest-container-runtime-public-beta"
#define RUNTIME_ARCHIVE "SteamLinuxRuntime_" RUNTIME_VERSION ".tar.xz"
#define BUFFER_SIZE 8192

static char *get_home_dir(void)
{
    const char *home = getenv( "HOME" );
    if (home)
        return strdup( home );

    struct passwd *pw = getpwuid( getuid() );
    return pw ? strdup( pw->pw_dir ) : NULL;
}

static char *join_path( const char *a, const char *b )
{
    char *result;
    asprintf( &result, "%s/%s", a, b );
    return result;
}

static int ensure_dir( const char *path )
{
    struct stat st;
    if (stat( path, &st ) == 0)
    {
        return S_ISDIR( st.st_mode ) ? 0 : -1;
    }
    return mkdir( path, 0755 );
}

static int download_file( const char *url, const char *output_path )
{
    CURL *curl = curl_easy_init();
    if (!curl)
        return -1;

    FILE *fp = fopen( output_path, "wb" );
    if (!fp)
    {
        curl_easy_cleanup( curl );
        return -1;
    }

    curl_easy_setopt( curl, CURLOPT_URL, url );
    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, NULL );
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, fp );
    curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 1L );
    curl_easy_setopt( curl, CURLOPT_SSL_VERIFYPEER, 1L );

    CURLcode res = curl_easy_perform( curl );

    fclose( fp );
    curl_easy_cleanup( curl );

    return ( res == CURLE_OK ) ? 0 : -1;
}

static int extract_archive( const char *archive_path, const char *extract_path )
{
    struct archive *a;
    struct archive *ext;
    struct archive_entry *entry;
    int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL |
                ARCHIVE_EXTRACT_FFLAGS | ARCHIVE_EXTRACT_OWNER;
    int r;

    a = archive_read_new();
    archive_read_support_format_tar( a );
    archive_read_support_filter_xz( a );

    ext = archive_write_disk_new();
    archive_write_disk_set_options( ext, flags );
    archive_write_disk_set_standard_lookup( ext );

    if (( r = archive_read_open_filename( a, archive_path, BUFFER_SIZE ) ))
    {
        return -1;
    }

    char *old_cwd = getcwd( NULL, 0 );
    if (chdir( extract_path ) != 0)
    {
        free( old_cwd );
        return -1;
    }

    while (archive_read_next_header( a, &entry ) == ARCHIVE_OK)
    {
        r = archive_write_header( ext, entry );
        if (r != ARCHIVE_OK)
            continue;

        const void *buff;
        size_t size;
        int64_t offset;

        while (archive_read_data_block( a, &buff, &size, &offset ) == ARCHIVE_OK)
        {
            if (archive_write_data_block( ext, buff, size, offset ) != ARCHIVE_OK)
            {
                break;
            }
        }
    }

    chdir( old_cwd );
    free( old_cwd );

    archive_read_close( a );
    archive_read_free( a );
    archive_write_close( ext );
    archive_write_free( ext );

    return 0;
}

static int setup_runtime(void)
{
    char *home = get_home_dir();
    if (!home)
        return -1;

    char *runtime_dir = join_path( home, ".local/share/" PROG_NAME );
    char *archive_path = join_path( runtime_dir, RUNTIME_ARCHIVE );
    char *runtime_path = join_path( runtime_dir, "SteamLinuxRuntime_" RUNTIME_VERSION );

    ensure_dir( runtime_dir );

    struct stat st;
    if (stat( runtime_path, &st ) == 0 && S_ISDIR( st.st_mode ))
    {
        free( runtime_dir );
        free( archive_path );
        free( runtime_path );
        free( home );
        return 0;
    }

    char runtime_url[512];
    snprintf( runtime_url, sizeof( runtime_url ), "%s/%s", RUNTIME_BASE_URL, RUNTIME_ARCHIVE );

    fprintf( stderr, "Downloading Steam Runtime (%s)...\n", RUNTIME_VERSION );
    if (download_file( runtime_url, archive_path ) != 0)
    {
        fprintf( stderr, "Failed to download runtime\n" );
        return -1;
    }

    fprintf( stderr, "Extracting runtime...\n" );
    if (extract_archive( archive_path, runtime_dir ) != 0)
    {
        fprintf( stderr, "Failed to extract runtime\n" );
        unlink( archive_path );
        return -1;
    }

    unlink( archive_path );
    free( runtime_dir );
    free( archive_path );
    free( runtime_path );
    free( home );

    return 0;
}

static char *build_library_paths( const char *wine_path )
{
    const char *system_paths[] = { "/lib", "/lib64", "/usr/lib", "/usr/lib64", NULL };

    char *lib64_path, *lib_path;
    asprintf( &lib64_path, "%s/lib64", wine_path );
    asprintf( &lib_path, "%s/lib", wine_path );

    size_t total_len = strlen( lib64_path ) + strlen( lib_path ) + 2;
    for (const char **path = system_paths; *path; path++)
    {
        if (access( *path, F_OK ) == 0)
        {
            total_len += strlen( *path ) + 1; /* +1 for : */
        }
    }

    const char *orig_path = getenv( "LD_LIBRARY_PATH" );
    if (orig_path)
    {
        total_len += strlen( orig_path ) + 1;
    }

    char *result = malloc( total_len );
    snprintf( result, total_len, "%s:%s", lib64_path, lib_path );

    for (const char **path = system_paths; *path; path++)
    {
        if (access( *path, F_OK ) == 0)
        {
            char *temp = result;
            asprintf( &result, "%s:%s", temp, *path );
            free( temp );
        }
    }

    if (orig_path)
    {
        char *temp = result;
        asprintf( &result, "%s:%s", temp, orig_path );
        free( temp );
    }

    free( lib64_path );
    free( lib_path );
    return result;
}

/* required for ancient Debian/Ubuntu */
static char *build_mesa_paths(void)
{
    const char *mesa_paths[] = { "/usr/lib/i386-linux-gnu/dri", "/usr/lib/x86_64-linux-gnu/dri",
                                 "/usr/lib/dri", "/usr/lib64/dri", NULL };

    size_t total_len = 1;
    for (const char **path = mesa_paths; *path; path++)
    {
        if (access( *path, F_OK ) == 0)
        {
            total_len += strlen( *path ) + 1;
        }
    }

    if (total_len == 1)
    {
        return NULL;
    }

    char *result = malloc( total_len );
    result[0] = '\0';

    for (const char **path = mesa_paths; *path; path++)
    {
        if (access( *path, F_OK ) == 0)
        {
            if (result[0] != '\0')
            {
                char *temp = result;
                asprintf( &result, "%s:%s", temp, *path );
                free( temp );
            }
            else
            {
                strcpy( result, *path );
            }
        }
    }

    return result;
}

static char *find_wine_binary( const char *wine_path )
{
    const char *binaries[] = { "wine64", "wine", NULL };
    char *result = NULL;

    for (const char **bin = binaries; *bin; bin++)
    {
        char *path;
        asprintf( &path, "%s/bin/%s", wine_path, *bin );

        if (access( path, X_OK ) == 0)
        {
            result = path;
            break;
        }
        free( path );
    }

    return result;
}

int main( int argc, char *argv[] )
{
    if (geteuid() == 0)
    {
        fprintf( stderr, "Error: This program should not be run as root\n" );
        return 1;
    }

    if (setup_runtime() != 0)
    {
        return 1;
    }

    const char *wine_path = getenv( "WINE_PATH" );
    if (!wine_path)
        wine_path = "/usr";

    const char *prefix = getenv( "WINEPREFIX" );
    if (!prefix)
    {
        char *home = get_home_dir();
        if (!home)
            return 1;
        char *default_prefix;
        asprintf( &default_prefix, "%s/.wine", home );
        setenv( "WINEPREFIX", default_prefix, 1 );
        free( default_prefix );
        free( home );
    }

    char *wine_bin = find_wine_binary( wine_path );
    if (!wine_bin)
    {
        fprintf( stderr, "Error: No wine binary found in %s/bin\n", wine_path );
        return 1;
    }

    char *home = get_home_dir();
    if (!home)
        return 1;

    char *entry_point;
    asprintf( &entry_point, "%s/.local/share/" PROG_NAME "/SteamLinuxRuntime_%s/_v2-entry-point",
              home, RUNTIME_VERSION );

    if (access( entry_point, X_OK ) != 0)
    {
        fprintf( stderr, "Error: Runtime entry point not found: %s\n", entry_point );
        free( entry_point );
        free( wine_bin );
        free( home );
        return 1;
    }

    char **new_argv = calloc( argc + 4, sizeof( char * ) );
    new_argv[0] = entry_point;
    new_argv[1] = "--verb=waitforexitandrun";
    new_argv[2] = "--";
    new_argv[3] = wine_bin;

    for (int i = 1; i < argc; i++)
    {
        new_argv[i + 3] = argv[i];
    }

    char *lib_paths = build_library_paths( wine_path );
    if (lib_paths)
    {
        setenv( "LD_LIBRARY_PATH", lib_paths, 1 );

        if (!getenv( "ORIG_LD_LIBRARY_PATH" ))
        {
            const char *orig_path = getenv( "LD_LIBRARY_PATH" );
            if (orig_path)
            {
                setenv( "ORIG_LD_LIBRARY_PATH", orig_path, 1 );
            }
        }
        free( lib_paths );
    }

    char *mesa_paths = build_mesa_paths();
    if (mesa_paths)
    {
        setenv( "LIBGL_DRIVERS_PATH", mesa_paths, 1 );
        free( mesa_paths );
    }

    execv( entry_point, new_argv );

    perror( "Failed to execute runtime" );

    free( entry_point );
    free( wine_bin );
    free( home );
    free( new_argv );

    return 1;
}
