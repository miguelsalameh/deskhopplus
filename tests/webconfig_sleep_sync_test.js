// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Derek Reynolds

// The Power group on the config page: Sleep sync (#287), an Off/On select in
// Keyboard & Mouse after the Status LED group, and Sleep when idle (#303),
// shown only while Sleep sync is On. A stored zero reads as Off and Never,
// and Save alone writes both to both boards.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const html = fs.readFileSync(process.argv[2], 'utf8');
const keyboard = html.match(/<section data-section="keyboard"[\s\S]*?<\/section>/)?.[0];
assert.ok(keyboard, 'the Keyboard & Mouse section is rendered');
assert.match(keyboard, /<h3 class="sub">Status LED<\/h3>[\s\S]*<h3 class="sub">Power<\/h3>/,
  'the Power group follows the Status LED group');
assert.match(keyboard, /Sleep when idle: if the other computer is already asleep, the computer\s+you're using sleeps after this long with no input\./);

// A select as the browser keeps it: a value the options lack reads as ''.
function select(key) {
  const markup = keyboard.match(new RegExp(`<select[^>]*data-key="${key}"[^>]*>([\\s\\S]*?)</select>`));
  assert.ok(markup, `field ${key} is a select in Keyboard & Mouse`);
  assert.equal(markup[0].includes('valueChangedHandler'), false, `field ${key} waits for Save`);
  const label = keyboard.match(new RegExp(`<label for="f${key}">([^<]*)</label>`))?.[1];
  const options = [...markup[1].matchAll(/<option[^>]*value="?([^">]*)"?>([^<]*)<\/option>/g)]
    .map(([, value, text]) => ({value, text}));
  const attrs = new Map([['data-key', String(key)], ['data-type', 'uint8']]);
  const row = {hidden: false};
  let value = '';
  return {
    tagName: 'SELECT', label, options, row, disabled: false,
    get value() { return value; },
    set value(v) { value = options.some(o => o.value === String(v)) ? String(v) : ''; },
    add(option) { options.push(option); },
    closest() { return row; },
    getAttribute(name) { return attrs.get(name) ?? null; },
    setAttribute(name, v) { attrs.set(name, String(v)); },
    hasAttribute(name) { return attrs.has(name); },
    dispatchEvent() {},
  };
}

const sync = select(103), idle = select(104);
assert.equal(sync.label, 'Sleep sync');
assert.equal(idle.label, 'Sleep when idle');
assert.deepEqual(sync.options.filter(o => o.value).map(o => [o.value, o.text]), [['0', 'Off'], ['1', 'On']]);
assert.deepEqual(idle.options.filter(o => o.value).map(o => [o.value, o.text]),
  [['0', 'Never'], ['15', '15 minutes'], ['30', '30 minutes'], ['60', '1 hour'], ['120', '2 hours']]);

const fields = new Map([[103, sync], [104, idle]]);
const context = {console, Uint8Array, ArrayBuffer, DataView, Event: function() {},
  Option: function(text, value) { this.text = text; this.value = String(value); },
  navigator: {}, window: {addEventListener() {}}, MutationObserver: class {observe() {}},
  document: {getElementById() { return {addEventListener() {}}; },
    querySelector(selector) { return fields.get(Number(selector.match(/data-key="(\d+)"/)?.[1])) || null; },
    querySelectorAll(selector) { return selector === '.api' ? [...fields.values()] : []; }}};
vm.createContext(context);
vm.runInContext(html.match(/<script>\s*([\s\S]*?)\s*<\/script>/)[1], context);
const sent = [];
context.redrawLayout = () => {};
context.sendReport = async (type, payload, both) => sent.push({type, payload, both});
context.saveHotkeys = async () => true;
context.saveKeymaps = async () => true;
context.device = {opened: true};

function read(key, number) {
  const report = new DataView(new ArrayBuffer(12));
  report.setUint32(4, number, true);
  context.updateElement(key, {data: report});
}

(async () => {
  read(103, 0);
  read(104, 0);
  assert.equal(sync.value, '0', 'a stored zero reads as Off');
  assert.equal(idle.value, '0', 'a stored zero reads as Never');
  context.refreshSleepIdle();
  assert.equal(idle.row.hidden, true, 'Sleep when idle is hidden while Sleep sync is Off');

  sync.value = '1';
  idle.value = '30';
  context.refreshSleepIdle();
  assert.equal(idle.row.hidden, false, 'Sleep when idle shows while Sleep sync is On');
  assert.equal(sent.length, 0, 'nothing reaches the board before Save');
  await context.saveHandler();
  const writes = sent.filter(x => x.type === 21).map(x => {
    const write = new DataView(x.payload.buffer);
    return [write.getUint8(0), write.getUint8(1), x.both];
  });
  assert.deepEqual(writes, [[103, 1, true], [104, 30, true]], 'Save writes each field once, to both boards');
  console.log('webconfig_sleep_sync_test: fields, Off and Never readings, row and Save passed');
})().catch(error => { console.error(error); process.exit(1); });
