/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */
/* Modified by Miguel Salameh, 2026, for deskhopplus. */

/*
 * Model test for cursor crossings (#310). The real crossing code drives two
 * modelled computers that follow the OS rules the board cannot change:
 *
 *   macOS    an absolute report lands on the screen the cursor is on now.
 *   Windows  an absolute report lands on the main screen (screen 1).
 *   Both     relative motion past an edge moves onto that computer's
 *            neighbouring screen, or stops at the edge if there is none.
 *   Helper   when running, PLACE puts the cursor on the asked screen, and a
 *            query reads the real cursor back.
 *
 * After every report it checks the one rule each #310 symptom broke: the
 * screen the board thinks the cursor is on is the screen it is on. It also
 * checks the board's remembered Mac screen, which a helper-free walk starts
 * from. With Windows' helper on, the board's estimate may lag after a chain
 * crossing that does not walk (#317); check() says how far. Two drivers:
 * every layout × start × direction pushed straight, and long seeded random
 * runs on every layout.
 *
 * The model is ideal on purpose: Windows moves a relative count by the
 * output's speed in board units, with no pointer speed or acceleration of its
 * own, and every screen is the same size and lined up. A failure is a logic
 * fault, not a measurement one. The board's own acceleration and mouse zoom
 * are on, as on hardware.
 *
 * Pixel mode (#312) is the exception: there Windows moves in real pixels by
 * its pointer speed's multiplier, keeping each report's part-pixel for the
 * next as the hardware does, and the
 * board, with that pointer speed and monitor size saved, must keep its
 * estimate in the pixel the cursor is in, at every pointer speed, monitor
 * size and hand speed, with the helpers off and on (on, not while the
 * estimate is unanchored, #317). There the Windows helper answers in whole
 * pixels, rounded as the real one rounds (#322).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"

#define MAX_CHAIN 3

enum screen_pos_e update_mouse_position(device_t *, mouse_values_t *);
void do_screen_switch(device_t *, int);
void cursor_output_switched(device_t *);

typedef struct {
    int os;
    int count;
    int cell_x[MAX_CHAIN + 1];   /* grid cell of screen k, 1-based */
    int cell_y[MAX_CHAIN + 1];
    bool helper;
    /* The real cursor. */
    int screen;
    int x, y;
    /* Pixel mode only: the real cursor in 1/32 px, and the part-pixel
       Windows keeps for each axis. */
    long px32_x, px32_y;
    long keep32_x, keep32_y;
} computer_t;

typedef struct {
    int arrangement;             /* 0: A above B, 1: A left of B */
    int os[2];
    int count[2];
    int chain[2];
    int offset;                  /* B's main screen, along the seam, in cells */
    bool mapped;
    bool helper[2];
    uint8_t turns[2];            /* config.monitor_turns; nonzero needs cells */
    /* A's screens by hand, for a desk that is not one straight line:
       cell[k] is screen k's cell, with main at (0,0). */
    bool a_cells;
    int cell[MAX_CHAIN + 1][2];
} layout_t;

device_t global_state;
static computer_t world[2];
static device_t board;
static uint8_t pending_query;    /* a correlated query the model must answer */
static uint8_t pending_output;
/* Windows' real pixels per board-estimated unit, in percent: its pointer
   speed, Enhance pointer precision and screen size, which the board cannot
   see. 100 is the board's exact guess. */
static int windows_gain = 100;
#define ENHANCED_PRECISION -1    /* windows_gain: depends on each report's speed */
/* Pixel mode (#312): Windows moves its cursor in real pixels, counts × its
   pointer speed's multiplier, on screens of pixel_w × pixel_h, keeping each
   report's part-pixel for the next, as the hardware showed. 0: off. */
static int pixel_w, pixel_h;
static int pixel_mult32;         /* the multiplier, in 1/32 */
static bool pixel_helpers;       /* pixel mode with both helpers on (#322) */
static long last_rel_px32_x, last_rel_px32_y;  /* after the last relative report */
static unsigned long checks, layouts_run;
static int reports_sent;         /* mouse reports the board has sent */
static bool unanchored;          /* the board's estimate may lag the cursor (#317); see check() */
static bool answered;            /* settle() applied a helper answer */
static int failures;
static char context[160];

static void fail(const char *what) {
    if (failures++ < 20)
        fprintf(stderr, "FAIL crossing_model: %s\n  %s\n", what, context);
}

/* ---- the modelled computers ---------------------------------------------- */

static int screen_at(const computer_t *c, int cx, int cy) {
    for (int k = 1; k <= c->count; k++)
        if (c->cell_x[k] == cx && c->cell_y[k] == cy)
            return k;
    return 0;
}

/* Relative motion along one axis: cross onto a neighbour, or stop at the edge. */
static void move_axis(computer_t *c, int *coord, int delta, int dx, int dy) {
    *coord += delta;
    if (*coord > MAX_SCREEN_COORD || *coord < MIN_SCREEN_COORD) {
        const int sign = *coord > MAX_SCREEN_COORD ? 1 : -1;
        const int next = screen_at(c, c->cell_x[c->screen] + dx * sign,
                                   c->cell_y[c->screen] + dy * sign);
        if (next) {
            c->screen = next;
            *coord -= sign * (MAX_SCREEN_COORD + 1);
        }
        if (*coord > MAX_SCREEN_COORD) *coord = MAX_SCREEN_COORD;
        if (*coord < MIN_SCREEN_COORD) *coord = MIN_SCREEN_COORD;
    }
}

/* Pixel mode: relative motion along one axis of `span` pixels, in 1/32 px. */
static void move_axis_px(computer_t *c, long *coord32, long delta32, int span, int dx, int dy) {
    const long size32 = (long)span * 32;
    *coord32 += delta32;
    if (*coord32 >= size32 || *coord32 < 0) {
        const int sign = *coord32 >= size32 ? 1 : -1;
        const int next = screen_at(c, c->cell_x[c->screen] + dx * sign,
                                   c->cell_y[c->screen] + dy * sign);
        if (next) {
            c->screen = next;
            *coord32 -= sign * size32;
        }
        if (*coord32 >= size32) *coord32 = size32 - 32;
        if (*coord32 < 0) *coord32 = 0;
    }
}

