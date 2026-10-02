/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

/*
 * Sleep sync (#287, #288, #289), on the host: when the active output's board
 * tells its peer to sleep or wake, when the peer obeys sleep, and when it
 * keeps a wake that came while its computer was still going to sleep. Also
 * how the active board learns that the other computer sleeps or wakes (#304),
 * and when it sleeps its own computer after the setting's idle time (#303),
 * or as soon as the other computer goes to sleep (#306).
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
    dh_sleep_sync_t s = {0};
    dh_sleep_sync_helper_says(&s, true);
    CHECK(!dh_sleep_sync_obey_sleep(true, false, dh_sleep_sync_asleep(&s, false)), "obey",
          "pressed sleep on a computer its helper said was asleep");
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

/* A Modern Standby PC sleeps with no USB suspend; its helper says so
   instead (#293), and that counts the same. */
static void test_the_helper_saying_asleep_counts_as_asleep(void) {
    dh_sleep_sync_t s = {0};
    dh_sleep_sync_helper_says(&s, true);
    dh_sleep_sync_step(&s, true, true, false, T0);
    CHECK(!dh_sleep_sync_step(&s, true, true, false, T0 + 4900000), "helper",
          "sent sleep 4.9 s after the helper said asleep");
    CHECK(dh_sleep_sync_step(&s, true, true, false, T0 + 5 * SEC), "helper",
          "did not send sleep 5 s after the helper said asleep");
}

/* A key or mouse move on a PC its helper called asleep wakes the peer too.
   The input also ends that sleep: a helper killed before it said awake must
   not leave the PC counted asleep, or the next sleep would never be sent. */
static void test_input_ends_a_sleep_the_helper_reported(void) {
    dh_sleep_sync_t s = {0};
    dh_sleep_sync_helper_says(&s, true);
    dh_sleep_sync_step(&s, true, true, false, T0);
    CHECK(dh_sleep_sync_wake(&s, true, true, false), "helper wake",
          "input while the helper said asleep did not send wake");
    dh_sleep_sync_step(&s, true, true, false, T0 + 1 * SEC);
    dh_sleep_sync_wake(&s, true, true, false);

    dh_sleep_sync_helper_says(&s, true);
    dh_sleep_sync_step(&s, true, true, false, T0 + 60 * SEC);
    CHECK(dh_sleep_sync_step(&s, true, true, false, T0 + 65 * SEC), "helper wake",
          "the next sleep after an input wake was not sent");
}

/* The helper's awake ends the sleep with no input: a power-button wake. */
static void test_the_helper_saying_awake_ends_the_sleep(void) {
    dh_sleep_sync_t s = {0};
    dh_sleep_sync_helper_says(&s, true);
    dh_sleep_sync_step(&s, true, true, false, T0);
    dh_sleep_sync_helper_says(&s, false);
    CHECK(!dh_sleep_sync_step(&s, true, true, false, T0 + 5 * SEC), "helper awake",
          "sent sleep after the helper said awake");
}

/* A Mac in a DarkWake ignores a mouse move but wakes for a key (#300), so
   the first move of a sleep its helper reported taps Left Shift. Only on a
   Mac, and only once: the input ends the sleep. */
static void test_a_mac_the_helper_called_asleep_gets_a_key_tap(void) {
    dh_sleep_sync_t s = {0};
    CHECK(!dh_sleep_sync_tap_key(&s, true), "key tap", "tapped a key on an awake Mac");
    dh_sleep_sync_helper_says(&s, true);
    CHECK(!dh_sleep_sync_tap_key(&s, false), "key tap", "tapped a key on a PC");
    CHECK(dh_sleep_sync_tap_key(&s, true), "key tap", "no key tap for a Mac the helper called asleep");
    dh_sleep_sync_wake(&s, true, true, false);
    CHECK(!dh_sleep_sync_tap_key(&s, true), "key tap", "tapped a key again after the input");
}

/* The peer pressed System Sleep at T0, and wake arrives at T0 + `wake_at`
   while its computer is still awake. The computer suspends at T0 +
   `suspend_at`. True when a pass then calls remote wakeup (#289). */
static bool owed_wake_fires(uint64_t wake_at, uint64_t suspend_at) {
    dh_sleep_sync_peer_t p = {0};
    dh_sleep_sync_peer_press(&p, T0);
    dh_sleep_sync_peer_wake(&p, T0 + wake_at);
    return dh_sleep_sync_peer_step(&p, true, T0 + suspend_at);
}

