/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

/*
 * When the Status LED goes dark on its own (#283), on the host.
 *
 * Style follows config_test.c: an assertion macro, a main, a printed failure
 * line, a non-zero exit — no framework.
 */

#include <stdio.h>
#include <string.h>

#include "config_layout.h"
#include "dh_status_led.h"

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

static bool dark(uint8_t mode, uint16_t seconds, uint64_t now, uint64_t input, uint64_t sw) {
    return dh_status_led_dark(mode, seconds, now, input, sw, false);
}

static void test_never_stays_lit(void) {
    CHECK(!dark(DH_STATUS_LED_NEVER, 60, T0 + 3600 * SEC, T0, T0), "never",
          "went dark with Turn off set to Never");
}

static void test_idle_goes_dark_at_the_time(void) {
    CHECK(!dark(DH_STATUS_LED_IDLE, 60, T0 + 59 * SEC, T0, T0 - 5 * SEC), "idle",
          "went dark before the idle time");
    CHECK(dark(DH_STATUS_LED_IDLE, 60, T0 + 60 * SEC, T0, T0 - 5 * SEC), "idle",
          "stayed lit after the idle time");
}

static void test_input_relights(void) {
    /* Dark a long time, then a key press one second ago. */
    CHECK(!dark(DH_STATUS_LED_IDLE, 60, T0 + 600 * SEC, T0 + 599 * SEC, T0), "idle",
          "input did not light it again");
}

static void test_a_switch_relights_and_restarts_the_idle_clock(void) {
    /* No input for ten minutes, a switch 30 s ago: lit, and the clock runs from
       the switch, not from the old input. */
    CHECK(!dark(DH_STATUS_LED_IDLE, 60, T0 + 600 * SEC, T0, T0 + 570 * SEC), "idle",
          "a switch did not light it again");
    CHECK(dark(DH_STATUS_LED_IDLE, 60, T0 + 630 * SEC, T0, T0 + 570 * SEC), "idle",
          "the idle clock did not run from the switch");
}

static void test_after_a_switch_goes_dark_while_typing(void) {
    CHECK(!dark(DH_STATUS_LED_AFTER_SWITCH, 10, T0 + 9 * SEC, T0 + 9 * SEC, T0), "switch",
          "went dark before the time after the switch");
    CHECK(dark(DH_STATUS_LED_AFTER_SWITCH, 10, T0 + 10 * SEC, T0 + 10 * SEC, T0), "switch",
          "input kept it lit in After a switch");
    CHECK(!dark(DH_STATUS_LED_AFTER_SWITCH, 10, T0 + 30 * SEC, T0 + 30 * SEC, T0 + 25 * SEC),
          "switch", "the next switch did not light it again");
}

static void test_zero_seconds_means_never(void) {
    CHECK(!dark(DH_STATUS_LED_IDLE, 0, T0 + 3600 * SEC, T0, T0), "zero",
          "zero seconds went dark in When idle");
    CHECK(!dark(DH_STATUS_LED_AFTER_SWITCH, 0, T0 + 3600 * SEC, T0, T0), "zero",
          "zero seconds went dark in After a switch");
}

static void test_an_unknown_mode_stays_lit(void) {
    CHECK(!dark(3, 60, T0 + 3600 * SEC, T0, T0), "mode", "an unknown mode went dark");
}

static void test_config_mode_is_always_lit(void) {
    CHECK(!dh_status_led_dark(DH_STATUS_LED_IDLE, 5, T0 + 3600 * SEC, T0, T0, true), "config",
          "went dark in config mode");
}

/* A stamp taken a moment after the clock read (another core, or a callee that
   read the clock again) must read as "just now", not as 584,000 years ago —
   the unsigned-difference trap of #107. */
static void test_a_stamp_ahead_of_the_clock_is_recent(void) {
    CHECK(!dark(DH_STATUS_LED_IDLE, 5, T0, T0 + 1, T0 - 60 * SEC), "signed",
          "an input stamp ahead of the clock read as long ago");
    CHECK(!dark(DH_STATUS_LED_AFTER_SWITCH, 5, T0, T0 - 60 * SEC, T0 + 1), "signed",
          "a switch stamp ahead of the clock read as long ago");
}

/* A config written before #283 holds zeros where the two fields now sit. */
static void test_an_old_config_keeps_the_led_lit(void) {
    config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    CHECK(!dark(cfg.led_off_mode, cfg.led_off_sec, T0 + 3600 * SEC, T0, T0), "compat",
          "an old config's zeros turned the LED off");
}

int main(void) {
    test_never_stays_lit();
    test_idle_goes_dark_at_the_time();
    test_input_relights();
    test_a_switch_relights_and_restarts_the_idle_clock();
    test_after_a_switch_goes_dark_while_typing();
    test_zero_seconds_means_never();
    test_an_unknown_mode_stays_lit();
    test_config_mode_is_always_lit();
    test_a_stamp_ahead_of_the_clock_is_recent();
    test_an_old_config_keeps_the_led_lit();

    if (failures) {
        printf("status_led_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("status_led_test: ok\n");
    return 0;
}
