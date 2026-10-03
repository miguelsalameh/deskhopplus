// Page glue for the settings window (#225): which section is shown, which
// computer's rows are under the picture, the connection words, the unsaved
// count and the refusal strip. Presentation only: no field is duplicated and
// nothing here talks to the board.

function showSection(id) {
  document.querySelectorAll('section[data-section]').forEach(s => {s.hidden = s.dataset.section !== id;});
  document.querySelectorAll('#sidebar button').forEach(b => {
    if (b.dataset.section === id) b.setAttribute('aria-current', 'page'); else b.removeAttribute('aria-current');
  });
}

document.getElementById('sidebar').addEventListener('click', event => {
  const button = event.target.closest('button[data-section]');
  if (button) showSection(button.dataset.section);
});

// Click a computer in the picture and its rows appear under it, the way the
// OS shows the settings of the monitor you click.
function selectComputer(letter) {
  document.getElementById('layout').dataset.selected = letter;
  document.querySelectorAll('.computer[data-computer]').forEach(c => {c.hidden = c.dataset.computer !== letter;});
  refreshTitles();
}

// "Output A · MacOS": the group title carries the OS the picture's label shows.
function refreshTitles() {
  document.querySelectorAll('[data-os-of]').forEach(span => {
    const os = [...document.querySelectorAll(`.computer[data-computer="${span.dataset.osOf}"] .row`)]
      .find(row => row.querySelector('label')?.textContent === 'Operating System')?.querySelector('select');
    const name = os && os.selectedIndex > 0 ? os.options[os.selectedIndex].text : '';
    span.textContent = name ? ' · ' + name : '';
  });
}

for (const type of ['pointerdown', 'focusin', 'keydown'])
  document.getElementById('layout').addEventListener(type, event => {
    const computer = event.target.closest('[data-output]');
    if (computer) selectComputer(computer.dataset.output);
  });

// Every gesture ends in a redraw of the picture, and a drop writes its fields
// without an event, so the redraw is where the unsaved count follows a drop.
// The same pass keeps a wide desk (up to seven monitors a side) readable: at
// least 0.9px per picture unit, and the picture scrolls inside the well
// instead of shrinking to a strip.
new MutationObserver(() => {
  const svg = document.querySelector('#layout svg');
  if (svg) svg.style.minWidth = Math.round(svg.viewBox.baseVal.width * 0.9) + 'px';
  refresh();
}).observe(document.getElementById('layout'), {childList: true});

// Connect flips every board-facing control on; the words in the toolbar say
// so. The markup starts disabled, so nothing runs at load.
const connected = () => document.getElementById('connection').dataset.on === '1';

function setConnected(on) {
  const words = document.getElementById('connection');
  words.textContent = on ? 'Connected — config mode' : 'Not connected';
  words.dataset.on = on ? '1' : '';
  document.getElementById('fields').disabled = !on;
  document.querySelectorAll('.online').forEach(b => {b.disabled = !on;});
  // Off: the picture goes back to its connect text, so nothing can be dragged.
  if (!on) redrawLayout();
}

// Unsaved: derived, never stored. A field is unsaved when its value differs
// from what the last Read fetched. Save shows the count; a section with one
// or more carries a dot.
function unsavedFields() {
  return [...document.querySelectorAll('.api, .hotkey-text, .keymap-text')].filter(f =>
    !f.readOnly && f.type !== 'hidden' && f.hasAttribute('fetched-value') && f.getAttribute('fetched-value') != getValue(f));
}

function refreshUnsaved() {
  const fields = unsavedFields();
  const badge = document.getElementById('unsaved');
  badge.textContent = fields.length;
  badge.hidden = !fields.length;
  const sections = new Set(fields.map(f => f.closest('section[data-section]').dataset.section));
  document.querySelectorAll('#sidebar button').forEach(b => b.toggleAttribute('data-unsaved', sections.has(b.dataset.section)));
}

// Status LED: After means nothing while Turn off is Never (#283) or Always (#309).
function refreshStatusLed() {
  const mode = document.querySelector('[data-key="101"]');
  const after = document.querySelector('[data-key="102"]');
  if (mode && after) after.disabled = !Number(mode.value) || mode.value === '3';
}

