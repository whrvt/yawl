/*
 * Runtime configuration
 *
 * Copyright (C) 2025 William Horvath
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * See the full license text in the repository LICENSE file.
 */

#include "config.h"

#include <cassert>

#include "yawlconfig.hpp"
#include "util.hpp"

#include "fmt/compile.h"
#include "fmt/printf.h"

#pragma GCC diagnostic ignored "-Wunused-variable"

namespace config {
using namespace fmt::literals;

const fmt::string_view RUNTIME_PREFIX = "SteamLinuxRuntime_";
const fmt::string_view RUNTIME_VERSION = "sniper";
const fmt::string_view RUNTIME_ARCHIVE_NAME =
    fmt::format("{},{},{}"_cf, RUNTIME_PREFIX, RUNTIME_VERSION, ".tar.xz");
const fmt::string_view RUNTIME_BASE_URL =
    fmt::format("https://repo.steampowered.com/steamrt-images-{}/snapshots/latest-container-runtime-public-beta"_cf,
                RUNTIME_VERSION);
const fmt::string_view DEFAULT_EXEC_PATH = "/usr/bin/wine";
const fmt::string_view CONFIG_EXTENSION = ".cfg";

static std::string s_yawl_dir;
static std::string s_config_dir;
const char *yawl_dir = nullptr;
const char *config_dir = nullptr;

RESULT setup_prog_dir(void) {
    struct passwd *pw;
    std::string result = {};
    const char *temp_path = getenv("YAWL_INSTALL_DIR");

    if (temp_path)
        result = expand_path(temp_path);
    else if ((temp_path = getenv("XDG_DATA_HOME")))
        result = fmt::format("{}/{}", temp_path, PROG_NAME);
    else if ((temp_path = getenv("HOME")) || ((pw = getpwuid(getuid())) && (temp_path = pw->pw_dir)))
        result = fmt::format("{}/.local/share/{}", temp_path, PROG_NAME);

    RESULT ensure_result = ensure_dir(result.c_str());
    if (FAILED(ensure_result)) {
        fmt::fprintf(stderr, "Error: Failed to create or access program directory: %s\n", result_to_string(ensure_result));
        if (!result.empty())
            fmt::fprintf(stderr, "Attempted directory: %s\n", result);
        return RESULT_FAIL;
    }

    if (result.empty())
        return RESULT_FAIL;

    s_yawl_dir = std::move(result);
    yawl_dir = s_yawl_dir.c_str();

    return RESULT_OK;
}

RESULT setup_config_dir(void) {
    assert(!!yawl_dir);
    std::string result = fmt::format("{}/{}", yawl_dir, CONFIG_DIR);

    RESULT ensure_result = ensure_dir(result.c_str());
    if (FAILED(ensure_result)) {
        fmt::fprintf(stderr, "Error: Failed to create or access config directory: %s\n", result_to_string(ensure_result));
        if (!result.empty())
            fmt::fprintf(stderr, "Attempted directory: %s\n", result);
        return RESULT_FAIL;
    }

    if (result.empty())
        return RESULT_FAIL;

    s_config_dir = std::move(result);
    config_dir = s_config_dir.c_str();

    return RESULT_OK;
}
}; // namespace config
