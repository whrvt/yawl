/*
 * Runtime configuration
 *
 * Copyright (C) 2025 William Horvath
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * See the full license text in the repository LICENSE file.
 */

#pragma once

#include "result.hpp"

namespace config {
    RESULT setup_prog_dir(void);
    RESULT setup_config_dir(void);

    /* The global installation path, set at startup in main() */
    extern const char *yawl_dir;
    /* The global configuration path, set at startup in main() */
    extern const char *config_dir;
};
