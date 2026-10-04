/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

/* Cursor crossing owner: movement, helper readback and switching completion.
 * Hardware adapters provide the lock, clock, channel and output effects.
 * Core 1 drives movement/completion; core 0 publishes readback under the lock.
 */
#include "main.h"
#include "dh_place.h"
#include <math.h>

/* macOS push: was 5 pushes of 10 counts; one push of 2 counts crossed every
   time on hardware, slow hand too, two monitors (#319). 1 count not tried. */
#define MACOS_WALK_COUNTS 2

/* Counts in a walk's one push. Windows: two pixels' worth (at a low pointer
   speed, many). Windows keeps the part-pixel of the last relative report, up
   to a pixel the other way, so one pixel's worth can move it none (#318).
   The push shows as a jump on the new monitor. */
static int walk_push(const output_t *output) {
    if (output->os != WINDOWS)
        return MACOS_WALK_COUNTS;
    const int32_t two_pixels = dh_windows_counts_for_pixels(2, output->pointer_speed);
    return two_pixels > 2 ? two_pixels : 2;
}

static int32_t windows_pixels(const output_t *output, bool vertical);

static bool position_is_at_pending_edge(const device_t *state, int16_t x, int16_t y) {
    const cursor_crossing_t *crossing = &state->cursor_crossing;
    int threshold = crossing->kind == CURSOR_CROSSING_CHAIN_REANCHOR
                        ? 0 : state->config.jump_threshold;
    /* With Windows' maths saved (#312), the helper's answer can be one count
       short of the edge: at speeds 10 and 14 on hardware it stayed exactly
       that far, so every 1-count push re-asked and was cancelled. The push
       that closes the gap is already sent, so within one count's pixels of
       the edge is at it (#321). */
    if (crossing->kind == CURSOR_CROSSING_SOURCE_REANCHOR) {
        const output_t *output = &state->config.output[crossing->output];
        const int32_t pixels = windows_pixels(
            output, dh_direction_is_vertical((dh_direction_t)crossing->direction));
        const int one_count = pixels ? (int)((MAX_SCREEN_COORD + pixels) / pixels *
                                             dh_windows_pixels_per_count(output->pointer_speed))
                                     : 0;
        if (one_count > threshold)
            threshold = one_count;
    }
    switch (crossing->direction) {
        case LEFT: return x <= MIN_SCREEN_COORD + threshold;
        case RIGHT: return x >= MAX_SCREEN_COORD - threshold;
        case TOP: return y <= MIN_SCREEN_COORD + threshold;
        case BOTTOM: return y >= MAX_SCREEN_COORD - threshold;
        default: return false;
    }
}

static bool position_confirms_pending_placement(const device_t *state, int16_t x, int16_t y) {
    const cursor_crossing_t *crossing = &state->cursor_crossing;
    const int expected_along = (int)(
        ((uint32_t)crossing->target_position * MAX_SCREEN_COORD +
         DH_SEAM_POSITION_MAX / 2) / DH_SEAM_POSITION_MAX);
    const int actual_along = dh_direction_is_vertical((dh_direction_t)crossing->direction)
                                 ? x
                                 : y;
    if (actual_along < expected_along - 2 || actual_along > expected_along + 2)
        return false;
    const int threshold = state->config.jump_threshold;
    switch (crossing->direction) {
        case LEFT: return x >= MAX_SCREEN_COORD - threshold;
        case RIGHT: return x <= MIN_SCREEN_COORD + threshold;
        case TOP: return y >= MAX_SCREEN_COORD - threshold;
        case BOTTOM: return y <= MIN_SCREEN_COORD + threshold;
        default: return false;
    }
}

static bool select_cursor_screen(device_t *state, uint8_t output, uint8_t screen) {
    if (output > OUTPUT_B || output != state->active_output || screen == 0 ||
        screen > state->config.output[output].screen_count)
        return false;
    state->config.output[output].screen_index = screen;
    const uint8_t os = state->config.output[output].os;
    state->relative_mouse = os == WINDOWS && screen > 1;
    return true;
}

bool apply_helper_cursor_position(device_t *state, uint8_t output, uint8_t screen,
                                  int16_t x, int16_t y, uint8_t query_id) {
    if (output > OUTPUT_B)
        return false;
    cursor_crossing_enter();
    cursor_crossing_t *crossing = &state->cursor_crossing;
    const cursor_crossing_phase_t phase = crossing->phase;
    if ((phase == CURSOR_CROSSING_WAITING &&
         (query_id == 0 || crossing->output != output || crossing->query_id != query_id)) ||
        (query_id != 0 && phase != CURSOR_CROSSING_WAITING)) {
        cursor_crossing_exit();
        return false;
    }
    if (query_id != 0 && crossing->kind == CURSOR_CROSSING_MACOS_PLACEMENT &&
        (screen != crossing->target_screen ||
         !position_confirms_pending_placement(state, x, y))) {
        cursor_crossing_exit();
        return false;
    }
    /* q=0 is the immediate readback of a placement whose target screen was
       already selected by firmware. At an internal seam, continued fast
       motion (or an asynchronous OS observation) can report the neighbouring
       screen before this uncorrelated readback arrives. Accepting that screen
       rewinds screen_index and makes the next chain crossing start from the
       wrong monitor (#28). Correlated re-anchor queries remain authoritative
       because their purpose is to repair a relative-source estimate. */
    if (query_id == 0 && screen != state->config.output[output].screen_index) {
        cursor_crossing_exit();
        return false;
    }
    if (!select_cursor_screen(state, output, screen)) {
        cursor_crossing_exit();
        return false;
    }
    state->pointer_x = x;
    state->pointer_y = y;
    const uint8_t direction = crossing->direction;
    if (crossing->phase == CURSOR_CROSSING_WAITING && crossing->output == output) {
        if (crossing->kind == CURSOR_CROSSING_MACOS_PLACEMENT ||
            ((crossing->kind == CURSOR_CROSSING_SOURCE_REANCHOR ||
              (crossing->kind == CURSOR_CROSSING_CHAIN_REANCHOR &&
               screen == crossing->target_screen)) &&
             position_is_at_pending_edge(state, x, y))) {
            crossing->phase = CURSOR_CROSSING_REANCHORED;
        } else {
            /* Core 1 owns the metadata and performs the full clear. Core 0
               publishes only the terminal result of this query. */
            crossing->phase = CURSOR_CROSSING_CANCELLED;
        }
    }
    cursor_crossing_exit();
    cursor_trace_event(state, DH_CURSOR_TRACE_RESPONSE, query_id, x, y, direction,
                       DH_MOUSE_TRANSITION_OUTPUT);
    return true;
}

