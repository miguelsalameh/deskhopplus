// Coordinates are monitor widths; half-widths are the layout grid.
// Directions are the board's: 1 Left, 2 Right, 4 Top, 5 Bottom.
const vectors = {1:[-1,0], 2:[1,0], 4:[0,-1], 5:[0,1]};
const opposite = {1:2, 2:1, 4:5, 5:4};
const at = (m, axis) => axis ? m.y : m.x;
// Monitor turns (CONTEXT.md: Turn; dh_mouse_layout.h): monitors 2 to 5 each
// carry two bits saying which way their line runs out of Main, relative to
// the chain axis: 0 along it, then clockwise, opposite, counter-clockwise.
// Monitors 6 and 7 run along the chain axis.
const LAST_TURNED = 5;
const turnable = number => number >= 2 && number <= LAST_TURNED;
// The directions clockwise, as the screen shows them.
const clockwise = [1, 4, 2, 5];
const turnBits = (turns, number) => turnable(number) ? (turns >> (2*(number-2))) & 3 : 0;
const turned = (chain, turn) => clockwise[(clockwise.indexOf(chain)+turn) % 4];
const turnOf = (chain, direction) => (clockwise.indexOf(direction)-clockwise.indexOf(chain)+4) % 4;
const directionOf = ([dx, dy]) => dx < 0 ? 1 : dx > 0 ? 2 : dy < 0 ? 4 : 5;
const fromMain = (main, m) => directionOf([m.x-main.x, m.y-main.y]);

// Where each monitor sits from Main, as the board places it: on its turn's
// line, one further out than the earlier monitors on that line.
function monitorsFromChain(count, chain, turns) {
  const reached = {};
  return Array.from({length:count}, (_, n) => {
    if (!n) return {number:1, x:0, y:0};
    const direction = turned(chain, turnBits(turns, n+1)) || chain;
    reached[direction] = (reached[direction] || 0)+1;
    const [dx, dy] = vectors[direction] || vectors[2];
    return {number:n+1, x:reached[direction]*dx, y:reached[direction]*dy};
  });
}

// The lines out of Main of a computer's boxes, each listed outward, or null
// when a box is off every line or a line has a hole.
function linesOf(ms) {
  const main = ms[0], lines = {};
  for (const m of ms.slice(1)) {
    const d = [m.x-main.x, m.y-main.y];
    if ((d[0] && d[1]) || (!d[0] && !d[1])) return null;
    (lines[directionOf(d)] ||= []).push({m, distance:Math.abs(d[0]+d[1])});
  }
  for (const line of Object.values(lines)) {
    line.sort((u, v) => u.distance-v.distance);
    if (line.some((e, n) => e.distance !== n+1)) return null;
  }
  return lines;
}

// The chain axis a computer's boxes give the board: the line of monitor 6
// when there is one, since 6 and 7 cannot turn, else the line of monitor 2.
const chainOf = ms => fromMain(ms[0], ms[ms.length > LAST_TURNED ? LAST_TURNED : 1]);

// Numbers a computer's boxes the way the board can run them: each line
// numbered outward, and every box past monitor 5 on one line. Keeps the
// numbers the boxes have when they already do.
function numbered(ms, lines) {
  const lineOf = new Map(Object.entries(lines).flatMap(([direction, entries]) =>
    entries.map(entry => [entry.m, Number(direction)])));
  const outward = Object.values(lines).every(entries =>
    entries.every((entry, n) => !n || entries[n-1].m.number < entry.m.number));
  const unturned = ms.filter(m => m.number > LAST_TURNED).map(m => lineOf.get(m));
  if (outward && unturned.every(direction => direction === unturned[0])) return ms;
  // The chain: the line of the lowest-numbered box, unless more than four
  // boxes would then sit off it; then the longest line. The boxes off it take
  // monitors 2 to 5 after as many of its own as fit; the rest of it follows.
  const lowest = ms.slice(1).reduce((u, v) => v.number < u.number ? v : u);
  const off = direction => ms.length-1-lines[direction].length;
  const chain = off(lineOf.get(lowest)) <= LAST_TURNED-1 ? lineOf.get(lowest) :
    Number(Object.keys(lines).reduce((u, v) => lines[v].length > lines[u].length ? v : u));
  const own = lines[chain].map(entry => entry.m), fit = LAST_TURNED-1-off(chain);
  const distance = m => lines[lineOf.get(m)].findIndex(entry => entry.m === m);
  const others = ms.slice(1).filter(m => lineOf.get(m) !== chain)
    .sort((u, v) => distance(u)-distance(v) || u.number-v.number);
  return [ms[0], ...own.slice(0, fit), ...others, ...own.slice(fit)].map((m, n) => ({...m, number:n+1}));
}

