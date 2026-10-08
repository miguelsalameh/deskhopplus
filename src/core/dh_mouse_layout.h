/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

/* Pure mouse-layout decisions shared by firmware and host tests (#24). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DH_DIRECTION_NONE = 0,
    DH_DIRECTION_LEFT = 1,
    DH_DIRECTION_RIGHT = 2,
    DH_DIRECTION_TOP = 4,
    DH_DIRECTION_BOTTOM = 5,
} dh_direction_t;

/* Which way a monitor's line runs out of main, relative to the chain
   direction (CONTEXT.md: Turn). Zero is along the chain, so a layout with no
   turns is today's straight line. */
typedef enum {
    DH_MOUSE_TURN_NONE = 0,
    DH_MOUSE_TURN_CLOCKWISE = 1,
    DH_MOUSE_TURN_OPPOSITE = 2,
    DH_MOUSE_TURN_COUNTER_CLOCKWISE = 3,
} dh_mouse_turn_t;

/* Monitors 2 to 5 can turn; later ones run along the chain. */
#define DH_MOUSE_TURN_FIRST_SCREEN 2u
#define DH_MOUSE_TURN_LAST_SCREEN 5u
#define DH_MOUSE_TURN(screen, turn) \
    ((uint8_t)((unsigned)(turn) << (((screen) - DH_MOUSE_TURN_FIRST_SCREEN) * 2u)))

/* Each monitor sits on a straight line out of main (CONTEXT.md: Line): its
   line runs the way its turn says, and it is the next one out along that
   line. Monitors on different lines meet only at main. */
typedef struct {
    dh_direction_t chain_direction;
    dh_direction_t border_direction;
    uint8_t turns; /* DH_MOUSE_TURN() per monitor, ORed together */
} dh_mouse_layout_t;

typedef struct {
    int32_t x;
    int32_t y;
} dh_mouse_coordinates_t;

typedef enum {
    DH_MOUSE_TRANSITION_NONE = 0,
    DH_MOUSE_TRANSITION_OUTPUT,
    DH_MOUSE_TRANSITION_CHAIN_BACK,
    DH_MOUSE_TRANSITION_CHAIN_FORWARD,
} dh_mouse_transition_t;

dh_direction_t dh_opposite_direction(dh_direction_t direction);
bool dh_direction_is_vertical(dh_direction_t direction);
bool dh_mouse_reports_are_relative(bool relative_mouse, bool gaming_mode);
int32_t dh_mouse_along_seam(dh_direction_t border_direction,
                            dh_mouse_coordinates_t pointer);

dh_mouse_transition_t dh_mouse_transition_for(
    const dh_mouse_layout_t *layout,
    uint32_t screen_index,
    uint32_t screen_count,
    dh_direction_t movement);

/* The direction monitor `screen`'s line runs out of main; NONE for main. */
dh_direction_t dh_mouse_monitor_line(const dh_mouse_layout_t *layout, uint32_t screen);

/* The monitor next to `screen` in `movement`, or 0 where there is none. */
uint32_t dh_mouse_neighbour(
    const dh_mouse_layout_t *layout,
    uint32_t screen_index,
    uint32_t screen_count,
    dh_direction_t movement);

/* The most steps a route takes: back down one line to main, out along
   another, on a computer of at most seven monitors. */
#define DH_MOUSE_ROUTE_CAPACITY 12u

/* The directions that walk the cursor from monitor `from` to `to`, one
   monitor per step, through main when they are on different lines. Returns
   the number of steps: 0 when they are the same monitor or either is not
   one of the computer's. */
uint8_t dh_mouse_route(
    const dh_mouse_layout_t *layout,
    uint32_t screen_count,
    uint32_t from,
    uint32_t to,
    dh_direction_t steps[DH_MOUSE_ROUTE_CAPACITY]);

/* A helper finds a display by stepping out of main in one direction
   (dh_place_target), so a monitor is named to it by its own `line`, and
   `index` 1 for main, 2 for the next display out, and so on. Main is named
   along the chain. False for a monitor the computer does not have. */
bool dh_mouse_helper_line(
    const dh_mouse_layout_t *layout,
    uint32_t screen_count,
    uint32_t screen,
    dh_direction_t *line,
    uint8_t *index);

/* The monitor a helper means by `index` along `line`, or 0 for none. */
uint32_t dh_mouse_screen_from_helper(
    const dh_mouse_layout_t *layout,
    uint32_t screen_count,
    dh_direction_t line,
    uint8_t index);

uint16_t dh_mouse_jump_threshold_for(
    const dh_mouse_layout_t *layout,
    uint32_t screen_index,
    uint32_t screen_count,
    dh_direction_t movement,
    uint16_t configured_threshold);

int32_t dh_mouse_park_coordinate(
    uint8_t park_position, int32_t previous, int32_t minimum, int32_t maximum);
dh_mouse_coordinates_t dh_mouse_hidden_coordinates(
    dh_direction_t direction,
    uint8_t park_position,
    dh_mouse_coordinates_t pointer,
    int32_t minimum,
    int32_t maximum);
dh_mouse_coordinates_t dh_mouse_entry_coordinates(
    dh_direction_t direction,
    dh_mouse_coordinates_t pointer,
    int32_t minimum,
    int32_t maximum);
dh_mouse_coordinates_t dh_mouse_edge_coordinates(
    dh_direction_t direction,
    dh_mouse_coordinates_t pointer,
    int32_t minimum,
    int32_t maximum);
dh_mouse_coordinates_t dh_mouse_nudge(dh_direction_t direction, int32_t distance);
