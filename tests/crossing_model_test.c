/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

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
 * from. Two drivers: every layout × start × direction pushed straight, and
 * long seeded random runs on every layout.
 *
 * The model is ideal on purpose: Windows moves a relative count by the
 * output's speed in board units, with no pointer speed or acceleration of its
 * own, and every screen is the same size and lined up. A failure is a logic
 * fault, not a measurement one. The board's own acceleration and mouse zoom
 * are on, as on hardware.
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
} computer_t;

typedef struct {
    int arrangement;             /* 0: A above B, 1: A left of B */
    int os[2];
    int count[2];
    int chain[2];
    int offset;                  /* B's main screen, along the seam, in cells */
    bool mapped;
    bool helper[2];
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
static unsigned long checks, layouts_run;
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

void output_mouse_report(mouse_report_t *report, device_t *state) {
    computer_t *c = &world[state->active_output];
    const output_t *o = &state->config.output[state->active_output];
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
}

bool channel_output_helper_present(uint8_t output) {
    return world[output].helper;
}

void channel_place_cursor(uint8_t output, uint8_t screen, uint8_t chain, uint8_t border,
                          uint16_t position) {
    (void)chain;
    if (world[output].helper)
        helper_place(output, screen, border, position);
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

/* The helper's answer, then the board's completion of the crossing. */
static void settle(void) {
    for (int round = 0; round < 4 && pending_query; round++) {
        const uint8_t q = pending_query, out = pending_output;
        pending_query = 0;
        const computer_t *c = &world[out];
        (void)apply_helper_cursor_position(&board, out, (uint8_t)c->screen,
                                           (int16_t)c->x, (int16_t)c->y, q);
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
            c->cell_x[k] = bx + (k - 1) * unit_x(l->chain[o]);
            c->cell_y[k] = by + (k - 1) * unit_y(l->chain[o]);
        }
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
    if (!next_report_places && (int)board.config.output[on].screen_index != world[on].screen) {
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
    if (board.relative_mouse &&
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
           screen: a third in from an edge, or within a walk's nudges of it. */
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
    const int before_screen = board.config.output[before_output].screen_index;
    if (direction != NONE)
        do_screen_switch(&board, direction);
    settle();
    attempted = direction != NONE;
    switched = board.active_output != before_output ||
               (int)board.config.output[before_output].screen_index != before_screen;
    board_crossed_vertically = direction == TOP || direction == BOTTOM;
    arrived_direction = direction;
    arrived_along = board.active_output != before_output && board.relative_mouse
                        ? source_along : -1;
    /* With a wrong gain the estimate drifts by design; a long push is judged
       by check_same_screen_after_push. */
    if (windows_gain == 100)
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
    layout_t l;
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

int main(void) {
    test_issue_310_layout();
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