function layoutFromFields(fields) {
  const border = Number(fields[17]) || 2;
  const normal = vectors[border] || vectors[2];
  const tangent = normal[0] ? 1 : 0;
  const outputs = [0,1].map(i => {
    const base = i ? 40 : 10;
    const chain = Number(fields[98+i]) || 2;
    const turns = Number(fields[105+i]) || 0;
    const count = Math.max(1, Math.min(7, Number(fields[base+1]) || 1));
    const monitors = monitorsFromChain(count, chain, turns);
    const minX = Math.min(...monitors.map(m => m.x));
    const minY = Math.min(...monitors.map(m => m.y));
    monitors.forEach(m => {m.x -= minX; m.y -= minY;});
    return {letter:i ? 'B' : 'A', os:({1:'Linux',2:'MacOS',3:'Windows',4:'Android',255:'Other'})[fields[base+6]] || 'Other',
      chain, turns, border:Number(fields[base+7]) || opposite[border], monitors};
  });
  const segments = Array.from({length:4}, (_, n) => [{{ seam_field_bases|join(',') }}].map(base => ({
    monitor:Number(fields[base+3*n]) || 0,
    start:Number(fields[base+3*n+1]) || 0, end:Number(fields[base+3*n+2]) || 0,
  })));
  const empty = segments.every(pair => pair.every(s => !s.monitor));
  let custom = !vectors[border] || outputs[1].border !== opposite[border] ||
    outputs.some(o => o.monitors.length > 1 && !o.turns && o.chain === o.border) ||
    // Turns for monitors the computer does not have: the next move drops them.
    outputs.some(o => o.turns >> 2*Math.max(0, Math.min(o.monitors.length, LAST_TURNED)-1));
  const a = outputs[0].monitors, b = outputs[1].monitors;
  // Start with left/top-aligned lines on the side selected by Output A.
  let shift = normal.map((v, axis) => v > 0 ? Math.max(...a.map(m => at(m, axis)))+1 :
    v < 0 ? -Math.max(...b.map(m => at(m, axis)))-1 : 0);
  const edge = (m, side, fraction) => [m.x, m.y].map((v, axis) =>
    v + (axis === tangent ? fraction : (side[axis] > 0 ? 1 : 0)));
  const close = (x,y) => Math.abs(x-y) <= 1/65535;
  let recovered = false;
  const bands = [];
  segments.forEach((pair, index) => {
    if (pair.every(s => !s.monitor)) return;
    if (pair.some((s,i) => !Number.isInteger(s.monitor) || s.monitor < 1 ||
        s.monitor > outputs[i].monitors.length || s.start < 0 || s.end > 65535 || s.start >= s.end)) {
      custom = true; return;
    }
    const points = pair.map((s,i) => [s.start,s.end].map(value =>
      edge(outputs[i].monitors[s.monitor-1], normal.map(v => i ? -v : v), value/65535)));
    const delta = points[0][0].map((v,axis) => v-points[1][0][axis]);
    const snapped = delta.map(v => Math.round(v*2)/2);
    const valid = delta.every((v,axis) => close(v,snapped[axis]) &&
      close(points[0][1][axis]-points[1][1][axis],snapped[axis])) &&
      pair.every(s => [s.start,s.end].every(v => close(v/65535, Math.round(v/65535*2)/2)));
    const origins = pair.map((s,i) => {
      const m = outputs[i].monitors[s.monitor-1];
      return at(m, tangent) + (i ? snapped[tangent] : 0);
    });
    const overlapStart = Math.max(...origins), overlapEnd = Math.min(...origins)+1;
    const collides = a.some(m => b.some(n =>
      Math.abs(m.x-n.x-snapped[0])<1 && Math.abs(m.y-n.y-snapped[1])<1));
    if (!valid || collides || !close(points[0][0][tangent], overlapStart) ||
        !close(points[0][1][tangent], overlapEnd)) {custom = true; return;}
    if (!recovered) {shift = snapped; recovered = true;}
    if (!snapped.every((v,axis) => close(v,shift[axis]))) {custom = true; return;}
    bands.push({number:index+1, start:points[0][0], end:points[0][1]});
  });
  b.forEach(m => {m.x += shift[0]; m.y += shift[1];});
  if (a.some(m => b.some(n => Math.abs(m.x-n.x)<1 && Math.abs(m.y-n.y)<1))) custom = true;
  const minX = Math.min(...a.concat(b).map(m => m.x));
  const minY = Math.min(...a.concat(b).map(m => m.y));
  a.concat(b).forEach(m => {m.x -= minX; m.y -= minY;});
  bands.forEach(s => [s.start,s.end].forEach(p => {p[0] -= minX; p[1] -= minY;}));
  return {outputs, bands, note:empty ? 'Segments are not set. Move a monitor to set them.' :
    custom ? 'Custom values are set in Advanced. Moving a monitor here replaces them.' : ''};
}

