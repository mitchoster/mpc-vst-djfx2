/* pagetest.mjs — run the companion page's script without a browser.
 *
 *   node vst/web/pagetest.mjs [path/to/index.html]
 *
 * The page is ~700 lines of canvas code that no compiler checks and no unit test runs; the last
 * bug of that class ("reel is not a function") only showed up when a browser rendered it. This
 * harness executes the real script against a recording canvas and a fake DOM, then checks the
 * things that break quietly:
 *
 *   1. draw() runs without throwing, in both layouts, over several frames
 *   2. every control registers a hit region (6 knobs per page, the switch rows, 12 mode cells)
 *   3. a pointerdown inside a knob sends POST /set?k=..., a pointerup sends the final value
 *   4. a lamp that is a switch toggles on tap
 *   5. tapping a mode cell sends that index, and the write carries the X-DJF2 header
 *   6. no control sits outside the screen or on top of another (all pages, both viewports)
 *
 * No DOM library: the stubs are the dozen things the page actually touches.
 */
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const file = process.argv[2] || new URL('./index.html', import.meta.url).pathname;
const html = readFileSync(file, 'utf8');
const m = html.match(/<script>([\s\S]*?)<\/script>/);
if (!m) { console.error('FAIL: no <script> in ' + file); process.exit(1); }
const code = m[1];

const STATE = {
  instance: 0, count: 1, active: 1, age_ms: 12,
  url: 'http://192.0.2.10:8102/',
  vu: 0.42, peak: 0.5, delay_ms: 250, bpm: 120, mix: 0.35,
  iso_lo: 0, iso_mid: -3, iso_hi: 2, iso_lo_x: 250, iso_hi_x: 2500,
  dj_filter: -0.3, dj_reso: 0.2,
  rep_mode: 3, rep_gate: 0.25, rep_chance: 1, rep_count: 4, rep_mix: 0.2, fill_mode: 0,
  dly_time: 400, dly_sync: 0, dly_div: 3, dly_fbk: 0.4, dly_tone: 0.5, dly_mix: 1, dly_pingpong: 0,
  verb_mode: 0, verb_amount: 0.3, verb_decay: 0.5, verb_damp: 0.5, verb_predelay: 20,
  hp_hz: 20, hp_reso: 0.1,
  fl_rate: 0.2, fl_depth: 0.4, fl_fbk: 0.3, fl_mix: 0.1,
  gr_rate: 20, gr_time: 80, gr_spray: 0.2, gr_mix: 0,
  mac_lo: 0.1, mac_mid: 0, mac_hi: 0, mac_lpf: 0.2, mac_glitch: 0, mac_verb: 0.3, mac_fade: 0, mac_fill: 0,
  output_gain: 0, limiter: 1, rep_env: 0.3, dly_env: 0.2, verb_env: 0.1
};

let calls = [];
const gradient = { addColorStop() {} };
const ctx = new Proxy({}, {
  get(_, k) {
    if (k === 'measureText') return (t) => ({ width: String(t).length * 7 });
    if (k === 'createLinearGradient' || k === 'createRadialGradient') return () => gradient;
    if (k === 'getImageData') return () => ({ data: new Uint8ClampedArray(4) });
    if (typeof k === 'string' && /^(canvas|fillStyle|strokeStyle|lineWidth|lineCap|lineJoin|font|textAlign|textBaseline|globalAlpha|shadowBlur|shadowColor)$/.test(k)) return ctx[k];
    return (...args) => { if (k === 'fill' || k === 'stroke' || k === 'fillRect') calls.push(k); };
  },
  set(_, k, v) { calls.push('set:' + k); return true; }
});

function el(extra = {}) {
  const listeners = {};
  return Object.assign({
    style: {}, classList: { add() {}, remove() {} }, className: '',
    addEventListener(t, f) { (listeners[t] = listeners[t] || []).push(f); },
    removeEventListener() {},
    appendChild() {}, remove() {}, click() {},
    getBoundingClientRect() { return { left: 0, top: 0 }; },
    setPointerCapture() {}, releasePointerCapture() {},
    _fire(t, ev) { (listeners[t] || []).forEach(f => f(ev)); }
  }, extra);
}

