/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

#pragma once
/*
 * What the helper tells its board about this PC's sleep (#293): the body of
 * a HOST_SLEEP frame, 1 asleep or 0 awake. A Modern Standby PC sleeps with no
 * USB suspend, so the board cannot see it; Sleep sync counts this instead.
 *
 * main.cpp feeds `power` from WM_POWERBROADCAST and sends what it returns,
 * and sends `session_started` at the start of each session. No Win32 here,
 * so the rules are tested on any machine (tests/host_sleep_test.cpp).
 */

#include <cstdint>
#include <optional>

namespace deskhop {

class HostSleep {
  public:
    /* The PBT_* values (WinUser.h), restated so this header needs no
       windows.h. main.cpp static_asserts them. */
    static constexpr unsigned kSuspend = 0x4;
    static constexpr unsigned kResumeSuspend = 0x7;

    /* A WM_POWERBROADCAST event: the body to send now, if any. Only
       PBT_APMRESUMESUSPEND means the user is back. PBT_APMRESUMEAUTOMATIC
       also fires on a maintenance wake with the screen dark (#292), and
       saying awake then would wake the other computer at every Sleep. */
    std::optional<uint8_t> power(unsigned event) {
        if (event == kSuspend) asleep_ = true;
        else if (event == kResumeSuspend) asleep_ = false;
        else return std::nullopt;
        return asleep_;
    }

    /* A session started: the body to send. The board ends the session while
       the PC sleeps, so the awake sent at a user wake can go nowhere; the
       next session carries it. */
    uint8_t session_started() const { return asleep_; }

  private:
    bool asleep_ = false;
};

} // namespace deskhop