// Derives every field the layout owns from where the boxes sit: counts, chain
// and border directions, and the segments in seam order. Returns {fields}, or
// {refused} with the one-line reason the board cannot run this layout.
function fieldsFromLayout(layout) {
  const [a, b] = layout.outputs.map(o => o.monitors);
  // Where the boxes of the two computers meet. B moves in half boxes, so two
  // boxes share an edge when they are one apart across it and less than one
  // along it. The board takes one straight seam.
  const seams = [];
  let corner = false;
  for (const p of a) for (const q of b) {
    const d = [q.x-p.x, q.y-p.y].map(Math.abs);
    if (d[0] < 1 && d[1] < 1) return {refused:'Not moved: the computers would overlap. Put them edge to edge.'};
    corner ||= d[0] === 1 && d[1] === 1;
    for (const axis of [0, 1])
      if (d[axis] === 1 && d[1-axis] < 1) {
        const forward = at(q, axis) > at(p, axis);
        const edge = {normal:axis, forward, seam:at(p, axis)+(forward ? 1 : 0)};
        if (!seams.some(s => s.normal === edge.normal && s.forward === edge.forward && s.seam === edge.seam))
          seams.push(edge);
      }
  }
  if (!seams.length) return {refused: corner ? 'Not moved: the computers would touch only at a corner. Put an edge against an edge.' :
    'Not moved: that leaves a gap between the computers. Put them edge to edge.'};
  if (seams.length > 1) return {refused:'Not moved: the computers would meet along more than one edge. Put them along one straight edge.'};
  const {normal, forward, seam} = seams[0], tangent = 1-normal;
  const borders = [normal ? (forward ? 5 : 4) : (forward ? 2 : 1)];
  borders.push(opposite[borders[0]]);
  const fields = {};
  for (const [i, o] of layout.outputs.entries()) {
    const ms = o.monitors, base = i ? 40 : 10;
    let chain = o.chain, turns = 0;
    if (ms.length > 1) {
      chain = chainOf(ms);
      for (const m of ms.filter(m => turnable(m.number)))
        turns |= turnOf(chain, fromMain(ms[0], m)) << (2*(m.number-2));
      // On a straight line the board lets only Main cross when the line
      // points at the other computer.
      if (!turns && chain === borders[i]) return {refused:'Not moved: put the main monitor next to the other computer.'};
    }
    fields[base+1] = ms.length; fields[98+i] = chain; fields[base+7] = borders[i]; fields[105+i] = turns;
  }
  const facing = (ms, positive) => ms.filter(m => at(m, normal) + (positive ? 1 : 0) === seam);
  const pairs = [];
  for (const p of facing(a, forward)) for (const q of facing(b, !forward)) {
    const start = Math.max(at(p, tangent), at(q, tangent)), end = Math.min(at(p, tangent), at(q, tangent))+1;
    if (end > start) pairs.push({start, end, sides:[p, q]});
  }
  pairs.sort((u, v) => u.start-v.start);
  if (pairs.length > 4) return {refused:`Not moved: that layout needs ${pairs.length} segments; the board holds 4.`};
  [{{ seam_field_bases|join(', ') }}].forEach((base, i) => {
    for (let n = 0; n < 4; n++) {
      const pair = pairs[n], m = pair && pair.sides[i];
      fields[base+3*n] = pair ? m.number : 0;
      fields[base+3*n+1] = pair ? Math.round((pair.start-at(m, tangent))*65535) : 0;
      fields[base+3*n+2] = pair ? Math.round((pair.end-at(m, tangent))*65535) : 0;
    }
  });
  return {fields};
}