/* Map the coordinate along the seam between the two legacy output ranges. */
static int16_t map_output_seam_coordinate(int pointer, int screen_from, int screen_to, device_t *state) {
    output_t *from = &state->config.output[screen_from];
    output_t *to   = &state->config.output[screen_to];
    return (int16_t)dh_seam_map_coordinate(pointer, from->border.start, from->border.end,
                                           to->border.start, to->border.end);
}

static void switch_to_another_pc(
    device_t *state, output_t *output, int output_to, int direction) {
    uint8_t *mouse_park_pos = &state->config.output[state->active_output].mouse_park_pos;
    const bool vertical = dh_direction_is_vertical((dh_direction_t)direction);
    const dh_mouse_coordinates_t pointer = {.x = state->pointer_x, .y = state->pointer_y};
    const dh_mouse_coordinates_t hidden = dh_mouse_hidden_coordinates(
        (dh_direction_t)direction,
        *mouse_park_pos,
        pointer,
        MIN_SCREEN_COORD,
        MAX_SCREEN_COORD);
    mouse_report_t hidden_pointer = {
        .x = (int16_t)hidden.x,
        .y = (int16_t)hidden.y,
    };

    output_mouse_report(&hidden_pointer, state);
    set_active_output(state, output_to);
    /* Relative mode belongs to the screen arrived on, not the one left. */
    (void)select_cursor_screen(state, (uint8_t)output_to,
                               (uint8_t)state->config.output[output_to].screen_index);
    const dh_mouse_coordinates_t entry = dh_mouse_entry_coordinates(
        (dh_direction_t)direction,
        pointer,
        MIN_SCREEN_COORD,
        MAX_SCREEN_COORD);
    state->pointer_x = (int16_t)entry.x;
    state->pointer_y = (int16_t)entry.y;
    if (vertical)
        state->pointer_x = map_output_seam_coordinate(
            state->pointer_x, output->number, 1 - output->number, state);
    else
        state->pointer_y = map_output_seam_coordinate(
            state->pointer_y, output->number, 1 - output->number, state);
}

/* Walk the OS cursor one screen in direction, starting from `from`:
 * 1. An absolute report puts the cursor on the edge of the screen it is on now.
 * 2. One relative push makes the OS itself move it onto the next screen.
 * The board cannot pick a screen with an absolute report on macOS (current screen)
 * or Windows (main screen), so this is the only helper-free way across (#310). */
static void walk_one_screen(device_t *state, dh_mouse_coordinates_t from, int direction) {
    const dh_mouse_coordinates_t edge = dh_mouse_edge_coordinates(
        (dh_direction_t)direction, from, MIN_SCREEN_COORD, MAX_SCREEN_COORD);
    mouse_report_t edge_position = {
        .x = (int16_t)edge.x,
        .y = (int16_t)edge.y,
        .mode = ABSOLUTE,
        .buttons = state->mouse_buttons,
    };

    const output_t *output = &state->config.output[state->active_output];
    const dh_mouse_coordinates_t push =
        dh_mouse_nudge((dh_direction_t)direction, walk_push(output));
    mouse_report_t push_report = {
        .x = (int16_t)push.x,
        .y = (int16_t)push.y,
        .mode = RELATIVE,
        /* Force buttons to 0 for relative movement to avoid duplicating the button
           press state, which would leave the relative HID mouse permanently stuck
           down if the user is dragging an item while switching desktops. */
        .buttons = 0,
    };

    output_mouse_report(&edge_position, state);
    output_mouse_report(&push_report, state);
}

/* The board's units per relative count along direction; at least 1, so a
   zero from a bad config cannot divide by zero or nudge backwards. */
static int walk_speed(const output_t *output, int direction) {
    const int speed = dh_direction_is_vertical((dh_direction_t)direction) ? output->speed_y
                                                                          : output->speed_x;
    return speed > 0 ? speed : 1;
}

/* The axis's monitor size in pixels when the board copies Windows' pointer
   maths for this output (#312), else 0. */
static int32_t windows_pixels(const output_t *output, bool vertical) {
    if (output->os != WINDOWS || !dh_windows_pointer_speed_is_set(output->pointer_speed) ||
        !output->monitor_width || !output->monitor_height)
        return 0;
    return vertical ? output->monitor_height : output->monitor_width;
}

/* Where `counts` along one axis move a relative Windows cursor from `at`, in
   board units: Windows' own maths when saved (#312), with the part-pixel it
   keeps, else counts × the walk speed. A result past an edge is on the next
   monitor along. */
static int32_t windows_moved(device_t *state, const output_t *output, bool vertical,
                             int32_t at, int32_t counts) {
    return at + dh_windows_estimate_offset(
                    at, counts, output->pointer_speed, (uint16_t)windows_pixels(output, vertical),
                    walk_speed(output, vertical ? DH_DIRECTION_TOP : DH_DIRECTION_LEFT),
                    &state->windows_part_pixel[output - state->config.output][vertical]);
}

/* A position past an edge, on the next monitor along. */
static int32_t on_next_monitor(int32_t at) {
    const int32_t span = MAX_SCREEN_COORD + 1;
    return (at % span + span) % span;
}

/* Sends `counts` along `direction` as relative reports Windows takes, and
   returns where they move its cursor from `at` on that axis. */
static int32_t send_relative(device_t *state, const output_t *output, int direction,
                             int32_t at, int32_t counts) {
    const bool vertical = dh_direction_is_vertical((dh_direction_t)direction);
    while (counts > 0) {
        const int32_t chunk = counts < MAX_SCREEN_COORD ? counts : MAX_SCREEN_COORD;
        const dh_mouse_coordinates_t move = dh_mouse_nudge((dh_direction_t)direction, chunk);
        mouse_report_t report = {.x = (int16_t)move.x, .y = (int16_t)move.y, .mode = RELATIVE};
        output_mouse_report(&report, state);
        at = windows_moved(state, output, vertical, at, vertical ? move.y : move.x);
        counts -= chunk;
    }
    return at;
}

