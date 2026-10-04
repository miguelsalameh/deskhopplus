/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

/* The board's copy of Windows' pointer maths (#312, ADR-0017). On a Windows
 * monitor past the main one, with no helper, Windows moves the cursor and the
 * board keeps an estimate of where it went. Windows moves each relative
 * report by counts × its pointer speed's multiplier, in whole pixels, toward
 * zero, and keeps the part-pixel for the next report, so a slow hand still
 * creeps the cursor at a low pointer speed and keeps its full speed at a high
 * one. That is what the hardware showed. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Windows' pointer speed multipliers, steps 1–20, Enhance pointer precision
   off, in 1/32. Step 10 is ×1. The config page reads this list at render. */
#define DH_WINDOWS_POINTER_MULT32 \
    {1, 2, 4, 8, 12, 16, 20, 24, 28, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112}
#define DH_WINDOWS_POINTER_STEPS 20u

/* Config field ids of each output's pointer speed, monitor width and height,
   in that order. The config page reads them from here. */
#define DH_WINDOWS_POINTER_FIELD_A_BASE 164u
#define DH_WINDOWS_POINTER_FIELD_B_BASE 167u

/* True when `step` is a Windows pointer speed, 1–20; 0 is not set. */
bool dh_windows_pointer_speed_is_set(uint8_t step);

/* How far `counts` move the estimate at `position` (board units) on an axis
 * `pixels` long, at Windows pointer speed `step` (1–20), in board units.
 * Monitors repeat past either edge, each 32768 units, so a position or a
 * result past an edge is on the next monitor along, as the real cursor is.
 * `part_pixel` is the part-pixel Windows keeps for this axis, in 1/32 pixel.
 * With no step (0, or out of range) or no pixels saved, it
 * is today's estimate: counts × `speed`. */
int32_t dh_windows_estimate_offset(int32_t position, int32_t counts, uint8_t step,
                                   uint16_t pixels, int32_t speed, int8_t *part_pixel);

/* Whole pixels one count can move Windows' cursor at pointer speed `step`,
 * rounded up, or 0 when `step` is not 1–20. */
int32_t dh_windows_pixels_per_count(uint8_t step);

/* The fewest counts that move Windows' cursor at least `pixels` pixels at
 * pointer speed `step` from no part-pixel (a kept one the other way can cost
 * a pixel), or 0 when `step` is not 1–20. */
int32_t dh_windows_counts_for_pixels(int32_t pixels, uint8_t step);

/* The counts whose move comes nearest `pixels` (signed) at pointer speed
 * `step`, with `part_pixel` (1/32 pixel) already kept, or 0 when `step` is
 * not 1–20. One count can move several pixels, so it can still miss. */
int32_t dh_windows_counts_nearest(int32_t pixels, uint8_t step, int8_t part_pixel);
