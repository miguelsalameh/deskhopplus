/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

/*
 * A "suspended" flag that outlives the suspend (#295).
 *
 * After a Modern Standby wake, board B kept receiving from Windows but sent
 * nothing: no keys, no mouse, no hello_ack. Every send waits on
 * tud_hid_n_ready, which is false while TinyUSB's suspended flag is set, and
 * only an interrupt clears that flag (a host resume, or the first SOF after a
 * remote wakeup). Miss that interrupt and the board is mute until a replug.
 *
 * A suspended bus carries no Start of Frame, so its frame number stands
 * still. A frame number that moves on two passes in a row while the flag
 * still says suspended is a live bus: the flag is stale and the caller
 * clears it. One move is not enough: the bus can resume and suspend again
 * between two passes, which moves the frame once and leaves it asleep.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t frame; /* the frame number the last pass saw */
    uint8_t moves;  /* passes in a row, all suspended, that saw it move */
} dh_usb_wake;

/* Call on passes some frames apart, reading the frame number before the
   flag (a SOF between the two reads then cannot look stale). True when the
   caller should clear the flag. */
static inline bool dh_usb_wake_stale_suspend(dh_usb_wake *w, bool suspended, uint16_t frame) {
    const bool moved = frame != w->frame;
    w->frame = frame;
    if (!suspended || !moved) {
        w->moves = 0;
        return false;
    }
    if (++w->moves < 2)
        return false;
    w->moves = 0;
    return true;
}

/* A key report that waits this long for the host is worth one trace record:
   a wake from standby takes a few seconds, a stuck endpoint for ever. */
#define DH_USB_BLOCKED_TRACE_US 1000000u

typedef struct {
    uint32_t since_us; /* when the report at the head of the queue began to wait */
    bool waiting;
    bool traced;       /* this stall already has its record */
} dh_usb_blocked;

/* Call each pass: waiting is true while a report is queued and the host is
   not ready for it. True once per stall, when it has lasted the limit. */
static inline bool dh_usb_blocked_due(dh_usb_blocked *b, bool waiting, uint32_t now_us) {
    if (!waiting) {
        b->waiting = b->traced = false;
        return false;
    }
    if (!b->waiting) {
        b->waiting = true;
        b->since_us = now_us;
    }
    if (b->traced || (uint32_t)(now_us - b->since_us) < DH_USB_BLOCKED_TRACE_US)
        return false;
    b->traced = true;
    return true;
}
