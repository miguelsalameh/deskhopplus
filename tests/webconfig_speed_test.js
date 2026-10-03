// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Derek Reynolds

// The config page's pointer speed calculation (#311): Speed X / Y from the
// output's OS, screen count, screen size and Windows' pointer speed (1–20, as
// Windows 11 Settings shows it), and the step read back from a saved Speed X / Y.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const html = fs.readFileSync(process.argv[2], 'utf8');
const context = {console, Uint8Array, ArrayBuffer, DataView, navigator: {}, window: {addEventListener() {}},
  MutationObserver: class {observe() {}},
  document: {getElementById() { return {addEventListener() {}}; }, querySelector() { return null; },
    querySelectorAll() { return []; }}};
vm.createContext(context);
vm.runInContext(html.match(/<script>\s*([\s\S]*?)\s*<\/script>/)[1], context);
const {speedFor, stepFor} = context;

const windows = (width, height, step) => ({os: 3, screens: 2, width, height, step});
const plain = result => ({x: result.x, y: result.y, warnings: [...result.warnings]});

// Worked by hand: round(32768 / pixels × Windows' multiplier), clamped to 1–128.
assert.deepEqual(plain(speedFor(windows(1920, 1080, 10))), {x: 17, y: 30, warnings: []});
assert.deepEqual(plain(speedFor(windows(2560, 1440, 10))), {x: 13, y: 23, warnings: []});
assert.deepEqual(plain(speedFor(windows(2560, 1440, 12))), {x: 19, y: 34, warnings: []});
assert.deepEqual(plain(speedFor(windows(1920, 1080, 9))), {x: 15, y: 27, warnings: []}, 'step 9 is 7/8');
assert.deepEqual(plain(speedFor(windows(1920, 1080, 11))), {x: 21, y: 38, warnings: []}, 'step 11 is 1.25');
assert.deepEqual(plain(speedFor(windows(1920, 1080, 1))), {x: 1, y: 1, warnings: ['below']},
  'step 1 needs less than 1, so it cannot be matched');
assert.deepEqual(plain(speedFor(windows(1280, 720, 20))), {x: 90, y: 128, warnings: ['above']},
  '720 rows at step 20 need 159, capped at 128');

// Every screen absolute: X is the feel and Y follows the screen shape.
assert.deepEqual(plain(speedFor({os: 2, screens: 2, width: 1920, height: 1080, x: 16})), {x: 16, y: 28, warnings: []});
assert.deepEqual(plain(speedFor({os: 3, screens: 1, width: 1920, height: 1080, x: 16, step: 10})),
  {x: 16, y: 28, warnings: []}, 'a one-screen Windows output is absolute too');
assert.deepEqual(plain(speedFor({os: 2, screens: 1, width: 1920, height: 1080, x: 16, y: 9, linked: false})),
  {x: 16, y: 9, warnings: []}, 'unlinked keeps its own Y');
assert.deepEqual(plain(speedFor({os: 2, screens: 1, width: 1280, height: 720, x: 100})),
  {x: 100, y: 128, warnings: []}, 'a derived Y stays in range');

// Read back: the step a saved Speed X / Y came from, or 0 for Custom.
for (const [width, height] of [[1920, 1080], [2560, 1440], [3840, 2160]])
  for (let step = 1; step <= 20; step++) {
    const saved = speedFor(windows(width, height, step));
    const found = stepFor({...windows(width, height), x: saved.x, y: saved.y});
    assert.ok(found, `${width} × ${height} step ${step} is found`);
    const again = speedFor(windows(width, height, found));
    assert.deepEqual([again.x, again.y], [saved.x, saved.y], `${width} × ${height} step ${step} round-trips`);
  }
assert.equal(stepFor({...windows(1920, 1080), x: 17, y: 30}), 10);
assert.equal(stepFor({...windows(1920, 1080), x: 16, y: 28}), 0, 'the firmware default is Custom at 1080p');

console.log('webconfig_speed_test: Windows steps, absolute feel and read-back passed');