/* Where a walk's push leaves the cursor on the new screen: Windows moves it
 * from the edge the walk's absolute report put it on, past that edge. Windows
 * keeps a relative cursor there, so the board's pointer must match it, or the
 * next small move back would cross straight back (#310).
 * ponytail: ignores Enhance pointer precision; only helper-free crossings walk. */
static dh_mouse_coordinates_t walk_landing(device_t *state, const output_t *output, int direction,
                                           dh_mouse_coordinates_t from) {
    const bool vertical = dh_direction_is_vertical((dh_direction_t)direction);
    dh_mouse_coordinates_t at = dh_mouse_edge_coordinates(
        (dh_direction_t)direction, from, MIN_SCREEN_COORD, MAX_SCREEN_COORD);
    const dh_mouse_coordinates_t push = dh_mouse_nudge((dh_direction_t)direction,
                                                       walk_push(output));
    int32_t *axis = vertical ? &at.y : &at.x;
    *axis = windows_moved(state, output, vertical, *axis, vertical ? push.y : push.x);
    *axis = on_next_monitor(*axis);
    return at;
}

static void switch_virtual_desktop(device_t *state, output_t *output, int new_index, int direction) {
    const dh_mouse_coordinates_t pointer = {.x = state->pointer_x, .y = state->pointer_y};
    switch (output->os) {
        case MACOS:
            walk_one_screen(state, pointer, direction);
            break;

        case WINDOWS:
            /* Off the absolute main screen, the report that hit the edge was
               absolute and stopped there; Windows would leave the cursor on
               the main screen until the next move. Walk it across now (#310).
               With a helper, skip the walk and its jump (#317): the next
               move takes the cursor across, and a seam crossing first asks
               the helper where it really is.
               Between relative screens, that report itself moved it across. */
            if (!state->relative_mouse &&
                !channel_output_helper_present((uint8_t)output->number)) {
                walk_one_screen(state, pointer, direction);
                const dh_mouse_coordinates_t landed = walk_landing(state, output, direction, pointer);
                state->pointer_x = (int16_t)landed.x;
                state->pointer_y = (int16_t)landed.y;
                (void)select_cursor_screen(state, (uint8_t)output->number, (uint8_t)new_index);
                return;
            }
            break;

        case LINUX:
        case ANDROID:
        case OTHER:
            /* Linux should treat all desktops as a single virtual screen, so you should leave
            screen_count at 1 and it should just work */
            break;
    }

    dh_mouse_coordinates_t entry = dh_mouse_entry_coordinates(
        (dh_direction_t)direction, pointer, MIN_SCREEN_COORD, MAX_SCREEN_COORD);
    /* Relative Windows: the overshoot update_mouse_position kept, past the
       far edge, is where Windows put the cursor on the new screen. */
    if (output->os == WINDOWS && state->relative_mouse) {
        const int32_t span = MAX_SCREEN_COORD + 1;
        if (pointer.x < MIN_SCREEN_COORD) entry.x = pointer.x + span;
        if (pointer.x > MAX_SCREEN_COORD) entry.x = pointer.x - span;
        if (pointer.y < MIN_SCREEN_COORD) entry.y = pointer.y + span;
        if (pointer.y > MAX_SCREEN_COORD) entry.y = pointer.y - span;
    }
    state->pointer_x = (int16_t)entry.x;
    state->pointer_y = (int16_t)entry.y;
    (void)select_cursor_screen(state, (uint8_t)output->number, (uint8_t)new_index);
}

#define ACCEL_POINTS 7
#define CURSOR_REANCHOR_TIMEOUT_US 30000u

static uint32_t unsigned_magnitude(int32_t value) {
    return value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
}

static dh_mouse_layout_t mouse_layout_for(const output_t *output) {
    return (dh_mouse_layout_t){
        .chain_direction = (dh_direction_t)output->chain_direction,
        .border_direction = (dh_direction_t)output->border_direction,
    };
}

uint16_t get_jump_threshold(output_t *output, enum screen_pos_e direction) {
    const dh_mouse_layout_t layout = mouse_layout_for(output);
    return dh_mouse_jump_threshold_for(&layout, output->screen_index, output->screen_count,
                                       (dh_direction_t)direction,
                                       global_state.config.jump_threshold);
}

typedef struct {
    enum screen_pos_e direction;
    int overshoot;
} screen_boundary_crossing_t;

static dh_mouse_transition_t actionable_transition_for(
    const device_t *state, const output_t *output, enum screen_pos_e direction, int buttons) {
    /* A boot-mode host cannot report its pointer, so the firmware position is
       a guess that drifts from it. Boot mode switches by hotkey only (#67). */
    if (direction == NONE || state->switch_lock || state->gaming_mode ||
        state->boot_mouse_mode[state->active_output])
        return DH_MOUSE_TRANSITION_NONE;
    const dh_mouse_layout_t layout = mouse_layout_for(output);
    const dh_mouse_transition_t transition = dh_mouse_transition_for(
        &layout, output->screen_index, output->screen_count,
        (dh_direction_t)direction);
    return transition == DH_MOUSE_TRANSITION_OUTPUT && buttons
               ? DH_MOUSE_TRANSITION_NONE
               : transition;
}

static void cursor_crossing_clear(device_t *state) {
    state->cursor_crossing = (cursor_crossing_t){.phase = CURSOR_CROSSING_IDLE};
}

static uint8_t next_cursor_query_id(device_t *state) {
    if (++state->next_cursor_query_id == 0)
        ++state->next_cursor_query_id;
    return state->next_cursor_query_id;
}