/* Pixel mode: whole pixels (in 1/32) that `counts` move, toward zero. */
static long whole_pixels32(int counts, long *keep32) {
    const long d32 = (long)counts * pixel_mult32 + *keep32;
    const long whole = d32 / 32 * 32;
    *keep32 = d32 - whole;
    return whole;
}

/* Pixel mode: the board's units for the real cursor's pixel. */
static void sync_units(computer_t *c) {
    c->x = (int)(c->px32_x / 32 * (MAX_SCREEN_COORD + 1) / pixel_w);
    c->y = (int)(c->px32_y / 32 * (MAX_SCREEN_COORD + 1) / pixel_h);
}

static void place_px(computer_t *c) {
    c->px32_x = (long)c->x * pixel_w / (MAX_SCREEN_COORD + 1) * 32;
    c->px32_y = (long)c->y * pixel_h / (MAX_SCREEN_COORD + 1) * 32;
}

void output_mouse_report(mouse_report_t *report, device_t *state) {
    computer_t *c = &world[state->active_output];
    const output_t *o = &state->config.output[state->active_output];
    reports_sent++;
    if (pixel_w && c->os == WINDOWS) {
        if (report->mode == ABSOLUTE) {
            c->screen = 1;
            c->x = report->x;
            c->y = report->y;
            place_px(c);
            return;
        }
        const long dx32 = whole_pixels32(report->x, &c->keep32_x);
        const long dy32 = whole_pixels32(report->y, &c->keep32_y);
        move_axis_px(c, &c->px32_x, dx32, pixel_w, 1, 0);
        move_axis_px(c, &c->px32_y, dy32, pixel_h, 0, 1);
        sync_units(c);
        last_rel_px32_x = c->px32_x;
        last_rel_px32_y = c->px32_y;
        return;
    }
    if (report->mode == ABSOLUTE) {
        if (c->os == WINDOWS)
            c->screen = 1;
        c->x = report->x;
        c->y = report->y;
        return;
    }
    int gain = c->os == WINDOWS ? windows_gain : 100;
    if (gain == ENHANCED_PRECISION) {
        /* Like Enhance pointer precision: slow reports move less, fast
           ones more, up to nearly three times. */
        const int fast = abs(report->x) > abs(report->y) ? abs(report->x) : abs(report->y);
        gain = fast <= 3 ? 60 : fast <= 10 ? 100 : fast <= 30 ? 180 : 290;
    }
    move_axis(c, &c->x, report->x * o->speed_x * gain / 100, 1, 0);
    move_axis(c, &c->y, report->y * o->speed_y * gain / 100, 0, 1);
}

void set_active_output(device_t *state, uint8_t output) {
    state->active_output = output;
}

/* A helper puts the cursor on the asked screen, on its border edge. */
static void helper_place(uint8_t output, uint8_t screen, uint8_t border, uint16_t position) {
    computer_t *c = &world[output];
    if (screen < 1 || screen > c->count)
        return;
    const int along = (int)(((uint32_t)position * MAX_SCREEN_COORD + 32767u) / 65535u);
    c->screen = screen;
    switch (border) {
        case LEFT: c->x = MIN_SCREEN_COORD; c->y = along; break;
        case RIGHT: c->x = MAX_SCREEN_COORD; c->y = along; break;
        case TOP: c->y = MIN_SCREEN_COORD; c->x = along; break;
        case BOTTOM: c->y = MAX_SCREEN_COORD; c->x = along; break;
    }
    if (pixel_w && c->os == WINDOWS) {
        /* The real helper's pixel along the edge (dh_place_target). */
        const bool vertical = border == TOP || border == BOTTOM;
        const int span = vertical ? pixel_w : pixel_h;
        const long px = ((long)position * (span - 1) + 32767) / 65535;
        place_px(c);
        *(vertical ? &c->px32_x : &c->px32_y) = px * 32;
        sync_units(c);
    }
}

bool channel_output_helper_present(uint8_t output) {
    return world[output].helper;
}

static int unit_x(int d);
static int unit_y(int d);

/* The real helper's search (dh_place_target): `index - 1` displays out of
   main, each the next one along `way`. */
static int helper_display(const computer_t *c, uint8_t index, uint8_t way) {
    int at = 1;
    for (int step = 1; at && step < index; step++)
        at = screen_at(c, c->cell_x[at] + unit_x(way), c->cell_y[at] + unit_y(way));
    return at;
}

/* The line each helper was last told; it answers along that line only. */
static uint8_t helper_line[2];

void channel_place_cursor(uint8_t output, uint8_t index, uint8_t line, uint8_t border,
                          uint16_t position) {
    if (!world[output].helper)
        return;
    helper_line[output] = line;
    helper_place(output, (uint8_t)helper_display(&world[output], index, line), border, position);
}

/* The real helpers' answer (positionBody): the cursor's display as an index
   along the last PLACE's line, or no answer off that line. 0: no answer. */
static uint8_t helper_index(int output) {
    const computer_t *c = &world[output];
    const uint8_t line = helper_line[output] ? helper_line[output]
                                             : board.config.output[output].chain_direction;
    for (uint8_t index = 1; index <= c->count; index++)
        if (helper_display(c, index, line) == c->screen)
            return index;
    return 0;
}

bool channel_place_cursor_correlated(uint8_t output, uint8_t screen, uint8_t chain,
                                     uint8_t border, uint16_t position, uint8_t query_id) {
    if (!world[output].helper)
        return false;
    channel_place_cursor(output, screen, chain, border, position);
    pending_query = query_id;
    pending_output = output;
    return true;
}

cursor_query_result_t channel_query_cursor(uint8_t output, uint8_t query_id) {
    if (!world[output].helper)
        return CURSOR_QUERY_UNAVAILABLE;
    pending_query = query_id;
    pending_output = output;
    return CURSOR_QUERY_SENT;
}

