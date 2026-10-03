# ADR-0017: Without a helper, the board copies Windows' pointer maths for monitors past the main one

- **Status:** Accepted
- **Date:** 2026-10-03
- **Resolves:** [#312](https://github.com/myn/deskhopplus/issues/312)
- **Amended:** 2026-10-03, #312 hardware checks: Windows keeps the part-pixel at every pointer
  speed

## Decision

On a Windows output with two or more monitors and no helper, the board cannot see the cursor on
a monitor past the main one. It sends raw counts there, and keeps an **estimate** of where Windows
put the cursor. The board now works that estimate out the way Windows does: each report moves the
cursor counts × the pointer speed's multiplier on each axis, in whole pixels, truncated toward
zero, and Windows keeps the part-pixel and adds it to the next report. The board keeps the same
part-pixel, per computer and axis, in device state (not config). The estimate is kept on the pixel
grid of the saved monitor size. A helper-free walk onto such a monitor uses the same maths for its
nudges, and picks the counts that land nearest for its move back along the seam.

- Each output saves Windows' **pointer speed** (1–20) and the **monitor size** in pixels, in
  alignment padding that every saved config holds as zero: no `CURRENT_CONFIG_VERSION` bump.
  Zero means not set, and the board then keeps the old estimate, counts × Speed X / Y.
- The 20 multipliers live in one firmware header, `src/core/dh_windows_pointer.h`. The config
  page reads them from there at render.
- **Speed X / Y** keep their meaning: the board's speed where it places the cursor, so on Windows
  the main monitor's feel. The page still works them out from pointer speed and monitor size.

## Why

#311 made the page work out Speed X / Y so that counts × Speed matched Windows. The hardware
check failed at most pointer speeds. Speed is a whole number, and the exact value is a fraction
(8.53 at step 6 on 1080p), so the estimate drifts; low pointer speeds are worst. Copying Windows'
own maths, on the saved pixel grid, removes that error at every pointer speed.

How Windows treats a report's part-pixel took three hardware rounds to settle (2026-10-03):

1. The first build dropped it at every speed, a guess from #311's results. Pointer speeds 1–9
   then stuck at the seam (BL → Mac) until a hard flick, and a slow hand still crept Windows'
   cursor at pointer speed 1: Windows keeps it there.
2. The second build kept it below ×1 only. 1–9 crossed smoothly, but 12 and 20 now showed a
   slight stop with a slow hand, where a 1-count report moves 1.5 or 3.5 pixels and a dropped
   half falls behind. 10 (×1, no part-pixel) was smooth both times.
3. So Windows keeps it at every speed. #311's failures at 12 and 13 are not explained by the
   part-pixel; they came with the whole-number Speed and the pre-#312 walk.

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
- The model test's fake Windows shares the board's part-pixel rule, so it proves the board copies
  the rule, not that the rule is Windows'. Only hardware tests the rule itself.
- Enhance pointer precision on still breaks the estimate. Its curve depends on hand speed and is
  not copied.
- A user who upgrades keeps the old estimate until they move the page's slider and Save once.