// Applies one gesture to one output and returns {layout, fields}, or
// {layout, refused} with the layout unchanged. Gestures:
//   {dx, dy}           move the computer as a block by dx, dy boxes;
//   {monitor, dx, dy}  drop one box: Main of a straight line onto its far end
//                      flips the line (the boxes stay put, renumbered from
//                      Main); another box moves alone to the end of the line
//                      out of Main its cell is on, and the boxes past where it
//                      was close up, so a computer's monitors can make an L
//                      or a T; the only box of a one-monitor computer moves
//                      the block;
//   {add: 1 | -1}      append a box at the end of monitor 2's line, or remove
//                      the highest-numbered box, the end of its line.
// A box drop also returns `moved`, the number the dropped box has now: the
// boxes can be renumbered, and the keyboard keeps focus on that box.
function moveMonitor(layout, gesture) {
  const o = layout.outputs.find(o => o.letter === gesture.output), ms = o.monitors;
  const main = ms[0], last = ms[ms.length-1];
  const dir = ms.length > 1 ? [Math.sign(ms[1].x-main.x), Math.sign(ms[1].y-main.y)] : vectors[o.chain] || [1,0];
  const line = (from, [dx, dy], count) => Array.from({length:count}, (_, n) => ({number:n+1, x:from.x+n*dx, y:from.y+n*dy}));
  // The cell past the end of Main's line toward `toward`.
  const endOf = (boxes, toward) => {
    let n = 1;
    while (boxes.some(m => m.x === main.x+n*toward[0] && m.y === main.y+n*toward[1])) n++;
    return {x:main.x+n*toward[0], y:main.y+n*toward[1]};
  };
  let monitors, dropped;
  if (gesture.add > 0) {
    if (ms.length >= 7) return {layout, refused:'Not added: a computer holds at most 7 monitors.'};
    monitors = [...ms, {number:ms.length+1, ...endOf(ms, dir)}];
  } else if (gesture.add < 0) {
    if (ms.length < 2) return {layout, refused:'Not removed: a computer keeps its main monitor.'};
    monitors = ms.slice(0, -1);
  } else if (!gesture.monitor || ms.length < 2) {
    monitors = ms.map(m => ({...m, x:m.x+gesture.dx, y:m.y+gesture.dy}));
  } else if (gesture.monitor === 1) {
    if (o.turns) return {layout, refused:'Not moved: drag the label to move the whole computer.'};
    if (last.x !== main.x+gesture.dx || last.y !== main.y+gesture.dy)
      return {layout, refused:`Not moved: drop Main on monitor ${last.number} to flip it, or drag the label to move the whole computer.`};
    monitors = line(last, dir.map(v => -v), ms.length);
  } else {
    const m = ms[gesture.monitor-1];
    const from = [Math.sign(m.x-main.x), Math.sign(m.y-main.y)];
    const d = [m.x+gesture.dx-main.x, m.y+gesture.dy-main.y];
    if (!d[0] && !d[1]) return {layout, refused:'Not moved: drop a monitor beside, above or below the main monitor.'};
    // A cell off every line out of Main takes the line across the box's own.
    const axis = d[0] && d[1] ? (from[0] ? 1 : 0) : (d[0] ? 0 : 1);
    const toward = axis ? [0, Math.sign(d[1])] : [Math.sign(d[0]), 0];
    // Lift the box; the boxes past it on its line close up toward Main.
    const rest = ms.filter(n => n !== m).map(n =>
      Math.sign(n.x-main.x) === from[0] && Math.sign(n.y-main.y) === from[1] &&
      Math.abs(n.x-main.x)+Math.abs(n.y-main.y) > Math.abs(m.x-main.x)+Math.abs(m.y-main.y)
        ? {...n, x:n.x-from[0], y:n.y-from[1]} : n);
    dropped = endOf(rest, toward);
    monitors = [...rest, {...m, ...dropped}].sort((u, v) => u.number-v.number);
  }
  if (monitors.length > 1) {
    const lines = linesOf(monitors);
    if (!lines) return {layout, refused:'Not moved: every monitor must sit on a line out of the main monitor.'};
    monitors = numbered(monitors, lines).sort((u, v) => u.number-v.number);
  }
  if (gesture.monitor > 1 && monitors.every((m, n) => m.x === ms[n].x && m.y === ms[n].y))
    return {layout, refused:'Not moved: that monitor is already on that line.'};
  const outputs = layout.outputs.map(p => p === o ? {...o, monitors} : p);
  const result = fieldsFromLayout({outputs});
  if (result.refused) return {layout, refused:result.refused};
  const moved = layoutFromFields(result.fields);
  moved.outputs.forEach((o, i) => {o.os = layout.outputs[i].os;});
  return {layout:moved, fields:result.fields,
    moved:dropped && monitors.find(m => m.x === dropped.x && m.y === dropped.y).number};
}

