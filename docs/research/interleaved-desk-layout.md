# Interleaved desk layouts: monitors of A and B mixed

Status: research, written 2026-10-08 against `a21e754`. The first step is built: a
computer's monitors can branch off main as an L or a T (CONTEXT.md: **Line**, **Turn**), set by
`monitor_turns` (API fields 105 and 106). The config page does not set it yet. Known limit:
Windows with its helper on a branched desk, because the helpers answer a position query only
along the line of the last PLACE.

## The goal

Let the two computers' monitors sit in any arrangement on the desk, not only
"A's block next to B's block". For example:

```
  A1 | B1 | A2          B1 | B2          A1 | A2
                        ---+---          ---+---
                        A1 | A2          B1 |
                                         ---+
                                         A3 |      (L-shape, two seams)
```

## Why it cannot be configured today

The model (issue #12, `src/include/screen.h:56-77`) is three facts per output:

| Field | Meaning | Assumption that blocks interleaving |
|---|---|---|
| `chain_direction` | which way monitor 2, 3 … extend | each computer's monitors form **one straight line** |
| `border_direction` | which side the other computer is on | the other computer is on **exactly one side** |
| `seam_ranges[4]` | segments along that one edge, keyed by monitor | the seam is **one straight edge** (accepted limit in #3, decision 2) |

The decision function is `dh_mouse_transition_for()` (`src/core/dh_mouse_layout.c:41`):
moving in `border_direction` → other computer; moving along `chain_direction` →
next monitor. So "A1 → right → B1 → right → A2" is inexpressible: A1's right
edge is either the chain or the border, never "the border, and A2 is also over
there".

The config page enforces the same thing: `fieldsFromLayout()`
(`webconfig/templates/layout.js:80`) refuses gaps, overlaps, corners, and any
layout where the two bounding boxes are not edge to edge.

## What already works in our favour

1. **The firmware already thinks per monitor.** HID absolute coordinates land
   on one monitor only (#11: Windows → main, macOS → the current one), so the
   board's `pointer_x/y` is already "position on monitor `screen_index`". There
   is no single virtual desktop to break.
2. **Arriving on any monitor of the other computer already works**:
   `DH_SEAM_CROSSING_MAPPED` sets `screen_index` and sends `PLACE`
   (`src/mouse_logic.c`, `cross_screen`). B1 → A2 (non-main) is the same path
   that "Windows BR → Mac TR" uses today.
3. **The helpers place the cursor on any display** (`SetCursorPos` /
   `CGWarpMouseCursorPosition`); without a helper the board walks (#310).
4. **The OS's own layout and the desk can differ, and that is fine.** On
   computer A, A1 and A2 are neighbours in Display Settings even though B1
   sits between them on the desk. The board never lets the cursor reach A1's
   right edge as an OS crossing: on an absolute monitor it clamps and switches,
   and on a relative (Windows non-main) one it parks the cursor on switching.

So the hard parts (placement, walking, Windows pointer maths, the crossing
transaction) are reusable. What changes is the **topology**: how the board
decides where an edge leads.

## Proposed model: per-monitor edge links

Replace `chain_direction` + `border_direction` + `seam_ranges` with two
layouts per pair of boards:

1. **Desk layout** — every monitor of both computers as a rectangle in one
   shared grid (what the user sees and drags on the config page). From it,
   derive for every monitor side a list of **links**:
   `{edge range on this monitor} → {output, monitor, edge range there}`.
   A link to the same output is a chain crossing; to the other output, a seam
   crossing. No link = wall.
2. **OS layout** — for each computer, where its monitors sit in *its own*
   Display Settings. Needed only to walk (helper-free) and to tell the helper
   which display "monitor 2" is. Straight-line chains become a special case.

Store **rectangles, derive links at runtime** in a pure `src/core/` module
(say `dh_desk.c`) with host tests, the same pattern as `dh_seam_map.c`.
7 monitors × 2 computers × 4 bytes (x, y, w, h in grid units) is ~56 bytes for
the desk, plus the same again for the OS layouts. That is far smaller than a
stored link table, and the config page and firmware cannot disagree on the
derivation if they share test vectors.

### Firmware changes

| Area | Change |
|---|---|
| `dh_mouse_transition_for` | look up the link at (monitor, side, position) instead of comparing to two directions; returns `{OUTPUT or CHAIN, target monitor, entry position}` |
| `dh_seam_resolve_crossing` | generalise from one edge to any side; the 0..65535 fraction mapping is reused unchanged |
| `walk_to_arrival_screen`, `switch_virtual_desktop` | walk along a **path** through the OS layout (BFS over monitor neighbours, a list of directions) instead of N steps along one axis |
| `get_jump_threshold` | per link: threshold on seam links, 0 on chain links (same rule as today) |
| `output_t` / `config_t` | new fields, `CURRENT_CONFIG_VERSION` 15 → 16, migration from today's chain/border/segments (all expressible in the new model) |
| corner tie-break, arrival guard | unchanged in spirit; the guard becomes "the side we arrived through" |

### Helper / protocol changes

`PLACE` today carries `chain_index + chain_direction + border_direction`, and
`dh_place_target()` (`src/core/dh_place.c`) finds monitor N by stepping from the
main display along one direction. With a non-linear OS layout that is
ambiguous. Two options:

- **Path in PLACE** (small): carry the OS-layout path from main as up to 6
  two-bit directions. Helper code barely changes; `dh_place_target` follows
  the path. Needs a protocol version bump.
- **Helper reports its displays** (better, bigger): a new frame where the
  helper sends its display rects to the board. The board then knows the OS
  layout without the user entering it, and the config page can show it. The
  user still maps desk boxes to OS displays once.

Recommendation: path in PLACE first; display reporting later.

### Config page

`fieldsFromLayout()` becomes "any boxes on the grid, no overlaps"; links are
derived from every shared edge, between any two monitors. Refusals left: a
monitor with no link to anything, more links than the board stores, and an OS
layout that is not connected. A second view (or tab) holds each computer's OS
layout, defaulting to "the desk layout with the other computer's monitors
squeezed out".

## Hard parts and risks

1. **Windows without a helper.** Arriving on a non-main Windows monitor means
   walking from the main one with relative moves and Windows' pointer maths
   (ADR-0017). With branching OS layouts the walk is longer and the estimate
   drifts more; #313 (mixed monitor sizes, one size per output today) gets
   worse. Realistic stance: **interleaved layouts require the helper on
   Windows** for exact entry, with the walk as a degraded fallback.
2. **Relative-mode leaks.** On a Windows non-main monitor the OS moves the
   cursor itself. Moving left on A2 toward B1, the report that crosses the edge
   also moves the Windows cursor into A1 before the board switches. Today the
   switch parks it; check that a parked cursor on the wrong monitor does not
   confuse the next arrival (it should not: arrivals re-place or re-walk).
3. **Link capacity.** A 2×2 checkerboard has 4 seam links; a 3-wide row
   interleaved with a 3-wide row has more. Set the cap from real desks.
4. **Hardware-only verification.** `docs/verification/` and the hardware labels
   (`needs-hardware-validation`) apply; much of the crossing work above was
   tuned on real boards (#310–#324). The pure core can be test-driven on the
   host now (`./tools/build.sh tests`), before boards arrive.
5. **Linux** keeps one virtual screen (`screen_count = 1`); interleaving there
   would need sub-rectangles of that one absolute space. Out of scope for a
   first cut.

## Suggested order

1. Write the desk model in `src/core/` with host tests: rects → links,
   transition lookup, OS-layout paths. Prove today's layouts produce identical
   decisions (regression vectors from `tests/crossing_model_test.c`).
2. Config migration v15 → v16, keeping behaviour identical.
3. Swap `cross_screen` and the walks onto the new lookups.
4. PLACE with a path; update both helpers' `dh_place_target` callers.
5. Config page: free grid + OS layout view.
6. Hardware validation once the boards arrive: A1 | B1 | A2 with and without
   helpers, Mac and Windows on each side.
