# ADR-0015: The helper reports its computer's sleep

- **Status:** Accepted
- **Date:** 2026-09-30
- **Resolves:** [#293](https://github.com/myn/deskhopplus/issues/293)
- **Amended:** 2026-09-30, [#298](https://github.com/myn/deskhopplus/issues/298): the Mac helper
  reports too, and a session start sends only awake

## Decision

Each helper tells its own board when its computer goes to sleep and when the user wakes it, in
a new helper-to-board message, `0x11 HOST_SLEEP` (one byte: 1 asleep, 0 awake). Sleep sync counts
"the helper says asleep" the same as a USB suspend. Nothing new crosses between the boards.

- The Windows helper sends **asleep** on `PBT_APMSUSPEND`, and **awake** on
  `PBT_APMRESUMESUSPEND`. It sends nothing on `PBT_APMRESUMEAUTOMATIC`.
- The Mac helper sends **asleep** on `NSWorkspace.willSleepNotification`, and **awake** on
  `didWakeNotification`. It sends nothing on `screensDidWakeNotification` or
  `screensDidSleepNotification` (#298).
- At the start of every session, a helper sends **awake** if its computer is awake, and nothing
  if it is asleep.
- The board keeps the state through a session end. Any keyboard or mouse report to its computer
  clears it.
- `DH_PROTO_VERSION` goes to 7, a gate like v4 to v6.

## Why

A Modern Standby PC stays in S0 while it sleeps, and the board's HID interfaces stay in D0, so the
board never sees a USB suspend ([research](../research/windows-modern-standby-sleep-signal.md)).
Without a signal from the PC itself, Sleep sync cannot start from such a PC. The helper is the
only thing running on the PC that the board can hear.

- **Why `PBT_APMRESUMESUSPEND` and not `PBT_APMRESUMEAUTOMATIC`.** On Derek's PC a maintenance
  wake with the screen dark followed every Sleep in one run, and sent `PBT_APMRESUMEAUTOMATIC`.
  Awake on that would wake the other computer at every Sleep. Microsoft sends
  `PBT_APMRESUMESUSPEND` only for a wake from user input. On three user wakes it fired within
  0.2 s of the screen turning on (#293). The screen state is logged as the fall-back signal.
- **Why the state survives a session end.** The helper is frozen while the PC sleeps, so the board
  ends its session on the liveness timeout a few seconds later. Clearing the state then would end
  every sleep after 3 s.
- **Why input clears it.** A helper killed or paused before it says awake would otherwise leave
  the PC counted asleep for good. Input to the PC means the user is back. One case stays: a PC
  that is not the active output, whose helper died, and that woke by its power button, gets no
  input, so its board still counts it asleep and does not press System Sleep on it. The next
  input or helper session clears it.
- **Why resend awake at session start.** The awake report at a user wake usually finds no
  session, because the board ended it during the sleep. The next session carries it.
- **Why not resend asleep.** The board keeps asleep through a session end, so it is not lost. A
  session can start during a dark wake (a Mac DarkWake, a Windows maintenance wake). A resent
  asleep could land after the user's input has cleared the state, and sleep the other computer
  again 5 s after waking it: a Mac DarkWake took 25 s to become a full wake on 2026-09-30 (#298).
  The cost: an asleep report sent with no session is lost, and the board falls back to the USB
  suspend alone, as with no helper.
- **Why the Mac reports too.** Docked on Thunderbolt, a sleeping Mac does a DarkWake about 45 s
  of every minute, with USB resumed and the screens off (#294). Its board read each one as awake,
  so the 5 s wait started over and the PC slept late or never, and input during a DarkWake sent no
  WAKE.
- **Why `didWake` marks the user.** In #298's runs `willSleep` and `didWake` came in pairs, ten
  of each, and no DarkWake posted `didWake`. A notification wake turned the screens on and posted
  `screensDidWake`, then went back to sleep with no `willSleep` (2026-10-01, 09:05:48). Awake on
  `screensDidWake` left the Mac counted awake for the rest of that sleep, so input sent no WAKE.
- **Why a gate.** A v6 board ends the session on an unknown type, so a v7 helper would drop its
  session at every sleep.

## Considered options

- **The screen state (`GUID_CONSOLE_DISPLAY_STATE`) for both edges.** It also fires on a
  display-off timeout, which is not sleep (#286). Rejected for asleep; kept as a log line for awake.
- **Clear the state on a session end.** Rejected: see above.
- **A helper on the Mac too.** First rejected: a Mac suspends USB when it sleeps, so its board
  already sees it. Reversed by #298 on the DarkWake data above.
- **`screensDidWakeNotification` for the Mac's awake.** First chosen, since a DarkWake keeps the
  screens off. Rejected on the notification wake above; kept as a log line.
