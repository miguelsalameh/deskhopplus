# ADR-0014: The pair chord opens both boards

- **Status:** Accepted
- **Date:** 2026-09-27
- **Resolves:** [#196](https://github.com/myn/deskhopplus/issues/196)

## Decision

A dedicated, fixed **pair chord** (Left Ctrl + Right Shift + P) opens a pairing window on the
board that hosts the keyboard **and** on its peer, which learns of the press over the
inter-board link. A config wipe does the same. The config chord no longer opens a window at all.
Each helper shows whether the other computer's helper is connected: "Other computer connected"
or "Other computer not connected".

**Amended 2026-09-27 ([#275](https://github.com/myn/deskhopplus/issues/275)).** This was a
one-shot "Other computer paired" event, sent when the peer registered a helper. It stayed on the
Mac menu for five minutes and read as a live status: a fresh, unpaired helper started on the
other computer in that time, and the menu still said paired. Now each board puts "my helper's
session is live" in the inter-board heartbeat, and tells its own helper when that changes.

## Why

ADR-0008 makes the physical chord the only proof that a human is present at pairing time, and
until now that meant the keyboard had to be on the pairing board — a cable move per helper. The
press is still physical; only its effect crosses the link. ADR-0007 already trusts that link to
carry firmware, which is strictly more than a one-minute window. So the proof is unchanged and
the cable move goes.

## Considered options

- **Auto-pair an unpaired board, no gesture.** Rejected: any same-user program on that computer
  could register first, and ADR-0008's threat model is exactly that program.
- **A rebindable pair chord in the hotkey table.** Rejected: the table is part of the stored
  config, so a new action is a config version bump, and a bump costs every user all their
  settings and their pairing. The chord is fixed instead, like the recovery chord.
- **A "Pair" button in the already-paired helper.** Rejected: ADR-0008 records that malware can
  use the helper's key while it runs, so a helper click or frame is not proof of a human.

## Consequences

- The user is usually not looking at the peer's screen when its window opens. The line changing
  to "Other computer connected" is how they learn it worked; "not connected" is how they learn it
  did not.
- The line proves *a* helper holds a session over there, not *which*. Malware that wins the
  peer's window still shows as connected; the peer's real helper keeps showing "not paired".
- "Not connected" also covers a peer helper that is paired but not running, a peer board that is
  rebooting, and a pulled link cable: no heartbeat for 3 s counts as not connected.
- A window opens on an already-paired board too, where no honest helper claims it. That is the
  same exposure the config chord had.
- The new board-to-helper message bumped the protocol to v5; the status that replaced it bumped
  it to v6. Older helpers are refused cleanly.
