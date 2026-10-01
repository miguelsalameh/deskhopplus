/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

/*
 * What the helper tells its board about this PC's sleep (#293), on any
 * machine. The power events are Windows', from #292's hardware runs: a
 * sleep sends PBT_APMSUSPEND, a wake by the user sends PBT_APMRESUMESUSPEND,
 * and every wake, a maintenance wake with the screen dark included, sends
 * PBT_APMRESUMEAUTOMATIC.
 *
 * Style follows autostart_ladder_test.cpp: an assertion macro, a main, a
 * printed failure line, a non-zero exit (ADR-0006).
 */

#include <cstdio>

#include "host_sleep.h"

using deskhop::HostSleep;

static int failures = 0;

#define CHECK(cond, what)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++failures;                                                      \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, (what));      \
        }                                                                    \
    } while (0)

/* The PBT_* values, from WinUser.h. */
static const unsigned kSuspend = 0x4, kResumeSuspend = 0x7, kResumeAutomatic = 0x12;

static void test_sleep_says_asleep_and_a_user_wake_says_awake() {
    HostSleep h;
    const auto asleep = h.power(kSuspend);
    CHECK(asleep && *asleep == 1, "a sleep did not say asleep");
    const auto awake = h.power(kResumeSuspend);
    CHECK(awake && *awake == 0, "a user wake did not say awake");
}

/* A maintenance wake is not the user: the other computer must stay asleep. */
static void test_an_automatic_wake_says_nothing() {
    HostSleep h;
    h.power(kSuspend);
    CHECK(!h.power(kResumeAutomatic), "an automatic wake said something");
    CHECK(!h.power(0x8013 /* PBT_POWERSETTINGCHANGE */), "another event said something");
}

/* The board ends the session while the PC sleeps, so the user wake's awake
   goes nowhere; the next session says it. Asleep is never resent: the board
   keeps it through a session end, and a resend after the user's input cleared
   it would sleep the other computer again (#298). */
static void test_a_new_session_hears_awake_only_when_awake() {
    HostSleep h;
    const auto fresh = h.session_started();
    CHECK(fresh && *fresh == 0, "a fresh helper did not say awake");
    h.power(kSuspend);
    h.power(kResumeAutomatic);
    CHECK(!h.session_started(), "a session in a maintenance wake said something");
    h.power(kResumeSuspend);
    const auto woken = h.session_started();
    CHECK(woken && *woken == 0, "a session after a user wake did not say awake");
}

int main() {
    test_sleep_says_asleep_and_a_user_wake_says_awake();
    test_an_automatic_wake_says_nothing();
    test_a_new_session_hears_awake_only_when_awake();
    if (failures) {
        std::printf("host_sleep_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("host_sleep_test: ok\n");
    return 0;
}