const canvas = el({ clientWidth: 1100, clientHeight: 660, width: 1100, height: 660, getContext: () => ctx, toDataURL: () => 'data:,' });
const bar = el();
const link = el({ firstElementChild: el() });
const host = el();
const fsBtn = el();

const document = {
  getElementById: (id) => ({ c: canvas, bar, link, host, fs: fsBtn })[id] || el(),
  createElement: () => el(),
  body: el(),
  documentElement: el({ requestFullscreen: () => Promise.resolve() }),
  addEventListener() {}
};

const fetched = [];
const sandbox = {
  console,
  document,
  window: null,
  location: { host: '192.0.2.10:8102', href: 'http://192.0.2.10:8102/' },
  navigator: {},
  performance: { now: () => Date.now() },
  devicePixelRatio: 2,
  addEventListener() {},
  setInterval: () => 1, clearInterval() {}, setTimeout: () => 1, clearTimeout() {},
  requestAnimationFrame: () => 1,
  fetch: (url, opts) => { fetched.push({ url, opts }); return Promise.resolve({ ok: true, json: () => Promise.resolve(STATE), text: () => Promise.resolve('') }); },
  Math, Date, JSON, Uint8Array, Float32Array, Object, Array, String, Number, Boolean, isNaN, parseInt, parseFloat, encodeURIComponent
};
sandbox.window = sandbox;
sandbox.globalThis = sandbox;

vm.createContext(sandbox);
vm.runInContext(code, sandbox, { filename: 'index.html' });

const P = sandbox.__djfxPanel;
let fails = 0;
const check = (ok, name, detail = '') => {
  console.log((ok ? 'PASS' : 'FAIL') + ': ' + name + (detail ? ' = ' + detail : ''));
  if (!ok) fails++;
};

check(!!P, 'the page exposes its test hook', P ? 'ok' : 'missing');
if (!P) process.exit(1);

/* 1 — the drawing runs, in both layouts */
function render(w, h, frames) {
  canvas.clientWidth = w; canvas.clientHeight = h;
  P.fit();
  /* timestamps come from the same clock the page uses; synthetic ones from the past made
   * the ripple maths run backwards (a real bug, found here) */
  const t0 = performance.now();
  for (let i = 0; i < frames; i++) P.draw(t0 + i * 130);
}
try {
  render(1100, 660, 12);
  check(true, 'wide layout draws', 'no exception');
  render(430, 850, 8);
  check(true, 'portrait layout draws', 'no exception');
} catch (e) {
  check(false, 'drawing runs', String(e && e.stack ? e.stack.split('\n')[0] : e));
  process.exit(1);
}

/* 2 — every control registered a region. The panel opens on MACRO (the device skin does too),
 * so the filter's controls live on page 1. */
render(1100, 660, 2);
const MACRO = 0, FILTER = 1, REPEAT = 2, DELAY = 3;
P.goPage(FILTER); render(1100, 660, 2);
const hits = P.hits();
const keys = hits.filter(h => h.t === 'knob').map(h => h.key);
for (const k of ['iso_lo', 'iso_mid', 'iso_hi', 'iso_lo_x', 'iso_hi_x', 'dj_reso']) {
  check(keys.includes(k), 'knob region: ' + k, keys.join(','));
}
const faders = hits.filter(h => h.t === 'fader').map(h => h.key);
check(faders.includes('dj_filter'), 'the DJ filter fader registers', faders.join(','));

/* pages: three faces, reachable by swipe and by the dots */
for (let i = 0; i < 4; i++) {
  P.goPage(i);
  render(1100, 660, 3);
  check(P.page().target === i, 'page ' + i + ' is reachable', String(P.page().at));
}