/* A wake that finds the computer just suspended is kept too: USB wants 5 ms
   of idle bus before a remote wakeup, so the first try can come too early. */
static void test_a_wake_just_after_the_suspend_retries(void) {
    dh_sleep_sync_peer_t p = {0};
    dh_sleep_sync_peer_press(&p, T0);
    dh_sleep_sync_peer_wake(&p, T0 + 3 * SEC);
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
    dh_sleep_sync_peer_wake(&p, T0);
    CHECK(!dh_sleep_sync_peer_step(&p, true, T0 + SEC), "owed", "kept a wake with no sleep press");
}

/* Remote wakeup is tried on every suspended pass in the window. A host can
   resume the device for a moment while it goes to sleep and then suspend it
   again, so a resume does not end the kept wake; only the window does. */
static void test_a_kept_wake_retries_through_a_resume(void) {
    dh_sleep_sync_peer_t p = {0};
    dh_sleep_sync_peer_press(&p, T0);
    dh_sleep_sync_peer_wake(&p, T0 + SEC);
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
    dh_sleep_sync_peer_wake(&p, T0 + SEC);
    dh_sleep_sync_peer_press(&p, T0 + 2 * SEC);
    CHECK(!dh_sleep_sync_peer_step(&p, true, T0 + 5 * SEC), "press",
          "a new sleep press kept the old wake");
}

/* A press stamped a moment after the clock read reads as recent. */
static void test_a_press_ahead_of_the_clock_is_recent(void) {
    dh_sleep_sync_peer_t p = {0};
    dh_sleep_sync_peer_press(&p, T0 + 1);
    dh_sleep_sync_peer_wake(&p, T0);
    CHECK(dh_sleep_sync_peer_step(&p, true, T0), "signed",
          "a press stamp ahead of the clock read as long ago");
}

/* macOS reads System Sleep as its power button, and ignores a press shorter
   than 350 ms; one held 1.5 s shows the shutdown dialog (#287, found on
   #293). So the release comes DH_SLEEP_SYNC_HOLD_US after the press, once. */
static void test_the_sleep_press_is_held_before_release(void) {
    dh_sleep_sync_peer_t p = {0};
    CHECK(!dh_sleep_sync_peer_release(&p, T0), "hold", "released with no press");
    dh_sleep_sync_peer_press(&p, T0);
    CHECK(!dh_sleep_sync_peer_release(&p, T0 + 349999), "hold",
          "released before macOS's 350 ms debounce");
    CHECK(dh_sleep_sync_peer_release(&p, T0 + 500000), "hold", "did not release after 0.5 s");
    CHECK(!dh_sleep_sync_peer_release(&p, T0 + 600000), "hold", "released twice");
    CHECK(DH_SLEEP_SYNC_HOLD_US > 350000 && DH_SLEEP_SYNC_HOLD_US < 1500000, "hold",
          "the hold is outside macOS's sleep window");
}

/* Sleep when idle needs the active board to know the other computer sleeps
   (#304). One pass of the board whose computer this is: dh_sleep_sync_step,
   then dh_sleep_sync_tell, as sleep_sync_task does. Returns the heartbeat's
   bit. */
static bool tell(dh_sleep_sync_t *s, bool on, bool active, bool suspended, uint64_t now) {
    dh_sleep_sync_step(s, on, active, suspended, now);
    dh_sleep_sync_tell(s, on, active, now);
    return s->tells_asleep;
}

/* Board B's computer sleeps from idle while A is the active output. A short
   USB suspend says nothing (#286). */
static void test_the_inactive_board_tells_asleep_after_the_wait(void) {
    dh_sleep_sync_t s = {0};
    CHECK(!tell(&s, true, false, false, T0 - SEC), "tell", "said asleep while awake");
    CHECK(!tell(&s, true, false, true, T0), "tell", "said asleep at once");
    CHECK(!tell(&s, true, false, true, T0 + 4900000), "tell", "said asleep before 5 s");
    CHECK(tell(&s, true, false, true, T0 + 5 * SEC), "tell", "did not say asleep after 5 s");
    CHECK(!tell(&s, true, false, false, T0 + 600 * SEC), "tell", "did not say awake on the wake");
}

/* A PC maintenance wake follows every Start > Sleep (#292): awake, then
   asleep again after the wait. */
