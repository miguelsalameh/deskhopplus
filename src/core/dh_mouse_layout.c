/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

#include "dh_mouse_layout.h"

bool dh_mouse_reports_are_relative(bool relative_mouse, bool gaming_mode) {
    return relative_mouse || gaming_mode;
}

static bool directions_are_perpendicular(dh_direction_t first, dh_direction_t second) {
    const bool first_is_horizontal =
        first == DH_DIRECTION_LEFT || first == DH_DIRECTION_RIGHT;
    const bool second_is_horizontal =
        second == DH_DIRECTION_LEFT || second == DH_DIRECTION_RIGHT;
    return first_is_horizontal != second_is_horizontal;
}

bool dh_direction_is_vertical(dh_direction_t direction) {
    return direction == DH_DIRECTION_TOP || direction == DH_DIRECTION_BOTTOM;
}

int32_t dh_mouse_along_seam(dh_direction_t border_direction,
                            dh_mouse_coordinates_t pointer) {
    return dh_direction_is_vertical(border_direction) ? pointer.x : pointer.y;
}

dh_direction_t dh_opposite_direction(dh_direction_t direction) {
    switch (direction) {
        case DH_DIRECTION_LEFT: return DH_DIRECTION_RIGHT;
        case DH_DIRECTION_RIGHT: return DH_DIRECTION_LEFT;
        case DH_DIRECTION_TOP: return DH_DIRECTION_BOTTOM;
        case DH_DIRECTION_BOTTOM: return DH_DIRECTION_TOP;
        case DH_DIRECTION_NONE: return DH_DIRECTION_NONE;
    }
    return DH_DIRECTION_NONE;
}

/* The four directions clockwise, as the screen shows them. */
static const dh_direction_t clockwise[4] = {
    DH_DIRECTION_LEFT, DH_DIRECTION_TOP, DH_DIRECTION_RIGHT, DH_DIRECTION_BOTTOM,
};

static dh_direction_t turned(dh_direction_t direction, unsigned turn) {
    for (unsigned i = 0; i < 4; i++)
        if (clockwise[i] == direction)
            return clockwise[(i + turn) % 4];
    return DH_DIRECTION_NONE;
}

static int32_t unit_x(dh_direction_t d) {
    return d == DH_DIRECTION_LEFT ? -1 : d == DH_DIRECTION_RIGHT ? 1 : 0;
}

static int32_t unit_y(dh_direction_t d) {
    return d == DH_DIRECTION_TOP ? -1 : d == DH_DIRECTION_BOTTOM ? 1 : 0;
}

dh_direction_t dh_mouse_monitor_line(const dh_mouse_layout_t *layout, uint32_t screen) {
    if (screen < DH_MOUSE_TURN_FIRST_SCREEN)
        return DH_DIRECTION_NONE;
    const unsigned turn = screen <= DH_MOUSE_TURN_LAST_SCREEN
                              ? (layout->turns >> ((screen - DH_MOUSE_TURN_FIRST_SCREEN) * 2u)) & 3u
                              : DH_MOUSE_TURN_NONE;
    return turned(layout->chain_direction, turn);
}

/* How far out along its line monitor `screen` is: 1 past main, and one more
   for each earlier monitor on the same line. */
static uint32_t distance_from_main(const dh_mouse_layout_t *layout, uint32_t screen) {
    if (screen < DH_MOUSE_TURN_FIRST_SCREEN)
        return 0;
    const dh_direction_t line = dh_mouse_monitor_line(layout, screen);
    uint32_t distance = 1;
    for (uint32_t earlier = DH_MOUSE_TURN_FIRST_SCREEN; earlier < screen; earlier++)
        if (dh_mouse_monitor_line(layout, earlier) == line)
            distance++;
    return distance;
}

/* Where monitor `screen` sits, in monitors from main. */
static dh_mouse_coordinates_t cell_of(const dh_mouse_layout_t *layout, uint32_t screen) {
    const dh_direction_t line = dh_mouse_monitor_line(layout, screen);
    const int32_t distance = (int32_t)distance_from_main(layout, screen);
    return (dh_mouse_coordinates_t){.x = distance * unit_x(line), .y = distance * unit_y(line)};
}

