// The page: pick the radio and files, the switches, build, check, flash. See docs/WEB-FLASHER.md.
import { STOCK_SHA, sha256, prepareImage, needsRescueWarning, RESCUE_WARNING, ufwcheck } from './lib/ufw.js';
import { buildFirmware } from './lib/build.js';
import { flashRadio, WebSerialLink, APP_BAUD } from './lib/flasher.js';

const $ = id => document.getElementById(id);
const BASE = new URL('.', import.meta.url).href;
const state = { variant: null, stock: null, patch: null, bin: null, img: null, checks: null, building: false,
  flashing: false, cancel: false, trace: [] };
let meta;

function msg(el, text, kind = '') { el.textContent = text; el.className = 'msg ' + kind; }
function logTo(el) { return s => { el.textContent += s + '\n'; el.scrollTop = el.scrollHeight; }; }
function download(bytes, name, type = 'application/octet-stream') {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([bytes], { type }));
  a.download = name; a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 5000);
}

// --- 1. radio and files
document.querySelectorAll('input[name=variant]').forEach(r => r.addEventListener('change', () => {
  state.variant = r.value;
  if (r.value === 'nrfb') {
    msg($('variant-msg'), 'NRFB radios are not supported yet: their flash layout is different, and an NRF image ' +
      'would put settings and calibration in the wrong place. Nothing on this page will flash an NRFB radio.', 'bad');
    $('files').hidden = true;
  } else {
    msg($('variant-msg'), 'NRF (V0.27): supported. The stock file you load is checked to be exactly stock-nrf-v0.27.ufw.', 'good');
    $('files').hidden = false;
  }
  refresh();
}));

$('f-stock').addEventListener('change', async e => {
  state.stock = null; state.img = null;
  const f = e.target.files[0]; if (!f) return refresh();
  const b = new Uint8Array(await f.arrayBuffer());
  const s = await sha256(b);
  if (s === STOCK_SHA) { state.stock = b; msg($('m-stock'), 'ok: stock-nrf-v0.27.ufw (SHA-256 matches)', 'good'); }
  else if (b.length === 520192) msg($('m-stock'), 'This looks like the NRFB stock file (V0.04). This page supports NRF only.', 'bad');
  else msg($('m-stock'), 'Not stock-nrf-v0.27.ufw (SHA-256 ' + s.slice(0, 16) + '…). Use the unchanged stock file.', 'bad');
  await combine(); refresh();
});

$('f-patch').addEventListener('change', async e => {
  state.patch = null; state.img = null;
  const f = e.target.files[0]; if (!f) return refresh();
  try {
    const p = JSON.parse(await f.text());
    if (p.name !== 'baofeng-uv5rmini-firmware' || p.format !== 1) throw new Error('not a baofeng-uv5rmini-firmware patch');
    if (p.base !== 'nrf-v0.27') throw new Error('this patch is for base ' + p.base + ', not nrf-v0.27');
    if (p.stock?.['.ufw']?.sha256 !== STOCK_SHA) throw new Error('this patch is not made from stock-nrf-v0.27.ufw');
    state.patch = p;
    msg($("m-patch"), `ok: ${p.build?.name || p.version || "patch"} (adds the loader to nrf-v0.27)`, "good");
  } catch (err) {
    msg($('m-patch'), err.message, 'bad');
  }
  await combine(); refresh();
});

