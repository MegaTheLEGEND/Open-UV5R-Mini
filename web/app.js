// The page: pick the radio and files, the switches, build, check, flash. See docs/WEB-FLASHER.md.
import { STOCK_SHA, sha256, prepareImage, needsRescueWarning, RESCUE_WARNING, ufwcheck } from './lib/ufw.js';
import { buildFirmware } from './lib/build.js';
import { fetchStock, fetchPatch } from './lib/fetchstock.js';
import { loadIndex, loadVersion, defaultSha, buildName } from './lib/versions.js';
import { flashRadio, WebSerialLink, APP_BAUD } from './lib/flasher.js';

const $ = id => document.getElementById(id);
const BASE = new URL('.', import.meta.url).href;
const state = { variant: null, stock: null, patch: null, bin: null, img: null, checks: null, building: false,
  flashing: false, cancel: false, trace: [] };
let meta, index, version = null, feats = [];   // feats: this version's switches (features.mk)

function msg(el, text, kind = '') { el.textContent = text; el.className = 'msg ' + kind; }
function logTo(el) { return s => { el.textContent += s + '\n'; el.scrollTop = el.scrollHeight; }; }
function download(bytes, name, type = 'application/octet-stream') {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([bytes], { type }));
  a.download = name; a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 5000);
}

// --- 1. radio and files
// Browsers put back ticks from the last visit without telling the page. The radio kind and the risk ticks must be
// chosen on each visit (never NRF by accident, AGENTS rule 4), so clear them all.
for (const e of document.querySelectorAll('input[name=variant], #ack, #confirm')) e.checked = false;
document.querySelectorAll('input[name=variant]').forEach(r => r.addEventListener('change', () => {
  state.variant = r.value;
  if (r.value === 'nrfb') {
    msg($('variant-msg'), 'NRFB radios are not supported yet: their flash layout is different, and an NRF image ' +
      'would put settings and calibration in the wrong place. Nothing on this page will flash an NRFB radio.', 'bad');
    $('files').hidden = true;
  } else {
    msg($('variant-msg'), 'NRF (V0.27): supported. The stock file you load is checked to be exactly stock-nrf-v0.27.ufw.', 'good');
    $('files').hidden = false;
    if (!state.stock || !state.patch) fetchBoth();
  }
  refresh();
}));

// both outside files, fetched by this browser from where the README links them, then checked like loaded ones
async function fetchBoth() {
  $('b-fetch').disabled = true;
  const jobs = [];
  if (!state.stock) jobs.push((async () => {
    msg($('m-stock'), 'Downloading from MEGA...');
    try {
      await setStock(await fetchStock((g, t) => msg($('m-stock'), `Downloading from MEGA... ${Math.floor(100 * g / (t || g))} %`)));
    } catch (e) { msg($('m-stock'), 'Could not fetch it: ' + e.message + '. Load the file by hand below.', 'bad'); }
  })());
  if (!state.patch) jobs.push((async () => {
    msg($('m-patch'), 'Downloading...');
    try { setPatch(await fetchPatch(BASE)); } catch (e) { msg($('m-patch'), 'Could not fetch it: ' + e.message + '. Load the file by hand below.', 'bad'); }
  })());
  await Promise.all(jobs);
  $('b-fetch').disabled = false;
  await combine(); refresh();
}
$('b-fetch').addEventListener('click', () => { state.stock = null; state.patch = null; state.img = null; fetchBoth(); });

$('f-stock').addEventListener('change', async e => {
  state.stock = null; state.img = null;
  const f = e.target.files[0]; if (!f) return refresh();
  await setStock(new Uint8Array(await f.arrayBuffer()));
  await combine(); refresh();
});
async function setStock(b) {
  const s = await sha256(b);
  if (s === STOCK_SHA) { state.stock = b; msg($('m-stock'), 'ok: stock-nrf-v0.27.ufw (SHA-256 matches)', 'good'); }
  else if (b.length === 520192) msg($('m-stock'), 'This looks like the NRFB stock file (V0.04). This page supports NRF only.', 'bad');
  else msg($('m-stock'), 'Not stock-nrf-v0.27.ufw (SHA-256 ' + s.slice(0, 16) + '…). Use the unchanged stock file.', 'bad');
}