static void test_a_maintenance_wake_says_awake_then_asleep(void) {
    dh_sleep_sync_t s = {0};
    tell(&s, true, false, true, T0);
    tell(&s, true, false, true, T0 + 5 * SEC);
    CHECK(!tell(&s, true, false, false, T0 + 60 * SEC), "maintenance",
          "the maintenance wake did not say awake");
    CHECK(!tell(&s, true, false, true, T0 + 90 * SEC), "maintenance", "said asleep at once");
    CHECK(tell(&s, true, false, true, T0 + 95 * SEC), "maintenance",
          "the sleep after it did not say asleep");
}

/* Turned off mid-sleep, the peer hears awake, so it holds no stale asleep. */
static void test_tell_says_nothing_while_off(void) {
    dh_sleep_sync_t s = {0};
    tell(&s, false, false, true, T0);
    CHECK(!tell(&s, false, false, true, T0 + 600 * SEC), "tell off",
          "said asleep with Sleep sync off");
    CHECK(tell(&s, true, false, true, T0 + 601 * SEC), "tell off", "turning it on did not say asleep");
    CHECK(!tell(&s, false, false, true, T0 + 602 * SEC), "tell off", "turning it off kept asleep");
}

/* The active board's own sleep goes as DH_SLEEP_SYNC_SLEEP, not as this. */
static void test_the_active_board_does_not_tell_asleep(void) {
    dh_sleep_sync_t s = {0};
    tell(&s, true, true, true, T0);
    CHECK(!tell(&s, true, true, true, T0 + 600 * SEC), "tell active", "the active board said asleep");
}

/* The receiving side: what the active board holds about the other computer,
   and the trace once per change. */
static void test_the_record_follows_the_heartbeat(void) {
    dh_sleep_sync_t a = {0};
    CHECK(!dh_sleep_sync_heard(&a, false), "record", "an unchanged awake was traced");
    CHECK(!dh_sleep_sync_other_asleep(&a, true, true), "record", "asleep before any word");
    CHECK(dh_sleep_sync_heard(&a, true), "record", "the change to asleep was not traced");
    CHECK(!dh_sleep_sync_heard(&a, true), "record", "asleep was traced every heartbeat");
    CHECK(dh_sleep_sync_other_asleep(&a, true, true), "record", "did not record asleep");
    CHECK(!dh_sleep_sync_other_asleep(&a, false, true), "record", "asleep with Sleep sync off");
    CHECK(!dh_sleep_sync_other_asleep(&a, true, false), "record",
          "the inactive board treats its peer as asleep");
    CHECK(dh_sleep_sync_heard(&a, false), "record", "the change to awake was not traced");
    CHECK(!dh_sleep_sync_other_asleep(&a, true, true), "record", "did not record awake");
}

/* Two boards wired together through switches of the active output: one pass
   of B, then the heartbeat to A. `a_active` false means B is active. */
static void pass(dh_sleep_sync_t *a, dh_sleep_sync_t *b, bool a_active, bool b_suspended,
                 uint64_t now) {
    dh_sleep_sync_heard(a, tell(b, true, !a_active, b_suspended, now));
}

/* The PC sleeps, you switch to it, and input wakes it: A must not think it
   still sleeps when you switch back. */
static void test_a_switch_leaves_no_stale_asleep(void) {
    dh_sleep_sync_t a = {0}, b = {0};
    pass(&a, &b, true, true, T0);
    pass(&a, &b, true, true, T0 + 5 * SEC);
    CHECK(dh_sleep_sync_other_asleep(&a, true, true), "switch", "A did not learn the PC sleeps");
    CHECK(!dh_sleep_sync_other_asleep(&a, true, false), "switch",
          "A treats its peer as asleep once B is active");
    pass(&a, &b, false, false, T0 + 60 * SEC); /* switched to B, input woke the PC */
    CHECK(!dh_sleep_sync_other_asleep(&a, true, true), "switch", "a switch back left a stale asleep");
}

/* The PC sleeps while it is the active output, then you switch to the Mac:
   A becomes active and must learn the PC still sleeps. */
static void test_the_board_that_becomes_active_learns_the_real_state(void) {
    dh_sleep_sync_t a = {0}, b = {0};
    pass(&a, &b, false, true, T0);
    pass(&a, &b, false, true, T0 + 600 * SEC);
    CHECK(!dh_sleep_sync_other_asleep(&a, true, true), "switch", "the active PC's sleep was told");
    pass(&a, &b, true, true, T0 + 601 * SEC);
    CHECK(dh_sleep_sync_other_asleep(&a, true, true), "switch",
          "the newly active board did not learn the PC sleeps");
}