/* page 1 and 2 expose their own controls */
P.goPage(REPEAT); render(1100, 660, 2);
const repKeys = P.hits().filter(h => h.t === 'knob' || h.t === 'seg' || h.t === 'mode').map(h => h.key);
for (const k of ['rep_gate', 'rep_chance', 'rep_count', 'rep_mix', 'verb_amount', 'fl_mix', 'fill_mode']) {
  check(repKeys.includes(k), 'REPEAT page control: ' + k, repKeys.join(','));
}
check(P.hits().filter(h => h.t === 'mode').length === 8, 'eight repeat cells',
      String(P.hits().filter(h => h.t === 'mode').length));
P.goPage(DELAY); render(1100, 660, 2);
const dlyKeys = P.hits().filter(h => h.t === 'knob' || h.t === 'seg' || h.t === 'div').map(h => h.key);
for (const k of ['dly_time', 'dly_fbk', 'dly_tone', 'dly_mix', 'hp_hz', 'dly_sync']) {
  check(dlyKeys.includes(k), 'DELAY page control: ' + k, dlyKeys.join(','));
}
check(P.hits().some(h => h.t === 'div'), 'the division stepper registers', 'ok');
P.goPage(MACRO); render(1100, 660, 2);
const macKeys = P.hits().filter(h => h.t === 'knob').map(h => h.key);
for (const k of ['mac_lo', 'mac_mid', 'mac_hi', 'mac_lpf', 'mac_glitch', 'mac_verb', 'mac_fade', 'mac_fill',
                 'output_gain', 'mix', 'limiter']) {
  check(macKeys.includes(k), 'MACRO page control: ' + k, macKeys.join(','));
}

/* geometry: every control inside the screen, no two touching. The pages are hand-placed;
 * a 10 px mistake is invisible in a JSON diff and obvious to a thumb. */
function box(h) {
  if (h.t === 'knob' || h.t === 'toggle') return { x: h.x - h.r, y: h.y - h.r, w: h.r * 2, h: h.r * 2, id: h.t + ':' + h.key };
  return { x: h.x, y: h.y, w: h.w, h: h.h, id: h.t + ':' + (h.key !== undefined ? h.key : h.i) };
}
for (let i = 0; i < 4; i++) {
  for (const [w, h] of [[1100, 660], [430, 850]]) {
    P.goPage(i); render(w, h, 2);
    const boxes = P.hits().filter(x => x.t !== 'page').map(box);
    const outside = boxes.filter(b => b.x < 0 || b.y < 0 || b.x + b.w > w || b.y + b.h > h);
    check(outside.length === 0, `page ${i} ${w}x${h}: controls inside the screen`,
          outside.map(b => b.id).slice(0, 3).join(' ') || 'ok');
    let overlaps = [];
    for (let a = 0; a < boxes.length; a++)
      for (let b = a + 1; b < boxes.length; b++) {
        const A = boxes[a], B = boxes[b];
        if (A.x < B.x + B.w && B.x < A.x + A.w && A.y < B.y + B.h && B.y < A.y + A.h) overlaps.push(A.id + ' x ' + B.id);
      }
    check(overlaps.length === 0, `page ${i} ${w}x${h}: no overlapping controls`, overlaps.slice(0, 3).join(' | ') || 'ok');
  }
}

/* the pairing address: /state's `url` when present, this page's own host otherwise */
P.setState({ url: 'http://192.0.2.5:8102/' });
P.refreshPair();
check(P.pair() === 'http://192.0.2.5:8102/', 'the bar shows the address /state publishes', P.pair());
P.setState({ url: '' });
P.refreshPair();
check(P.pair() === '192.0.2.10:8102', 'without it the bar falls back to this page\'s host', P.pair());

/* a segment tap sends its index */
P.goPage(REPEAT); render(1100, 660, 2);
const fillSeg = P.hits().find(h => h.t === 'seg' && h.key === 'fill_mode' && h.i === 1);
fetched.length = 0;
canvas._fire('pointerdown', { clientX: fillSeg.x + 3, clientY: fillSeg.y + 3, pointerId: 9, preventDefault() {} });
check(fetched.some(f => f.url.includes('k=fill_mode') && f.url.includes('v=1')), 'a segment tap sends its index',
      fetched.map(f => f.url).join(' '));

