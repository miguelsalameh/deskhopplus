/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

/*
 * Sleep sync (#287): when the active output's computer sleeps, its board tells
 * the peer, and the peer presses System Sleep on its own computer. When input
 * reaches the active output while its computer sleeps, its board tells the
 * peer to wake too (#288), and the peer calls remote wakeup.
 *
 * "Asleep" is the USB host suspending the device (tud_suspended). The sender
 * asks dh_sleep_sync_step on every pass of sleep_sync_task (tasks.c), and
 * dh_sleep_sync_wake from wake_on_input (tasks.c); the receiver asks
 * dh_sleep_sync_obey_sleep in handle_sleep_sync_msg (handlers.c). Pure C11,
 * no clock read of its own: the caller passes one `now` (the #107 rule).
 */

#ifndef DH_SLEEP_SYNC_H_
#define DH_SLEEP_SYNC_H_

#include <stdbool.h>
#include <stdint.h>

/* How long the computer stays suspended before its peer is told. Rides out
   the short suspends Windows USB power saving makes (#286). Not a setting. */
#define DH_SLEEP_SYNC_WAIT_US 5000000u

/* The SLEEP_SYNC_MSG payload byte. */
enum {
    DH_SLEEP_SYNC_SLEEP = 1,
    DH_SLEEP_SYNC_WAKE = 2,
};

/* What the sender remembers about its computer's current sleep. Zero is
   "awake, nothing sent". */
typedef struct {
    bool asleep;
    bool sent; /* or nothing to send: this board was not active at some point in this sleep,
                  or input ended it */
    uint64_t asleep_since_us;
    bool woken; /* input in this sleep sent wake; written by dh_sleep_sync_wake on core 0 */
} dh_sleep_sync_t;

/* True once per sleep, on the pass that should send sleep to the peer: the
   setting is on, and this board's computer has been suspended for
   DH_SLEEP_SYNC_WAIT_US while it was the active output all along. A sleep
   during which this board was ever not active never counts, so a switch onto
   a computer asleep from idle does not sleep the one just left. Nor does a
   sleep that input already ended (dh_sleep_sync_wake), while the USB resume
   is still under way. A wake starts over. Times compare signed, so a stamp taken just after `now` reads as
   recent. */
static inline bool dh_sleep_sync_step(dh_sleep_sync_t *s, bool on, bool active, bool suspended,
                                      uint64_t now_us) {
    if (!suspended) {
        s->asleep = false;
        return false;
    }
    if (!s->asleep) {
        s->asleep = true;
        s->sent = false;
        s->asleep_since_us = now_us;
    }
    if (!active || s->woken)
        s->sent = true;
    if (s->sent || !on
        || (int64_t)(now_us - s->asleep_since_us) < (int64_t)DH_SLEEP_SYNC_WAIT_US)
        return false;

    s->sent = true;
    return true;
}

/* True when the peer's sleep should press System Sleep here: the setting is
   on, this board is not the active output, and its computer is awake. */
static inline bool dh_sleep_sync_obey_sleep(bool on, bool active, bool suspended) {
    return on && !active && !suspended;
}

/* Called when input reaches this board's computer. True once per sleep, when
   wake should go to the peer: the setting is on, this board is the active
   output, and its computer is asleep. A call while awake clears it for the
   next sleep. A computer that wakes by itself sees no input here, so it
   wakes alone. */
static inline bool dh_sleep_sync_wake(dh_sleep_sync_t *s, bool on, bool active, bool suspended) {
    if (!suspended) {
        s->woken = false;
        return false;
    }
    if (s->woken || !on || !active)
        return false;

    s->woken = true;
    return true;
}

#endif /* DH_SLEEP_SYNC_H_ */