/* A reboots while the PC sleeps: the next heartbeat restores its record. */
static void test_a_rebooted_board_learns_again(void) {
    dh_sleep_sync_t a = {0}, b = {0};
    pass(&a, &b, true, true, T0);
    pass(&a, &b, true, true, T0 + 5 * SEC);
    a = (dh_sleep_sync_t){0};
    pass(&a, &b, true, true, T0 + 6 * SEC);
    CHECK(dh_sleep_sync_other_asleep(&a, true, true), "reboot", "a rebooted A forgot the PC sleeps");
}

/* Sleep when idle (#303). The other computer sleeps (A heard it at T0), and
   the last input to A's computer was at `last_input`. True when a pass at
   `now` presses System Sleep on A's computer. */
#define MINUTE (60 * SEC)
static bool idle_sleeps(bool on, uint8_t minutes, bool active, uint64_t last_input, uint64_t now) {
    dh_sleep_sync_t a = {0};
    dh_sleep_sync_heard(&a, true);
    return dh_sleep_sync_idle(&a, on, minutes, active, false, T0 + last_input, T0 - SEC, T0 + now);
}

/* The ticket's example: PC sleeps at 2:00, last Mac input at 1:50, 30
   minutes: the Mac sleeps at 2:20, not before. */
static void test_idle_sleeps_after_the_time(void) {
    CHECK(!idle_sleeps(true, 30, true, 0, 30 * MINUTE - 1), "idle", "slept before 30 minutes");
    CHECK(idle_sleeps(true, 30, true, 0, 30 * MINUTE), "idle", "did not sleep after 30 minutes");
    CHECK(idle_sleeps(true, 15, true, 0, 15 * MINUTE), "idle", "15 minutes did not sleep");
    CHECK(idle_sleeps(true, 120, true, 0, 120 * MINUTE), "idle", "2 hours did not sleep");
    CHECK(!idle_sleeps(true, 120, true, 0, 119 * MINUTE), "idle", "2 hours slept early");
}

/* Never (0), Sleep sync off, or this board not the active output: nothing. */
static void test_idle_off_does_nothing(void) {
    CHECK(!idle_sleeps(true, 0, true, 0, 600 * MINUTE), "idle off", "Never slept");
    CHECK(!idle_sleeps(false, 30, true, 0, 600 * MINUTE), "idle off", "slept with Sleep sync off");
    CHECK(!idle_sleeps(true, 30, false, 0, 600 * MINUTE), "idle off", "the inactive board slept");
}

/* The other computer is awake: nothing, however long the idle. */
static void test_idle_waits_for_the_other_to_sleep(void) {
    dh_sleep_sync_t a = {0};
    CHECK(!dh_sleep_sync_idle(&a, true, 30, true, false, T0, T0 - SEC, T0 + 600 * MINUTE), "idle awake",
          "slept with the other computer awake");
}

/* One press per idle stretch: a computer that ignores it is not pressed again
   every pass. New input starts a new stretch. */
static void test_idle_presses_once_per_stretch(void) {
    dh_sleep_sync_t a = {0};
    dh_sleep_sync_heard(&a, true);
    CHECK(dh_sleep_sync_idle(&a, true, 15, true, false, T0, T0 - SEC, T0 + 15 * MINUTE), "idle once", "no press");
    CHECK(!dh_sleep_sync_idle(&a, true, 15, true, false, T0, T0 - SEC, T0 + 16 * MINUTE), "idle once",
          "pressed twice in one stretch");
    CHECK(!dh_sleep_sync_idle(&a, true, 15, true, false, T0 + 20 * MINUTE, T0 - SEC, T0 + 34 * MINUTE), "idle once",
          "new input did not restart the time");
    CHECK(dh_sleep_sync_idle(&a, true, 15, true, false, T0 + 20 * MINUTE, T0 - SEC, T0 + 35 * MINUTE), "idle once",
          "the next stretch did not press");
}

/* A switch after the last input restarts the time, as the Status LED does. */
static void test_a_switch_restarts_idle(void) {
    dh_sleep_sync_t a = {0};
    dh_sleep_sync_heard(&a, true);
    CHECK(!dh_sleep_sync_idle(&a, true, 15, true, false, T0, T0 + 10 * MINUTE, T0 + 24 * MINUTE),
          "idle switch", "a switch did not restart the time");
    CHECK(dh_sleep_sync_idle(&a, true, 15, true, false, T0, T0 + 10 * MINUTE, T0 + 25 * MINUTE),
          "idle switch", "did not sleep 15 minutes after the switch");
}