/* a swipe on the bare panel moves to the next page */
P.goPage(0); render(1100, 660, 2);
/* the finger moves left (700 -> 40): the next page comes in, as on a phone */
canvas._fire('pointerdown', { clientX: 700, clientY: 300, pointerId: 10, preventDefault() {} });
canvas._fire('pointermove', { clientX: 40, clientY: 300, pointerId: 10 });
canvas._fire('pointerup', { clientX: 40, clientY: 300, pointerId: 10 });
check(P.page().target === 1, 'a leftward swipe turns to the next page', JSON.stringify(P.page()));
/* and back again */
render(1100, 660, 20);
canvas._fire('pointerdown', { clientX: 60, clientY: 300, pointerId: 11, preventDefault() {} });
canvas._fire('pointermove', { clientX: 720, clientY: 300, pointerId: 11 });
canvas._fire('pointerup', { clientX: 720, clientY: 300, pointerId: 11 });
check(P.page().target === 0, 'a rightward swipe goes back', JSON.stringify(P.page()));

/* 3 — a knob drag writes, and the mapped value is the plugin's unit (ms here) */
P.goPage(FILTER); render(1100, 660, 2);
const lowKnob = P.hits().find(h => h.t === 'knob' && h.key === 'iso_lo');
fetched.length = 0;
canvas._fire('pointerdown', { clientX: lowKnob.x, clientY: lowKnob.y, pointerId: 1, preventDefault() {} });
canvas._fire('pointermove', { clientX: lowKnob.x, clientY: lowKnob.y - 90, pointerId: 1, preventDefault() {} });
canvas._fire('pointerup', { clientX: lowKnob.x, clientY: lowKnob.y - 90, pointerId: 1 });
const lowUrls = fetched.map(f => f.url).filter(u => u.includes('k=iso_lo'));
check(lowUrls.length > 0, 'turning the LOW knob sends /set?k=iso_lo', lowUrls[lowUrls.length - 1] || 'none');
const lowVal = lowUrls.length ? parseFloat(lowUrls[lowUrls.length - 1].split('v=')[1]) : -999;
check(lowVal > 0 && lowVal <= 6, 'the value went up, in dB (-24..+6)', String(lowVal));
const timeCall = fetched.find(f => f.url === lowUrls[0]);
check(timeCall.opts && timeCall.opts.method === 'POST', 'writes are POST', 'ok');
check(timeCall.opts.headers && timeCall.opts.headers['X-DJF2'] === '1',
      'writes carry the X-DJF2 header (no cross-site form can set one)', JSON.stringify(timeCall.opts.headers || {}));

/* 4 — a lamp that is also a switch toggles on tap */
P.goPage(MACRO); render(1100, 660, 2);
const limKnob = P.hits().find(h => h.t === 'knob' && h.key === 'limiter');
fetched.length = 0;
canvas._fire('pointerdown', { clientX: limKnob.x, clientY: limKnob.y, pointerId: 2, preventDefault() {} });
canvas._fire('pointermove', { clientX: limKnob.x, clientY: limKnob.y - 90, pointerId: 2, preventDefault() {} });
canvas._fire('pointerup', { clientX: limKnob.x, clientY: limKnob.y - 90, pointerId: 2 });
const limVals = fetched.map(f => f.url).filter(u => u.includes('k=limiter')).map(u => u.split('v=')[1]);
check(limVals.length > 0 && (limVals[limVals.length - 1] === '1' || limVals[limVals.length - 1] === '0'),
      'the LIMITER knob sends a 0/1', limVals.join(' -> '));

/* 5 — the mode grid */
P.goPage(REPEAT); render(1100, 660, 2);
const cell = P.hits().find(h => h.t === 'mode' && h.i === 5);
fetched.length = 0;
canvas._fire('pointerdown', { clientX: cell.x + 2, clientY: cell.y + 2, pointerId: 3, preventDefault() {} });
check(fetched.some(f => f.url.includes('k=rep_mode') && f.url.includes('v=5')), 'a repeat cell sends its index',
      fetched.map(f => f.url).join(' '));

console.log(fails ? 'FAILED' : 'PASSED');
process.exit(fails ? 1 : 0);
