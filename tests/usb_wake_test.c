/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

#include <stdio.h>
#include <stdlib.h>
#include "dh_usb_wake.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    exit(1); \
} } while (0)

/*
 * After a Modern Standby wake, board B's USB stack can keep saying
 * "suspended" while the PC polls it again (#295). Every send waits on that
 * flag, so nothing reaches the PC. These are the two checks that heal and
 * trace it, with the bus frame number and clock supplied.
 */
static void stale_suspend_is_healed_only_while_frames_move(void) {
    dh_usb_wake w = {0};

    /* Awake bus: never. */
    CHECK(!dh_usb_wake_stale_suspend(&w, false, 100));
    CHECK(!dh_usb_wake_stale_suspend(&w, false, 120));

    /* A real suspend: the last pass before it saw an older frame, then the
       frame stops. One move is the bus going idle, not a stale flag. */
    CHECK(!dh_usb_wake_stale_suspend(&w, true, 130));
    CHECK(!dh_usb_wake_stale_suspend(&w, true, 130));
    CHECK(!dh_usb_wake_stale_suspend(&w, true, 130));

    /* The PC is polling again and the flag did not clear: heal on the second
       pass in a row that sees the frame move. */
    CHECK(!dh_usb_wake_stale_suspend(&w, true, 150));
    CHECK(dh_usb_wake_stale_suspend(&w, true, 170));

    /* A resume and a new suspend between two passes moves the frame once,
       then it stops again. The bus is asleep, so no heal. */
    CHECK(!dh_usb_wake_stale_suspend(&w, false, 200));
    CHECK(!dh_usb_wake_stale_suspend(&w, true, 900));
    CHECK(!dh_usb_wake_stale_suspend(&w, true, 900));

    /* The 11-bit frame number wraps; a move is a move. */
    CHECK(!dh_usb_wake_stale_suspend(&w, true, 2047));
    CHECK(dh_usb_wake_stale_suspend(&w, true, 5));
}

static void a_blocked_report_is_traced_once_per_stall(void) {
    const uint32_t T = DH_USB_BLOCKED_TRACE_US;
    dh_usb_blocked b = {0};

    /* Sent at once, or waiting less than the limit: no trace. */
    CHECK(!dh_usb_blocked_due(&b, false, 5 * T));
    CHECK(!dh_usb_blocked_due(&b, true, 6 * T));
    CHECK(!dh_usb_blocked_due(&b, true, 7 * T - 1));

    /* Waiting the whole limit: one trace, then quiet while it waits on. */
    CHECK(dh_usb_blocked_due(&b, true, 7 * T));
    CHECK(!dh_usb_blocked_due(&b, true, 9 * T));

    /* It went out; the next stall gets its own trace. */
    CHECK(!dh_usb_blocked_due(&b, false, 9 * T + 1));
    CHECK(!dh_usb_blocked_due(&b, true, 10 * T));
    CHECK(dh_usb_blocked_due(&b, true, 11 * T));

    /* The clock wraps and the arithmetic does not care. */
    b = (dh_usb_blocked){0};
    CHECK(!dh_usb_blocked_due(&b, true, UINT32_MAX - T / 2));
    CHECK(dh_usb_blocked_due(&b, true, T / 2));
}

int main(void) {
    stale_suspend_is_healed_only_while_frames_move();
    a_blocked_report_is_traced_once_per_stall();
    printf("usb wake tests passed\n");
    return 0;
}