/* This computer already sleeps: no press. */
static void test_idle_leaves_a_sleeping_computer_alone(void) {
    dh_sleep_sync_t a = {0};
    dh_sleep_sync_heard(&a, true);
    CHECK(!dh_sleep_sync_idle(&a, true, 15, true, true, T0, T0 - SEC, T0 + 60 * MINUTE), "idle asleep",
          "pressed sleep on a computer already asleep");
}

/* A PC maintenance wake pauses the rule; the sleep after it, with the time
   still counted from the last input, presses at once. */
static void test_a_maintenance_wake_pauses_idle(void) {
    dh_sleep_sync_t a = {0};
    dh_sleep_sync_heard(&a, true);
    dh_sleep_sync_heard(&a, false);
    CHECK(!dh_sleep_sync_idle(&a, true, 15, true, false, T0, T0 - SEC, T0 + 20 * MINUTE), "idle pause",
          "slept during the maintenance wake");
    dh_sleep_sync_heard(&a, true);
    CHECK(dh_sleep_sync_idle(&a, true, 15, true, false, T0, T0 - SEC, T0 + 21 * MINUTE), "idle pause",
          "the sleep after the maintenance wake did not press");
}

/* Both directions: either board, active while the other's computer sleeps,
   sleeps its own. The record comes over the heartbeat (#304). */
static void test_idle_works_both_ways(void) {
    for (int a_active = 0; a_active < 2; ++a_active) {
        dh_sleep_sync_t a = {0}, b = {0};
        dh_sleep_sync_t *active = a_active ? &a : &b, *other = a_active ? &b : &a;
        tell(other, true, false, true, T0);
        dh_sleep_sync_heard(active, tell(other, true, false, true, T0 + 5 * SEC));
        CHECK(dh_sleep_sync_idle(active, true, 15, true, false, T0, T0 - SEC, T0 + 15 * MINUTE), "idle both",
              a_active ? "A did not sleep for B" : "B did not sleep for A");
    }
}

/* 1 minute is a time like the others (#306). */
static void test_idle_one_minute(void) {
    CHECK(!idle_sleeps(true, 1, true, 0, MINUTE - 1), "idle 1 min", "slept before 1 minute");
    CHECK(idle_sleeps(true, 1, true, 0, MINUTE), "idle 1 min", "did not sleep after 1 minute");
}

/* Immediately (#306). A pass of A's rule at `now`, with input a moment ago
   and the last switch long past. */
static bool now_sleeps(dh_sleep_sync_t *a, bool on, bool active, bool asleep, uint64_t now) {
    return dh_sleep_sync_idle(a, on, DH_SLEEP_SYNC_IMMEDIATELY, active, asleep, now - SEC, T0 - 600 * SEC, now);
}

/* A, active, hears the other computer awake on a heartbeat a second from
   `from` up to `to`, and runs its rule on each. */
static void hear_awake(dh_sleep_sync_t *a, uint64_t from, uint64_t to) {
    for (uint64_t t = from; t <= to; t += SEC) {
        dh_sleep_sync_heard(a, false);
        now_sleeps(a, true, true, false, t);
    }
}

/* The other computer goes to sleep: A's computer sleeps on that pass, with
   recent input, and only once however long the other stays asleep. */
static void test_immediately_fires_once_on_the_edge(void) {
    dh_sleep_sync_t a = {0};
    hear_awake(&a, T0 - 60 * SEC, T0);
    dh_sleep_sync_heard(&a, true);
    CHECK(now_sleeps(&a, true, true, false, T0 + SEC), "immediately", "did not sleep on the edge");
    CHECK(!now_sleeps(&a, true, true, false, T0 + 2 * SEC), "immediately", "slept again on the next pass");
    CHECK(!now_sleeps(&a, true, true, false, T0 + 600 * SEC), "immediately", "slept again later");
}

/* You wake both; your computer resumes while the other still reads asleep,
   or never wakes. Yours must stay awake. */