// --- 2. features
const DEPS = {     // shown as hints; the Makefile's own overrides are applied by plan.js
  ENABLE_SCREEN_TEST: [['ENABLE_CHIRP', 1]],
  ENABLE_BT_LE: [['ENABLE_APP_FM', 1]],
  ENABLE_APP_BTCHIRP: [['ENABLE_CHIRP', 1], ['ENABLE_BT_LE', 1]],
  ENABLE_BT_PAIRED_ONLY: [['ENABLE_BT_LE', 1]],
  REGION: [['ENABLE_REGIONS', 1]],
};
// Plain names and groups for the switches; anything new in features.mk lands in "Other" with its own name.
const NAMES = {
  ENABLE_TX: 'Transmit', ENABLE_REGIONS: 'Region transmit modes', REGION: 'Region', ENABLE_TX_UNLOCK: 'UNLOCKED transmit mode',
  ENABLE_CHIRP: 'CHIRP over the cable', ENABLE_SAVE: 'Save channels and settings', ENABLE_SCREEN_TEST: 'Cable set-up test at start-up',
  ENABLE_APP_SPECTRUM: 'Spectrum', ENABLE_APP_METER: 'Signal meter', ENABLE_APP_CHANNELS: 'Channel book',
  ENABLE_APP_SYSINFO: 'System info', ENABLE_APP_APRS: 'APRS', ENABLE_APP_SUBGHZ: 'Sub-GHz receive (315/433 MHz)',
  ENABLE_APP_FM: 'FM broadcast radio', ENABLE_APP_SNAKE: 'Snake',
  ENABLE_BT_LE: 'Bluetooth LE', ENABLE_BT_PAIRED_ONLY: 'Paired devices only', ENABLE_APP_BTSCAN: 'BLE scan',
  ENABLE_APP_BTINFO: 'BT info', ENABLE_APP_BTBEACON: 'BT beacon', ENABLE_APP_BTSERIAL: 'BT serial terminal',
  ENABLE_APP_BTCHIRP: 'CHIRP over Bluetooth',
  ENABLE_LCD_SPI: 'Fast screen and flash (SPI1 hardware)', ENABLE_KEYTEST: 'Key test', ENABLE_DEBUGGING: 'Debug output',
};
const GROUPS = [
  ['Transmit', ['ENABLE_TX', 'ENABLE_REGIONS', 'REGION', 'ENABLE_TX_UNLOCK']],
  ['Programming and memory', ['ENABLE_CHIRP', 'ENABLE_SAVE', 'ENABLE_SCREEN_TEST']],
  ['Apps', ['ENABLE_APP_SPECTRUM', 'ENABLE_APP_METER', 'ENABLE_APP_CHANNELS', 'ENABLE_APP_SYSINFO', 'ENABLE_APP_APRS',
    'ENABLE_APP_SUBGHZ', 'ENABLE_APP_FM', 'ENABLE_APP_SNAKE']],
  ['Bluetooth', ['ENABLE_BT_LE', 'ENABLE_BT_PAIRED_ONLY', 'ENABLE_APP_BTSCAN', 'ENABLE_APP_BTINFO', 'ENABLE_APP_BTBEACON',
    'ENABLE_APP_BTSERIAL', 'ENABLE_APP_BTCHIRP']],
  ['Hardware and debugging', ['ENABLE_LCD_SPI', 'ENABLE_KEYTEST', 'ENABLE_DEBUGGING']],
];
// one line of help: drop a leading "Name: " and the source file references, keep the first sentence
function firstSentence(t) {
  t = t.replace(/^[A-Za-z][\w -]{0,24}: /, '').replace(/\s*\((src|tools)\/[^)]*\)/g, '');
  const m = t.match(/^.*?\.(\s|$)/);
  const one = (m ? m[0] : t).trim();
  return one.charAt(0).toUpperCase() + one.slice(1);
}
function el(tag, cls, text) { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }
function features() {
  const out = {};
  for (const f of meta.features) {
    const e = $('ft-' + f.name);
    out[f.name] = f.values ? +e.value : (e.checked ? 1 : 0);
  }
  return out;
}
function saveFeatures() { try { localStorage.setItem('uv5r-features', JSON.stringify(features())); } catch { /* private window */ } }
function savedFeatures() { try { return JSON.parse(localStorage.getItem('uv5r-features') || '{}'); } catch { return {}; } }
function renderFeatures() {
  const saved = savedFeatures();
  const byName = Object.fromEntries(meta.features.map(f => [f.name, f]));
  const known = new Set(GROUPS.flatMap(g => g[1]));
  const groups = [...GROUPS, ['Other', meta.features.map(f => f.name).filter(n => !known.has(n))]];
  const box = $('features'); box.textContent = '';
  for (const [title, names] of groups) {
    const items = names.filter(n => byName[n]);
    if (!items.length) continue;
    const g = el('div', 'group'); g.append(el('h3', null, title));
    for (const n of items) {
      const f = byName[n];
      const v = n in saved ? saved[n] : f.default;
      const row = el('div', 'feat' + (f.values ? ' select' : '')); row.id = 'fd-' + n;
      const lab = el('label'); lab.htmlFor = 'ft-' + n;
      lab.append(el('span', 'name', NAMES[n] || n), el('span', 'sw', n));
      if (f.values) {
        const sel = el('select'); sel.id = 'ft-' + n;
        for (const [k, t] of Object.entries(f.values)) sel.add(new Option(t, k));
        sel.value = String(v);
        const ctl = el('div', 'ctl'); ctl.append(lab, document.createTextNode(' '), sel);
        row.append(ctl);
      } else {
        const c = el('input', 'toggle'); c.type = 'checkbox'; c.id = 'ft-' + n; c.checked = !!+v;
        const ctl = el('div', 'ctl'); ctl.append(c);
        row.append(ctl, lab);
      }
      const help = el('div', 'help', firstSentence(f.help)); help.title = f.help;
      row.append(help, el('div', 'needs'));
      g.append(row);
    }
    box.append(g);
  }
}
$('features').addEventListener('change', () => { saveFeatures(); state.bin = null; state.img = null; state.checks = null; showChecks(); refresh(); });
function featureHints() {
  const f = features();
  for (const x of meta.features) {
    const blocked = (DEPS[x.name] || []).filter(([dep, want]) => f[dep] !== want).map(([dep]) => NAMES[dep] || dep);
    const needs = document.querySelector('#fd-' + x.name + ' .needs');
    needs.textContent = blocked.length && f[x.name] ? 'Has no effect without ' + blocked.join(' and ') + '.' : '';
  }
  const diff = meta.features.filter(x => f[x.name] !== x.default).map(x => `${x.name}=${f[x.name]}`);
  $('make-cmd').textContent = ['make ufw TOOLCHAIN=…', ...diff].join(' ');
}
$('b-defaults').addEventListener('click', () => { try { localStorage.removeItem('uv5r-features'); } catch { /* */ } renderFeatures(); state.bin = null; state.img = null; refresh(); });

