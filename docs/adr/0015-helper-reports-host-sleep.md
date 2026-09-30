# ADR-0015: The helper reports its computer's sleep

- **Status:** Accepted
- **Date:** 2026-09-30
- **Resolves:** [#293](https://github.com/myn/deskhopplus/issues/293)

## Decision

The Windows helper tells its own board when Windows goes to sleep and when the user wakes it, in
a new helper-to-board message, `0x11 HOST_SLEEP` (one byte: 1 asleep, 0 awake). Sleep sync counts
"the helper says asleep" the same as a USB suspend. Nothing new crosses between the boards.

- The helper sends **asleep** on `PBT_APMSUSPEND`, and **awake** on `PBT_APMRESUMESUSPEND`. It
  sends nothing on `PBT_APMRESUMEAUTOMATIC`.
- It sends the current state again at the start of every session.
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
- **Why resend at session start.** The awake report at a user wake usually finds no session,
  because the board ended it during the sleep. The next session carries it.
- **Why a gate.** A v6 board ends the session on an unknown type, so a v7 helper would drop its
  session at every sleep.

## Considered options

- **The screen state (`GUID_CONSOLE_DISPLAY_STATE`) for both edges.** It also fires on a
  display-off timeout, which is not sleep (#286). Rejected for asleep; kept as a log line for awake.
- **Clear the state on a session end.** Rejected: see above.
- **A helper on the Mac too.** A Mac suspends USB when it sleeps, so its board already sees it.