// Power: Sleep when idle shows only while Sleep sync is On (#303).
function refreshSleepIdle() {
  const sync = document.querySelector('[data-key="103"]');
  const row = document.querySelector('[data-key="104"]')?.closest('.row');
  if (sync && row) row.hidden = !Number(sync.value);
}

// What every edit, report and toolbar action refreshes.
function refresh() {
  refreshSpeed();
  refreshUnsaved();
  refreshTitles();
  refreshStatusLed();
  refreshSleepIdle();
}

document.getElementById('main').addEventListener('input', event => {
  const speed = event.target.closest('[data-speed]');
  if (speed) writeSpeed(speed, event.target);
  if (event.target.getAttribute('aria-invalid')) {
    event.target.removeAttribute('aria-invalid');
    event.target.closest('.row').querySelector('small').textContent = '';
  }
  refresh();
});
document.getElementById('main').addEventListener('change', refresh);

// Exit reboots the board, which drops the device: the toolbar says so. A
// page that already reported why (an action threw first) keeps its reason.
navigator.hid?.addEventListener('disconnect', event => {
  if (event.device === device && connected()) setConnected(false);
});

// Toolbar actions run one at a time: the toolbar is disabled while one runs,
// so a second click on Save cannot start a second Save loop. A refused Save
// bands the top of the window with the fields that need a fix. An action that
// throws means the board is gone or was never chosen: the page says so and
// keeps the user's values for a later Read or Save.
document.getElementById('menu-buttons').addEventListener('click', async event => {
  const handler = event.target.closest('button')?.dataset.handler;
  if (!handler) return;
  const buttons = [...document.querySelectorAll('#menu-buttons button')];
  buttons.forEach(b => {b.disabled = true;});
  try {
    const result = await window[handler]();
    // Back to the rule, not to a snapshot: Connect may just have turned the page on.
    buttons.forEach(b => {b.disabled = b.classList.contains('online') && !connected();});
    if (handler === 'saveHandler') showRefusal(result === false);
    // Read replaces the values the last Save refused, so its errors go too.
    if (handler === 'readHandler') clearErrors();
  } catch (error) {
    console.error(error);
    setConnected(false);
    document.querySelector('[data-handler="connectHandler"]').disabled = false;
    document.getElementById('connection').textContent = 'Not connected — ' + (error.message || error);
  }
  refresh();
});

function clearErrors() {
  document.querySelectorAll('.hotkey-error, .keymap-error').forEach(e => {e.textContent = '';});
  showRefusal(false);
}

function revealField(field) {
  showSection(field.closest('section[data-section]').dataset.section);
  const computer = field.closest('.computer[data-computer]');
  if (computer) selectComputer(computer.dataset.computer);
  const advanced = field.closest('details');
  if (advanced) advanced.open = true;
  field.scrollIntoView({block: 'center'});
  field.focus();
}

function showRefusal(refused) {
  const strip = document.getElementById('refusal');
  const rows = refused ? [...document.querySelectorAll('.row')].filter(r => r.querySelector('small')?.textContent) : [];
  const control = row => row.querySelector('input, textarea');
  document.querySelectorAll('[aria-invalid]').forEach(f => f.removeAttribute('aria-invalid'));
  rows.forEach(r => control(r).setAttribute('aria-invalid', 'true'));
  strip.hidden = !rows.length;
  if (!rows.length) return;
  const name = row => {
    const section = row.closest('section[data-section]').querySelector('h2').textContent;
    const computer = row.closest('.computer[data-computer]');
    return `${section} › ${computer ? 'Output ' + computer.dataset.computer + ' ' : ''}${row.querySelector('label').textContent}`;
  };
  const esc = text => text.replace(/[&<>]/g, c => ({'&':'&amp;', '<':'&lt;', '>':'&gt;'}[c]));
  strip.lastElementChild.innerHTML = `<strong>Save refused.</strong> ${rows.length} field${rows.length > 1 ? 's need' : ' needs'} a fix; nothing was sent.<ul>` +
    rows.map(row => `<li><a href="#">${esc(name(row))}</a>: ${esc(row.querySelector('small').textContent)}</li>`).join('') + '</ul>';
  strip.querySelectorAll('a').forEach((a, i) => a.addEventListener('click', e => {e.preventDefault(); revealField(control(rows[i]));}));
  revealField(control(rows[0]));
}