// --- 3. build and combine
$('b-build').addEventListener('click', async () => {
  const log = logTo($('build-log'));
  $('build-log').textContent = '';
  $('build-details').open = true;
  state.building = true; state.bin = null; state.img = null; showChecks(); refresh();
  try {
    const r = await buildFirmware(BASE, features(), log);
    state.bin = r.bin; state.build = r.build;
    await combine(log);
  } catch (e) {
    log('FAILED: ' + e.message);
  }
  state.building = false; refresh();
});

async function combine(log = logTo($('build-log'))) {
  state.img = null; state.checks = null;
  if (!state.bin || !state.stock || !state.patch) { showChecks(); return; }
  try {
    const { img, crcs, checks, slot } = await prepareImage(state.stock, state.patch, state.bin);
    state.img = img; state.checks = checks; state.crcs = crcs; state.slot = slot;
    log(`Combined with your stock file and loader: ${img.length} bytes, every CRC checked.`);
  } catch (e) {
    log('Not flashable: ' + e.message);
  }
  showChecks();
}

function showChecks() {
  const t = $('checks'); t.textContent = '';
  $('slot').hidden = !state.bin;
  if (state.bin) {
    const slot = state.slot, n = state.bin.length;
    $('slot-fill').style.width = slot ? (100 * n / slot).toFixed(1) + '%' : '0';
    $('slot-text').textContent = slot
      ? `firmware.bin ${n.toLocaleString('en')} B of the ${slot.toLocaleString('en')} B app slot (${(100 * n / slot).toFixed(0)} %)`
      : `firmware.bin ${n.toLocaleString('en')} B. Load your stock file to see how much of the app slot it fills.`;
  }
  if (!state.checks) { $('rescue').hidden = true; return; }
  const rows = [...state.crcs.res.map(([w, ok]) => [w, ok, '']), ...state.checks];
  for (const [what, ok, detail] of rows) {
    const li = el('li');
    li.append(el('span', 'r ' + (ok ? 'ok' : 'bad'), ok ? 'OK' : 'FAIL'));
    const d = el('span', null, what);
    if (detail) d.append(el('span', 'd', ' · ' + detail));
    li.append(d);
    t.append(li);
  }
  $('rescue').hidden = !needsRescueWarning(state.checks);
  $('rescue-text').textContent = RESCUE_WARNING;
}

