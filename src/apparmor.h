/*
 * Special apparmor handling
 *
 * Copyright (C) 2025 William Horvath
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * See the full license text in the repository LICENSE file.
 */

#pragma once

#include "result.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Handle AppArmor configuration if needed (usually Ubuntu/Debian distros) */
RESULT handle_apparmor(const char *entry_point);

#ifdef __cplusplus
}
#endif