function renderLayout(layout) {
  if (!layout) return '<p>Connect to see your monitor layout.</p>';
  const monitors = layout.outputs.flatMap(o => o.monitors);
  const width = Math.max(...monitors.map(m => m.x))+1;
  const height = Math.max(...monitors.map(m => m.y))+1;
  const spareRow = height < 2 ? 1 : 0;
  const canvasHeight = (height+2*spareRow)*100+20;
  // Each cell is 100 units: the label bar sits in the top 18, then a gap, then
  // the monitor box. The bar spans the block and is the handle that moves it;
  // each box is a focusable handle of its own.
  const groups = layout.outputs.map(o => {
    const left = Math.min(...o.monitors.map(m => m.x))*100+12, top = Math.min(...o.monitors.map(m => m.y))*100+12;
    const right = Math.max(...o.monitors.map(m => m.x))*100+88;
    return `<g class="layout-${o.letter}" data-output="${o.letter}" aria-label="Output ${o.letter}: ${o.os}"><g class="layout-handle"><rect x="${left}" y="${top}" width="${right-left}" height="18" rx="5"/><text x="${(left+right)/2}" y="${top+13}">${right-left > 76 ? 'Output ' : ''}${o.letter} · ${o.os}</text></g>${o.monitors.map(m =>
      `<g class="layout-box" data-output="${o.letter}" data-monitor="${m.number}" tabindex="0" aria-label="Output ${o.letter} ${m.number === 1 ? 'main monitor' : 'monitor '+m.number}"><rect class="layout-monitor${m.number === 1 ? ' layout-main' : ''}" x="${m.x*100+12}" y="${m.y*100+36}" width="76" height="52" rx="5"/><text x="${m.x*100+50}" y="${m.y*100+68}">${m.number === 1 ? 'Main' : m.number}</text></g>`).join('')}</g>`;
  }).join('');
  const bands = layout.bands.map(s => `<g data-segment="${s.number}"><line class="layout-band" x1="${s.start[0]*100}" y1="${s.start[1]*100}" x2="${s.end[0]*100}" y2="${s.end[1]*100}"/><text class="layout-band-number" x="${(s.start[0]+s.end[0])*50}" y="${(s.start[1]+s.end[1])*50+5}">${s.number}</text></g>`).join('');
  const counts = layout.outputs.map(o => `<span>Output ${o.letter}: <button type="button" aria-label="Remove a monitor from Output ${o.letter}" onclick="applyGesture({output:'${o.letter}',add:-1})">−</button> ${o.monitors.length} <button type="button" aria-label="Add a monitor to Output ${o.letter}" onclick="applyGesture({output:'${o.letter}',add:1})">+</button></span>`).join('');
  return `<svg role="group" aria-label="Monitor layout" viewBox="-10 ${-10-spareRow*100} ${width*100+20} ${canvasHeight}" style="max-height:${canvasHeight}px">${groups}${bands}</svg><p class="layout-counts">${counts}</p>${layout.note ? `<p>${layout.note} <a href="#advanced" onclick="document.getElementById('advanced').open = true">Open Advanced</a></p>` : ''}`;
}