// Service: a button that wipes or reboots takes two clicks. The first arms it
// and says so; the second acts; a click anywhere else disarms it.
window.addEventListener('click', event => {
  const armed = document.querySelector('#service-buttons button[data-armed]');
  if (armed && !armed.contains(event.target)) {armed.textContent = armed.dataset.armed; delete armed.dataset.armed;}
});
document.getElementById('service-buttons').addEventListener('click', event => {
  const button = event.target.closest('button[data-handler]');
  if (!button) return;
  if (button.dataset.arm && !button.dataset.armed) {
    button.dataset.armed = button.textContent;
    button.textContent = button.dataset.arm;
    return;
  }
  if (button.dataset.armed) {button.textContent = button.dataset.armed; delete button.dataset.armed;}
  window[button.dataset.handler]();
});

// Pointer speed (#311): the page works out Speed X / Y, the board's fields.
// A Windows output with two or more screens matches Windows' pointer speed:
// its other screens get relative counts that Windows scales by that speed, and
// the board's estimate of that cursor is exact only at 32768 ÷ pixels × the
// speed's multiplier, with Enhance pointer precision off. The steps are
// Windows 11 Settings' 1–20 (MouseSensitivity); the old Pointer Options
// slider's 11 ticks are the even steps and 1. Elsewhere every
// screen is absolute, so X is only the feel and Y follows the screen shape.
const windowsNotches = [1/32, 1/16, 1/8, 2/8, 3/8, 4/8, 5/8, 6/8, 7/8, 1,
  1.25, 1.5, 1.75, 2, 2.25, 2.5, 2.75, 3, 3.25, 3.5];
const speedSizes = {{ speed_sizes | map('list') | list }};
const clampSpeed = v => Math.max(1, Math.min(128, Math.round(v)));
const matchesWindows = (os, screens) => os == 3 && screens >= 2;

// {x, y, warnings}; warnings: 'below' or 'above' (a step the 1–128 range
// cannot match) and 'epp' (Enhance pointer precision not ticked off).
function speedFor({os, screens, width, height, notch, x, y, linked = true, epp = true}) {
  if (!matchesWindows(os, screens))
    return {x: clampSpeed(x), y: clampSpeed(linked ? x * width / height : y), warnings: []};
  const exact = [width, height].map(px => 32768 / px * windowsNotches[notch - 1]);
  const warnings = [];
  if (exact.some(v => v < 1)) warnings.push('below');
  if (exact.some(v => v > 128)) warnings.push('above');
  if (!epp) warnings.push('epp');
  return {x: clampSpeed(exact[0]), y: clampSpeed(exact[1]), warnings};
}

// The step a saved Speed X / Y came from, or 0 (Custom). Low steps can
// share a value; the highest is taken.
function notchFor(output) {
  for (let notch = windowsNotches.length; notch; notch--) {
    const s = speedFor({...output, notch});
    if (s.x == output.x && s.y == output.y) return notch;
  }
  return 0;
}

// The computer this page runs on, as the Operating System field numbers it.
const pageOs = /Windows/.test(navigator.userAgent) ? 3 : /Mac/.test(navigator.userAgent) ? 2 : 0;

// One output's speed inputs, as the group's controls and fields hold them.
function speedInputs(group) {
  const base = Number(group.dataset.speed), raw = n => document.querySelector(`.api[data-key="${base + n}"]`);
  const ui = name => group.querySelector(name), size = ui('.size');
  const [width, height] = size.value === 'other' ? [ui('.size-w').value, ui('.size-h').value].map(Number) : size.value.split('x').map(Number);
  return {os: Number(raw(6).value), screens: Number(raw(1).value), width, height, notch: Number(ui('.feel').value),
    x: Number(raw(2).value), y: Number(raw(3).value), linked: ui('.link').checked, epp: ui('.epp').checked,
    rawX: raw(2), rawY: raw(3), ui};
}

