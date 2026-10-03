/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

#include "dh_windows_pointer.h"

#include <stdlib.h>

#define UNITS 32768  /* board units per monitor */

static const uint8_t mult32[DH_WINDOWS_POINTER_STEPS] = DH_WINDOWS_POINTER_MULT32;

bool dh_windows_pointer_speed_is_set(uint8_t step) {
    return step >= 1 && step <= DH_WINDOWS_POINTER_STEPS;
}

/* The first board unit of pixel p: ceil(p × UNITS ÷ pixels), so the pixel a
   unit falls in, floor(u × pixels ÷ UNITS), gives p back. */
static int64_t unit_of(int64_t p, uint16_t pixels) {
    const int64_t n = p * UNITS;
    return n >= 0 ? (n + pixels - 1) / pixels : -((-n) / pixels);
}

int32_t dh_windows_estimate_offset(int32_t position, int32_t counts, uint8_t step,
                                   uint16_t pixels, int32_t speed, int8_t *part_pixel) {
    if (!dh_windows_pointer_speed_is_set(step) || pixels == 0)
        return counts * speed;
    /* The pixel `position` is in: floor, also past the near edge. */
    const int64_t n = (int64_t)position * pixels;
    const int64_t pixel = n >= 0 ? n / UNITS : -((-n + UNITS - 1) / UNITS);
    const int64_t moved32 = (int64_t)counts * mult32[step - 1] + *part_pixel;
    /* C division truncates toward zero, as Windows does. */
    const int64_t moved = moved32 / 32;
    *part_pixel = (int8_t)(moved32 - moved * 32);
    if (moved == 0)
        return 0;
    return (int32_t)(unit_of(pixel + moved, pixels) - position);
}

/* Whole pixels `counts` move with `part_pixel` kept, toward zero. */
static int64_t pixels_moved(int64_t counts, int32_t m, int8_t part_pixel) {
    return (counts * m + part_pixel) / 32;
}

int32_t dh_windows_counts_nearest(int32_t pixels, uint8_t step, int8_t part_pixel) {
    if (!dh_windows_pointer_speed_is_set(step))
        return 0;
    const int32_t m = mult32[step - 1];
    const int64_t n = (int64_t)pixels * 32 - part_pixel;
    /* The counts just short of it, by floor, or one more. */
    const int64_t c = n >= 0 ? n / m : -((-n + m - 1) / m);
    const int64_t miss = pixels_moved(c, m, part_pixel) - pixels;
    const int64_t miss_next = pixels_moved(c + 1, m, part_pixel) - pixels;
    return (int32_t)(llabs(miss_next) < llabs(miss) ? c + 1 : c);
}

int32_t dh_windows_counts_for_pixels(int32_t pixels, uint8_t step) {
    if (!dh_windows_pointer_speed_is_set(step))
        return 0;
    const int32_t m = mult32[step - 1];
    return (pixels * 32 + m - 1) / m;
}
