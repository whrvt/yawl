/*
 * Self-update functionality
 *
 * Copyright (C) 2025 William Horvath
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * See the full license text in the repository LICENSE file.
 */

#pragma once

#include "result.hpp"



/* (private for now) Check if a new version is available and print information about it
 * Returns RESULT_OK if no update is available or if the update check was successful
 * Returns error RESULT on failure
 */
/* RESULT check_for_updates(void); */

/* (private for now) Download and apply available update
 * Returns RESULT_OK on success, error RESULT on failure
 */
/* RESULT perform_update(void); */

/* Handle update operations based on command line options
 * check_only: 1 = just check for updates, 0 = don't check
 * do_update: 1 = check and apply updates, 0 = don't update
 * Returns RESULT_OK on success, error RESULT on failure
 */
RESULT handle_updates(int check_only, int do_update);