/* Pixel mode (#322): the board's units for a whole pixel, as the Windows
   helper sends it, round(px * 65535 / (span - 1)), and channel.c converts
   it, round(n * 32767 / 65535). Row 1 of 1080 arrives as 30 units, which
   the board's pixel maths call row 0. */
static int16_t helper_units(long px32, int span) {
    const long n = (px32 / 32 * 65535 + (span - 1) / 2) / (span - 1);
    return (int16_t)((n * MAX_SCREEN_COORD + 32767) / 65535);
}

/* The helper's answer, then the board's completion of the crossing. */
static void settle(void) {
    answered = pending_query != 0;
    for (int round = 0; round < 4 && pending_query; round++) {
        const uint8_t q = pending_query, out = pending_output;
        pending_query = 0;
        const computer_t *c = &world[out];
        const bool pixels = pixel_w && c->os == WINDOWS;
        /* channel.c reads the answer back along the line it last sent. */
        const uint8_t index = helper_index(out);
        if (!index)
            continue;
        const uint8_t line = helper_line[out] ? helper_line[out]
                                              : board.config.output[out].chain_direction;
        (void)apply_helper_cursor_position(
            &board, out, cursor_screen_from_helper(&board, out, line, index),
            pixels ? helper_units(c->px32_x, pixel_w) : (int16_t)c->x,
            pixels ? helper_units(c->px32_y, pixel_h) : (int16_t)c->y, q);
        mouse_crossing_task(&board, 1000000u);
    }
    mouse_crossing_task(&board, 1000000u);
}

/* ---- layouts ------------------------------------------------------------- */

static int unit_x(int d) { return d == LEFT ? -1 : d == RIGHT ? 1 : 0; }
static int unit_y(int d) { return d == TOP ? -1 : d == BOTTOM ? 1 : 0; }

/* Builds the world and the board's config; false for a layout that cannot
   exist (screens on top of each other, or a chain running into the seam). */
static bool build(const layout_t *l) {
    memset(world, 0, sizeof world);
    memset(&board, 0, sizeof board);
    board.config.enable_acceleration = 1;
    pending_query = 0;
    helper_line[0] = helper_line[1] = 0;
    unanchored = false;
    const int border[2] = {
        l->arrangement == 0 ? BOTTOM : RIGHT,
        l->arrangement == 0 ? TOP : LEFT,
    };
    for (int o = 0; o < 2; o++) {
        computer_t *c = &world[o];
        c->os = l->os[o];
        c->count = l->count[o];
        c->helper = l->helper[o];
        /* A's main screen at (0,0); B's beside it across the seam. */
        int bx = 0, by = 0;
        if (o == 1) {
            bx = l->arrangement == 0 ? l->offset : 1;
            by = l->arrangement == 0 ? 1 : l->offset;
        }
        for (int k = 1; k <= c->count; k++) {
            c->cell_x[k] = o == 0 && l->a_cells ? l->cell[k][0]
                                                : bx + (k - 1) * unit_x(l->chain[o]);
            c->cell_y[k] = o == 0 && l->a_cells ? l->cell[k][1]
                                                : by + (k - 1) * unit_y(l->chain[o]);
        }
        board.config.monitor_turns[o] = l->turns[o];
        board.config.output[o] = (output_t){
            .number = (uint8_t)o, .screen_count = (uint8_t)c->count, .screen_index = 1,
            .speed_x = 16, .speed_y = 28, .os = (uint8_t)c->os,
            .border = {MIN_SCREEN_COORD, MAX_SCREEN_COORD},
            .chain_direction = (uint8_t)l->chain[o],
            .border_direction = (uint8_t)border[o],
        };
    }
    /* No two screens in one cell: a chain parallel to the seam must run away
       from it, and the two computers must not overlap. */
    for (int o = 0; o < 2; o++)
        for (int k = 1; k <= world[o].count; k++)
            for (int p = 0; p < 2; p++)
                for (int j = 1; j <= world[p].count; j++)
                    if ((p != o || j != k) && world[o].cell_x[k] == world[p].cell_x[j] &&
                        world[o].cell_y[k] == world[p].cell_y[j])
                        return false;
    /* A chain parallel to the seam leaves it on screen 1 only: the board's
       rule. A screen of A past the seam line would sit in B's half. */
    for (int k = 1; k <= world[0].count; k++)
        if (l->arrangement == 0 ? world[0].cell_y[k] > 0 : world[0].cell_x[k] > 0)
            return false;
    for (int k = 1; k <= world[1].count; k++)
        if (l->arrangement == 0 ? world[1].cell_y[k] < 1 : world[1].cell_x[k] < 1)
            return false;
    if (l->mapped) {
        /* One seam segment per pair of screens that touch across the seam. */
        int segment = 0;
        for (int k = 1; k <= world[0].count; k++) {
            const int nx = world[0].cell_x[k] + unit_x(border[0]);
            const int ny = world[0].cell_y[k] + unit_y(border[0]);
            const int j = screen_at(&world[1], nx, ny);
            if (!j)
                continue;
            if (segment == (int)DH_SEAM_RANGE_CAPACITY)
                return false;
            board.config.output[0].seam_ranges[segment] = (dh_seam_range_t){
                .screen_index = (uint8_t)k, .start = 0, .end = DH_SEAM_POSITION_MAX};
            board.config.output[1].seam_ranges[segment] = (dh_seam_range_t){
                .screen_index = (uint8_t)j, .start = 0, .end = DH_SEAM_POSITION_MAX};
            segment++;
        }
        if (segment == 0)
            return false;
    }
    return true;
}

/* Starts on computer `on`, screen `screen`, with the other computer's board
   memory at `remembered`; the real cursors agree with the board. */