uint32_t dh_mouse_neighbour(
    const dh_mouse_layout_t *layout,
    uint32_t screen_index,
    uint32_t screen_count,
    dh_direction_t movement) {
    /* A straight line counts along the chain, as it always has: a saved
       monitor past a lowered Screen Count still steps back to the last one. */
    if (layout->turns == 0) {
        if (movement == layout->chain_direction && screen_index < screen_count)
            return screen_index + 1;
        if (movement == dh_opposite_direction(layout->chain_direction) && screen_index > 1)
            return screen_index - 1;
        return 0;
    }
    if (screen_index == 0 || screen_index > screen_count ||
        (unit_x(movement) == 0 && unit_y(movement) == 0))
        return 0;
    const dh_mouse_coordinates_t from = cell_of(layout, screen_index);
    for (uint32_t screen = 1; screen <= screen_count; screen++) {
        const dh_mouse_coordinates_t at = cell_of(layout, screen);
        if (screen != screen_index && at.x == from.x + unit_x(movement) &&
            at.y == from.y + unit_y(movement))
            return screen;
    }
    return 0;
}

uint8_t dh_mouse_route(
    const dh_mouse_layout_t *layout,
    uint32_t screen_count,
    uint32_t from,
    uint32_t to,
    dh_direction_t steps[DH_MOUSE_ROUTE_CAPACITY]) {
    /* A straight line walks along the chain, as it always has, even from a
       saved monitor past a lowered Screen Count. */
    if (layout->turns == 0) {
        if (from == 0 || to == 0 || to > screen_count || from == to)
            return 0;
        const uint32_t distance = from < to ? to - from : from - to;
        if (distance > DH_MOUSE_ROUTE_CAPACITY)
            return 0;
        for (uint32_t step = 0; step < distance; step++)
            steps[step] = from < to ? layout->chain_direction
                                    : dh_opposite_direction(layout->chain_direction);
        return (uint8_t)distance;
    }
    if (from == 0 || to == 0 || from > screen_count || to > screen_count || from == to)
        return 0;
    const dh_direction_t from_line = dh_mouse_monitor_line(layout, from);
    const dh_direction_t to_line = dh_mouse_monitor_line(layout, to);
    uint32_t back = distance_from_main(layout, from);
    uint32_t out = distance_from_main(layout, to);
    if (from_line == to_line) {
        /* One line: only the stretch between them. */
        if (back > out) { back -= out; out = 0; }
        else { out -= back; back = 0; }
    }
    if (back + out > DH_MOUSE_ROUTE_CAPACITY)
        return 0;
    uint8_t count = 0;
    while (back--)
        steps[count++] = dh_opposite_direction(from_line);
    while (out--)
        steps[count++] = to_line;
    return count;
}

bool dh_mouse_helper_line(
    const dh_mouse_layout_t *layout,
    uint32_t screen_count,
    uint32_t screen,
    dh_direction_t *line,
    uint8_t *index) {
    if (screen == 0 || screen > screen_count)
        return false;
    *line = screen == 1 ? layout->chain_direction : dh_mouse_monitor_line(layout, screen);
    *index = (uint8_t)(distance_from_main(layout, screen) + 1);
    return true;
}

uint32_t dh_mouse_screen_from_helper(
    const dh_mouse_layout_t *layout,
    uint32_t screen_count,
    dh_direction_t line,
    uint8_t index) {
    if (index == 1)
        return screen_count >= 1 ? 1 : 0;
    for (uint32_t screen = DH_MOUSE_TURN_FIRST_SCREEN; screen <= screen_count; screen++)
        if (dh_mouse_monitor_line(layout, screen) == line &&
            distance_from_main(layout, screen) + 1 == index)
            return screen;
    return 0;
}