$('f-patch').addEventListener('change', async e => {
  state.patch = null; state.img = null;
  const f = e.target.files[0]; if (!f) return refresh();
  try { setPatch(JSON.parse(await f.text())); } catch (err) { msg($('m-patch'), err.message, 'bad'); }
  await combine(); refresh();
});
function setPatch(p) {
  try {
    if (p.name !== 'baofeng-uv5rmini-firmware' || p.format !== 1) throw new Error('not a baofeng-uv5rmini-firmware patch');
    if (p.base !== 'nrf-v0.27') throw new Error('this patch is for base ' + p.base + ', not nrf-v0.27');
    if (p.stock?.['.ufw']?.sha256 !== STOCK_SHA) throw new Error('this patch is not made from stock-nrf-v0.27.ufw');
    state.patch = p;
    msg($("m-patch"), `ok: ${p.build?.name || p.version || "patch"} (adds the loader to nrf-v0.27)`, "good");
  } catch (err) {
    msg($('m-patch'), err.message, 'bad');
  }
}

// --- 2. version and features
function short(c) { return c.sha.slice(0, 7); }
function trim(t, n) { return t.length > n ? t.slice(0, n - 1) + '…' : t; }
function renderVersions(selected) {
  const sel = $('ver'); sel.textContent = '';
  const group = (label, rows) => {
    if (!rows.length) return;
    const g = document.createElement('optgroup'); g.label = label;
    for (const [value, text] of rows) g.append(new Option(text, value));
    sel.append(g);
  };
  const bySha = Object.fromEntries(index.commits.map(c => [c.sha, c]));
  group('Releases', index.releases.map(r => [r.sha, `${r.tag} · ${r.date}`]));
  group('Branches', index.branches.map(b => index.commits.find(c => (c.tips || []).includes(b)))
    .filter(Boolean).map(c => [c.sha, `${c.tips.join(', ')} · ${short(c)} · ${c.date}`]));
  group('Recent commits', index.commits.map(c => [c.sha, `${short(c)} · ${c.date} · ${trim(c.subject, 60)}`]));
  if (selected && !bySha[selected]) group('From GitHub', [[selected, `${selected.slice(0, 7)} · ${version ? version.date + ' · ' + trim(version.subject, 60) : ''}`]]);
  sel.value = selected || '';
}
async function pickVersion(ref) {
  msg($('ver-msg'), 'Loading ' + ref.slice(0, 40) + '...');
  $('st-ver').textContent = 'loading'; $('st-ver').className = 'v';
  try {
    const v = await loadVersion(BASE, index, ref, s => msg($('ver-msg'), s));
    version = v; feats = v.features;
    try { localStorage.setItem('uv5r-version', v.sha); } catch { /* private window */ }
    renderVersions(v.sha);
    const rel = index.releases.find(r => r.sha === v.sha);
    const tips = (index.commits.find(c => c.sha === v.sha)?.tips || []);
    msg($('ver-msg'), [rel ? 'Release ' + rel.tag : tips.length ? 'Newest on ' + tips.join(', ') : 'Commit',
      v.sha.slice(0, 10), v.date, v.subject].filter(Boolean).join(' · ') +
      (v.from === 'github' ? ' (fetched from GitHub)' : '') +
      (feats.length ? '' : '. This version has no features.mk: it builds with its Makefile as it is.'), feats.length ? '' : 'warn');
  } catch (e) {
    msg($('ver-msg'), e.message, 'bad');
    if (version) renderVersions(version.sha);
  }
  state.bin = null; state.img = null; state.checks = null;
  renderFeatures(); showChecks(); refresh();
}
$('ver').addEventListener('change', e => pickVersion(e.target.value));
$('ver-form').addEventListener('submit', e => { e.preventDefault(); const r = $('ver-ref').value.trim(); if (r) pickVersion(r); });

const DEPS = {     // shown as hints; the Makefile's own overrides apply in the build (make.js)
  ENABLE_SCREEN_TEST: [['ENABLE_CHIRP', 1]],
  ENABLE_BT_LE: [['ENABLE_APP_FM', 1]],
  ENABLE_APP_BTCHIRP: [['ENABLE_CHIRP', 1], ['ENABLE_BT_LE', 1]],
  ENABLE_BT_PAIRED_ONLY: [['ENABLE_BT_LE', 1]],
  REGION: [['ENABLE_REGIONS', 1]],
};
// Off on this page unless ticked, and marked experimental (not yet working reliably on a radio), whatever
// features.mk says; the build then passes them as =0, as the shown make command does.
const EXPERIMENTAL = new Set(['ENABLE_BT_LE', 'ENABLE_BT_PAIRED_ONLY', 'ENABLE_APP_BTSCAN', 'ENABLE_APP_BTINFO',
  'ENABLE_APP_BTBEACON', 'ENABLE_APP_BTSERIAL', 'ENABLE_APP_BTCHIRP', 'ENABLE_APP_SUBGHZ', 'ENABLE_APP_APRS']);