$('b-dl-bin').addEventListener('click', () => download(state.bin, 'firmware.bin'));
$('b-dl-ufw').addEventListener('click', () => download(state.img, `open-uv5r-mini-${state.build}.ufw`));

// --- 4. flash
function flashAllowed() {
  if (!state.img || state.variant !== 'nrf' || state.flashing || !('serial' in navigator)) return false;
  if (!state.checks.every(c => c[1]) || !state.crcs.ok || !ufwcheck(state.img, state.bin).ok) return false;
  if (needsRescueWarning(state.checks) && !$('confirm').checked) return false;
  return $('ack').checked;
}
$('ack').addEventListener('change', refresh);
$('confirm').addEventListener('change', refresh);

$('b-flash').addEventListener('click', async () => {
  if (!flashAllowed()) return;
  const log = logTo($('flash-log'));
  $('flash-log').textContent = '';
  state.trace = []; state.cancel = false; state.flashing = true; refresh();
  let link;
  try {
    const port = await navigator.serial.requestPort();
    link = new WebSerialLink(port);
    await link.open(APP_BAUD);
    const [ok, text] = await flashRadio(link, state.img, {
      log, progress: p => { $('progress').value = p; }, cancelled: () => state.cancel,
      trace: l => state.trace.push(l),
    });
    log((ok ? 'OK: ' : 'NOT DONE: ') + text);
  } catch (e) {
    log('Stopped: ' + e.message);
  }
  if (link) await link.close();
  state.flashing = false; refresh();
});
$('b-cancel').addEventListener('click', () => { state.cancel = true; });
$('b-dl-trace').addEventListener('click', () => download(state.trace.join('\n') + '\n', 'flash-log.txt', 'text/plain'));

function refresh() {
  if (!meta) return;
  featureHints();
  $('b-build').disabled = state.building;
  $('b-dl-bin').disabled = !state.bin;
  $('b-dl-ufw').disabled = !state.img;
  $('b-flash').disabled = !flashAllowed();
  $('b-cancel').disabled = !state.flashing;
  $('b-dl-trace').disabled = !state.trace.length;
  const s = $('serial-msg');
  if (!('serial' in navigator)) msg(s, 'This browser has no Web Serial: flashing needs Chrome or Edge on a desktop computer. You can still build and download the .ufw.', 'bad');
  else if (state.variant === 'nrfb') msg(s, 'NRFB radios cannot be flashed from this page.', 'bad');
  else if (!state.img) msg(s, 'Flash unlocks once there is a build combined with your stock files (steps 1 and 3).');
  else if (!state.checks.every(c => c[1])) msg(s, 'A check failed: this image will not be flashed.', 'bad');
  else msg(s, '');
  status();
}

// the strip at the top: where each step stands
function status() {
  const set = (id, text, kind = '') => { $(id).textContent = text; $(id).className = 'v ' + kind; };
  set('st-radio', state.variant === 'nrf' ? 'NRF V0.27' : state.variant === 'nrfb' ? 'NRFB: not supported' : 'not chosen',
    state.variant === 'nrf' ? 'ok' : state.variant ? 'bad' : '');
  set('st-stock', state.stock ? 'V0.27, checked' : 'not loaded', state.stock ? 'ok' : '');
  set('st-patch', state.patch ? 'L2 1.1.0' : 'not loaded', state.patch ? 'ok' : '');
  set('st-fw', state.building ? 'building...' : state.bin ? state.bin.length.toLocaleString('en') + ' B' : 'not built',
    state.bin ? 'ok' : '');
  if (!state.checks) set('st-checks', '–');
  else {
    const all = [...state.crcs.res, ...state.checks], bad = all.filter(c => !c[1]).length;
    set('st-checks', bad ? bad + ' failed' : all.length + ' passed', bad ? 'bad' : 'ok');
  }
}

(async () => {
  meta = await fetch(BASE + 'meta.json').then(r => r.json());
  $('build-id').textContent = meta.build;
  renderFeatures();
  refresh();
})();