static bool query_source_cursor(device_t *state, int direction,
                                dh_mouse_transition_t transition,
                                cursor_crossing_kind_t kind) {
    const uint8_t query_id = next_cursor_query_id(state);
    cursor_crossing_enter();
    state->cursor_crossing = (cursor_crossing_t){
        .phase = CURSOR_CROSSING_WAITING,
        .kind = kind,
        .direction = (uint8_t)direction,
        .output = state->active_output,
        .query_id = query_id,
        .target_screen = (uint8_t)state->config.output[state->active_output].screen_index,
        .started_us = time_us_32(),
    };
    cursor_crossing_exit();
    cursor_trace_event(state, DH_CURSOR_TRACE_QUERY, query_id, 0, 0,
                       (uint8_t)direction, (uint8_t)transition);
    const cursor_query_result_t result = channel_query_cursor(state->active_output, query_id);
    if (result != CURSOR_QUERY_UNAVAILABLE) {
        if (result == CURSOR_QUERY_SENT) {
            cursor_crossing_enter();
            if (state->cursor_crossing.phase == CURSOR_CROSSING_WAITING &&
                state->cursor_crossing.query_id == query_id)
                state->cursor_crossing.query_sent = true;
            cursor_crossing_exit();
        }
        return true;
    }
    cursor_crossing_enter();
    if (state->cursor_crossing.phase == CURSOR_CROSSING_WAITING &&
        state->cursor_crossing.query_id == query_id)
        cursor_crossing_clear(state);
    cursor_crossing_exit();
    cursor_trace_event(state, DH_CURSOR_TRACE_CANCEL, query_id, 0, 0,
                       (uint8_t)direction, (uint8_t)transition);
    return false;
}

static screen_boundary_crossing_t screen_boundary_crossing(
    output_t *output,
    int position,
    int offset,
    enum screen_pos_e negative_direction,
    enum screen_pos_e positive_direction) {
    const enum screen_pos_e direction =
        (offset < 0) ? negative_direction : positive_direction;
    if (offset == 0)
        return (screen_boundary_crossing_t){.direction = NONE};

    const int threshold = get_jump_threshold(output, direction);
    const int next_position = position + offset;

    if (next_position < MIN_SCREEN_COORD - threshold)
        return (screen_boundary_crossing_t){
            .direction = negative_direction,
            .overshoot = MIN_SCREEN_COORD - next_position,
        };
    if (next_position > MAX_SCREEN_COORD + threshold)
        return (screen_boundary_crossing_t){
            .direction = positive_direction,
            .overshoot = next_position - MAX_SCREEN_COORD,
        };
    return (screen_boundary_crossing_t){.direction = NONE};
}

int32_t move_and_keep_on_screen(int position, int offset) {
    if (position + offset < MIN_SCREEN_COORD)
        return MIN_SCREEN_COORD;
    if (position + offset > MAX_SCREEN_COORD)
        return MAX_SCREEN_COORD;
    return position + offset;
}

float calculate_mouse_acceleration_factor(int32_t offset_x, int32_t offset_y) {
    const struct curve {
        int value;
        float factor;
    } acceleration[ACCEL_POINTS] = {
        {2, 1}, {5, 1.1}, {15, 1.4}, {30, 1.9}, {45, 2.6}, {60, 3.4}, {70, 4.0},
    };

    if (offset_x == 0 && offset_y == 0)
        return 1.0;
    if (!global_state.config.enable_acceleration)
        return 1.0;

    const float magnitude = sqrtf((float)(offset_x * offset_x) + (float)(offset_y * offset_y));
    if (magnitude <= acceleration[0].value)
        return acceleration[0].factor;
    if (magnitude >= acceleration[ACCEL_POINTS - 1].value)
        return acceleration[ACCEL_POINTS - 1].factor;

    for (int i = 0; i < ACCEL_POINTS - 1; i++) {
        if (magnitude < acceleration[i + 1].value) {
            const struct curve *lower = &acceleration[i];
            const struct curve *upper = &acceleration[i + 1];
            const float interpolation =
                (magnitude - lower->value) / (upper->value - lower->value);
            return lower->factor + interpolation * (upper->factor - lower->factor);
        }
    }
    return 1.0;
}

mouse_report_t create_mouse_report(device_t *state, mouse_values_t *values) {
    mouse_report_t report = {
        .buttons = values->buttons,
        .x = state->pointer_x,
        .y = state->pointer_y,
        .wheel = values->wheel,
        .pan = values->pan,
        .mode = ABSOLUTE,
    };

    if (state->boot_mouse_mode[state->active_output] ||
        dh_mouse_reports_are_relative(state->relative_mouse, state->gaming_mode)) {
        report.x = values->move_x;
        report.y = values->move_y;
        report.mode = state->boot_mouse_mode[state->active_output]
                          ? BOOT_RELATIVE : RELATIVE;
    }

    return report;
}

enum screen_pos_e update_mouse_position(device_t *state, mouse_values_t *values) {
    /* A relative-source output crossing is a short transaction: its helper
       must report the OS cursor before seam mapping can finish. Fast diagonal
       packets can otherwise take a monitor-chain seam while that query is in
       flight, changing screen_index underneath the pending output crossing.
       Hold positional motion until the transaction resolves, while still
       forwarding buttons, wheel and pan through create_mouse_report(). */
    cursor_crossing_enter();
    const bool crossing_pending =
        state->cursor_crossing.phase != CURSOR_CROSSING_IDLE;
    const uint8_t pending_query_id = state->cursor_crossing.query_id;
    const uint8_t pending_direction = state->cursor_crossing.direction;
    cursor_crossing_exit();
    if (crossing_pending) {
        cursor_trace_event(state, DH_CURSOR_TRACE_INPUT, pending_query_id,
                           values->move_x, values->move_y, pending_direction,
                           DH_MOUSE_TRANSITION_OUTPUT);
        values->move_x = 0;
        values->move_y = 0;
        state->mouse_buttons = values->buttons;
        return NONE;
    }

    /* An overshoot kept for a relative chain switch (below) is spent by
       now: the switch happened, or it timed out and did not. */
    state->pointer_x = (int16_t)move_and_keep_on_screen(state->pointer_x, 0);
    state->pointer_y = (int16_t)move_and_keep_on_screen(state->pointer_y, 0);