static void test_immediately_does_not_loop_after_a_wake(void) {
    dh_sleep_sync_t a = {0};
    hear_awake(&a, T0 - 60 * SEC, T0);
    dh_sleep_sync_heard(&a, true);
    now_sleeps(&a, true, true, false, T0 + SEC);
    CHECK(!now_sleeps(&a, true, true, true, T0 + 10 * SEC), "immediately wake", "pressed while asleep");
    CHECK(!now_sleeps(&a, true, true, false, T0 + 60 * SEC), "immediately wake",
          "slept again after the wake, the other still asleep");
}

/* A maintenance wake of the other computer, then its re-sleep: a new sleep
   (#306 story 8). */
static void test_immediately_fires_again_after_a_maintenance_wake(void) {
    dh_sleep_sync_t a = {0};
    hear_awake(&a, T0 - 60 * SEC, T0);
    dh_sleep_sync_heard(&a, true);
    now_sleeps(&a, true, true, false, T0 + SEC);
    hear_awake(&a, T0 + 60 * SEC, T0 + 89 * SEC);
    CHECK(!now_sleeps(&a, true, true, false, T0 + 89 * SEC), "immediately again", "slept while the other woke");
    dh_sleep_sync_heard(&a, true);
    CHECK(now_sleeps(&a, true, true, false, T0 + 90 * SEC), "immediately again", "the re-sleep did not count");
}

/* An asleep heard less than DH_SLEEP_SYNC_WAIT_US after the other was first
   heard awake is not a sleep: a real one is told only after the wait. */
static void test_immediately_needs_the_other_awake_first(void) {
    dh_sleep_sync_t a = {0};
    hear_awake(&a, T0, T0 + 4 * SEC);
    dh_sleep_sync_heard(&a, true);
    CHECK(!now_sleeps(&a, true, true, false, T0 + 4900000), "immediately awake first",
          "slept for an asleep heard 4.9 s after awake");
    dh_sleep_sync_t b = {0};
    hear_awake(&b, T0, T0 + 4 * SEC);
    dh_sleep_sync_heard(&b, true);
    CHECK(now_sleeps(&b, true, true, false, T0 + 5 * SEC), "immediately awake first",
          "did not sleep for an asleep heard 5 s after awake");
}

/* You switch to the Mac while the PC sleeps: B sets its bit at once, so A
   hears asleep a second or two after the switch. Not a sleep. */
static void test_immediately_ignores_a_switch(void) {
    dh_sleep_sync_t a = {0}, b = {0};
    pass(&a, &b, false, true, T0); /* B active, the PC asleep */
    pass(&a, &b, false, true, T0 + 600 * SEC);
    now_sleeps(&a, true, false, false, T0 + 600 * SEC);
    CHECK(!now_sleeps(&a, true, true, false, T0 + 601 * SEC), "immediately switch",
          "the switch slept the Mac");
    pass(&a, &b, true, true, T0 + 602 * SEC); /* B hears of the switch */
    CHECK(!now_sleeps(&a, true, true, false, T0 + 602 * SEC), "immediately switch",
          "the switch slept the Mac");
    CHECK(!now_sleeps(&a, true, true, false, T0 + 900 * SEC), "immediately switch",
          "slept later for the same sleep");
}

/* Sleep sync turned On while the PC sleeps: B sets its bit at once. Not a
   sleep. */
static void test_immediately_ignores_turning_sleep_sync_on(void) {
    dh_sleep_sync_t a = {0};
    for (uint64_t t = T0; t < T0 + 60 * SEC; t += SEC) {
        dh_sleep_sync_heard(&a, false); /* B, Off, says awake */
        now_sleeps(&a, false, true, false, t);
    }
    now_sleeps(&a, true, true, false, T0 + 60 * SEC); /* Save turns it On */
    dh_sleep_sync_heard(&a, true);
    CHECK(!now_sleeps(&a, true, true, false, T0 + 61 * SEC), "immediately on", "turning it On slept the Mac");
}

/* B goes silent (a reflash or reboot) while the PC sleeps, then returns and
   says asleep again. Not a new sleep. */
static void test_immediately_ignores_a_silent_peer_returning(void) {
    dh_sleep_sync_t a = {0};
    hear_awake(&a, T0 - 60 * SEC, T0);
    dh_sleep_sync_heard(&a, true);
    now_sleeps(&a, true, true, false, T0 + SEC);
    for (uint64_t t = T0 + 10 * SEC; t < T0 + 40 * SEC; t += SEC) {
        dh_sleep_sync_heard(&a, false); /* the silent-peer path */
        dh_sleep_sync_lost(&a);
        now_sleeps(&a, true, true, false, t);
    }
    dh_sleep_sync_heard(&a, true);
    CHECK(!now_sleeps(&a, true, true, false, T0 + 40 * SEC), "immediately silent",
          "a returning peer slept the Mac");
}

