// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Derek Reynolds

// The Status LED group on the config page (#283): the two selects render in
// Keyboard & Mouse, After is off while Turn off is Never or Always (#309), a
// stored time the list lacks shows as its own option, and Save alone sends the
// two fields.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const html = fs.readFileSync(process.argv[2], 'utf8');
const keyboard = html.match(/<section data-section="keyboard"[\s\S]*?<\/section>/)?.[0];
assert.ok(keyboard, 'the Keyboard & Mouse section is rendered');
assert.match(keyboard, /<h3 class="sub">Status LED<\/h3>/, 'the Status LED group is in Keyboard & Mouse');
assert.match(keyboard, /When idle: any key or mouse move turns it back on\. Config mode always blinks\./);

// A select as the browser keeps it: a value the options lack reads as ''.
function select(key) {
  const markup = keyboard.match(new RegExp(`<select[^>]*data-key="${key}"[^>]*>([\\s\\S]*?)</select>`));
  assert.ok(markup, `field ${key} is a select in Keyboard & Mouse`);
  assert.equal(markup[0].includes('valueChangedHandler'), false, `field ${key} waits for Save`);
  const label = keyboard.match(new RegExp(`<label for="f${key}">([^<]*)</label>`))?.[1];
  const options = [...markup[1].matchAll(/<option[^>]*value="?([^">]*)"?>([^<]*)<\/option>/g)]
    .map(([, value, text]) => ({value, text}));
  const attrs = new Map([['data-key', String(key)], ['data-type', key === 101 ? 'uint8' : 'uint16']]);
  let value = '';
  return {
    tagName: 'SELECT', label, options, disabled: false,
    get value() { return value; },
    set value(v) { value = options.some(o => o.value === String(v)) ? String(v) : ''; },
    add(option) { options.push(option); },
    getAttribute(name) { return attrs.get(name) ?? null; },
    setAttribute(name, v) { attrs.set(name, String(v)); },
    hasAttribute(name) { return attrs.has(name); },
    dispatchEvent() {},
  };
}

const mode = select(101), after = select(102);
assert.equal(mode.label, 'Turn off');
assert.equal(after.label, 'After');
assert.deepEqual(mode.options.filter(o => o.value).map(o => [o.value, o.text]),
  [['0', 'Never'], ['1', 'When idle'], ['2', 'After a switch'], ['3', 'Always']]);
assert.deepEqual(after.options.filter(o => o.value).map(o => [o.value, o.text]),
  [['5', '5 seconds'], ['10', '10 seconds'], ['30', '30 seconds'], ['60', '1 minute'],
   ['300', '5 minutes'], ['900', '15 minutes'], ['3600', '1 hour']]);

const fields = new Map([[101, mode], [102, after]]);
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
  read(101, 0);
  read(102, 60);
  context.refreshStatusLed();
  assert.equal(after.value, '60');
  assert.equal(after.disabled, true, 'After is off while Turn off is Never');
  mode.value = '1';
  context.refreshStatusLed();
  assert.equal(after.disabled, false, 'After is on for When idle');
  mode.value = '3';
  context.refreshStatusLed();
  assert.equal(after.disabled, true, 'After is off while Turn off is Always');
  mode.value = '1';
  context.refreshStatusLed();
  assert.equal(after.disabled, false, 'After is on again for When idle');

  // A hand-built config's time shows as its own option, not as a blank.
  read(102, 90);
  assert.equal(after.value, '90', 'a stored time outside the list is shown');
  assert.equal(after.options.at(-1).text, '90');
  assert.equal(after.options.filter(o => o.value === '90').length, 1);
  read(102, 90);
  assert.equal(after.options.filter(o => o.value === '90').length, 1, 'a second Read adds no second option');

  read(101, 0);
  read(102, 60);
  mode.value = '2';
  after.value = '3600';
  assert.equal(sent.length, 0, 'nothing reaches the board before Save');
  await context.saveHandler();
  const writes = sent.filter(x => x.type === 21);
  assert.equal(writes.length, 2, 'Save writes both fields');
  assert.ok(writes.every(x => x.both), 'Save writes both boards');
  const [modeWrite, afterWrite] = writes.map(x => new DataView(x.payload.buffer));
  assert.equal(modeWrite.getUint8(0), 101);
  assert.equal(modeWrite.getUint8(1), 2);
  assert.equal(afterWrite.getUint8(0), 102);
  assert.equal(afterWrite.getUint16(1, true), 3600);

  // Always (#309) packs to 3 in field 101.
  sent.length = 0;
  mode.value = '3';
  assert.equal(sent.length, 0, 'nothing reaches the board before Save');
  await context.saveHandler();
  const always = sent.filter(x => x.type === 21).map(x => new DataView(x.payload.buffer))
    .find(v => v.getUint8(0) === 101);
  assert.equal(always.getUint8(1), 3, 'Always packs to 3');
  console.log('webconfig_status_led_test: group, After rule, unknown time and Save passed');
})().catch(error => { console.error(error); process.exit(1); });