    output_t *current = &state->config.output[state->active_output];
    /* A relative report carries the raw counts, with neither acceleration
       nor zoom, so the estimate of where they put the cursor has neither. */
    uint8_t reduce_speed = state->mouse_zoom && !state->relative_mouse
                               ? MOUSE_ZOOM_SCALING_FACTOR : 0;
    float acceleration = state->relative_mouse
                             ? 1.0f
                             : calculate_mouse_acceleration_factor(values->move_x,
                                                                   values->move_y);
    int offset_x = round(values->move_x * acceleration * (current->speed_x >> reduce_speed));
    int offset_y = round(values->move_y * acceleration * (current->speed_y >> reduce_speed));
    /* Windows moves a relative cursor by its own maths; copy it when the
       pointer speed and monitor size are saved (#312). */
    if (state->relative_mouse) {
        offset_x = dh_windows_estimate_offset(state->pointer_x, values->move_x,
                                              current->pointer_speed,
                                              (uint16_t)windows_pixels(current, false),
                                              current->speed_x,
                                              &state->windows_part_pixel[state->active_output][0]);
        offset_y = dh_windows_estimate_offset(state->pointer_y, values->move_y,
                                              current->pointer_speed,
                                              (uint16_t)windows_pixels(current, true),
                                              current->speed_y,
                                              &state->windows_part_pixel[state->active_output][1]);
    }
    const screen_boundary_crossing_t horizontal =
        screen_boundary_crossing(current, state->pointer_x, offset_x, LEFT, RIGHT);
    const screen_boundary_crossing_t vertical =
        screen_boundary_crossing(current, state->pointer_y, offset_y, TOP, BOTTOM);
    bool horizontal_actionable =
        actionable_transition_for(state, current, horizontal.direction, values->buttons) !=
        DH_MOUSE_TRANSITION_NONE;
    bool vertical_actionable =
        actionable_transition_for(state, current, vertical.direction, values->buttons) !=
        DH_MOUSE_TRANSITION_NONE;
    const dh_direction_t arrival_guard = (dh_direction_t)state->output_arrival_guard;
    if (arrival_guard != DH_DIRECTION_NONE) {
        const int32_t raw_guard_axis = dh_direction_is_vertical(arrival_guard)
                                           ? values->move_y
                                           : values->move_x;
        const int32_t raw_cross_axis = dh_direction_is_vertical(arrival_guard)
                                           ? values->move_x
                                           : values->move_y;
        const uint32_t guard_magnitude = unsigned_magnitude(raw_guard_axis);
        const uint32_t cross_magnitude = unsigned_magnitude(raw_cross_axis);
        const bool moved_inward =
            guard_magnitude >= cross_magnitude &&
            ((arrival_guard == DH_DIRECTION_LEFT && offset_x > 0) ||
             (arrival_guard == DH_DIRECTION_RIGHT && offset_x < 0) ||
             (arrival_guard == DH_DIRECTION_TOP && offset_y > 0) ||
             (arrival_guard == DH_DIRECTION_BOTTOM && offset_y < 0));
        const bool moved_reverse =
            (arrival_guard == DH_DIRECTION_LEFT && raw_guard_axis < 0) ||
            (arrival_guard == DH_DIRECTION_RIGHT && raw_guard_axis > 0) ||
            (arrival_guard == DH_DIRECTION_TOP && raw_guard_axis < 0) ||
            (arrival_guard == DH_DIRECTION_BOTTOM && raw_guard_axis > 0);
        if (moved_reverse) {
            const uint32_t accumulated =
                state->output_arrival_reverse + guard_magnitude;
            state->output_arrival_reverse = (uint16_t)(
                accumulated > UINT16_MAX ? UINT16_MAX : accumulated);
        }
        const bool deliberate_reverse =
            state->output_arrival_reverse > state->config.jump_threshold;
        if (moved_inward || deliberate_reverse) {
            state->output_arrival_guard = DH_DIRECTION_NONE;
            state->output_arrival_reverse = 0;
        } else {
            if (horizontal.direction == (enum screen_pos_e)arrival_guard)
                horizontal_actionable = false;
            if (vertical.direction == (enum screen_pos_e)arrival_guard)
                vertical_actionable = false;
        }
    }
    const enum screen_pos_e direction =
        vertical_actionable &&
                (!horizontal_actionable || vertical.overshoot > horizontal.overshoot)
            ? vertical.direction
            : horizontal_actionable ? horizontal.direction : NONE;
    const dh_mouse_transition_t chosen_transition =
        actionable_transition_for(state, current, direction, values->buttons);
    if (horizontal.direction != NONE || vertical.direction != NONE) {
        cursor_trace_event(state, DH_CURSOR_TRACE_DECISION, 0, values->move_x,
                           values->move_y, (uint8_t)direction,
                           (uint8_t)chosen_transition);
    }

    /* Relative reports (Windows secondary monitors) otherwise let the OS take
       the losing seam before firmware performs the winning transition. Keep
       only the winning-axis motion in a simultaneous actionable crossing. */
    if (state->relative_mouse && horizontal_actionable && vertical_actionable) {
        if (dh_direction_is_vertical((dh_direction_t)direction))
            values->move_x = 0;
        else
            values->move_y = 0;
    }

    /* A crossing report chooses one seam below, but it may have overshot both
       axes near a corner. Keep every crossing axis at its last valid position
       so the unchosen seam cannot become a synthetic exact corner on the next
       report. This is the vertical-layout behavior proven in the #28 prior
       art, combined with our farther-overshoot arbitration. */
    if (!horizontal_actionable)
        state->pointer_x = move_and_keep_on_screen(state->pointer_x, offset_x);
    if (!vertical_actionable)
        state->pointer_y = move_and_keep_on_screen(state->pointer_y, offset_y);
    /* A relative report that takes a chain seam moves the Windows cursor past
       it by the overshoot. Keep that on the chosen axis, unclamped, so
       switch_virtual_desktop can enter where Windows put it (#310). */
    if (state->relative_mouse && chosen_transition != DH_MOUSE_TRANSITION_NONE &&
        chosen_transition != DH_MOUSE_TRANSITION_OUTPUT) {
        if (dh_direction_is_vertical((dh_direction_t)direction))
            state->pointer_y = (int16_t)(state->pointer_y + offset_y);
        else
            state->pointer_x = (int16_t)(state->pointer_x + offset_x);
    }
    state->mouse_buttons = values->buttons;
    return direction;
}