/* A reboots while the PC sleeps: its first heartbeat says asleep. Not a
   sleep, however long the boot took. */
static void test_immediately_ignores_a_reboot(void) {
    dh_sleep_sync_t a = {0};
    now_sleeps(&a, true, true, false, 10 * SEC);
    dh_sleep_sync_heard(&a, true);
    CHECK(!now_sleeps(&a, true, true, false, 11 * SEC), "immediately reboot", "the reboot slept the Mac");
}

/* Sleep sync off, the inactive board, or A's computer already asleep. */
static void test_immediately_needs_on_active_awake(void) {
    for (int i = 0; i < 3; ++i) {
        dh_sleep_sync_t a = {0};
        hear_awake(&a, T0 - 60 * SEC, T0);
        dh_sleep_sync_heard(&a, true);
        CHECK(!now_sleeps(&a, i != 0, i != 1, i == 2, T0 + SEC), "immediately off",
              i == 0 ? "slept with Sleep sync off" : i == 1 ? "the inactive board slept" : "pressed a computer already asleep");
    }
}

/* Both directions, the record over the heartbeat. */
static void test_immediately_works_both_ways(void) {
    for (int a_active = 0; a_active < 2; ++a_active) {
        dh_sleep_sync_t a = {0}, b = {0};
        dh_sleep_sync_t *active = a_active ? &a : &b, *other = a_active ? &b : &a;
        for (uint64_t t = T0 - 60 * SEC; t < T0; t += SEC) {
            dh_sleep_sync_heard(active, tell(other, true, false, false, t));
            now_sleeps(active, true, true, false, t);
        }
        tell(other, true, false, true, T0);
        dh_sleep_sync_heard(active, tell(other, true, false, true, T0 + 5 * SEC));
        CHECK(now_sleeps(active, true, true, false, T0 + 6 * SEC), "immediately both",
              a_active ? "A did not sleep for B" : "B did not sleep for A");
    }
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
    test_the_helper_saying_asleep_counts_as_asleep();
    test_input_ends_a_sleep_the_helper_reported();
    test_the_helper_saying_awake_ends_the_sleep();
    test_a_mac_the_helper_called_asleep_gets_a_key_tap();
    test_a_wake_during_the_sleep_is_kept();
    test_a_wake_just_after_the_suspend_retries();
    test_a_kept_wake_expires();
    test_a_wake_without_a_press_is_dropped();
    test_a_kept_wake_retries_through_a_resume();
    test_a_late_wake_is_dropped();
    test_a_new_press_drops_a_kept_wake();
    test_a_press_ahead_of_the_clock_is_recent();
    test_the_sleep_press_is_held_before_release();
    test_the_inactive_board_tells_asleep_after_the_wait();
    test_a_maintenance_wake_says_awake_then_asleep();
    test_tell_says_nothing_while_off();
    test_the_active_board_does_not_tell_asleep();
    test_the_record_follows_the_heartbeat();
    test_a_switch_leaves_no_stale_asleep();
    test_the_board_that_becomes_active_learns_the_real_state();
    test_a_rebooted_board_learns_again();
    test_idle_sleeps_after_the_time();
    test_idle_off_does_nothing();
    test_idle_waits_for_the_other_to_sleep();
    test_idle_presses_once_per_stretch();
    test_a_switch_restarts_idle();
    test_idle_leaves_a_sleeping_computer_alone();
    test_a_maintenance_wake_pauses_idle();
    test_idle_works_both_ways();
    test_idle_one_minute();
    test_immediately_fires_once_on_the_edge();
    test_immediately_does_not_loop_after_a_wake();
    test_immediately_fires_again_after_a_maintenance_wake();
    test_immediately_needs_the_other_awake_first();
    test_immediately_ignores_a_switch();
    test_immediately_ignores_turning_sleep_sync_on();
    test_immediately_ignores_a_silent_peer_returning();
    test_immediately_ignores_a_reboot();
    test_immediately_needs_on_active_awake();
    test_immediately_works_both_ways();

    if (failures) {
        printf("sleep_sync_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("sleep_sync_test: ok\n");
    return 0;
}
