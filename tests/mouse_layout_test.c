/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

/*
 * Host coverage for the mouse layout seam (#24).
 *
 * The firmware adapter owns I/O; this suite drives the pure transition
 * decision with the same per-output state that update_mouse_position() and
 * do_screen_switch() use.
 */
#include <stdio.h>

#include "dh_mouse_layout.h"

static int failures;

#define CHECK(condition, name, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s: %s\n", name, message); \
        failures++; \
    } \
} while (0)

static void test_side_by_side_layout_preserves_current_switching(void) {
    const dh_mouse_layout_t output_a = {
        .chain_direction = DH_DIRECTION_RIGHT,
        .border_direction = DH_DIRECTION_LEFT,
    };

    CHECK(dh_mouse_transition_for(&output_a, 1, 2, DH_DIRECTION_LEFT) ==
              DH_MOUSE_TRANSITION_OUTPUT,
          "side_by_side", "output A did not cross left from its border-adjacent monitor");
    CHECK(dh_mouse_transition_for(&output_a, 1, 2, DH_DIRECTION_RIGHT) ==
              DH_MOUSE_TRANSITION_CHAIN_FORWARD,
          "side_by_side", "output A did not move right to its second monitor");
    CHECK(dh_mouse_transition_for(&output_a, 2, 2, DH_DIRECTION_LEFT) ==
              DH_MOUSE_TRANSITION_CHAIN_BACK,
          "side_by_side", "output A crossed computers from a non-border monitor");
    CHECK(dh_mouse_transition_for(&output_a, 2, 2, DH_DIRECTION_RIGHT) ==
              DH_MOUSE_TRANSITION_NONE,
          "side_by_side", "output A moved past the end of its monitor chain");

    CHECK(dh_mouse_jump_threshold_for(&output_a, 1, 2, DH_DIRECTION_LEFT, 700) == 700,
          "threshold", "the inter-computer crossing lost its configured threshold");
    CHECK(dh_mouse_jump_threshold_for(&output_a, 1, 2, DH_DIRECTION_RIGHT, 700) == 0,
          "threshold", "a local virtual-desktop switch gained a jump threshold");
    CHECK(dh_mouse_neighbour(&output_a, 1, 2, DH_DIRECTION_RIGHT) == 2,
          "virtual_desktop", "the forward chain move chose the wrong screen");
    CHECK(dh_mouse_neighbour(&output_a, 2, 2, DH_DIRECTION_LEFT) == 1,
          "virtual_desktop", "the backward chain move chose the wrong screen");

    CHECK(dh_mouse_park_coordinate(0, 1234, 0, 32767) == 0,
          "parking", "top parking moved away from the top coordinate");
    CHECK(dh_mouse_park_coordinate(1, 1234, 0, 32767) == 32767,
          "parking", "bottom parking moved away from the bottom coordinate");
    CHECK(dh_mouse_park_coordinate(3, 1234, 0, 32767) == 1234,
          "parking", "previous-position parking stopped preserving the coordinate");
}

static void test_chain_direction_is_independent_of_the_border(void) {
    const dh_mouse_layout_t perpendicular = {
        .chain_direction = DH_DIRECTION_RIGHT,
        .border_direction = DH_DIRECTION_TOP,
    };

    CHECK(dh_mouse_transition_for(&perpendicular, 2, 2, DH_DIRECTION_LEFT) ==
              DH_MOUSE_TRANSITION_CHAIN_BACK,
          "independent_axes", "chain-back movement was derived from the border direction");
    CHECK(dh_mouse_transition_for(&perpendicular, 2, 2, DH_DIRECTION_TOP) ==
              DH_MOUSE_TRANSITION_OUTPUT,
          "independent_axes", "perpendicular seam did not cross from every monitor");

    const dh_mouse_layout_t vertical_parallel = {
        .chain_direction = DH_DIRECTION_BOTTOM,
        .border_direction = DH_DIRECTION_TOP,
    };
    CHECK(dh_mouse_transition_for(&vertical_parallel, 2, 2, DH_DIRECTION_TOP) ==
              DH_MOUSE_TRANSITION_CHAIN_BACK,
          "parallel_axes", "parallel seam crossed before reaching the adjacent monitor");
    CHECK(dh_mouse_transition_for(&vertical_parallel, 1, 2, DH_DIRECTION_TOP) ==
              DH_MOUSE_TRANSITION_OUTPUT,
          "parallel_axes", "parallel seam did not cross from its adjacent monitor");
}

