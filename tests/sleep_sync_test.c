/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

/*
 * Sleep sync (#287, #288, #289), on the host: when the active output's board
 * tells its peer to sleep or wake, when the peer obeys sleep, and when it
 * keeps a wake that came while its computer was still going to sleep.
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

/* Input reaches the active board while its computer sleeps (#288). */
static bool sends_wake(bool on, bool active, bool suspended) {
    dh_sleep_sync_t s = {0};
    return dh_sleep_sync_wake(&s, on, active, suspended);
}

static void test_input_while_asleep_sends_wake(void) {
    CHECK(sends_wake(true, true, true), "wake", "input while asleep did not send wake");
    CHECK(!sends_wake(true, true, false), "wake", "input while awake sent wake");
    CHECK(!sends_wake(false, true, true), "wake", "sent wake with Sleep sync off");
    /* Typing on the computer you use never wakes an idle one (#286 story 7). */
    CHECK(!sends_wake(true, false, true), "wake", "the inactive board sent wake");
}

/* Every report queued while asleep asks again; one wake per sleep is enough. */
static void test_wake_is_sent_once_per_sleep(void) {
    dh_sleep_sync_t s = {0};
    dh_sleep_sync_wake(&s, true, true, true);
    CHECK(!dh_sleep_sync_wake(&s, true, true, true), "wake once", "sent wake twice in one sleep");
    dh_sleep_sync_wake(&s, true, true, false);
    CHECK(dh_sleep_sync_wake(&s, true, true, true), "wake once", "the next sleep sent no wake");
}

/* A key just before the 5 s mark wakes the computer, but the USB resume takes
   a moment, so the next pass still sees it suspended. That sleep is over: the
   peer must not get sleep after the wake. */
static void test_a_wake_cancels_the_pending_sleep(void) {
    dh_sleep_sync_t s = {0};
    dh_sleep_sync_step(&s, true, true, true, T0);
    dh_sleep_sync_wake(&s, true, true, true);
    CHECK(!dh_sleep_sync_step(&s, true, true, true, T0 + 5 * SEC), "wake race",
          "sent sleep after the input that woke the computer");
}

/* The peer pressed System Sleep at T0, and wake arrives at T0 + `wake_at`
   while its computer is still awake. The computer suspends at T0 +
   `suspend_at`. True when a pass then calls remote wakeup (#289). */
static bool owed_wake_fires(uint64_t wake_at, uint64_t suspend_at) {
    dh_sleep_sync_peer_t p = {0};
    dh_sleep_sync_peer_press(&p, T0);
    CHECK(dh_sleep_sync_peer_wake(&p, false, T0 + wake_at) == DH_SLEEP_SYNC_NUDGE, "owed",
          "called remote wakeup on a computer that was not asleep");
    return dh_sleep_sync_peer_step(&p, true, T0 + suspend_at);
}

/* A wake that finds the peer's computer suspended calls remote wakeup. */
static void test_a_suspended_peer_gets_remote_wakeup(void) {
    dh_sleep_sync_peer_t p = {0};
    CHECK(dh_sleep_sync_peer_wake(&p, true, T0) == DH_SLEEP_SYNC_REMOTE_WAKEUP, "peer wake",
          "a suspended peer did not get remote wakeup");
}

/* A Modern Standby PC can sleep without a USB suspend. A wake that finds the
   peer's computer not suspended nudges the pointer instead (#291). */
static void test_a_peer_not_suspended_gets_a_nudge(void) {
    dh_sleep_sync_peer_t p = {0};
    CHECK(dh_sleep_sync_peer_wake(&p, false, T0) == DH_SLEEP_SYNC_NUDGE, "peer wake",
          "a peer that was not suspended got no nudge");
}

/* A wake that finds the computer just suspended is kept too: USB wants 5 ms
   of idle bus before a remote wakeup, so the first try can come too early. */
static void test_a_wake_just_after_the_suspend_retries(void) {
    dh_sleep_sync_peer_t p = {0};
    dh_sleep_sync_peer_press(&p, T0);
    dh_sleep_sync_peer_wake(&p, true, T0 + 3 * SEC);
    CHECK(dh_sleep_sync_peer_step(&p, true, T0 + 3 * SEC + 100000), "early",
          "a wake to a just-suspended computer was not tried again");
}