static void start(int on, int screen, int remembered, int x, int y) {
    const int other = 1 - on;
    board.active_output = (uint8_t)on;
    board.config.output[on].screen_index = (uint8_t)screen;
    board.config.output[other].screen_index = (uint8_t)remembered;
    board.relative_mouse = world[on].os == WINDOWS && screen > 1;
    board.pointer_x = (int16_t)x;
    board.pointer_y = (int16_t)y;
    world[on].screen = screen;
    world[on].x = x;
    world[on].y = y;
    /* The source park is absolute: Windows sits on its main screen, macOS on
       the screen it was left on. */
    world[other].screen = world[other].os == WINDOWS ? 1 : remembered;
    world[other].x = MAX_SCREEN_COORD;
    world[other].y = MAX_SCREEN_COORD;
    global_state = board;
}

/* ---- the invariant ------------------------------------------------------- */

static bool switched;            /* this report changed screen or computer */
static bool attempted;           /* this report took a seam, even a blocked one */
static bool board_crossed_vertically;
static int arrived_direction;
static int arrived_along = -1;   /* a crossing onto relative Windows: the source's
                                    coordinate along the seam, else -1 */

static void check(void) {
    checks++;
    const int on = board.active_output;
    /* Windows' main screen is absolute: the next report puts the cursor
       there wherever a hotkey left it, before anything acts on it. */
    const bool next_report_places =
        switched && world[on].os == WINDOWS && board.config.output[on].screen_index == 1;
    /* The one exception (#317): with Windows' helper on, a chain crossing off
       the main screen does not walk. The real cursor stays where the
       crossing report left it, up to a report short of the edge, and the
       next report takes it across. The board's estimate on the relative
       screens can be that far out, so it can see the next chain crossing
       early or late, until the helper answers a query or the board leaves
       those screens. The helper reads the cursor before any crossing out. */
    if (!next_report_places && !unanchored &&
        (int)board.config.output[on].screen_index != world[on].screen) {
        char what[96];
        snprintf(what, sizeof what, "board thinks %c screen %d, cursor is on screen %d",
                 'A' + on, board.config.output[on].screen_index, world[on].screen);
        fail(what);
    }
    const int other = 1 - on;
    if (world[other].os == MACOS &&
        (int)board.config.output[other].screen_index != world[other].screen) {
        char what[96];
        snprintf(what, sizeof what, "board remembers Mac %c on screen %d, it was left on %d",
                 'A' + other, board.config.output[other].screen_index, world[other].screen);
        fail(what);
    }
    if (board.relative_mouse != (world[on].os == WINDOWS && board.config.output[on].screen_index > 1))
        fail("relative mode does not match the screen");
    /* On an absolute screen the report just sent put the cursor at the
       board's pointer, unless a crossing then moved the pointer or walked. */
    if (!attempted && !board.relative_mouse &&
        (world[on].x != board.pointer_x || world[on].y != board.pointer_y))
        fail("absolute screen: cursor is not at the board's pointer");
    /* On a relative screen the board's pointer is its estimate of the
       cursor; the model's Windows moves exactly that estimate. */
    if (board.relative_mouse && !unanchored &&
        (world[on].x != board.pointer_x || world[on].y != board.pointer_y))
        fail("relative screen: cursor is not at the board's pointer");
    /* Without a helper the walk must still leave the cursor under where it
       left the other computer, not mid-edge (#310). Rounding to whole
       counts may cost one count's worth. */
    if (arrived_along >= 0) {
        const output_t *o = &board.config.output[on];
        const bool vertical = board_crossed_vertically;
        const int along = vertical ? world[on].x : world[on].y;
        const int slack = vertical ? o->speed_x : o->speed_y;
        /* A move back along the chain stops a third of the way to an edge
           with a screen past it, so a fast Windows cannot run onto that
           screen: a third in from an edge, or within a walk's push of it. */
        const bool chain_vertical = o->chain_direction == TOP || o->chain_direction == BOTTOM;
        const bool along_chain = chain_vertical != vertical;
        const bool held_short = along_chain &&
                                (abs(along - MAX_SCREEN_COORD / 3) <= 12 * slack ||
                                 abs(along - 2 * MAX_SCREEN_COORD / 3) <= 12 * slack ||
                                 abs(along - arrived_along) <= 10 * slack);
        if (abs(along - arrived_along) > slack && !held_short)
            fail("relative arrival: cursor is not under where it crossed");
        /* Across the seam: on the edge it came in by, not mid-screen (the
           #310 hardware report). Only a screen that touches the seam has
           that edge; a walk along the chain ends on such a screen. */
        const int across = vertical ? world[on].y : world[on].x;
        const int edge = arrived_direction == BOTTOM || arrived_direction == RIGHT
                             ? MIN_SCREEN_COORD : MAX_SCREEN_COORD;
        if (along_chain && across != edge)
            fail("relative arrival: cursor is not on the edge it came in by");
    }
}

/* With a wrong Windows gain the board may cross early or late, but a long
   push must not leave it stuck: the board and the cursor end on the same
   screen of the same computer, so every screen stays reachable. */
static void check_same_screen_after_push(void) {
    checks++;
    const int on = board.active_output;
    if ((int)board.config.output[on].screen_index != world[on].screen) {
        char what[96];
        snprintf(what, sizeof what, "gain %d%% (-1: enhanced): after a long push board thinks %c screen %d, "
                 "cursor is on %d", windows_gain, 'A' + on,
                 board.config.output[on].screen_index, world[on].screen);
        fail(what);
    }
}

