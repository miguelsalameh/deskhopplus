/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

/*
 * Sleep sync (#287): when the active output's computer sleeps, its board tells
 * the peer, and the peer presses System Sleep on its own computer. When input
 * reaches the active output while its computer sleeps, its board tells the
 * peer to wake too (#288), and the peer calls remote wakeup, or nudges the
 * pointer when its computer is not suspended (#291). A wake that finds
 * the peer still going to sleep is kept until its computer suspends (#289).
 *
 * "Asleep" is the USB host suspending the device (tud_suspended). A Modern
 * Standby PC can sleep without one, so it never sends sleep, and its peer
 * wakes it with a nudge (#291). The sender
 * asks dh_sleep_sync_step on every pass of sleep_sync_task (tasks.c), and
 * dh_sleep_sync_wake from wake_on_input (tasks.c); the receiver asks
 * dh_sleep_sync_obey_sleep and dh_sleep_sync_peer_wake in
 * handle_sleep_sync_msg (handlers.c), and dh_sleep_sync_peer_step in
 * sleep_sync_task. Pure C11, no clock read of its own: the caller passes one
 * `now` (the #107 rule).
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

/* How long the peer keeps a wake that came while its computer was still
   going to sleep, counted from its System Sleep press (#289). */
#define DH_SLEEP_SYNC_OWED_WAKE_US 30000000u

/* What the receiving board remembers about its last System Sleep press. */
typedef struct {
    bool pressed;
    uint64_t pressed_at_us;
    bool owed; /* a wake came before the computer suspended */
} dh_sleep_sync_peer_t;

/* The receiver pressed System Sleep. A new sleep drops any wake kept from
   before. */
static inline void dh_sleep_sync_peer_press(dh_sleep_sync_peer_t *p, uint64_t now_us) {
    *p = (dh_sleep_sync_peer_t){.pressed = true, .pressed_at_us = now_us};
}

/* How the receiver wakes its computer. */
typedef enum {
    DH_SLEEP_SYNC_REMOTE_WAKEUP, /* tud_remote_wakeup */
    DH_SLEEP_SYNC_NUDGE,         /* a mouse move out and back (#291) */
} dh_sleep_sync_wake_action_t;

/* Wake arrived from the active board. A suspended computer gets remote
   wakeup. One that is not suspended gets a nudge: a Modern Standby PC sleeps
   without a USB suspend, and real input wakes it (#291). On an awake
   computer the nudge is harmless, since the pointer ends where it started.
   Within DH_SLEEP_SYNC_OWED_WAKE_US of a sleep press, the wake is also kept
   for dh_sleep_sync_peer_step: an awake computer may still be going to
   sleep, and a just-suspended one may ignore a try made before USB's 5 ms of
   idle bus. Times compare signed, so a press stamped just after `now` reads
   as recent. */
static inline dh_sleep_sync_wake_action_t dh_sleep_sync_peer_wake(dh_sleep_sync_peer_t *p, bool suspended,
                                                            uint64_t now_us) {
    if (p->pressed && (int64_t)(now_us - p->pressed_at_us) < (int64_t)DH_SLEEP_SYNC_OWED_WAKE_US)
        p->owed = true;
    return suspended ? DH_SLEEP_SYNC_REMOTE_WAKEUP : DH_SLEEP_SYNC_NUDGE;
}

/* Asked on every pass. True while a kept wake should call remote wakeup: the
   computer is suspended within the window. A host can resume the device for
   a moment while it goes to sleep, so only the window's end drops the kept
   wake; a new press drops it too. */
static inline bool dh_sleep_sync_peer_step(dh_sleep_sync_peer_t *p, bool suspended, uint64_t now_us) {
    if (!p->owed)
        return false;
    if ((int64_t)(now_us - p->pressed_at_us) >= (int64_t)DH_SLEEP_SYNC_OWED_WAKE_US) {
        p->owed = false;
        return false;
    }
    return suspended;
}

#endif /* DH_SLEEP_SYNC_H_ */
