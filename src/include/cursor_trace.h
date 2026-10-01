/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dh_cursor_trace.h"

void cursor_trace_boot(bool config_mode);
void cursor_trace_event(const device_t *state, dh_cursor_trace_event_t event,
                        uint8_t query_id, int16_t move_x, int16_t move_y,
                        uint8_t direction, uint8_t transition);
/* One Sleep sync step, stamped with the caller's clock as seconds since
   boot (#296). */
void sleep_sync_trace(const device_t *state, dh_sleep_sync_trace_t what, int16_t value,
                      uint64_t now_us);
size_t cursor_trace_count(void);
bool cursor_trace_read(size_t index, dh_cursor_trace_record_t *record);
