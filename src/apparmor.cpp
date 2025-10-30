/*
 * Special apparmor handling
 *
 * Copyright (C) 2025 William Horvath
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * See the full license text in the repository LICENSE file.
 */

#include <cstdio>
#include <cstring>

#include "apparmor.hpp"
#include "log.hpp"
#include "macros.hpp"
#include "util.hpp"
#include "yawlconfig.hpp"

#define APPARMOR_DIR "/etc/apparmor.d"
#define APPARMOR_PROFILE_NAME "bwrap-userns-restrict-" PROG_NAME
#define APPARMOR_PROFILE_PATH APPARMOR_DIR "/" APPARMOR_PROFILE_NAME

/* Test if the container works by running a simple command inside it */
static RESULT test_container(const char *entry_point) {
    autofree char *test_cmd = nullptr;
    autofree_del char *stdout_file = nullptr;
    autofree_del char *stderr_file = nullptr;
    autoclose FILE *stderr_fp = nullptr;
    char error_buf[BUFFER_SIZE] = {};
    int ret = 0;
    int apparmor_issue = 0;

    join_paths(stdout_file, config::yawl_dir, "test_stdout.tmp");
    join_paths(stderr_file, config::yawl_dir, "test_stderr.tmp");

    append_sep(test_cmd, " ", entry_point, "--verb=waitforexitandrun", "--", "/bin/true", ">", stdout_file, "2>",
               stderr_file);

    LOG_DEBUG("Testing container with command: %s", test_cmd);

    /* Run the test */
#pragma message "FIXME: don't use system(3)"
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
    }

    if (apparmor_issue) {
        LOG_DEBUG("AppArmor restriction detected");
        return MAKE_RESULT(SEV_ERROR, CAT_APPARMOR, E_ACCESS_DENIED);
    } else if (ret != 0) {
        LOG_WARNING("Container test exited with code %d", WEXITSTATUS(ret));
        return MAKE_RESULT(SEV_ERROR, CAT_APPARMOR, E_UNKNOWN);
    }

    return RESULT_OK;
}

/*
 * Data from the /usr/share/apparmor/extra-profiles/bwrap-userns-restrict file from
 * http://ftp.us.debian.org/debian/pool/main/a/apparmor/apparmor-profiles_4.1.0~beta5-3_all.deb
 *
 * Allows pressure-vessel to run with AppArmor enabled, i.e. on Ubuntu 24+ or Debian
 * umu-launcher also installs this as part of its .deb package
 */
static constexpr const unsigned char bwrap_userns_restrict[] = {
#embed "../assets/external/bwrap-userns-restrict"
};

static RESULT write_temp_apparmor_profile(char *temp_path[]) {
    autoclose FILE *fp = nullptr;

    /* Create a temporary file in the yawl directory */
    join_paths(*temp_path, config::yawl_dir, APPARMOR_PROFILE_NAME ".tmp");

    LOG_DEBUG("Writing temporary AppArmor profile to: %s", *temp_path);

    fp = fopen(*temp_path, "wb");
    if (!fp) {
        RESULT result = result_from_errno();
        LOG_RESULT(Level::Error, result, "Failed to create temporary AppArmor profile");
        return result;
    }

    if (fwrite(bwrap_userns_restrict, 1, sizeof(bwrap_userns_restrict), fp) != sizeof(bwrap_userns_restrict)) {
        RESULT result = result_from_errno();
        LOG_RESULT(Level::Error, result, "Failed to write AppArmor profile data");
        unlink(*temp_path);
        return result;
    }

    return RESULT_OK;
}

/* Install the AppArmor profile using pkexec */
static RESULT install_apparmor_profile(void) {
    autofree_del char *temp_profile_path = nullptr;
    autofree char *install_cmd = nullptr;
    struct stat st;
    int ret = 0;
    RESULT result = RESULT_OK;

    /* Write the profile to a temporary file */
    if (stat(APPARMOR_PROFILE_PATH, &st) != 0) {
        result = write_temp_apparmor_profile(&temp_profile_path);
        if (FAILED(result))
            return result;
    }

    LOG_INFO("Installing AppArmor profile to enable container functionality...");
    LOG_SYSTEM("Please enter your password when prompted.\nThis just installs a file to "
               "/etc/apparmor.d/, which gives enough permissions to the pressure-vessel container "
               "to function properly.\nIf you don't trust me, follow this guide to install it manually:\n"
               "https://github.com/ocaml/opam/issues/5968#issuecomment-2151748424");

    /* Create the command to install the profile */
    append_sep(install_cmd, " ", "pkexec", "sh", "-c", "'mkdir -p " APPARMOR_DIR " && cp", temp_profile_path,
               APPARMOR_PROFILE_PATH " && chmod 644 " APPARMOR_PROFILE_PATH " && "
                                     "apparmor_parser -r -W " APPARMOR_PROFILE_PATH "'");

    LOG_DEBUG("Running installation command: %s", install_cmd);

#pragma message "FIXME: don't use system(3)"
    ret = system(install_cmd);
    if (ret != 0)
        result = MAKE_RESULT(SEV_ERROR, CAT_APPARMOR, E_ACCESS_DENIED);
    else
        result = RESULT_OK;

    return result;
}

RESULT handle_apparmor(const char *entry_point) {
    RESULT result;

    LOG_DEBUG("Testing container functionality with entry point: %s", entry_point);

    result = test_container(entry_point);
    if (SUCCEEDED(result)) {
        LOG_DEBUG("Container test passed, no AppArmor issues detected");
        return RESULT_OK;
    } else if (RESULT_CODE(result) == E_UNKNOWN) {
        LOG_DEBUG("Container test returned an error unrelated to AppArmor, returning OK");
        return RESULT_OK;
    }

    LOG_INFO("Detected AppArmor restrictions preventing container operation");
    /* Try to install the AppArmor profile */
    result = install_apparmor_profile();
    if (FAILED(result)) {
        LOG_RESULT(Level::Debug, result, "Failed to install AppArmor profile");

        LOG_SYSTEM("Failed to install AppArmor profile. Container may not work correctly.\n"
                   "Please follow this guide to manually install the AppArmor profile:\n"
                   "https://github.com/ocaml/opam/issues/5968#issuecomment-2151748424");
        return result;
    }

    /* Test the container again after installing the profile */
    LOG_DEBUG("Testing container again after AppArmor profile installation");
    result = test_container(entry_point);
    if (FAILED(result)) {
        LOG_RESULT(Level::Debug, result, "Container still not working after AppArmor profile installation");

        LOG_SYSTEM("Container still not working after AppArmor profile installation.\n"
                   "You may need to restart the system for AppArmor changes to take effect.");
        return result;
    }

    LOG_INFO("AppArmor profile installation successful, container now working");
    return RESULT_OK;
}