// A gesture on a group's controls writes Speed X / Y; Save sends them. On a
// Windows output only the slider replaces a Custom value.
function writeSpeed(group, target) {
  const s = speedInputs(group), windows = matchesWindows(s.os, s.screens);
  if (target.matches('.size')) target.dataset.touched = '1';
  if (target.matches('.link')) group.dataset.unlinked = target.checked ? '' : '1';
  if (target.matches('.size-w, .size-h')) s.ui('.size').dataset.touched = '1';
  if (!(s.width > 0 && s.height > 0) || (windows && !target.matches('.feel') && group.dataset.custom)) return;
  const result = speedFor({...s, x: windows ? s.x : Number(s.ui('.feel').value), y: Number(s.ui('.feel-y').value)});
  // Both values before either event: a refresh between them would see a
  // half-written pair and break the link.
  s.rawX.value = result.x;
  s.rawY.value = result.y;
  for (const field of [s.rawX, s.rawY]) field.dispatchEvent(new Event('input', {bubbles: true}));
}

// Shows each group as its fields say: the Windows pointer speed or the plain speed,
// the screen size (detected on this computer, until the user picks one), and
// the Speed X / Y the board will get. Never writes a field; marks a group
// Custom (data-custom) for writeSpeed.
function refreshSpeed() {
  const groups = [...document.querySelectorAll('[data-speed]')];
  const here = groups.filter(g => speedInputs(g).os === pageOs);
  for (const group of groups) {
    const ui = name => group.querySelector(name), size = ui('.size'), row = name => ui(name).closest('.row');
    const detect = here.length === 1 && here[0] === group;
    if (!size.dataset.touched) {
      const px = [screen.width, screen.height].map(v => Math.round(v * devicePixelRatio));
      const near = speedSizes.find(s => s.every((v, i) => Math.abs(v - px[i]) <= v / 50));
      size.value = !detect ? '1920x1080' : near ? near.join('x') : 'other';
      if (detect && !near) [ui('.size-w').value, ui('.size-h').value] = px;
      ui('.detected').textContent = detect ? `Detected: ${(near || px).join(' × ')}` : '';
    }
    ui('.size-w').hidden = ui('.size-h').hidden = size.value !== 'other';
    const s = speedInputs(group), windows = matchesWindows(s.os, s.screens), feel = ui('.feel');
    let note, result = `Speed X ${s.x} · Speed Y ${s.y}.`, warnings = [];
    if (windows) {
      feel.max = windowsNotches.length;
      const matched = speedFor(s);
      const notch = matched.x === s.x && matched.y === s.y ? s.notch : notchFor(s);
      if (notch) {feel.value = notch; warnings = speedFor({...s, notch}).warnings;}
      else if (!s.epp) warnings = ['epp'];
      group.dataset.custom = notch ? '' : '1';
      feel.previousElementSibling.textContent = notch || 'Custom';
      note = 'Set this to the number Windows shows for Mouse pointer speed (Settings → Bluetooth & devices → Mouse). ' +
        'Turn off Enhance pointer precision in Additional mouse settings → Pointer Options. ' +
        'Windows uses one speed for both directions. Enable Acceleration changes only the main screen.';
      result = notch ? `→ ${result}${warnings.length ? '' : ' Crossings land on the edge.'}`
        : `Custom: ${result} Move the slider to match Windows.`;
    } else {
      feel.max = 128;
      feel.value = s.x;
      feel.previousElementSibling.textContent = s.x;
      // Linked while Y fits the shape, unless the user unlinked. Derived each
      // time: Read lands Speed X before Speed Y, so a half-read pair is off.
      ui('.link').checked = !group.dataset.unlinked && speedFor({...s, linked: true}).y === s.y;
      ui('.feel-y').value = ui('.feel-y').previousElementSibling.textContent = s.y;
      note = ui('.link').checked ? 'Speed Y follows the screen shape, so both directions feel the same.' : '';
    }
    row('.epp').hidden = !windows;
    row('.link').hidden = windows;
    row('.feel-y').hidden = windows || ui('.link').checked;
    ui('.speed-note .hint').textContent = note;
    ui('.result').textContent = result;
    ui('.warn').textContent = [...warnings.map(w => ({below: 'This step needs a speed below 1, so it uses 1.',
      above: 'This step needs a speed above 128, so it uses 128.'})[w]),
      warnings.length && 'Crossings can be early or late.'].filter(Boolean).join(' ');
  }
}