/* When nothing places it (no helper, or a legacy crossing that sends no
 * PLACE), an arriving cursor is on the screen the OS put it on: os_screen,
 * the one it was left on (macOS), or the main one (Windows, as the source
 * park is absolute). Walk it from there to `screen` (#310), along the middle
 * of each edge, where screens that are not level still touch; `travel` is
 * the direction the cursor crossed the seam in.
 * macOS takes every step as a walk; its next absolute report on the
 * new screen then puts the cursor at the entry point.
 * Windows sends every absolute report to its main screen, so after the
 * first step each one is a relative move of one screen (or a push past the
 * far edge of the chain's last screen). Relative moves then bring the
 * cursor back toward the entry point along the seam, and push it onto the
 * edge it came in by. The board's pointer follows the counts sent. */
static void walk_to_arrival_screen(device_t *state, const output_t *target,
                                   uint8_t os_screen, uint8_t screen, int travel) {
    if (target->os == WINDOWS)
        os_screen = 1;
    else if (target->os != MACOS)
        return;
    if (os_screen == screen)
        return;
    const int steps = os_screen < screen ? screen - os_screen : os_screen - screen;
    const int direction = os_screen < screen
                              ? target->chain_direction
                              : dh_opposite_direction((dh_direction_t)target->chain_direction);
    const dh_mouse_coordinates_t middle = {MAX_SCREEN_COORD / 2, MAX_SCREEN_COORD / 2};
    walk_one_screen(state, middle, direction);
    if (target->os == MACOS) {
        for (int step = 1; step < steps; step++)
            walk_one_screen(state, middle, direction);
        return;
    }
    const dh_mouse_coordinates_t entry = {.x = state->pointer_x, .y = state->pointer_y};
    const bool chain_vertical = dh_direction_is_vertical((dh_direction_t)direction);
    const int speed = walk_speed(target, direction);
    const int32_t chain_pixels = windows_pixels(target, chain_vertical);
    /* One screen, rounded up; without Windows' maths, within one report. */
    int32_t counts = chain_pixels
                         ? dh_windows_counts_for_pixels(chain_pixels, target->pointer_speed)
                         : (MAX_SCREEN_COORD + speed) / speed;
    if (!chain_pixels && counts > MAX_SCREEN_COORD)
        counts = MAX_SCREEN_COORD;
    dh_mouse_coordinates_t landed = walk_landing(state, target, direction, middle);
    int32_t *chain_at = chain_vertical ? &landed.y : &landed.x;
    if (steps > 1 && screen == target->screen_count) {
        /* The last screen ends the chain: push past its far edge, twice
           over, and Windows stops the cursor there even at half speed. */
        (void)send_relative(state, target, direction, 0, 2 * steps * counts);
        const dh_mouse_coordinates_t far = dh_mouse_edge_coordinates(
            (dh_direction_t)direction, middle, MIN_SCREEN_COORD, MAX_SCREEN_COORD);
        *chain_at = chain_vertical ? far.y : far.x;
    } else {
        /* ponytail: Enhance pointer precision breaks a middle screen's
           estimate; a helper places it exactly. */
        for (int step = 1; step < steps; step++)
            *chain_at = send_relative(state, target, direction, *chain_at, counts);
        *chain_at = on_next_monitor(*chain_at);
    }
    state->pointer_x = (int16_t)landed.x;
    state->pointer_y = (int16_t)landed.y;

    /* Along the seam: back under where the cursor left the other computer.
       ponytail: exact only with Windows' maths saved (or Speed X / Y matching
       it) and Enhance pointer precision off; a helper places it exactly. */
    const bool seam_vertical = dh_direction_is_vertical((dh_direction_t)travel);
    const dh_direction_t along = seam_vertical ? DH_DIRECTION_RIGHT : DH_DIRECTION_BOTTOM;
    const int along_speed = walk_speed(target, along);
    const int32_t along_pixels = windows_pixels(target, !seam_vertical);
    int16_t *at = seam_vertical ? &state->pointer_x : &state->pointer_y;
    int distance = (seam_vertical ? entry.x : entry.y) - *at;
    if (dh_direction_is_vertical((dh_direction_t)direction) != seam_vertical) {
        /* Moving along the chain: where a screen of this computer lies past
           the edge ahead, go at most a third of the way there, so a Windows
           that moves up to three times the estimate (one fast report with
           Enhance pointer precision) still stays on this screen. */
        const bool forward = direction == DH_DIRECTION_RIGHT || direction == DH_DIRECTION_BOTTOM;
        const bool screen_past_end = screen < target->screen_count;
        const bool screen_below = forward || screen_past_end;   /* past MIN */
        const bool screen_above = !forward || screen_past_end;  /* past MAX */
        if (distance < 0 && screen_below && distance < -(*at - MIN_SCREEN_COORD) / 3)
            distance = -(*at - MIN_SCREEN_COORD) / 3;
        if (distance > 0 && screen_above && distance > (MAX_SCREEN_COORD - *at) / 3)
            distance = (MAX_SCREEN_COORD - *at) / 3;
    }
    int32_t back = distance / along_speed;
    if (along_pixels) {
        /* The counts that land nearest, with the part-pixel Windows keeps. */
        const int32_t pixels = (int32_t)((int64_t)(*at + distance) * along_pixels / (MAX_SCREEN_COORD + 1) -
                                         (int64_t)*at * along_pixels / (MAX_SCREEN_COORD + 1));
        back = dh_windows_counts_nearest(
            pixels, target->pointer_speed,
            state->windows_part_pixel[target - state->config.output][!seam_vertical]);
    }
    *at = (int16_t)send_relative(state, target, back < 0 ? dh_opposite_direction(along) : along,
                                 *at, back < 0 ? -back : back);

    /* Across the seam: a walk along the seam left the cursor mid-screen.
       The entry edge faces the other computer, so no screen of this one is
       past it: push past it and Windows stops the cursor exactly there,
       whatever its speed. */
    if (dh_direction_is_vertical((dh_direction_t)direction) == seam_vertical)
        return;
    const int32_t across_pixels = windows_pixels(target, seam_vertical);
    int32_t past = across_pixels
                       ? dh_windows_counts_for_pixels(2 * across_pixels, target->pointer_speed)
                       : 2 * (MAX_SCREEN_COORD + 1) / walk_speed(target, travel);
    if (!across_pixels && past > MAX_SCREEN_COORD)
        past = MAX_SCREEN_COORD;
    (void)send_relative(state, target, dh_opposite_direction((dh_direction_t)travel), 0, past);
    if (seam_vertical)
        state->pointer_y = (int16_t)entry.y;
    else
        state->pointer_x = (int16_t)entry.x;
}