/* One mouse report through the same steps as process_mouse_report. */
static void report(int dx, int dy) {
    mouse_values_t values = {.move_x = dx, .move_y = dy};
    global_state = board;
    const enum screen_pos_e direction = update_mouse_position(&board, &values);
    const int source_along = direction == TOP || direction == BOTTOM ? board.pointer_x
                                                                     : board.pointer_y;
    mouse_report_t r = create_mouse_report(&board, &values);
    output_mouse_report(&r, &board);
    const int before_output = board.active_output;
    /* An unanchored estimate is not where the cursor left: the source query
       reads where it really did. */
    const computer_t *source = &world[before_output];
    const int left_along = !unanchored ? source_along
                           : direction == TOP || direction == BOTTOM ? source->x : source->y;
    const int before_screen = board.config.output[before_output].screen_index;
    const bool was_unanchored = unanchored;
    if (direction != NONE)
        do_screen_switch(&board, direction);
    settle();
    attempted = direction != NONE;
    switched = board.active_output != before_output ||
               (int)board.config.output[before_output].screen_index != before_screen;
    if (answered || !board.relative_mouse || board.active_output != before_output)
        unanchored = false;
    /* Set by a chain crossing off the main screen that the cursor has not
       made; kept by a later one it has not made, from an estimate already out. */
    if (switched && board.active_output == before_output && world[before_output].os == WINDOWS &&
        world[before_output].helper && world[before_output].screen == before_screen &&
        (before_screen == 1 || was_unanchored))
        unanchored = true;
    board_crossed_vertically = direction == TOP || direction == BOTTOM;
    arrived_direction = direction;
    arrived_along = board.active_output != before_output && board.relative_mouse
                        ? left_along : -1;
    /* With a wrong gain the estimate drifts by design; a long push is judged
       by check_same_screen_after_push. */
    if (windows_gain == 100 && !pixel_w)
        check();
}

/* ---- drivers ------------------------------------------------------------- */

static const int directions[4] = {LEFT, RIGHT, TOP, BOTTOM};
static const char *os_name(int os) { return os == MACOS ? "mac" : "win"; }

static void describe(const layout_t *l, const char *driver) {
    snprintf(context, sizeof context,
             "%s: %s A=%s%d chain%d helper%d B=%s%d chain%d helper%d offset%d %s",
             driver, l->arrangement ? "side-by-side" : "stacked", os_name(l->os[0]),
             l->count[0], l->chain[0], l->helper[0], os_name(l->os[1]), l->count[1],
             l->chain[1], l->helper[1], l->offset, l->mapped ? "mapped" : "legacy");
}

/* Every start screen and remembered screen, pushed straight each way. */
static void push_every_way(const layout_t *l) {
    /* Only Windows has a speed of its own. */
    if (windows_gain != 100 && l->os[0] != WINDOWS && l->os[1] != WINDOWS)
        return;
    for (int on = 0; on < 2; on++)
        for (int s = 1; s <= l->count[on]; s++)
            for (int r = 1; r <= l->count[1 - on]; r++)
                for (int d = 0; d < 4; d++) {
                    if (!build(l))
                        return;
                    describe(l, "push");
                    snprintf(context + strlen(context), sizeof context - strlen(context),
                             " from %c%d remembered %d dir %d", 'A' + on, s, r, directions[d]);
                    start(on, s, r, MAX_SCREEN_COORD / 2, MAX_SCREEN_COORD / 3);
                    const int dx = unit_x(directions[d]) * 37;
                    const int dy = unit_y(directions[d]) * 23;
                    /* A slow Windows needs a longer push to reach the wall:
                       5 times, for the slowest gain, 60%, with room to spare. */
                    const int length = windows_gain == 100 ? 160 : 160 * 5;
                    for (int i = 0; i < length; i++)
                        report(dx + (i % 3) - 1, dy + (i % 5) - 2);
                    if (windows_gain != 100)
                        check_same_screen_after_push();
                }
}

static uint32_t seed;
static int next_random(int span) {
    seed = seed * 1664525u + 1013904223u;
    return (int)((seed >> 8) % (uint32_t)span);
}

/* Long wandering strokes, each with its own heading, speed and jitter. */
static void wander(const layout_t *l, uint32_t run_seed) {
    if (!build(l))
        return;
    seed = run_seed;
    /* Speeds the config page allows, where one screen's counts round. */
    static const int speeds[] = {1, 3, 16, 28, 128};
    for (int o = 0; o < 2; o++) {
        board.config.output[o].speed_x = speeds[next_random(5)];
        board.config.output[o].speed_y = speeds[next_random(5)];
    }
    describe(l, "wander");
    snprintf(context + strlen(context), sizeof context - strlen(context),
             " speeds A=%d/%d B=%d/%d", board.config.output[0].speed_x,
             board.config.output[0].speed_y, board.config.output[1].speed_x,
             board.config.output[1].speed_y);
    start(0, 1, 1, MAX_SCREEN_COORD / 2, MAX_SCREEN_COORD / 2);
    for (int stroke = 0; stroke < 60; stroke++) {
        /* Now and then: mouse zoom, or the output hotkey. The hotkey moves
           nothing, so the next report shows where the OS put the cursor. */
        board.mouse_zoom = next_random(6) == 0;
        if (next_random(10) == 0) {
            set_active_output(&board, (uint8_t)(1 - board.active_output));
            cursor_output_switched(&board);
        }
        const int dx = next_random(81) - 40, dy = next_random(81) - 40;
        const int length = 5 + next_random(120);
        for (int i = 0; i < length; i++)
            report(dx + next_random(5) - 2, dy + next_random(5) - 2);
    }
}

static void for_each_layout(void (*run)(const layout_t *)) {
    layout_t l = {0};
    for (l.arrangement = 0; l.arrangement < 2; l.arrangement++)
    for (int os = 0; os < 4; os++)
    for (l.count[0] = 1; l.count[0] <= MAX_CHAIN; l.count[0]++)
    for (l.count[1] = 1; l.count[1] <= MAX_CHAIN; l.count[1]++)
    for (int c0 = 0; c0 < 4; c0++)
    for (int c1 = 0; c1 < 4; c1++)
    for (l.offset = -1; l.offset <= 1; l.offset++)
    for (int m = 0; m < 2; m++)
    for (int h = 0; h < 4; h++) {
        l.os[0] = os & 1 ? WINDOWS : MACOS;
        l.os[1] = os & 2 ? WINDOWS : MACOS;
        l.chain[0] = directions[c0];
        l.chain[1] = directions[c1];
        /* One screen has no chain; try it once. */
        if ((l.count[0] == 1 && c0) || (l.count[1] == 1 && c1))
            continue;
        l.mapped = m;
        l.helper[0] = h & 1;
        l.helper[1] = (h & 2) != 0;
        if (!build(&l))
            continue;
        layouts_run++;
        run(&l);
    }
}