dh_mouse_transition_t dh_mouse_transition_for(
    const dh_mouse_layout_t *layout,
    uint32_t screen_index,
    uint32_t screen_count,
    dh_direction_t movement) {
    /* With turns, every move is a neighbour lookup. Without, the straight
       line keeps its own rule unchanged, down to layouts the page refuses. */
    if (layout->turns != 0) {
        const uint32_t next = dh_mouse_neighbour(layout, screen_index, screen_count, movement);
        if (next != 0)
            return next > screen_index ? DH_MOUSE_TRANSITION_CHAIN_FORWARD
                                       : DH_MOUSE_TRANSITION_CHAIN_BACK;
        return movement == layout->border_direction ? DH_MOUSE_TRANSITION_OUTPUT
                                                    : DH_MOUSE_TRANSITION_NONE;
    }

    if (movement == layout->border_direction &&
        (screen_index == 1 ||
         directions_are_perpendicular(layout->chain_direction, layout->border_direction)))
        return DH_MOUSE_TRANSITION_OUTPUT;

    if (movement == layout->chain_direction && screen_index < screen_count)
        return DH_MOUSE_TRANSITION_CHAIN_FORWARD;

    if (movement == dh_opposite_direction(layout->chain_direction) && screen_index > 1)
        return DH_MOUSE_TRANSITION_CHAIN_BACK;

    return DH_MOUSE_TRANSITION_NONE;
}

uint16_t dh_mouse_jump_threshold_for(
    const dh_mouse_layout_t *layout,
    uint32_t screen_index,
    uint32_t screen_count,
    dh_direction_t movement,
    uint16_t configured_threshold) {
    return dh_mouse_transition_for(layout, screen_index, screen_count, movement) ==
                   DH_MOUSE_TRANSITION_OUTPUT
               ? configured_threshold
               : 0;
}

int32_t dh_mouse_park_coordinate(
    uint8_t park_position, int32_t previous, int32_t minimum, int32_t maximum) {
    if (park_position == 0)
        return minimum;
    if (park_position == 1)
        return maximum;
    return previous;
}

dh_mouse_coordinates_t dh_mouse_hidden_coordinates(
    dh_direction_t direction,
    uint8_t park_position,
    dh_mouse_coordinates_t pointer,
    int32_t minimum,
    int32_t maximum) {
    if (dh_direction_is_vertical(direction))
        return (dh_mouse_coordinates_t){
            .x = dh_mouse_park_coordinate(park_position, pointer.x, minimum, maximum),
            .y = maximum,
        };
    return (dh_mouse_coordinates_t){
        .x = maximum,
        .y = dh_mouse_park_coordinate(park_position, pointer.y, minimum, maximum),
    };
}

dh_mouse_coordinates_t dh_mouse_entry_coordinates(
    dh_direction_t direction,
    dh_mouse_coordinates_t pointer,
    int32_t minimum,
    int32_t maximum) {
    dh_mouse_coordinates_t coordinates = pointer;
    if (direction == DH_DIRECTION_LEFT)
        coordinates.x = maximum;
    else if (direction == DH_DIRECTION_RIGHT)
        coordinates.x = minimum;
    else if (direction == DH_DIRECTION_TOP)
        coordinates.y = maximum;
    else if (direction == DH_DIRECTION_BOTTOM)
        coordinates.y = minimum;
    return coordinates;
}

dh_mouse_coordinates_t dh_mouse_edge_coordinates(
    dh_direction_t direction,
    dh_mouse_coordinates_t pointer,
    int32_t minimum,
    int32_t maximum) {
    return dh_mouse_entry_coordinates(
        dh_opposite_direction(direction), pointer, minimum, maximum);
}

dh_mouse_coordinates_t dh_mouse_nudge(dh_direction_t direction, int32_t distance) {
    dh_mouse_coordinates_t movement = {0};
    const int32_t signed_distance =
        (direction == DH_DIRECTION_LEFT || direction == DH_DIRECTION_TOP) ? -distance : distance;
    if (dh_direction_is_vertical(direction))
        movement.y = signed_distance;
    else
        movement.x = signed_distance;
    return movement;
}