const pageDefault = f => (EXPERIMENTAL.has(f.name) ? 0 : f.default);
const STORE = 'uv5r-features-2';   // new key: settings saved before the experimental defaults are not reused
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
  for (const f of feats) {
    const e = $('ft-' + f.name);
    out[f.name] = f.values ? +e.value : (e.checked ? 1 : 0);
  }
  return out;
}
// kept across versions: a switch keeps its setting in every version that has it
function saveFeatures() { try { localStorage.setItem(STORE, JSON.stringify({ ...savedFeatures(), ...features() })); } catch { /* private window */ } }
function savedFeatures() { try { return JSON.parse(localStorage.getItem(STORE) || '{}'); } catch { return {}; } }
function renderFeatures() {
  const saved = savedFeatures();
  const byName = Object.fromEntries(feats.map(f => [f.name, f]));
  const known = new Set(GROUPS.flatMap(g => g[1]));
  const groups = [...GROUPS, ['Other', feats.map(f => f.name).filter(n => !known.has(n))]];
  const box = $('features'); box.textContent = '';
  for (const [title, names] of groups) {
    const items = names.filter(n => byName[n]);
    if (!items.length) continue;
    const g = el('div', 'group'); g.append(el('h3', null, title));
    for (const n of items) {
      const f = byName[n];
      const v = n in saved ? saved[n] : pageDefault(f);
      const row = el('div', 'feat' + (f.values ? ' select' : '')); row.id = 'fd-' + n;
      const lab = el('label'); lab.htmlFor = 'ft-' + n;
      lab.append(el('span', 'name', NAMES[n] || n), el('span', 'sw', n));
      if (EXPERIMENTAL.has(n)) lab.append(el('span', 'exp', 'experimental'));
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
// the switches set away from this version's defaults (what `make X=...` gets)
function changed() {
  const f = features();
  return Object.fromEntries(feats.filter(x => f[x.name] !== x.default).map(x => [x.name, f[x.name]]));
}
function featureHints() {
  const f = features();
  for (const x of feats) {
    const blocked = (DEPS[x.name] || []).filter(([dep, want]) => dep in f && f[dep] !== want).map(([dep]) => NAMES[dep] || dep);
    const needs = document.querySelector('#fd-' + x.name + ' .needs');
    needs.textContent = blocked.length && f[x.name] ? 'Has no effect without ' + blocked.join(' and ') + '.' : '';
  }
  $('make-cmd').textContent = (version ? `git checkout ${version.sha.slice(0, 7)} && ` : '') +
    ['make ufw TOOLCHAIN=…', ...Object.entries(changed()).map(([k, v]) => `${k}=${v}`)].join(' ');
}
$('b-defaults').addEventListener('click', () => {
  const saved = savedFeatures();
  for (const f of feats) delete saved[f.name];
  try { localStorage.setItem(STORE, JSON.stringify(saved)); } catch { /* */ }
  renderFeatures(); state.bin = null; state.img = null; state.checks = null; showChecks(); refresh();
});

// --- 3. build and combine
$('b-build').addEventListener('click', async () => {
  const log = logTo($('build-log'));
  $('build-log').textContent = '';
  $('build-details').open = true;
  state.building = true; state.bin = null; state.img = null; showChecks(); refresh();
  try {
    if (!version) throw new Error('no version loaded');
    const v = version;
    log(`Version ${v.sha.slice(0, 10)} (${v.date}): ${v.subject}`);
    const r = await buildFirmware(BASE, v, changed(), buildName(index, v), log);
    if (v !== version) throw new Error('the version changed during the build; build again');
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
    if (/open/i.test(e.message)) log('Another program probably has the cable's port open (CHIRP, a serial monitor, another flasher, or this page in a second tab). Close it and press Flash again.');
  }
  if (link) await link.close();
  state.flashing = false; refresh();
});
$('b-cancel').addEventListener('click', () => { state.cancel = true; });
$('b-dl-trace').addEventListener('click', () => download(state.trace.join('\n') + '\n', 'flash-log.txt', 'text/plain'));

function refresh() {
  if (!meta) return;
  featureHints();
  $('b-build').disabled = state.building || !version;
  $('ver').disabled = state.building;
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
  const rel = version && index.releases.find(r => r.sha === version.sha);
  set('st-ver', version ? (rel ? rel.tag : version.sha.slice(0, 7)) : 'none', version ? 'ok' : 'bad');
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
  index = await loadIndex(BASE);
  let start = defaultSha(index);
  try { const s = localStorage.getItem('uv5r-version'); if (s) start = s; } catch { /* */ }
  const asked = new URLSearchParams(location.search).get('version');   // ?version=<hash, branch or tag>
  await pickVersion(asked || start);
  if (!version && (asked || start) !== defaultSha(index)) await pickVersion(defaultSha(index));
})();