static void test_vertical_coordinate_actions_use_the_y_axis(void) {
    dh_mouse_coordinates_t coordinates = dh_mouse_hidden_coordinates(
        DH_DIRECTION_TOP, 2, (dh_mouse_coordinates_t){.x = 1234, .y = 5678}, 0, 32767);
    CHECK(coordinates.x == 1234 && coordinates.y == 32767,
          "vertical_parking", "vertical crossing did not park using the live X coordinate");

    coordinates = dh_mouse_entry_coordinates(
        DH_DIRECTION_TOP, (dh_mouse_coordinates_t){.x = 1234, .y = 5678}, 0, 32767);
    CHECK(coordinates.x == 1234 && coordinates.y == 32767,
          "vertical_entry", "top crossing did not enter through the target's bottom edge");

    coordinates = dh_mouse_edge_coordinates(
        DH_DIRECTION_BOTTOM, (dh_mouse_coordinates_t){.x = 1234, .y = 5678}, 0, 32767);
    CHECK(coordinates.x == 1234 && coordinates.y == 32767,
          "vertical_desktop", "bottom desktop switch did not use the live X coordinate");

    coordinates = dh_mouse_nudge(DH_DIRECTION_TOP, 10);
    CHECK(coordinates.x == 0 && coordinates.y == -10,
          "vertical_desktop", "top desktop switch did not nudge upward on Y");
}

static void test_seam_position_uses_the_axis_along_the_seam(void) {
    const dh_mouse_coordinates_t pointer = {.x = 1234, .y = 5678};
    CHECK(dh_mouse_along_seam(DH_DIRECTION_TOP, pointer) == 1234,
          "seam_axis", "top seam calibration did not use X");
    CHECK(dh_mouse_along_seam(DH_DIRECTION_BOTTOM, pointer) == 1234,
          "seam_axis", "bottom seam calibration did not use X");
    CHECK(dh_mouse_along_seam(DH_DIRECTION_LEFT, pointer) == 5678,
          "seam_axis", "left seam calibration did not use Y");
    CHECK(dh_mouse_along_seam(DH_DIRECTION_RIGHT, pointer) == 5678,
          "seam_axis", "right seam calibration did not use Y");
}

/* The L desk: A2 left of A1, A3 below it, B to the right.
 *   A2 A1 | B1
 *      A3 | B2
 * Monitor 3 turns counter-clockwise off a leftward chain, so it sits below. */
static const dh_mouse_layout_t l_desk = {
    .chain_direction = DH_DIRECTION_LEFT,
    .border_direction = DH_DIRECTION_RIGHT,
    .turns = DH_MOUSE_TURN(3, DH_MOUSE_TURN_COUNTER_CLOCKWISE),
};

static void test_monitors_can_branch_off_main(void) {
    CHECK(dh_mouse_neighbour(&l_desk, 1, 3, DH_DIRECTION_LEFT) == 2,
          "branch", "main did not lead left to monitor 2");
    CHECK(dh_mouse_neighbour(&l_desk, 1, 3, DH_DIRECTION_BOTTOM) == 3,
          "branch", "main did not lead down to monitor 3");
    CHECK(dh_mouse_neighbour(&l_desk, 1, 3, DH_DIRECTION_TOP) == 0,
          "branch", "main led up to a monitor that is not there");
    CHECK(dh_mouse_neighbour(&l_desk, 2, 3, DH_DIRECTION_RIGHT) == 1,
          "branch", "monitor 2 did not lead back right to main");
    CHECK(dh_mouse_neighbour(&l_desk, 2, 3, DH_DIRECTION_BOTTOM) == 0,
          "branch", "monitor 2 led down although monitor 3 is only diagonal to it");
    CHECK(dh_mouse_neighbour(&l_desk, 3, 3, DH_DIRECTION_TOP) == 1,
          "branch", "monitor 3 did not lead back up to main");
    CHECK(dh_mouse_neighbour(&l_desk, 3, 3, DH_DIRECTION_LEFT) == 0,
          "branch", "monitor 3 led left although monitor 2 is only diagonal to it");
}

