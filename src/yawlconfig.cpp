/*
 * Runtime configuration
 *
 * Copyright (C) 2025 William Horvath
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * See the full license text in the repository LICENSE file.
 */

#include <cassert>
#include <memory>

#include "fmt/base.h"
#include "fmt/compile.h"

#include "yawlconfig.hpp"
#include "util.hpp"
#include "macros.hpp"

#pragma GCC diagnostic ignored "-Wunused-variable"

namespace config {
using namespace fmt::literals;

static const fmt::string_view RUNTIME_PREFIX = "SteamLinuxRuntime_";
static const fmt::string_view RUNTIME_VERSION = "sniper";
static const fmt::string_view RUNTIME_ARCHIVE_NAME =
    fmt::format("{},{},{}"_cf, RUNTIME_PREFIX, RUNTIME_VERSION, ".tar.xz");
static const fmt::string_view RUNTIME_BASE_URL =
    fmt::format("https://repo.steampowered.com/steamrt-images-{}/snapshots/latest-container-runtime-public-beta"_cf,
                RUNTIME_VERSION);
static const fmt::string_view DEFAULT_EXEC_PATH = "/usr/bin/wine";
static const fmt::string_view CONFIG_EXTENSION = ".cfg";

static std::unique_ptr<std::string> s_yawl_dir = nullptr;
static std::unique_ptr<std::string> s_config_dir = nullptr;

const char *yawl_dir = nullptr;
const char *config_dir = nullptr;

RESULT setup_prog_dir(void) {
    autofree char *result = nullptr;
    struct passwd *pw;

    const char *temp_dir = getenv("YAWL_INSTALL_DIR");
    if (temp_dir)
        result = expand_path(temp_dir);
    else if ((temp_dir = getenv("XDG_DATA_HOME")))
        join_paths(result, temp_dir, PROG_NAME);
    else if ((temp_dir = getenv("HOME")) || ((pw = getpwuid(getuid())) && (temp_dir = pw->pw_dir)))
        join_paths(result, temp_dir, ".local/share/" PROG_NAME);

    RESULT ensure_result = ensure_dir(result);
    if (FAILED(ensure_result)) {
        fprintf(stderr, "Error: Failed to create or access program directory: %s\n", result_to_string(ensure_result));
        if (result)
            fprintf(stderr, "Attempted directory: %s\n", result);
        return RESULT_FAIL;
    }

    s_yawl_dir = std::make_unique<std::string>(result);
    if (s_yawl_dir->empty())
        return RESULT_FAIL;

    yawl_dir = s_yawl_dir->c_str();

    return RESULT_OK;
}

RESULT setup_config_dir(void) {
    autofree char *result = nullptr;
    assert(!!yawl_dir);
    join_paths(result, yawl_dir, CONFIG_DIR);

    RESULT ensure_result = ensure_dir(result);
    if (FAILED(ensure_result)) {
        fprintf(stderr, "Error: Failed to create or access config directory: %s\n", result_to_string(ensure_result));
        if (result)
            fprintf(stderr, "Attempted directory: %s\n", result);
        return RESULT_FAIL;
    }

    s_config_dir = std::make_unique<std::string>(result);
    if (s_config_dir->empty())
        return RESULT_FAIL;

    config_dir = s_config_dir->c_str();

    return RESULT_OK;
}
}; // namespace config