static void run_wander(const layout_t *l) {
    for (uint32_t s = 1; s <= 3; s++)
        wander(l, s * 2654435761u + (uint32_t)layouts_run);
}

/* #310's own layout, as a named case the sweeps also cover. */
static void test_issue_310_layout(void) {
    const layout_t l = {
        .arrangement = 0, .os = {MACOS, WINDOWS}, .count = {2, 2},
        .chain = {LEFT, LEFT}, .offset = 0, .mapped = true, .helper = {false, false},
    };
    push_every_way(&l);
    if (windows_gain == 100)
        run_wander(&l);
}

/* #317: #310's desk with Windows' helper on, from the middle of BR, left
   until the board crosses onto BL. */
static void cross_br_to_bl_with_helper(void) {
    const layout_t l = {
        .arrangement = 0, .os = {MACOS, WINDOWS}, .count = {2, 2},
        .chain = {LEFT, LEFT}, .offset = 0, .mapped = true, .helper = {false, true},
    };
    build(&l);
    start(1, 1, 2, MAX_SCREEN_COORD / 2, MAX_SCREEN_COORD / 2);
    for (int i = 0; i < 1000 && board.config.output[1].screen_index == 1; i++) {
        reports_sent = 0;
        report(-37, 0);
    }
}

/* #317: BR -> BL sends no walk, only the user's own report; the next one
   takes the real cursor onto BL. */
static void test_issue_317_no_walk_with_helper(void) {
    snprintf(context, sizeof context, "#317: BR -> BL, helper on");
    cross_br_to_bl_with_helper();
    if (board.config.output[1].screen_index != 2)
        fail("the board never crossed onto BL");
    if (reports_sent != 1)
        fail("BR -> BL with the helper on sent walk reports");
    report(-37, 0);
    if (world[1].screen != 2)
        fail("the next report did not take the cursor onto BL");
}

/* #317: touch BR's left edge, then move only straight up: the helper says
   the cursor is still on BR, so it lands on the Mac monitor above BR. */
static void test_issue_317_edge_then_up(void) {
    snprintf(context, sizeof context, "#317: BR edge then up, helper on");
    cross_br_to_bl_with_helper();
    for (int i = 0; i < 1000 && board.active_output == 1; i++)
        report(0, -23);
    if (board.active_output != 0 || world[0].screen != 1 ||
        board.config.output[0].screen_index != 1)
        fail("BR edge then up did not land on the Mac monitor above BR");
}

/* #319: #310's desk without helpers, Mac TL <-> TR. The crossing sends the
   user's report, the walk's absolute edge report and one push, and the
   cursor is on the new monitor. */
static void test_issue_319_mac_walk_is_one_push(void) {
    const layout_t l = {
        .arrangement = 0, .os = {MACOS, WINDOWS}, .count = {2, 2},
        .chain = {LEFT, LEFT}, .offset = 0, .mapped = true, .helper = {false, false},
    };
    for (int from = 1; from <= 2; from++) {
        const int to = 3 - from, dx = from == 2 ? 37 : -37;
        snprintf(context, sizeof context, "#319: Mac screen %d -> %d, helper off", from, to);
        build(&l);
        start(0, from, 1, MAX_SCREEN_COORD / 2, MAX_SCREEN_COORD / 2);
        for (int i = 0; i < 1000 && board.config.output[0].screen_index == from; i++) {
            reports_sent = 0;
            report(dx, 0);
        }
        if (board.config.output[0].screen_index != to || world[0].screen != to)
            fail("did not cross");
        if (reports_sent != 3)
            fail("the walk is not one push");
    }
}

/* ---- a desk that branches ------------------------------------------------ */

/* The L desk: A's monitors branch off main, B is a straight line.
 *   A2 A1 | B1
 *      A3 | B2      (B2 only when B has two)
 * A's chain runs left to A2; A3 turns counter-clockwise off it, below main. */
static layout_t l_desk(int os_a, int os_b, int b_count, bool helper_a, bool helper_b) {
    return (layout_t){
        .arrangement = 1, .os = {os_a, os_b}, .count = {3, b_count},
        .chain = {LEFT, BOTTOM}, .offset = 0, .mapped = true,
        .helper = {helper_a, helper_b},
        .turns = {DH_MOUSE_TURN(3, DH_MOUSE_TURN_COUNTER_CLOCKWISE), 0},
        .a_cells = true, .cell = {{0, 0}, {0, 0}, {-1, 0}, {0, 1}},
    };
}

/* Pushes one way until the board leaves `screen` of `output`, or gives up. */
static void push_off(int dx, int dy) {
    const int output = board.active_output;
    const int screen = board.config.output[output].screen_index;
    for (int i = 0; i < 1000 && board.active_output == output &&
                    (int)board.config.output[output].screen_index == screen; i++)
        report(dx, dy);
}

static void expect_on(int output, int screen, const char *step) {
    if (board.active_output != output || (int)board.config.output[output].screen_index != screen ||
        world[output].screen != screen) {
        char what[120];
        snprintf(what, sizeof what, "%s: board on %c%d, cursor on %c%d", step,
                 'A' + board.active_output, board.config.output[board.active_output].screen_index,
                 'A' + output, world[output].screen);
        fail(what);
    }
}

/* The L desk with B of one or two monitors, every pair of systems, each
 * helper on and off. Not with Windows' helper on A: the helpers answer a
 * position query only along the line of the last PLACE, and a Windows cursor
 * moves between lines by itself with no PLACE, so a query off that line gets
 * no answer. A Mac is only queried right after a PLACE. A helper that
 * answered along any line would lift this. (A straight line has the same
 * desk under check()'s #317 allowance in the layout sweeps.) */
static void for_each_l_desk(void (*run)(const layout_t *)) {
    for (int b_count = 1; b_count <= 2; b_count++)
        for (int h = 0; h < 4; h++)
            for (int os = 0; os < 4; os++) {
                const layout_t l = l_desk(os & 1 ? WINDOWS : MACOS, os & 2 ? WINDOWS : MACOS,
                                          b_count, h & 1, (h & 2) != 0);
                if (l.os[0] == WINDOWS && l.helper[0])
                    continue;
                run(&l);
            }
}

