/*
 * Special apparmor handling
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

#include "apparmor.h"
#include "bwrap_data.h"
#include "log.h"
#include "util.h"

#define APPARMOR_DIR "/etc/apparmor.d"
#define APPARMOR_PROFILE_NAME "bwrap-userns-restrict-" PROG_NAME
#define APPARMOR_PROFILE_PATH APPARMOR_DIR "/" APPARMOR_PROFILE_NAME

/* Test if the container works by running a simple command inside it */
static RESULT test_container(const char *entry_point) {
    char *test_cmd = NULL;
    char *stdout_file = NULL;
    char *stderr_file = NULL;
    FILE *stderr_fp = NULL;
    int ret = 0;
    int apparmor_issue = 0;
    char error_buf[BUFFER_SIZE] = {0};

    join_paths(stdout_file, g_yawl_dir, "test_stdout.tmp");
    join_paths(stderr_file, g_yawl_dir, "test_stderr.tmp");

    append_sep(test_cmd, " ", entry_point, "--verb=waitforexitandrun", "--", "/bin/true", ">", stdout_file, "2>",
               stderr_file);

    LOG_DEBUG("Testing container with command: %s", test_cmd);

    /* Run the test */
    ret = system(test_cmd);

    /* Check stderr for AppArmor issues */
    stderr_fp = fopen(stderr_file, "r");
    if (stderr_fp) {
        while (fgets(error_buf, sizeof(error_buf), stderr_fp)) {
            if (strstr(error_buf, "bwrap") && strstr(error_buf, "Permission denied")) {
                apparmor_issue = 1;
                LOG_DEBUG("Found AppArmor issue in stderr: %s", error_buf);
                break;
            }
        }
        fclose(stderr_fp);
    }

    /* Clean up temporary files */
    unlink(stdout_file);
    unlink(stderr_file);

    free(test_cmd);
    free(stdout_file);
    free(stderr_file);

    if (ret != 0)
        LOG_WARNING("Container test exited with code %d", WEXITSTATUS(ret));

    if (apparmor_issue)
        LOG_DEBUG("AppArmor restriction detected");

    if (ret != 0 || apparmor_issue)
        return MAKE_RESULT(SEV_ERROR, CAT_APPARMOR, E_ACCESS_DENIED);

    return RESULT_OK;
}

/* Write the AppArmor profile to a temporary file */
static RESULT write_temp_apparmor_profile(char **temp_path) {
    FILE *fp = NULL;

    /* Create a temporary file in the yawl directory */
    join_paths(*temp_path, g_yawl_dir, APPARMOR_PROFILE_NAME ".tmp");
    RETURN_NULL_CHECK(*temp_path, "Failed to allocate memory for temporary AppArmor profile path");

    LOG_DEBUG("Writing temporary AppArmor profile to: %s", *temp_path);

    fp = fopen(*temp_path, "wb");
    if (!fp) {
        RESULT result = result_from_errno();
        LOG_RESULT(LOG_ERROR, result, "Failed to create temporary AppArmor profile");
        return result;
    }

    if (fwrite(bwrap_userns_restrict, 1, bwrap_userns_restrict_len, fp) != bwrap_userns_restrict_len) {
        RESULT result = result_from_errno();
        LOG_RESULT(LOG_ERROR, result, "Failed to write AppArmor profile data");
        fclose(fp);
        unlink(*temp_path);
        return result;
    }

    fclose(fp);
    return RESULT_OK;
}

/* Install the AppArmor profile using pkexec */
static RESULT install_apparmor_profile(void) {
    struct stat st;
    char *temp_profile_path = NULL;
    char *install_cmd = NULL;
    int ret = 0;
    RESULT result = RESULT_OK;

    /* Write the profile to a temporary file */
    if (stat(APPARMOR_PROFILE_PATH, &st) != 0) {
        result = write_temp_apparmor_profile(&temp_profile_path);
        if (FAILED(result)) {
            free(temp_profile_path);
            return result;
        }
    }

    LOG_INFO("Installing AppArmor profile to enable container functionality...");

    /* These messages are interactive prompts for the user, so we keep them as console output */
    /* But we also log them to maintain a complete log record */

    /* Display important user instructions to the console */
    printf("Please enter your password when prompted. This just installs a file to\n");
    printf("    /etc/apparmor.d/, which gives enough permissions to the pressure-vessel container\n");
    printf("    to function properly. If you don't trust me, follow this guide to install it manually:\n");
    printf("    https://github.com/ocaml/opam/issues/5968#issuecomment-2151748424\n");

    /* Create the command to install the profile */
    append_sep(install_cmd, " ", "pkexec", "sh", "-c", "'mkdir -p " APPARMOR_DIR " && cp", temp_profile_path,
               APPARMOR_PROFILE_PATH " && chmod 644 " APPARMOR_PROFILE_PATH " && "
                                     "apparmor_parser -r -W " APPARMOR_PROFILE_PATH "'");

    LOG_DEBUG("Running installation command: %s", install_cmd);

    ret = system(install_cmd);
    if (ret != 0)
        result = MAKE_RESULT(SEV_ERROR, CAT_APPARMOR, E_ACCESS_DENIED);
    else
        result = RESULT_OK;

    unlink(temp_profile_path);
    free(temp_profile_path);
    free(install_cmd);

    return result;
}

RESULT handle_apparmor(const char *entry_point) {
    RESULT result;

    LOG_DEBUG("Testing container functionality with entry point: %s", entry_point);

    result = test_container(entry_point);
    if (SUCCEEDED(result)) {
        LOG_DEBUG("Container test passed, no AppArmor issues detected");
        return RESULT_OK;
    }

    LOG_INFO("Detected AppArmor restrictions preventing container operation");
    /* Try to install the AppArmor profile */
    result = install_apparmor_profile();
    if (FAILED(result)) {
        LOG_RESULT(LOG_WARNING, "Failed to install AppArmor Profile", result);

        printf("Warning: Failed to install AppArmor profile. Container may not work correctly.\n");
        printf("Please follow this guide to manually install the AppArmor profile:\n");
        printf("   https://github.com/ocaml/opam/issues/5968#issuecomment-2151748424\n");
        return result;
    }

    /* Test the container again after installing the profile */
    LOG_DEBUG("Testing container again after AppArmor profile installation");
    result = test_container(entry_point);
    if (FAILED(result)) {
        LOG_RESULT(LOG_WARNING, "Container still not working after AppArmor profile installation", result);

        /* Keep console output for user feedback */
        printf("Warning: Container still not working after AppArmor profile installation.\n");
        printf("You may need to restart the system for AppArmor changes to take effect.\n");
        return result;
    }

    LOG_INFO("AppArmor profile installation successful, container now working");
    return RESULT_OK;
}