static void test_a_branch_crosses_where_it_faces_the_other_computer(void) {
    CHECK(dh_mouse_transition_for(&l_desk, 1, 3, DH_DIRECTION_RIGHT) ==
              DH_MOUSE_TRANSITION_OUTPUT,
          "branch_border", "main did not cross right to B1");
    CHECK(dh_mouse_transition_for(&l_desk, 3, 3, DH_DIRECTION_RIGHT) ==
              DH_MOUSE_TRANSITION_OUTPUT,
          "branch_border", "monitor 3 did not cross right to B2");
    CHECK(dh_mouse_transition_for(&l_desk, 2, 3, DH_DIRECTION_RIGHT) !=
              DH_MOUSE_TRANSITION_OUTPUT,
          "branch_border", "monitor 2 skipped main and crossed to B");
    CHECK(dh_mouse_transition_for(&l_desk, 1, 3, DH_DIRECTION_BOTTOM) !=
              DH_MOUSE_TRANSITION_NONE,
          "branch_border", "main did not move down to monitor 3");
    CHECK(dh_mouse_transition_for(&l_desk, 3, 3, DH_DIRECTION_LEFT) ==
              DH_MOUSE_TRANSITION_NONE,
          "branch_border", "monitor 3 moved left into nothing");
    CHECK(dh_mouse_jump_threshold_for(&l_desk, 1, 3, DH_DIRECTION_BOTTOM, 700) == 0,
          "branch_border", "a move onto a branch gained the seam threshold");
}

static void test_a_walk_between_branches_goes_through_main(void) {
    dh_direction_t steps[DH_MOUSE_ROUTE_CAPACITY];
    CHECK(dh_mouse_route(&l_desk, 3, 2, 3, steps) == 2 &&
              steps[0] == DH_DIRECTION_RIGHT && steps[1] == DH_DIRECTION_BOTTOM,
          "route", "monitor 2 to 3 did not go right to main, then down");
    CHECK(dh_mouse_route(&l_desk, 3, 3, 2, steps) == 2 &&
              steps[0] == DH_DIRECTION_TOP && steps[1] == DH_DIRECTION_LEFT,
          "route", "monitor 3 to 2 did not go up to main, then left");
    CHECK(dh_mouse_route(&l_desk, 3, 1, 3, steps) == 1 && steps[0] == DH_DIRECTION_BOTTOM,
          "route", "main to monitor 3 was not one step down");
    CHECK(dh_mouse_route(&l_desk, 3, 2, 2, steps) == 0,
          "route", "a walk to the monitor already under the cursor took steps");

    const dh_mouse_layout_t straight = {
        .chain_direction = DH_DIRECTION_RIGHT,
        .border_direction = DH_DIRECTION_LEFT,
    };
    CHECK(dh_mouse_route(&straight, 3, 1, 3, steps) == 2 &&
              steps[0] == DH_DIRECTION_RIGHT && steps[1] == DH_DIRECTION_RIGHT,
          "route", "a straight line stopped walking along its chain");
    CHECK(dh_mouse_route(&straight, 3, 3, 2, steps) == 1 && steps[0] == DH_DIRECTION_LEFT,
          "route", "a straight line did not walk back one monitor");
}

/* A helper finds monitor N by stepping N - 1 displays out of main in one
   direction (dh_place_target), and answers in the same terms. */
