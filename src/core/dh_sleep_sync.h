/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

/*
 * Sleep sync (#287): when the active output's computer sleeps, its board tells
 * the peer, and the peer presses System Sleep on its own computer. When input
 * reaches the active output while its computer sleeps, its board tells the
 * peer to wake too (#288), and the peer nudges the pointer, which calls
 * remote wakeup first when its computer is suspended (#291, #296). A Mac its
 * helper called asleep also gets a Left Shift tap (#300). A wake
 * that finds the peer still going to sleep is kept until its computer
 * suspends (#289).
 *
 * "Asleep" is the USB host suspending the device (tud_suspended), or the
 * helper saying its computer sleeps (#293). A Modern Standby PC sleeps
 * without a USB suspend, so without a helper it never sends sleep; its peer
 * wakes it with a nudge (#291). The helper's word reaches
 * dh_sleep_sync_helper_says from channel_task (channel.c). The sender
 * asks dh_sleep_sync_step on every pass of sleep_sync_task (tasks.c), and
 * dh_sleep_sync_wake from wake_on_input (tasks.c); the receiver asks
 * dh_sleep_sync_obey_sleep and dh_sleep_sync_peer_wake in
 * handle_sleep_sync_msg (handlers.c), and dh_sleep_sync_peer_step in
 * sleep_sync_task.
 *
 * For Sleep when idle (#303), each board also tells its peer whether its
 * computer sleeps while it is not the active output (#304), in a heartbeat
 * bit: sleep_sync_task asks dh_sleep_sync_tell, heartbeat_output_task sends
 * `tells_asleep` (tasks.c), and handle_heartbeat_msg passes it to
 * dh_sleep_sync_heard (handlers.c).
 *
 * Pure C11, no clock read of its own: the caller passes one
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

/* What the sender remembers about its computer's current sleep, and what
   this board heard about the other computer's (#304). Zero is "awake,
   nothing sent". */
typedef struct {
    bool asleep;
    bool sent; /* or nothing to send: this board was not active at some point in this sleep,
                  or input ended it */
    uint64_t asleep_since_us;
    bool woken; /* input in this sleep sent wake; written by dh_sleep_sync_wake on core 0 */
    bool helper_asleep; /* the helper said its computer sleeps (#293); core 0 writes it */
    bool tells_asleep;  /* the heartbeat says this computer sleeps (#304); core 1 */
    bool other_asleep;  /* the peer's heartbeat said its computer sleeps; core 1 */
} dh_sleep_sync_t;

/* The helper says its computer went to sleep or woke (#293). A Modern
   Standby PC sleeps with no USB suspend, so its helper tells the board
   instead. The helper is frozen while the PC sleeps and the board ends its
   session, so a session end leaves this alone. Input clears it
   (dh_sleep_sync_wake), so a helper killed before it said awake cannot
   leave it set. */
static inline void dh_sleep_sync_helper_says(dh_sleep_sync_t *s, bool asleep) {
    s->helper_asleep = asleep;
}

/* This board's computer counts as asleep: USB suspended, or its helper said
   so. */
static inline bool dh_sleep_sync_asleep(const dh_sleep_sync_t *s, bool suspended) {
    return suspended || s->helper_asleep;
}

/* True once per sleep, on the pass that should send sleep to the peer: the
   setting is on, and this board's computer has been asleep (dh_sleep_sync_asleep) for
   DH_SLEEP_SYNC_WAIT_US while it was the active output all along. A sleep
   during which this board was ever not active never counts, so a switch onto
   a computer asleep from idle does not sleep the one just left. Nor does a
   sleep that input already ended (dh_sleep_sync_wake), while the USB resume
   is still under way. A wake starts over. Times compare signed, so a stamp taken just after `now` reads as
   recent. */