/* B1 -> A1 -> A2 -> A1 -> A3, then right: to B2, or a wall when B has one. */
static void tour_l_desk(const layout_t *l) {
    describe(l, "L desk tour");
    if (!build(l)) {
        fail("the L desk does not build");
        return;
    }
    start(1, 1, 1, MAX_SCREEN_COORD / 2, MAX_SCREEN_COORD / 2);
    push_off(-37, 0);
    expect_on(0, 1, "B1 left");
    push_off(-37, 0);
    expect_on(0, 2, "A1 left");
    push_off(37, 0);
    expect_on(0, 1, "A2 right");
    push_off(0, 23);
    expect_on(0, 3, "A1 down");
    push_off(37, 0);
    expect_on(l->count[1] == 2 ? 1 : 0, l->count[1] == 2 ? 2 : 3, "A3 right");
}

/* Every start and every way, then long wanders: the board always knows
   which monitor the cursor is on. */
static void sweep_l_desk(const layout_t *l) {
    push_every_way(l);
    run_wander(l);
}

/* ---- Windows in real pixels (#312) ---------------------------------------- */

static const int windows_mult32[DH_WINDOWS_POINTER_STEPS] = DH_WINDOWS_POINTER_MULT32;

/* The config page's Speed for one axis: round(32768 ÷ pixels × multiplier). */
static int page_speed(int pixels, int mult32) {
    const int speed = (int)((32768L * mult32 * 2 / pixels / 32 + 1) / 2);
    return speed < 1 ? 1 : speed > 128 ? 128 : speed;
}

/* #310's desk (Mac above, Windows below, both chains left), helpers as
   pixel_helpers says, with the pointer speed and monitor size saved as the
   page saves them. */
static void build_pixel_desk(int step, int w, int h) {
    const layout_t l = {
        .arrangement = 0, .os = {MACOS, WINDOWS}, .count = {2, 2},
        .chain = {LEFT, LEFT}, .offset = 0, .mapped = true,
        .helper = {pixel_helpers, pixel_helpers},
    };
    pixel_w = w;
    pixel_h = h;
    pixel_mult32 = windows_mult32[step - 1];
    build(&l);
    output_t *win = &board.config.output[1];
    win->speed_x = page_speed(w, pixel_mult32);
    win->speed_y = page_speed(h, pixel_mult32);
    win->pointer_speed = (uint8_t)step;
    win->monitor_width = (uint16_t)w;
    win->monitor_height = (uint16_t)h;
}

/* One report, then: on a relative Windows screen the board's estimate is in
   the pixel the cursor is in, on the same screen, unless it is unanchored
   (#317; see check()). */
static void pixel_report(int dx, int dy) {
    report(dx, dy);
    const computer_t *win = &world[1];
    if (!board.relative_mouse || board.active_output != 1 || unanchored)
        return;
    const long x = (long)board.pointer_x * pixel_w / (MAX_SCREEN_COORD + 1);
    const long y = (long)board.pointer_y * pixel_h / (MAX_SCREEN_COORD + 1);
    if ((int)board.config.output[1].screen_index != win->screen || x != win->px32_x / 32 ||
        y != win->px32_y / 32) {
        char what[120];
        snprintf(what, sizeof what, "estimate screen %d pixel %ld,%ld; cursor screen %d pixel %ld,%ld",
                 board.config.output[1].screen_index, x, y, win->screen, win->px32_x / 32,
                 win->px32_y / 32);
        fail(what);
    }
}

/* Up from the cursor on Windows into the Mac, in strokes of `stroke` counts.
   Fails unless the board crosses on the report that takes the cursor to the
   top edge, or after at most one more report or one more pixel's worth of
   counts (the cursor can stop exactly on the edge, and below ×1 Windows needs
   that many to move a pixel), and the Mac's cursor lands within 2 pixels of
   under the Windows one. */
static void push_up_into_mac(int stroke) {
    computer_t *win = &world[1];
    long late_counts = 0;
    for (int i = 0; i < 100000 && board.active_output == 1; i++) {
        if (win->px32_y < 32)
            late_counts += stroke;
        pixel_report(0, -stroke);
    }
    /* Leaving parks the Windows cursor; where the last relative report left it counts. */
    const long win_y = last_rel_px32_y / 32, win_x = last_rel_px32_x / 32;
    const long mac_x = (long)board.pointer_x * pixel_w / (MAX_SCREEN_COORD + 1);
    char what[96];
    if (board.active_output != 0 || world[0].screen != 2)
        fail("never crossed into the Mac's screen 2");
    else if (win_y > 0) {
        snprintf(what, sizeof what, "crossed %ld px before the top edge", win_y);
        fail(what);
    } else if (late_counts > stroke + (32 + pixel_mult32 - 1) / pixel_mult32) {
        snprintf(what, sizeof what, "crossed %ld counts after the top edge", late_counts);
        fail(what);
    } else if (labs(mac_x - win_x) > 2) {
        snprintf(what, sizeof what, "Mac cursor %ld px sideways of the Windows one",
                 mac_x - win_x);
        fail(what);
    }
}

/* Each report counts: slow strokes, which move less than a pixel per report
   at a low pointer speed (the cursor creeps), to fast ones. */
static const int pixel_strokes[] = {1, 2, 3, 5, 7, 15, 40, 127};
static const int pixel_sizes[][2] = {{1920, 1080}, {2560, 1440}, {3840, 2160}};

static void for_each_pixel_case(void (*run)(int step, int w, int h, int stroke)) {
    for (int helpers = 0; helpers < 2; helpers++) {
        pixel_helpers = helpers;
        for (size_t z = 0; z < 3; z++)
            for (int step = 1; step <= (int)DH_WINDOWS_POINTER_STEPS; step++)
                for (size_t s = 0; s < sizeof pixel_strokes / sizeof pixel_strokes[0]; s++) {
                    const int stroke = pixel_strokes[s];
                    snprintf(context, sizeof context,
                             "pixels %dx%d pointer speed %d stroke %d helpers %d",
                             pixel_sizes[z][0], pixel_sizes[z][1], step, stroke, helpers);
                    run(step, pixel_sizes[z][0], pixel_sizes[z][1], stroke);
                }
    }
    pixel_w = 0;
    pixel_helpers = false;
}

