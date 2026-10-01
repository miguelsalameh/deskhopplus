// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Derek Reynolds

import DeskhopChannel

/*
 * What the Mac helper tells its board about the Mac's sleep (#298). Docked on
 * Thunderbolt, a sleeping Mac does a DarkWake about 45 s of every minute, with
 * USB resumed and the screens off (#294). A notification can also turn the
 * screens on and go back to sleep with no willSleep (2026-10-01). Only
 * didWake, which pairs with willSleep, means the user.
 */
let hostSleepTests: [(String, () throws -> Void)] = [
    ("a sleep says asleep and a system wake says awake", {
        var sleep = HostSleep()
        Check.equal(sleep.event(.willSleep), 1, "a sleep did not say asleep")
        Check.equal(sleep.event(.didWake), 0, "a system wake did not say awake")
    }),

    ("a notification wake and a display sleep say nothing", {
        var sleep = HostSleep()
        sleep.event(.willSleep)
        Check.equal(sleep.event(.screensDidWake), nil, "a notification's screens wake said something")
        Check.equal(sleep.event(.screensDidSleep), nil, "a display sleep said something")
        Check.equal(sleep.sessionStarted(), nil, "a session after a notification wake said awake")
    }),

    /* Asleep is never resent: the board keeps it through a session end, and a
       resend after the user's input cleared it would sleep the PC again. */
    ("a new session hears awake only when the Mac is awake", {
        var sleep = HostSleep()
        Check.equal(sleep.sessionStarted(), 0, "a fresh helper did not say awake")
        sleep.event(.willSleep)
        Check.equal(sleep.sessionStarted(), nil, "a session in a DarkWake said something")
        sleep.event(.didWake)
        Check.equal(sleep.sessionStarted(), 0, "a session after a user wake did not say awake")
    }),
]
