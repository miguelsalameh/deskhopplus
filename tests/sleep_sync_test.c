/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

/*
 * Sleep sync (#287), on the host: when the active output's board tells its
 * peer to sleep, and when the peer obeys.
 *
 * Style follows status_led_test.c: an assertion macro, a main, a printed
 * failure line, a non-zero exit — no framework.
 */

#include <stdio.h>

#include "dh_sleep_sync.h"

static int failures = 0;

#define CHECK(cond, name, what)                                                 \
    do {                                                                        \
        if (!(cond)) {                                                          \
            ++failures;                                                         \
            printf("FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, (name), (what)); \
        }                                                                       \
    } while (0)

#define SEC 1000000ull
#define T0 (1000 * SEC) /* the boot is long past, so "time zero" is not special */

/* The active board's computer goes to sleep at T0; true when a pass at T0 +
   `after` sends sleep. */
static bool sends_after(bool on, bool active, uint64_t after) {
    dh_sleep_sync_t s = {0};
    dh_sleep_sync_step(&s, on, active, false, T0 - SEC);
    dh_sleep_sync_step(&s, on, active, true, T0);
    return dh_sleep_sync_step(&s, on, active, true, T0 + after);
}

static void test_sleep_is_sent_after_five_seconds(void) {
    CHECK(!sends_after(true, true, 4900000), "wait", "sent sleep after 4.9 s");
    CHECK(sends_after(true, true, 5 * SEC), "wait", "did not send sleep after 5 s");
}

static void test_off_does_nothing(void) {
    CHECK(!sends_after(false, true, 3600 * SEC), "off", "sent sleep with Sleep sync off");
}

/* The computer you are not using sleeps from idle alone (#286). */
static void test_an_inactive_board_never_sends(void) {
    CHECK(!sends_after(true, false, 3600 * SEC), "inactive", "the inactive board sent sleep");
}

static void test_sleep_is_sent_once_per_sleep(void) {
    dh_sleep_sync_t s = {0};
    dh_sleep_sync_step(&s, true, true, true, T0);
    CHECK(dh_sleep_sync_step(&s, true, true, true, T0 + 5 * SEC), "once", "did not send sleep");
    CHECK(!dh_sleep_sync_step(&s, true, true, true, T0 + 6 * SEC), "once",
          "sent sleep a second time in one sleep");
}

/* A wake starts a new sleep: the next one is sent too, 5 s after it begins. */
static void test_a_wake_resets_it(void) {
    dh_sleep_sync_t s = {0};
    dh_sleep_sync_step(&s, true, true, true, T0);
    dh_sleep_sync_step(&s, true, true, true, T0 + 5 * SEC);
    dh_sleep_sync_step(&s, true, true, false, T0 + 60 * SEC);
    dh_sleep_sync_step(&s, true, true, true, T0 + 70 * SEC);
    CHECK(!dh_sleep_sync_step(&s, true, true, true, T0 + 74 * SEC), "wake",
          "the second sleep was timed from the first");
    CHECK(dh_sleep_sync_step(&s, true, true, true, T0 + 75 * SEC), "wake",
          "the second sleep was not sent");
}

/* A USB suspend blip shorter than the wait sends nothing (#286 story 11). */
static void test_a_short_blip_sends_nothing(void) {
    dh_sleep_sync_t s = {0};
    dh_sleep_sync_step(&s, true, true, true, T0);
    dh_sleep_sync_step(&s, true, true, false, T0 + 3 * SEC);
    dh_sleep_sync_step(&s, true, true, true, T0 + 4 * SEC);
    CHECK(!dh_sleep_sync_step(&s, true, true, true, T0 + 6 * SEC), "blip",
          "two short suspends added up to a sleep");
}

/* A sleep stamped a moment after the clock read the caller passes must read
   as "just now", not as 584,000 years ago — the unsigned-difference trap of
   #107. */
static void test_a_stamp_ahead_of_the_clock_is_recent(void) {
    dh_sleep_sync_t s = {0};
    dh_sleep_sync_step(&s, true, true, true, T0 + 1);
    CHECK(!dh_sleep_sync_step(&s, true, true, true, T0), "signed",
          "a sleep stamp ahead of the clock read as long ago");
}

/* The peer presses System Sleep only with the setting on, when its computer
   is not the active output, and while that computer is awake. */
static void test_the_peer_obeys_sleep_only_when_it_should(void) {
    CHECK(dh_sleep_sync_obey_sleep(true, false, false), "obey", "an awake inactive peer did not sleep");
    CHECK(!dh_sleep_sync_obey_sleep(false, false, false), "obey", "obeyed with Sleep sync off");
    CHECK(!dh_sleep_sync_obey_sleep(true, true, false), "obey", "the active output obeyed sleep");
    CHECK(!dh_sleep_sync_obey_sleep(true, false, true), "obey", "pressed sleep on a computer already asleep");
}

/* B's computer sleeps from idle, then you switch to B: that sleep was not
   the active computer's, so A must not sleep (#286 story 6). */
static void test_switching_to_a_sleeping_computer_sends_nothing(void) {
    dh_sleep_sync_t s = {0};
    dh_sleep_sync_step(&s, true, false, true, T0);
    dh_sleep_sync_step(&s, true, false, true, T0 + 60 * SEC);
    CHECK(!dh_sleep_sync_step(&s, true, true, true, T0 + 61 * SEC), "switch",
          "a switch onto a sleeping computer sent sleep");
    CHECK(!dh_sleep_sync_step(&s, true, true, true, T0 + 70 * SEC), "switch",
          "a switch onto a sleeping computer sent sleep 5 s later");
}

/* A sleeps as the active computer, you switch to B within the wait, then
   back to A while it still sleeps: B is the computer you just left, so it
   must not sleep either. */
static void test_a_switch_away_mid_wait_cancels_the_sleep(void) {
    dh_sleep_sync_t s = {0};
    dh_sleep_sync_step(&s, true, true, true, T0);
    dh_sleep_sync_step(&s, true, false, true, T0 + 2 * SEC);
    CHECK(!dh_sleep_sync_step(&s, true, true, true, T0 + 30 * SEC), "switch",
          "a sleep interrupted by a switch away was sent on the switch back");
}

int main(void) {
    test_sleep_is_sent_after_five_seconds();
    test_off_does_nothing();
    test_an_inactive_board_never_sends();
    test_sleep_is_sent_once_per_sleep();
    test_a_wake_resets_it();
    test_a_short_blip_sends_nothing();
    test_a_stamp_ahead_of_the_clock_is_recent();
    test_switching_to_a_sleeping_computer_sends_nothing();
    test_a_switch_away_mid_wait_cancels_the_sleep();
    test_the_peer_obeys_sleep_only_when_it_should();

    if (failures) {
        printf("sleep_sync_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("sleep_sync_test: ok\n");
    return 0;
}