function readFields() {
  return Object.fromEntries([...document.querySelectorAll('.api[data-key]')].map(e => [e.dataset.key, getValue(e)]));
}

function showLayoutStatus(message = '', ok = false) {
  const status = document.getElementById('layout-status');
  if (status.textContent !== message) status.textContent = message;
  status.classList.toggle('layout-ok', ok);
}

function redrawLayout() {
  document.getElementById('layout').innerHTML = renderLayout(device && device.opened ? layoutFromFields(readFields()) : null);
  showLayoutStatus();
}

// Every gesture ends here. A valid one writes the derived values into the
// Advanced fields, the same as picking them by hand: Save sends them to the
// board, Read throws them away. A refused one shows its reason and touches nothing.
function applyGesture(gesture) {
  const result = moveMonitor(layoutFromFields(readFields()), gesture);
  if (result.refused) {showLayoutStatus(result.refused); return gesture.monitor;}
  for (const [key, value] of Object.entries(result.fields))
    document.querySelector(`.api[data-key="${key}"]`).value = value;
  redrawLayout();
  return result.moved || gesture.monitor;
}

// Pointer glue: a label bar drags the computer in half-box steps, a box drags
// in whole boxes, so each lands where it is shown. The only box of a
// one-monitor computer is its label.
document.getElementById('layout').addEventListener('pointerdown', event => {
  const target = event.target.closest('.layout-handle, .layout-box');
  if (!target || event.button !== 0 || !event.isPrimary) return;
  event.preventDefault();
  showLayoutStatus();
  const computer = target.parentNode, scale = target.ownerSVGElement.getScreenCTM().a;
  const box = target.dataset.monitor && computer.querySelectorAll('.layout-box').length > 1 ? target : null;
  const moving = box || computer, step = box ? 1 : 0.5;
  const delta = e => [e.clientX-event.clientX, e.clientY-event.clientY].map(v => Math.round(v/scale/100/step)*step);
  const gesture = e => {const [dx, dy] = delta(e); return {output:computer.dataset.output, monitor:Number(target.dataset.monitor), dx, dy};};
  const samePointer = e => e.pointerId === event.pointerId;
  const stop = () => {
    window.removeEventListener('pointermove', follow); window.removeEventListener('pointerup', drop);
    window.removeEventListener('pointercancel', cancel); moving.removeAttribute('transform');
  };
  const follow = e => {
    if (!samePointer(e)) return;
    const current = gesture(e);
    moving.setAttribute('transform', `translate(${[current.dx,current.dy].map(v => v*100).join(' ')})`);
    if (!current.dx && !current.dy) return showLayoutStatus();
    const result = moveMonitor(layoutFromFields(readFields()), current);
    showLayoutStatus(result.refused || 'Release to apply.', !result.refused);
  };
  const drop = e => {
    if (!samePointer(e)) return;
    stop();
    const current = gesture(e);
    // A click on a box only focuses it.
    if (target.dataset.monitor && !current.dx && !current.dy) return target.focus();
    applyGesture(current);
  };
  const cancel = e => {if (samePointer(e)) {stop(); showLayoutStatus();}};
  window.addEventListener('pointermove', follow); window.addEventListener('pointerup', drop);
  window.addEventListener('pointercancel', cancel);
});

// Arrow keys on a focused box are a one-step drop; the box keeps focus.
document.getElementById('layout').addEventListener('keydown', event => {
  const box = event.target.closest('.layout-box');
  const step = {ArrowLeft:[-1,0], ArrowRight:[1,0], ArrowUp:[0,-1], ArrowDown:[0,1]}[event.key];
  if (!box || !step) return;
  event.preventDefault();
  const {output, monitor} = box.dataset;
  // The box may come back under a new number; focus follows it.
  const now = applyGesture({output, monitor:Number(monitor), dx:step[0], dy:step[1]});
  document.querySelector(`.layout-box[data-output="${output}"][data-monitor="${now}"]`).focus();
});