static void cross_screen(device_t *state, int direction, bool source_resolved) {
    output_t *output = &state->config.output[state->active_output];
    const dh_mouse_transition_t transition = actionable_transition_for(
        state, output, (enum screen_pos_e)direction, state->mouse_buttons);
    switch (transition) {
        case DH_MOUSE_TRANSITION_OUTPUT:
                /* Windows secondary monitors are relative, so their stored
                   coordinate is only an estimate. Resolve the seam only after
                   the source helper has reported the OS cursor position. */
                if (state->relative_mouse && !source_resolved &&
                    query_source_cursor(state, direction, transition,
                                        CURSOR_CROSSING_SOURCE_REANCHOR))
                    break;
                output_t *target = &state->config.output[1 - state->active_output];
                const int along = dh_mouse_along_seam(
                    (dh_direction_t)direction,
                    (dh_mouse_coordinates_t){.x = state->pointer_x, .y = state->pointer_y});
                const uint16_t normalized = (uint16_t)(
                    ((uint32_t)along * DH_SEAM_POSITION_MAX + MAX_SCREEN_COORD / 2) /
                    MAX_SCREEN_COORD);
                dh_seam_entry_t mapped_entry;
                dh_seam_crossing_kind_t crossing = dh_seam_resolve_crossing(
                    output->seam_ranges, target->seam_ranges, output->screen_index,
                    output->screen_count, target->screen_count, normalized,
                    &mapped_entry);
                if (crossing == DH_SEAM_CROSSING_BLOCKED) {
                    const dh_mouse_coordinates_t edge = dh_mouse_edge_coordinates(
                        (dh_direction_t)direction,
                        (dh_mouse_coordinates_t){.x = state->pointer_x,
                                                 .y = state->pointer_y},
                        MIN_SCREEN_COORD, MAX_SCREEN_COORD);
                    state->pointer_x = (int16_t)edge.x;
                    state->pointer_y = (int16_t)edge.y;
                    break;
                }
                switch_to_another_pc(state, output, 1 - state->active_output, direction);
                state->output_arrival_guard = (uint8_t)dh_opposite_direction(
                    (dh_direction_t)direction);
                state->output_arrival_reverse = 0;
                cursor_trace_event(state, DH_CURSOR_TRACE_SWITCH, 0, 0, 0,
                                   (uint8_t)direction, (uint8_t)transition);
                const uint8_t os_screen = target->screen_index;
                const bool helper = channel_output_helper_present((uint8_t)target->number);
                if (crossing == DH_SEAM_CROSSING_MAPPED) {
                    const int entry = (int)(((uint32_t)mapped_entry.position * MAX_SCREEN_COORD +
                                             DH_SEAM_POSITION_MAX / 2) /
                                            DH_SEAM_POSITION_MAX);
                    if (dh_direction_is_vertical((dh_direction_t)direction))
                        state->pointer_x = (int16_t)entry;
                    else
                        state->pointer_y = (int16_t)entry;
                    if (!select_cursor_screen(state, (uint8_t)target->number,
                                              mapped_entry.screen_index))
                        break;
                    if (helper) {
                        channel_place_cursor((uint8_t)target->number,
                                             mapped_entry.screen_index,
                                             target->chain_direction,
                                             target->border_direction,
                                             mapped_entry.position);
                        /* The helper puts a relative Windows cursor on pixel
                           dh_place_along() of the edge; start the estimate
                           in that pixel, not the one next to it (#323). */
                        const bool seam_vertical =
                            dh_direction_is_vertical((dh_direction_t)direction);
                        const int32_t span = windows_pixels(target, !seam_vertical);
                        if (state->relative_mouse && span) {
                            const int32_t px = dh_place_along(mapped_entry.position, span);
                            const int16_t units = (int16_t)(
                                (px * (MAX_SCREEN_COORD + 1) + span - 1) / span);
                            *(seam_vertical ? &state->pointer_x : &state->pointer_y) = units;
                        }
                        cursor_trace_event(state, DH_CURSOR_TRACE_PLACE, 0, 0, 0,
                                           (uint8_t)direction, (uint8_t)transition);
                    }
                }
                /* A legacy crossing keeps the remembered screen and sends no
                   PLACE, so it walks even with a helper; for macOS that is
                   a walk of no steps, as its cursor never left that screen. */
                if (!helper || crossing != DH_SEAM_CROSSING_MAPPED)
                    walk_to_arrival_screen(state, target, os_screen, target->screen_index,
                                           direction);
            break;
        case DH_MOUSE_TRANSITION_CHAIN_BACK:
        case DH_MOUSE_TRANSITION_CHAIN_FORWARD:
            if (output->os == WINDOWS && state->relative_mouse && !source_resolved &&
                query_source_cursor(state, direction, transition,
                                    CURSOR_CROSSING_CHAIN_REANCHOR))
                break;
            if (output->os == MACOS) {
                const uint8_t target_screen = dh_mouse_next_screen_index(
                    transition, output->screen_index);
                const int along = dh_mouse_along_seam(
                    (dh_direction_t)direction,
                    (dh_mouse_coordinates_t){.x = state->pointer_x,
                                             .y = state->pointer_y});
                const uint16_t normalized = (uint16_t)(
                    ((uint32_t)along * DH_SEAM_POSITION_MAX + MAX_SCREEN_COORD / 2) /
                    MAX_SCREEN_COORD);
                const uint8_t query_id = next_cursor_query_id(state);
                cursor_crossing_enter();
                state->cursor_crossing = (cursor_crossing_t){
                    .phase = CURSOR_CROSSING_WAITING,
                    .kind = CURSOR_CROSSING_MACOS_PLACEMENT,
                    .direction = (uint8_t)direction,
                    .output = state->active_output,
                    .query_id = query_id,
                    .target_screen = target_screen,
                    .target_position = normalized,
                    .started_us = time_us_32(),
                };
                cursor_crossing_exit();
                if (channel_place_cursor_correlated(
                        state->active_output, target_screen, output->chain_direction,
                        (uint8_t)dh_opposite_direction((dh_direction_t)direction),
                        normalized, query_id))
                    break;
                cursor_crossing_enter();
                if (state->cursor_crossing.phase == CURSOR_CROSSING_WAITING &&
                    state->cursor_crossing.query_id == query_id)
                    cursor_crossing_clear(state);
                cursor_crossing_exit();
            }
            switch_virtual_desktop(state, output,
                                  dh_mouse_next_screen_index(transition, output->screen_index),
                                  direction);
            cursor_trace_event(state, DH_CURSOR_TRACE_SWITCH, 0, 0, 0,
                               (uint8_t)direction, (uint8_t)transition);
            break;
        case DH_MOUSE_TRANSITION_NONE:
            break;
    }
}