static inline bool dh_sleep_sync_step(dh_sleep_sync_t *s, bool on, bool active, bool suspended,
                                      uint64_t now_us) {
    if (!dh_sleep_sync_asleep(s, suspended)) {
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

/* Asked on every pass of sleep_sync_task, just after dh_sleep_sync_step,
   which keeps the sleep state read here. Sets `tells_asleep`, which every
   heartbeat carries to the peer (#304). It turns on once this computer has
   been asleep for DH_SLEEP_SYNC_WAIT_US while this board is not the active
   output; the active output's own sleep goes as DH_SLEEP_SYNC_SLEEP instead.
   A switch away from a sleeping computer turns it on at once, so the board
   that becomes active learns the real state. It turns off when the computer
   wakes, whatever the active output, or when the setting is off. A
   heartbeat a second restates it, so a peer that reboots or misses one
   learns it again. Times compare signed. */
static inline void dh_sleep_sync_tell(dh_sleep_sync_t *s, bool on, bool active, uint64_t now_us) {
    if (!on || !s->asleep)
        s->tells_asleep = false;
    else if (!active && (int64_t)(now_us - s->asleep_since_us) >= (int64_t)DH_SLEEP_SYNC_WAIT_US)
        s->tells_asleep = true;
}

/* A heartbeat brought the peer's bit (#304). True when the record changed,
   so the caller traces each change once, not each second. */
static inline bool dh_sleep_sync_heard(dh_sleep_sync_t *s, bool asleep) {
    const bool changed = s->other_asleep != asleep;
    s->other_asleep = asleep;
    return changed;
}

/* The other computer sleeps, as far as this board knows. Only the active
   output with the setting on trusts the record: a board that stops being
   active stops treating its peer as asleep. */
static inline bool dh_sleep_sync_other_asleep(const dh_sleep_sync_t *s, bool on, bool active) {
    return on && active && s->other_asleep;
}

/* True when the peer's sleep should press System Sleep here: the setting is
   on, this board is not the active output, and its computer is awake. */
static inline bool dh_sleep_sync_obey_sleep(bool on, bool active, bool asleep) {
    return on && !active && !asleep;
}

/* Called when input reaches this board's computer. True once per sleep, when
   wake should go to the peer: the setting is on, this board is the active
   output, and its computer is asleep. A call while awake clears it for the
   next sleep. A computer that wakes by itself sees no input here, so it
   wakes alone. Input also ends a sleep the helper reported (#293). */
static inline bool dh_sleep_sync_wake(dh_sleep_sync_t *s, bool on, bool active, bool suspended) {
    const bool asleep = dh_sleep_sync_asleep(s, suspended);
    s->helper_asleep = false;
    if (!asleep) {
        s->woken = false;
        return false;
    }
    if (s->woken || !on || !active)
        return false;

    s->woken = true;
    return true;
}

/* Asked by the mouse drain before wake_on_input, which ends the sleep. True
   when a mouse move should also tap Left Shift: the output is a Mac and its
   helper said asleep. A Mac in a DarkWake (helper said asleep, USB resumed)
   ignores a mouse move but wakes at once for a key (#300). A lone modifier
   types nothing and opens nothing. */
static inline bool dh_sleep_sync_tap_key(const dh_sleep_sync_t *s, bool mac) {
    return mac && s->helper_asleep;
}

/* How long the peer keeps a wake that came while its computer was still
   going to sleep, counted from its System Sleep press (#289). */
#define DH_SLEEP_SYNC_OWED_WAKE_US 30000000u

/* How long the receiver holds System Sleep down. macOS reads the key as its
   power button: it ignores a press shorter than 350 ms, and shows the
   shutdown dialog for one held 1.5 s (#287; loginwindow's log, found on
   #293). Windows sleeps on the press, so the hold costs it nothing. */
#define DH_SLEEP_SYNC_HOLD_US 500000u

/* What the receiving board remembers about its last System Sleep press. */
typedef struct {
    bool pressed;
    uint64_t pressed_at_us;
    bool owed; /* a wake came before the computer suspended */
    bool held; /* the release is still to send */
} dh_sleep_sync_peer_t;

/* The receiver pressed System Sleep. A new sleep drops any wake kept from
   before. */
static inline void dh_sleep_sync_peer_press(dh_sleep_sync_peer_t *p, uint64_t now_us) {
    *p = (dh_sleep_sync_peer_t){.pressed = true, .pressed_at_us = now_us, .held = true};
}

/* Asked on every pass of sleep_sync_task. True once, DH_SLEEP_SYNC_HOLD_US
   after the press: send the release now. Times compare signed. */
static inline bool dh_sleep_sync_peer_release(dh_sleep_sync_peer_t *p, uint64_t now_us) {
    if (!p->held || (int64_t)(now_us - p->pressed_at_us) < (int64_t)DH_SLEEP_SYNC_HOLD_US)
        return false;
    p->held = false;
    return true;
}

/* Wake arrived from the active board. The caller always nudges the pointer
   out and back, and the nudge's drain calls remote wakeup while the computer
   is suspended, like real input. Remote wakeup alone is not enough: a
   Modern Standby PC suspends the board's USB after minutes asleep, and turns
   its screen on only for the input that follows the resume (#296).
   Within DH_SLEEP_SYNC_OWED_WAKE_US of a sleep press, the wake is also kept
   for dh_sleep_sync_peer_step: an awake computer may still be going to
   sleep, and a just-suspended one may ignore a try made before USB's 5 ms of
   idle bus. Times compare signed, so a press stamped just after `now` reads
   as recent. */
static inline void dh_sleep_sync_peer_wake(dh_sleep_sync_peer_t *p, uint64_t now_us) {
    if (p->pressed && (int64_t)(now_us - p->pressed_at_us) < (int64_t)DH_SLEEP_SYNC_OWED_WAKE_US)
        p->owed = true;
}

/* Asked on every pass. True while a kept wake should call remote wakeup: the
   computer is suspended within the window. This covers a computer that was
   awake at the wake and suspends after it, such as a Mac just after the
   press; a wake that finds it suspended is retried by the nudge's drain. A
   host can resume the device for a moment while it goes to sleep, so only
   the window's end drops the kept wake; a new press drops it too. */
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
