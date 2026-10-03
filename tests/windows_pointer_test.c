/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

/*
 * The board's copy of Windows' pointer maths (#312, #314): how far one
 * relative report moves the estimate on a Windows monitor the board cannot
 * place, in the board's 0–32767 units. Worked examples on a 1920-pixel axis,
 * where pixel p is board unit ceil(p × 32768 ÷ 1920).
 */
#include <stdio.h>

#include "dh_windows_pointer.h"

static int failures;

static void expect(int32_t position, int32_t counts, uint8_t step, uint16_t pixels,
                   int32_t speed, int32_t want, const char *what) {
    const int32_t got = dh_windows_estimate_offset(position, counts, step, pixels, speed);
    if (got != want) {
        fprintf(stderr, "FAIL %s: got %d, want %d\n", what, (int)got, (int)want);
        failures++;
    }
}

int main(void) {
    /* Pointer speed 10 is ×1: 5 counts are 5 pixels, 0 → pixel 5 (unit 86). */
    expect(0, 5, 10, 1920, 17, 86, "speed 10 moves one pixel per count");
    /* Pointer speed 6 is ×1/2: 3 counts are 1.5 pixels, and Windows drops the half. */
    expect(0, 3, 6, 1920, 17, 18, "speed 6 drops the fraction");
    /* Toward zero, not down: -1.5 pixels is -1. Pixel 100 (unit 1707) → 99 (unit 1690). */
    expect(1707, -3, 6, 1920, 17, -17, "negative counts truncate toward zero");
    /* Speed 13 is ×1.75: -3 counts are -5.25, so -5 pixels. Pixel 100 → 95 (unit 1622). */
    expect(1707, -3, 13, 1920, 17, -85, "speed 13 negative");
    /* Speed 1 is ×1/32: 31 counts do not move a pixel, 32 do. */
    expect(0, 31, 1, 1920, 17, 0, "too few counts move nothing");
    expect(0, 32, 1, 1920, 17, 18, "32 counts at speed 1 move one pixel");
    /* No remainder carries over: ten 1-count reports at ×1/2 move nothing. */
    int32_t position = 0;
    for (int i = 0; i < 10; i++)
        position += dh_windows_estimate_offset(position, 1, 6, 1920, 17);
    if (position != 0) {
        fprintf(stderr, "FAIL no remainder is kept: estimate moved to %d\n", (int)position);
        failures++;
    }
    /* A move too small for a pixel leaves an estimate between pixels alone. */
    expect(1700, 1, 6, 1920, 17, 0, "no pixel moved, no offset");
    /* A position between pixels is the pixel it is in: unit 1700 is pixel 99. */
    expect(1700, 1, 10, 1920, 17, 7, "off-grid position starts from its pixel");
    /* Past the far edge: pixel 1919 + 6 = 1925, which is pixel 5 of the next
       monitor; the overshoot is unit 32854 = 32768 + 86. */
    expect(32767, 3, 14, 1920, 17, 87, "past the edge keeps the overshoot");
    /* Past the near edge: pixel 0 - 2 = pixel 1918 of the monitor before. */
    expect(0, -2, 10, 1920, 17, -34, "past the near edge");
    /* Nothing saved, or a bad value: today's estimate, counts × Speed. */
    expect(100, 3, 0, 1920, 17, 51, "no pointer speed saved");
    expect(100, 3, 21, 1920, 17, 51, "pointer speed out of range");
    expect(100, -3, 10, 0, 30, -90, "no monitor size saved");
    /* A 3840-pixel axis: a pixel is less than 9 units. */
    expect(0, 1, 10, 3840, 1, 9, "4K: one pixel is 8.53 units, unit 9");
    /* Further past the near edge (a walk across monitors): pixel -2 is
       still pixel 1918 of the monitor before, so one more pixel left is -51. */
    expect(-34, -1, 10, 1920, 17, -17, "a position past the near edge is its pixel");
    /* The fewest counts that move at least so many pixels; 0 with no step. */
    static const struct {int32_t pixels; uint8_t step; int32_t counts;} need[] = {
        {1, 1, 32}, {1, 10, 1}, {1, 20, 1}, {1080, 1, 34560}, {10, 13, 6}, {8, 20, 3}, {1, 0, 0}, {1, 21, 0},
    };
    for (size_t i = 0; i < sizeof need / sizeof need[0]; i++) {
        const int32_t got = dh_windows_counts_for_pixels(need[i].pixels, need[i].step);
        if (got != need[i].counts) {
            fprintf(stderr, "FAIL %d pixels at speed %d: got %d counts, want %d\n",
                    (int)need[i].pixels, need[i].step, (int)got, (int)need[i].counts);
            failures++;
        }
    }
    if (!failures)
        printf("windows_pointer_test: ok\n");
    return failures ? 1 : 0;
}