/* #314: from the middle of Windows' second monitor (BL), sideways a while,
   then up into the Mac. */
static void bl_to_mac(int step, int w, int h, int stroke) {
    build_pixel_desk(step, w, h);
    start(1, 2, 2, MAX_SCREEN_COORD / 2, MAX_SCREEN_COORD * 3 / 4);
    place_px(&world[1]);
    /* A sixth of the monitor right, so the x estimate is tested too. */
    const long right_of = world[1].px32_x + (long)w / 6 * 32;
    for (int i = 0; i < 100000 && world[1].px32_x < right_of; i++)
        pixel_report(stroke, 0);
    push_up_into_mac(stroke);
}

/* #315: from the middle of Windows' main monitor (BR) left onto the second
   (BL), where the board's walk puts the cursor, a third of the way in, then
   up into the Mac. The crossing sends the user's report, the walk's absolute
   edge report and one push (#318); with the helpers on, no walk (#317). */
static void br_to_bl_to_mac(int step, int w, int h, int stroke) {
    build_pixel_desk(step, w, h);
    start(1, 1, 2, MAX_SCREEN_COORD / 2, MAX_SCREEN_COORD * 3 / 4);
    place_px(&world[1]);
    computer_t *win = &world[1];
    for (int i = 0; i < 100000 && !(win->screen == 2 && win->px32_x < (long)w * 32 * 2 / 3); i++) {
        const int before = board.config.output[1].screen_index;
        reports_sent = 0;
        pixel_report(-stroke, 0);
        if (before == 1 && board.config.output[1].screen_index == 2 &&
            reports_sent != (pixel_helpers ? 1 : 3))
            fail(pixel_helpers ? "BR -> BL, helper on: sent walk reports"
                               : "BR -> BL, helper off: the walk is not one push");
    }
    if (board.config.output[1].screen_index != 2) {
        fail("the board did not follow the cursor onto Windows' second monitor");
        return;
    }
    push_up_into_mac(stroke);
}

/* #318: from BL right onto BR, then back left onto BL. The last relative
   report on BL can leave Windows a part-pixel to the right, and BR's
   absolute reports keep it, so the walk's one push left must still move
   the cursor a whole pixel past BR's edge. With the helpers on there is no
   walk, and the user's own pushes cross. */
static void bl_to_br_to_bl(int step, int w, int h, int stroke) {
    build_pixel_desk(step, w, h);
    start(1, 2, 2, MAX_SCREEN_COORD / 2, MAX_SCREEN_COORD * 3 / 4);
    place_px(&world[1]);
    for (int i = 0; i < 100000 && board.config.output[1].screen_index == 2; i++)
        pixel_report(stroke, 0);
    for (int i = 0; i < 100000 && board.config.output[1].screen_index == 1; i++)
        pixel_report(-stroke, 0);
    /* With the helper on there is no walk: the user's next pixel crosses (#317). */
    for (int i = 0; i < 100000 && pixel_helpers && world[1].screen == 1 &&
                    board.config.output[1].screen_index == 2; i++)
        pixel_report(-stroke, 0);
    if (board.config.output[1].screen_index != 2 || world[1].screen != 2)
        fail("BL -> BR -> BL: did not land on BL");
}

/* #315: down from the Mac's second monitor (TL) onto Windows' second (BL).
   The walk (or with the helpers on, PLACE) must leave the cursor on BL's top
   edge, within 2 pixels of under where it left the Mac, with the estimate on
   it; then a short move down. */
static void mac_to_bl(int step, int w, int h, int stroke) {
    build_pixel_desk(step, w, h);
    const int mac_x = MAX_SCREEN_COORD / 3;
    start(0, 2, 2, mac_x, MAX_SCREEN_COORD / 2);
    for (int i = 0; i < 100000 && board.active_output == 0; i++)
        pixel_report(0, stroke);
    const computer_t *win = &world[1];
    const long under = (long)mac_x * w / (MAX_SCREEN_COORD + 1), x = win->px32_x / 32;
    char what[96];
    if (board.active_output != 1 || win->screen != 2) {
        fail("never crossed onto Windows' second monitor");
        return;
    }
    if (labs(x - under) > 2 || win->px32_y / 32 > stroke * pixel_mult32 / 32) {
        snprintf(what, sizeof what, "landed at pixel %ld,%ld, not on the top edge at x %ld", x,
                 win->px32_y / 32, under);
        fail(what);
    }
    for (int i = 0; i < 20; i++)
        pixel_report(0, stroke);
}

int main(void) {
    for_each_pixel_case(bl_to_mac);
    for_each_pixel_case(br_to_bl_to_mac);
    for_each_pixel_case(mac_to_bl);
    for_each_pixel_case(bl_to_br_to_bl);
    test_issue_310_layout();
    test_issue_317_no_walk_with_helper();
    test_issue_317_edge_then_up();
    test_issue_319_mac_walk_is_one_push();
    for_each_l_desk(tour_l_desk);
    for_each_l_desk(sweep_l_desk);
    for_each_layout(push_every_way);
    static const int gains[] = {60, 80, 125, 160, ENHANCED_PRECISION};
    for (size_t g = 0; g < sizeof gains / sizeof gains[0]; g++) {
        windows_gain = gains[g];
        test_issue_310_layout();
        for_each_layout(push_every_way);
    }
    windows_gain = 100;
    layouts_run = 0;
    for_each_layout(run_wander);
    if (failures) {
        fprintf(stderr, "crossing_model_test: %d failures in %lu checks\n", failures, checks);
        return 1;
    }
    printf("crossing_model_test: %lu layouts, %lu checks passed\n", layouts_run, checks);
    return 0;
}