static void test_the_helper_is_told_each_monitor_by_its_line(void) {
    dh_direction_t line = DH_DIRECTION_NONE;
    uint8_t index = 0;
    CHECK(dh_mouse_helper_line(&l_desk, 3, 3, &line, &index) &&
              line == DH_DIRECTION_BOTTOM && index == 2,
          "helper_line", "monitor 3 was not one display down from main");
    CHECK(dh_mouse_helper_line(&l_desk, 3, 2, &line, &index) &&
              line == DH_DIRECTION_LEFT && index == 2,
          "helper_line", "monitor 2 was not one display left of main");
    CHECK(dh_mouse_helper_line(&l_desk, 3, 1, &line, &index) && index == 1,
          "helper_line", "main was not the helper's first display");
    CHECK(!dh_mouse_helper_line(&l_desk, 3, 4, &line, &index),
          "helper_line", "a monitor past the count was sent to the helper");

    CHECK(dh_mouse_screen_from_helper(&l_desk, 3, DH_DIRECTION_BOTTOM, 2) == 3,
          "helper_answer", "one display down from main was not monitor 3");
    CHECK(dh_mouse_screen_from_helper(&l_desk, 3, DH_DIRECTION_LEFT, 2) == 2,
          "helper_answer", "one display left of main was not monitor 2");
    CHECK(dh_mouse_screen_from_helper(&l_desk, 3, DH_DIRECTION_BOTTOM, 1) == 1,
          "helper_answer", "the helper's first display was not main");
    CHECK(dh_mouse_screen_from_helper(&l_desk, 3, DH_DIRECTION_RIGHT, 2) == 0,
          "helper_answer", "a display right of main named a monitor that is not there");

    const dh_mouse_layout_t straight = {
        .chain_direction = DH_DIRECTION_RIGHT,
        .border_direction = DH_DIRECTION_LEFT,
    };
    CHECK(dh_mouse_helper_line(&straight, 3, 3, &line, &index) &&
              line == DH_DIRECTION_RIGHT && index == 3,
          "helper_line", "a straight line stopped naming monitors by chain index");
    CHECK(dh_mouse_screen_from_helper(&straight, 3, DH_DIRECTION_RIGHT, 3) == 3,
          "helper_answer", "a straight line stopped reading the chain index back");
}

/* A saved monitor past a lowered Screen Count recovers on the first move
   back along a straight line, as it always has. */
static void test_a_stale_monitor_past_the_count_moves_back(void) {
    const dh_mouse_layout_t straight = {
        .chain_direction = DH_DIRECTION_RIGHT,
        .border_direction = DH_DIRECTION_LEFT,
    };
    CHECK(dh_mouse_transition_for(&straight, 3, 2, DH_DIRECTION_LEFT) ==
              DH_MOUSE_TRANSITION_CHAIN_BACK,
          "stale_screen", "monitor 3 of 2 did not move back along the chain");
    CHECK(dh_mouse_neighbour(&straight, 3, 2, DH_DIRECTION_LEFT) == 2,
          "stale_screen", "monitor 3 of 2 did not move back to monitor 2");
    dh_direction_t steps[DH_MOUSE_ROUTE_CAPACITY];
    CHECK(dh_mouse_route(&straight, 2, 3, 2, steps) == 1 && steps[0] == DH_DIRECTION_LEFT,
          "stale_screen", "a walk from monitor 3 of 2 did not take one step back");
}

int main(void) {
    test_side_by_side_layout_preserves_current_switching();
    test_chain_direction_is_independent_of_the_border();
    test_vertical_coordinate_actions_use_the_y_axis();
    test_seam_position_uses_the_axis_along_the_seam();
    test_monitors_can_branch_off_main();
    test_a_branch_crosses_where_it_faces_the_other_computer();
    test_a_walk_between_branches_goes_through_main();
    test_the_helper_is_told_each_monitor_by_its_line();
    test_a_stale_monitor_past_the_count_moves_back();

    if (failures != 0)
        return 1;

    printf("mouse_layout_test: all checks passed\n");
    return 0;
}
