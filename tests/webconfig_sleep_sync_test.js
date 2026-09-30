// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Derek Reynolds

// The Sleep sync field on the config page (#287): an Off/On select in
// Keyboard & Mouse, after the Status LED group, where a stored zero reads as
// Off and Save alone writes it to both boards.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const html = fs.readFileSync(process.argv[2], 'utf8');
const keyboard = html.match(/<section data-section="keyboard"[\s\S]*?<\/section>/)?.[0];
assert.ok(keyboard, 'the Keyboard & Mouse section is rendered');
assert.match(keyboard, /<h3 class="sub">Status LED<\/h3>[\s\S]*<h3 class="sub">Power<\/h3>/,
  'the Power group follows the Status LED group');
assert.match(keyboard, /An idle computer you are not using sleeps alone\./);

// A select as the browser keeps it: a value the options lack reads as ''.
const markup = keyboard.match(/<select[^>]*data-key="103"[^>]*>([\s\S]*?)<\/select>/);
assert.ok(markup, 'field 103 is a select in Keyboard & Mouse');
assert.equal(markup[0].includes('valueChangedHandler'), false, 'Sleep sync waits for Save');
assert.equal(keyboard.match(/<label for="f103">([^<]*)<\/label>/)?.[1], 'Sleep sync');
const options = [...markup[1].matchAll(/<option[^>]*value="?([^">]*)"?>([^<]*)<\/option>/g)]
  .map(([, value, text]) => ({value, text}));
assert.deepEqual(options.filter(o => o.value).map(o => [o.value, o.text]), [['0', 'Off'], ['1', 'On']]);

const attrs = new Map([['data-key', '103'], ['data-type', 'uint8']]);
let value = '';
const field = {
  tagName: 'SELECT', options, disabled: false,
  get value() { return value; },
  set value(v) { value = options.some(o => o.value === String(v)) ? String(v) : ''; },
  add(option) { options.push(option); },
  getAttribute(name) { return attrs.get(name) ?? null; },
  setAttribute(name, v) { attrs.set(name, String(v)); },
  hasAttribute(name) { return attrs.has(name); },
  dispatchEvent() {},
};

const context = {console, Uint8Array, ArrayBuffer, DataView, Event: function() {},
  Option: function(text, value) { this.text = text; this.value = String(value); },
  navigator: {}, window: {addEventListener() {}}, MutationObserver: class {observe() {}},
  document: {getElementById() { return {addEventListener() {}}; },
    querySelector(selector) { return selector.includes('data-key="103"') ? field : null; },
    querySelectorAll(selector) { return selector === '.api' ? [field] : []; }}};
vm.createContext(context);
vm.runInContext(html.match(/<script>\s*([\s\S]*?)\s*<\/script>/)[1], context);
const sent = [];
context.redrawLayout = () => {};
context.sendReport = async (type, payload, both) => sent.push({type, payload, both});
context.saveHotkeys = async () => true;
context.saveKeymaps = async () => true;
context.device = {opened: true};

function read(number) {
  const report = new DataView(new ArrayBuffer(12));
  report.setUint32(4, number, true);
  context.updateElement(103, {data: report});
}

(async () => {
  read(0);
  assert.equal(field.value, '0', 'a stored zero reads as Off');

  field.value = '1';
  assert.equal(sent.length, 0, 'nothing reaches the board before Save');
  await context.saveHandler();
  const writes = sent.filter(x => x.type === 21);
  assert.equal(writes.length, 1, 'Save writes the field once');
  assert.ok(writes[0].both, 'Save writes both boards');
  const write = new DataView(writes[0].payload.buffer);
  assert.equal(write.getUint8(0), 103);
  assert.equal(write.getUint8(1), 1);
  console.log('webconfig_sleep_sync_test: field, Off reading and Save passed');
})().catch(error => { console.error(error); process.exit(1); });
