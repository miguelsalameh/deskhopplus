# ADR-0014: The pair chord opens both boards

- **Status:** Accepted
- **Date:** 2026-09-27
- **Resolves:** [#196](https://github.com/myn/deskhopplus/issues/196)

## Decision

A dedicated, fixed **pair chord** (Left Ctrl + Right Shift + P) opens a pairing window on the
board that hosts the keyboard **and** on its peer, which learns of the press over the
inter-board link. A config wipe does the same. The config chord no longer opens a window at all.
When a board registers a helper, it tells its peer, and the peer's helper shows "Other computer
paired".

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

- The user is usually not looking at the peer's screen when its window opens. The "Other
  computer paired" notice is how they learn it worked; its absence is how they learn it did not.
- The notice proves *a* helper paired, not *which*. Malware that wins the peer's window still
  triggers it; the peer's real helper keeps showing "not paired".
- A window opens on an already-paired board too, where no honest helper claims it. That is the
  same exposure the config chord had.
- The new board-to-helper message bumps the protocol to v5; older helpers are refused cleanly.
