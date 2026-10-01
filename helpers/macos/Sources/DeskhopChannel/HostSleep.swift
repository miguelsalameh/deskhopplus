// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Derek Reynolds

/*
 * What the helper tells its board about the Mac's sleep (#298): the body of a
 * HOST_SLEEP frame, 1 asleep or 0 awake (ADR-0015). Docked on Thunderbolt, a
 * sleeping Mac does a DarkWake about 45 s of every minute, with USB resumed
 * and the screens off (#294), so the board's USB suspend alone reads it awake.
 *
 * HelperRuntime feeds `event` from NSWorkspace notifications and sends what
 * it returns, and sends `sessionStarted` at the start of each session. No
 * AppKit here, so the rules are tested in channel-tests.
 */
public struct HostSleep {
    public enum Event {
        /// `willSleepNotification`: system sleep only, never a display-off timeout.
        case willSleep
        /// `didWakeNotification`: may be a DarkWake, so it says nothing.
        case didWake
        /// `screensDidSleepNotification`: a display sleep or timeout, which is not sleep.
        case screensDidSleep
        /// `screensDidWakeNotification`: a DarkWake keeps the screens off, so this is the user.
        case screensDidWake
    }

    private var asleep = false

    public init() {}

    /// The body to send now, if any.
    @discardableResult
    public mutating func event(_ event: Event) -> UInt8? {
        switch event {
        case .willSleep: asleep = true
        case .screensDidWake: asleep = false
        case .didWake, .screensDidSleep: return nil
        }
        return asleep ? 1 : 0
    }

    /*
     * A session started: awake if the Mac is awake, else nothing. Asleep is
     * not resent: the board keeps it through a session end, and a resend that
     * lands after the user's input cleared it would sleep the other computer
     * again 5 s after waking it (a DarkWake to FullWake took 25 s on 2026-09-30).
     */
    public func sessionStarted() -> UInt8? { asleep ? nil : 0 }
}
