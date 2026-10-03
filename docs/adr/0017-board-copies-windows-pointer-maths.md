# ADR-0017: Without a helper, the board copies Windows' pointer maths for monitors past the main one

- **Status:** Accepted
- **Date:** 2026-10-03
- **Resolves:** [#312](https://github.com/myn/deskhopplus/issues/312)
- **Amended:** 2026-10-03, #312 hardware check: below ×1 Windows keeps the part-pixel

## Decision

On a Windows output with two or more monitors and no helper, the board cannot see the cursor on
a monitor past the main one. It sends raw counts there, and keeps an **estimate** of where Windows
put the cursor. The board now works that estimate out the way Windows does: each report moves the
cursor counts × the pointer speed's multiplier on each axis, in whole pixels, truncated toward
zero. Below ×1 (pointer speeds 1–9) Windows keeps the part-pixel and adds it to the next report;
from ×1 up it drops it. The board keeps the same part-pixel, per computer and axis, in device
state (not config). The estimate is kept on the pixel grid of the saved monitor size. A
helper-free walk onto such a monitor uses the same maths for its nudges.

- Each output saves Windows' **pointer speed** (1–20) and the **monitor size** in pixels, in
  alignment padding that every saved config holds as zero: no `CURRENT_CONFIG_VERSION` bump.
  Zero means not set, and the board then keeps the old estimate, counts × Speed X / Y.
- The 20 multipliers live in one firmware header, `src/core/dh_windows_pointer.h`. The config
  page reads them from there at render.
- **Speed X / Y** keep their meaning: the board's speed where it places the cursor, so on Windows
  the main monitor's feel. The page still works them out from pointer speed and monitor size.

## Why

#311 made the page work out Speed X / Y so that counts × Speed matched Windows. The hardware
check failed at most pointer speeds, for two reasons that stack. Speed is a whole number, and the
exact value is a fraction (8.53 at step 6 on 1080p), so the estimate drifts; low pointer speeds
are worst. And from ×1 up Windows drops each report's fraction of a pixel, so it moves the cursor
less than counts × multiplier, by an amount that depends on how fast the hand moves. No Speed
value can match that. Only pointer speeds 10, 14 and 18 (×1, ×2, ×3) worked, which is what the hardware
showed. A pixel model of Windows in `tests/crossing_model_test.c` reproduced both faults.

The first build dropped the part-pixel at every speed. On hardware, pointer speeds 10–20 then
crossed cleanly, but 1–9 stuck at the seam: a slow hand still crept Windows' cursor at pointer
speed 1, so Windows keeps the part-pixel there, and the board's estimate fell behind. Keeping it
below ×1 and dropping it from ×1 up fits every hardware result so far (#311's and #312's).

## Alternatives

- **A page-only warning.** Only pointer speeds 10, 14 and 18 would work. Rejected: it asks the
  user to change a Windows setting they chose.
- **Helper only.** The helper reads and places the real cursor, so every step works. Rejected as
  the only answer: the board must cross correctly without a helper too.
- **Per-monitor sizes.** Monitors of different sizes on one output need a size and a position
  each, and a config version bump. Not needed by any desk yet; tracked as #313.

## Consequences

- The firmware knows Windows' pointer speed table. A Windows change to that table would need a
  firmware change.
- Enhance pointer precision on still breaks the estimate. Its curve depends on hand speed and is
  not copied.
- A user who upgrades keeps the old estimate until they move the page's slider and Save once.
