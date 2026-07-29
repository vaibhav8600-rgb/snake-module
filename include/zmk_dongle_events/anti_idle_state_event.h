/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

struct zmk_anti_idle_state {
    bool active;
};

ZMK_EVENT_DECLARE(zmk_anti_idle_state);