/* Wake came while the peer was still going to sleep: it wakes the computer
   once the suspend comes (#289). */
static void test_a_wake_during_the_sleep_is_kept(void) {
    CHECK(owed_wake_fires(2 * SEC, 8 * SEC), "owed", "a wake during the sleep was lost");
}

/* The wake is kept only for DH_SLEEP_SYNC_OWED_WAKE_US after the press, so a
   much later idle sleep is left alone. */
static void test_a_kept_wake_expires(void) {
    CHECK(!owed_wake_fires(2 * SEC, 30 * SEC), "owed", "a kept wake fired after the window");
    CHECK(owed_wake_fires(2 * SEC, 30 * SEC - 1), "owed", "a kept wake expired early");
}

/* No press, nothing to keep: a wake to an awake peer is dropped. */
static void test_a_wake_without_a_press_is_dropped(void) {
    dh_sleep_sync_peer_t p = {0};
    dh_sleep_sync_peer_wake(&p, false, T0);
    CHECK(!dh_sleep_sync_peer_step(&p, true, T0 + SEC), "owed", "kept a wake with no sleep press");
}

/* Remote wakeup is tried on every suspended pass in the window. A host can
   resume the device for a moment while it goes to sleep and then suspend it
   again, so a resume does not end the kept wake; only the window does. */
static void test_a_kept_wake_retries_through_a_resume(void) {
    dh_sleep_sync_peer_t p = {0};
    dh_sleep_sync_peer_press(&p, T0);
    dh_sleep_sync_peer_wake(&p, false, T0 + SEC);
    CHECK(dh_sleep_sync_peer_step(&p, true, T0 + 5 * SEC), "retry", "no first try");
    CHECK(dh_sleep_sync_peer_step(&p, true, T0 + 6 * SEC), "retry", "no second try");
    dh_sleep_sync_peer_step(&p, false, T0 + 7 * SEC);
    CHECK(dh_sleep_sync_peer_step(&p, true, T0 + 9 * SEC), "retry",
          "a resume during the sleep ended the kept wake");
}

/* A wake that comes after the window, to a computer still awake, is not kept. */
static void test_a_late_wake_is_dropped(void) {
    CHECK(!owed_wake_fires(30 * SEC, 31 * SEC), "late", "kept a wake that came after the window");
}

/* A new sleep press is a new sleep: it drops a wake kept from before. */
static void test_a_new_press_drops_a_kept_wake(void) {
    dh_sleep_sync_peer_t p = {0};
    dh_sleep_sync_peer_press(&p, T0);
    dh_sleep_sync_peer_wake(&p, false, T0 + SEC);
    dh_sleep_sync_peer_press(&p, T0 + 2 * SEC);
    CHECK(!dh_sleep_sync_peer_step(&p, true, T0 + 5 * SEC), "press",
          "a new sleep press kept the old wake");
}

/* A press stamped a moment after the clock read reads as recent. */
static void test_a_press_ahead_of_the_clock_is_recent(void) {
    dh_sleep_sync_peer_t p = {0};
    dh_sleep_sync_peer_press(&p, T0 + 1);
    dh_sleep_sync_peer_wake(&p, false, T0);
    CHECK(dh_sleep_sync_peer_step(&p, true, T0), "signed",
          "a press stamp ahead of the clock read as long ago");
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
    test_input_while_asleep_sends_wake();
    test_wake_is_sent_once_per_sleep();
    test_a_wake_cancels_the_pending_sleep();
    test_a_suspended_peer_gets_remote_wakeup();
    test_a_peer_not_suspended_gets_a_nudge();
    test_a_wake_during_the_sleep_is_kept();
    test_a_wake_just_after_the_suspend_retries();
    test_a_kept_wake_expires();
    test_a_wake_without_a_press_is_dropped();
    test_a_kept_wake_retries_through_a_resume();
    test_a_late_wake_is_dropped();
    test_a_new_press_drops_a_kept_wake();
    test_a_press_ahead_of_the_clock_is_recent();

    if (failures) {
        printf("sleep_sync_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("sleep_sync_test: ok\n");
    return 0;
}