/* The active output changed with no crossing: the output hotkey, on this
 * board or the peer. Windows takes the next absolute report on its main
 * screen, so that is where its cursor will be; macOS keeps the screen it was
 * left on. Either way relative mode follows that screen (#310). */
void cursor_output_switched(device_t *state) {
    const output_t *output = &state->config.output[state->active_output];
    (void)select_cursor_screen(state, state->active_output,
                               output->os == WINDOWS ? 1 : (uint8_t)output->screen_index);
}

void do_screen_switch(device_t *state, int direction) {
    cursor_crossing_enter();
    const bool pending = state->cursor_crossing.phase != CURSOR_CROSSING_IDLE;
    cursor_crossing_exit();
    if (!pending)
        cross_screen(state, direction, false);
}

void mouse_crossing_task(device_t *state, uint32_t now_us) {
    cursor_crossing_enter();
    cursor_crossing_t *crossing = &state->cursor_crossing;
    if (crossing->phase == CURSOR_CROSSING_IDLE) {
        cursor_crossing_exit();
        return;
    }
    if (state->active_output != crossing->output ||
        (crossing->kind == CURSOR_CROSSING_SOURCE_REANCHOR && state->mouse_buttons) ||
        state->switch_lock || state->gaming_mode) {
        const uint8_t query_id = crossing->query_id;
        const uint8_t direction = crossing->direction;
        cursor_crossing_clear(state);
        cursor_crossing_exit();
        cursor_trace_event(state, DH_CURSOR_TRACE_CANCEL, query_id, 0, 0, direction, 0);
        return;
    }
    if (crossing->phase == CURSOR_CROSSING_CANCELLED) {
        cursor_crossing_clear(state);
        cursor_crossing_exit();
        return;
    }
    if (crossing->phase == CURSOR_CROSSING_WAITING &&
        (crossing->kind == CURSOR_CROSSING_SOURCE_REANCHOR ||
         crossing->kind == CURSOR_CROSSING_CHAIN_REANCHOR) &&
        !crossing->query_sent) {
        const uint8_t retry_output = crossing->output;
        const uint8_t retry_query_id = crossing->query_id;
        cursor_crossing_exit();
        const cursor_query_result_t retry_result =
            channel_query_cursor(retry_output, retry_query_id);
        cursor_crossing_enter();
        crossing = &state->cursor_crossing;
        if (crossing->phase != CURSOR_CROSSING_WAITING ||
            (crossing->kind != CURSOR_CROSSING_SOURCE_REANCHOR &&
             crossing->kind != CURSOR_CROSSING_CHAIN_REANCHOR) ||
            crossing->output != retry_output || crossing->query_id != retry_query_id) {
            cursor_crossing_exit();
            return;
        }
        if (retry_result == CURSOR_QUERY_SENT) {
            crossing->query_sent = true;
            crossing->started_us = now_us;
        } else if (retry_result == CURSOR_QUERY_UNAVAILABLE) {
            crossing->phase = CURSOR_CROSSING_FALLBACK;
        }
    }
    bool timed_out = false;
    uint8_t timeout_query_id = 0;
    uint8_t timeout_direction = NONE;
    if (crossing->phase == CURSOR_CROSSING_WAITING &&
        (uint32_t)(now_us - crossing->started_us) >= CURSOR_REANCHOR_TIMEOUT_US) {
        crossing->phase = CURSOR_CROSSING_FALLBACK;
        timed_out = true;
        timeout_query_id = crossing->query_id;
        timeout_direction = crossing->direction;
    }
    if (crossing->phase == CURSOR_CROSSING_WAITING) {
        cursor_crossing_exit();
        return;
    }

    const uint8_t direction = crossing->direction;
    const cursor_crossing_kind_t kind = crossing->kind;
    const uint8_t target_screen = crossing->target_screen;
    const bool unconfirmed_chain = kind == CURSOR_CROSSING_CHAIN_REANCHOR && timed_out;
    if (kind == CURSOR_CROSSING_MACOS_PLACEMENT &&
        crossing->phase == CURSOR_CROSSING_REANCHORED) {
        cursor_crossing_clear(state);
        cursor_crossing_exit();
        return;
    }
    /* Completion runs synchronously on core 1. Retire the query before emitting
       effects, so every return (including a changed layout) releases motion. */
    cursor_crossing_clear(state);
    cursor_crossing_exit();
    if (timed_out)
        cursor_trace_event(state, DH_CURSOR_TRACE_TIMEOUT, timeout_query_id, 0, 0,
                           timeout_direction,
                           kind == CURSOR_CROSSING_CHAIN_REANCHOR
                               ? (uint8_t)actionable_transition_for(
                                     state, &state->config.output[state->active_output],
                                     (enum screen_pos_e)direction, state->mouse_buttons)
                               : DH_MOUSE_TRANSITION_OUTPUT);
    if (unconfirmed_chain)
        return;
    if (kind == CURSOR_CROSSING_MACOS_PLACEMENT) {
        output_t *output = &state->config.output[state->active_output];
        switch_virtual_desktop(state, output, target_screen, direction);
    } else {
        cross_screen(state, direction, true);
    }
}

void mouse_crossing_query_unavailable(device_t *state, uint8_t output, uint8_t query_id) {
    cursor_crossing_enter();
    cursor_crossing_t *crossing = &state->cursor_crossing;
    if (crossing->phase == CURSOR_CROSSING_WAITING && crossing->output == output &&
        crossing->query_id == query_id)
        crossing->phase = CURSOR_CROSSING_FALLBACK;
    cursor_crossing_exit();
}
