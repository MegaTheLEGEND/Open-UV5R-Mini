// The .ufw tools for the web flasher, ported from our Python ones (same rules, same checks):
//   tools/jl_ufw_unpack.py  jl_crypt
//   tools/ufwpatch.py       applyPatch (MM9PDY's to-stock patch: stock .ufw -> flashable base with the loader)
//   tools/mkufw.py          mkufw (our app into the base's app slot; refuses an app without an update mode)
//   tools/ufwcheck.py       ufwcheck (every CRC)
//   tools/uv5r_flasher.py   updateModeStatus, checkImage (what must pass before anything is sent to a radio)
// Runs in the browser and in Node (tests). Nothing here talks to a radio.

export const STOCK_SHA = '811b4b37253e0685c7efbd3ff0a24aac2bcad223e5453d95b5bfc3f42c089128'; // stock-nrf-v0.27.ufw
export const FLASH_SOC = 0x1000;
export const AREA_START = 0x4000, AREA_END = 0x7C000;
export const APP_AREA = 0x3000, CHIP_KEY = 0xB50F, APP_BIN_OFFSET = 0x100, APP_BASE = 0x01E00100;
const WINDOWS = [[0x4100, 0x7BBC8], [0x4000, 0x4004], [0x4020, 0x4024]];
const PINNED_L2 = {
  '957bb486bb1d8cb5c971e366cd5c45a6da3c64fe611203096b56bb799ab02372': 'L2 (1.1.0)',
  '1e6c3e0d563a50f38caa2972c773ccdff99aa875c1e9098cc923be1cb5b6193e': 'L2-v2',
};
const STOCK_UPDATE_CODE = [
  [0x01E492C0, 0x01E4948C], [0x01E4917A, 0x01E491C2], [0x01E42AF2, 0x01E42D30], [0x01E42EE4, 0x01E42F6C],
  [0x01E43206, 0x01E432E6], [0x01E431A2, 0x01E431EE], [0x01E433C8, 0x01E43426], [0x01E43590, 0x01E43676],
  [0x01E59374, 0x01E59546], [0x01E68F1A, 0x01E693FC], [0x01E6AAAC, 0x01E6AAAC + 53492],
];
const OUR_MARKER = enc('OPENUV5R-UPDATE-MODE/1 writes=');
const ALLOWED_WRITES = [[0x1000, 0x7C000], [0xFA000, 0xFE000]];
export const RESCUE_WARNING =
  "WARNING: this image's cable update mode has not been proven on a radio. If it does not work, this radio can NOT " +
  'be flashed again over the cable. The only way back is to open the case and use the ROM USB rescue mode: wires ' +
  'soldered to the G, M and P pads, a USB key sender (an ESP32 with tools/jl_usbkey) and a full backup of this ' +
  "radio's flash made beforehand (docs/TESTING.md, roadmap 1.8). Without all of that, do not continue.";
const MAX_EDITS = 4096, MAX_RESULT = 4 * 1024 * 1024;   // as tools/ufwpatch.py

export class UfwError extends Error {}

function enc(s) { return new TextEncoder().encode(s); }
function latin1(b) { let s = ''; for (const x of b) s += String.fromCharCode(x); return s; }
export function hex(b) { return Array.from(b, x => x.toString(16).padStart(2, '0')).join(''); }
export function unhex(s) {
  if (s.length % 2) throw new UfwError('odd hex length');
  const out = new Uint8Array(s.length / 2);
  for (let i = 0; i < out.length; i++) {
    const v = parseInt(s.substr(i * 2, 2), 16);
    if (Number.isNaN(v)) throw new UfwError('bad hex');
    out[i] = v;
  }
  return out;
}
export async function sha256(b) {
  return hex(new Uint8Array(await globalThis.crypto.subtle.digest('SHA-256', b)));
}
const u16 = (b, o) => b[o] | (b[o + 1] << 8);
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
const p16 = (b, o, v) => { b[o] = v & 0xFF; b[o + 1] = (v >> 8) & 0xFF; };
const p32 = (b, o, v) => { b[o] = v & 0xFF; b[o + 1] = (v >>> 8) & 0xFF; b[o + 2] = (v >>> 16) & 0xFF; b[o + 3] = (v >>> 24) & 0xFF; };
function find(hay, needle, from = 0) {
  outer: for (let i = from; i <= hay.length - needle.length; i++) {
    for (let j = 0; j < needle.length; j++) if (hay[i + j] !== needle[j]) continue outer;
    return i;
  }
  return -1;
}
function eq(a, b) { if (a.length !== b.length) return false; for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false; return true; }
function cstr(b) { const i = b.indexOf(0); return latin1(i < 0 ? b : b.subarray(0, i)); }

// CRC16-CCITT, poly 0x1021, init 0 (XMODEM), table-driven
const CRC_T = new Uint16Array(256);
for (let i = 0; i < 256; i++) {
  let c = i << 8;
  for (let k = 0; k < 8; k++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xFFFF : (c << 1) & 0xFFFF;
  CRC_T[i] = c;
}
export function crc16(data, crc = 0) {
  for (let i = 0; i < data.length; i++) crc = ((crc << 8) & 0xFFFF) ^ CRC_T[((crc >> 8) ^ data[i]) & 0xFF];
  return crc;
}

// JieLi ENC: XOR with a CRC16-CCITT LFSR keystream
export function jlCrypt(buf, key = 0xFFFF) {
  const out = new Uint8Array(buf.length);
  for (let i = 0; i < buf.length; i++) {
    out[i] = buf[i] ^ (key & 0xFF);
    key = ((key << 1) ^ (key & 0x8000 ? 0x1021 : 0)) & 0xFFFF;
  }
  return out;
}

export function appAreaCrypt(data, start) {
  if (start % 32) throw new Error('start must be a multiple of 32');
  const out = new Uint8Array(data.length);
  for (let s = 0; s < data.length; s += 32) {
    out.set(jlCrypt(data.subarray(s, s + 32), CHIP_KEY ^ (((start + s) >> 2) & 0xFFFF)), s);
  }
  return out;
}

// .ufw container: header, records; {name: {off, len, crc, recOff, rec}}
export function ufwFiles(u) {
  const hdr = jlCrypt(u.subarray(0, 0x40));
  const n = u16(hdr, 8);
  const files = {};
  for (let i = 0; i < n; i++) {
    const recOff = 0x40 + 0x50 * i;
    const r = jlCrypt(u.subarray(recOff, recOff + 0x50));
    files[cstr(r.subarray(0x40))] = { off: u32(r, 8), len: u32(r, 12), crc: u16(r, 4), recOff, rec: r };
  }
  return { hdr, n, files };
}

// --- tools/ufwpatch.py apply
export async function applyPatch(p, stock) {
  if (p.name !== 'baofeng-uv5rmini-firmware' || p.format !== 1) throw new UfwError('not a baofeng-uv5rmini-firmware format 1 patch');
  const st = p.stock['.ufw'], rs = p.result['.ufw'];
  if (stock.length !== st.size || await sha256(stock) !== st.sha256) throw new UfwError('the stock file does not match the patch');
  const trunc = p.truncate;
  if (trunc > stock.length || p.edits.length > MAX_EDITS) throw new UfwError('patch malformed');
  const out = new Uint8Array(stock);
  let last = 0;
  for (const e of p.edits) {
    const d = unhex(e.hex);
    if (!d.length || e.offset < last || e.offset + d.length > trunc) throw new UfwError('patch malformed: edit at 0x' + e.offset.toString(16));
    out.set(d, e.offset);
    last = e.offset + d.length;
  }
  const app = unhex(p.append);
  const res = new Uint8Array(trunc + app.length);
  res.set(out.subarray(0, trunc));
  res.set(app, trunc);
  if (res.length > MAX_RESULT || res.length !== rs.size || await sha256(res) !== rs.sha256) throw new UfwError('result does not match the patch');
  return res;
}

// --- tools/uv5r_flasher.py update_mode_status: ['stock' | 'ours' | 'none', detail]
export function updateModeStatus(app, stockApp) {
  if (!app || !app.length) return ['none', 'no app'];
  if (stockApp && STOCK_UPDATE_CODE.every(([lo, hi]) => hi - APP_BASE <= app.length &&
      eq(app.subarray(lo - APP_BASE, hi - APP_BASE), stockApp.subarray(lo - APP_BASE, hi - APP_BASE)))) {
    return ['stock', 'the stock update mode, unchanged'];
  }
  const i = find(app, OUR_MARKER);
  if (i >= 0) {
    const end = app.indexOf(0x3B, i);   // ';'
    const spec = latin1(app.subarray(i + OUR_MARKER.length, end > i ? end : i + 80));
    const wins = [];
    for (const w of spec.split(',').filter(Boolean)) {
      const parts = w.split('-');
      if (!parts.every(x => /^[0-9a-fA-F]+$/.test(x))) return ['none', 'our marker is unreadable'];
      wins.push(parts.map(x => parseInt(x, 16)));
    }
    const bad = wins.filter(w => w.length !== 2 || !ALLOWED_WRITES.some(([lo, hi]) => lo <= w[0] && w[0] < w[1] && w[1] <= hi));
    if (!wins.length || bad.length) return ['none', 'its update mode may write outside the app and loader slot: ' + spec];
    return ['ours', 'Open-UV5R update mode (writes ' + spec + '); not proven on a radio'];
  }
  return ['none', 'MISSING: a radio running this could not be flashed again over the cable'];
}

function hasLoader(ufw, files) {
  const r = files['ota.bin'];
  if (!r) return false;
  for (let i = r.off; i < r.off + r.len; i++) if (ufw[i] !== 0xFF) return true;
  return false;
}

// --- tools/mkufw.py build. The guard stays: no app without an update mode on a flashable base.
export function mkufw(base, app) {
  const ufw = new Uint8Array(base);
  const hdr = jlCrypt(ufw.subarray(0, 0x40));
  if (crc16(hdr.subarray(2, 0x40)) !== u16(hdr, 0)) throw new UfwError('base .ufw: header CRC mismatch (not a stock UV-5R Mini .ufw?)');
  if (latin1(hdr.subarray(0x10, 0x16)) !== 'AC695N') throw new UfwError('base .ufw: chip is not AC695N');
  const { n: nrec, files } = ufwFiles(ufw);
  const fr = files['flash.bin'];
  if (!fr) throw new UfwError('base .ufw has no flash.bin');
  const hasStockMode = find(app, enc('UPDATE\0')) >= 0;
  const ours = updateModeStatus(app, null)[0] === 'ours';
  if (hasLoader(ufw, files) && !hasStockMode && !ours) {
    throw new UfwError('refusing: this base is flashable (it carries a loader) but the app has no update mode, ' +
      'so a radio running it could not be flashed again.');
  }
  const flash = ufw.slice(fr.off, fr.off + fr.len);
  const area = appAreaCrypt(flash.subarray(APP_AREA), 0);
  const head = area.slice(0, 32), ent = area.slice(32, 64);
  if (latin1(head.subarray(16, 29)) !== 'app_area_head' || latin1(ent.subarray(16, 23)) !== 'app.bin') {
    throw new UfwError('base .ufw: unexpected app area layout');
  }
  const areaLen = u32(head, 8);
  const maxApp = areaLen - APP_BIN_OFFSET;
  if (app.length > maxApp) throw new UfwError(`app is ${app.length} bytes, the slot holds ${maxApp}`);
  const padded = new Uint8Array(maxApp).fill(0xFF);
  padded.set(app);
  area.set(padded, APP_BIN_OFFSET);
  p32(ent, 8, padded.length);
  p16(ent, 2, crc16(padded));
  p16(ent, 0, crc16(ent.subarray(2, 32)));
  area.set(ent, 32);
  p32(head, 8, areaLen);
  area.set(head, 0);
  p16(head, 2, crc16(area.subarray(0x20, areaLen)));
  p16(head, 0, crc16(head.subarray(2, 32)));
  area.set(head, 0);
  flash.set(appAreaCrypt(area, 0), APP_AREA);
  ufw.set(flash, fr.off);
  p16(fr.rec, 4, crc16(flash));
  ufw.set(jlCrypt(fr.rec), fr.recOff);
  const tableEnd = 0x40 + 0x50 * nrec;
  p16(hdr, 2, crc16(ufw.subarray(0x40, tableEnd)));
  p16(hdr, 0, crc16(hdr.subarray(2, 0x40)));
  ufw.set(jlCrypt(hdr), 0);
  return ufw;
}

// --- tools/ufwcheck.py: [[what, ok, got, stored]], and whether the slot equals `app`
export function ufwcheck(u, app = null) {
  const res = [];
  const check = (what, got, want) => res.push([what, got === want, got, want]);
  const { hdr, n, files } = ufwFiles(u);
  check('ufw header', crc16(hdr.subarray(2, 0x40)), u16(hdr, 0));
  check('ufw record table', crc16(u.subarray(0x40, 0x40 + 0x50 * n)), u16(hdr, 2));
  let flash = null;
  for (const [name, f] of Object.entries(files)) {
    check('ufw file ' + name, crc16(u.subarray(f.off, f.off + f.len)), f.crc);
    if (name === 'flash.bin') flash = u.subarray(f.off, f.off + f.len);
  }
  const fh = jlCrypt(flash.subarray(0, 32));
  check('flash.bin header', crc16(fh.subarray(2, 32)), u16(fh, 0));
  const area = appAreaCrypt(flash.subarray(APP_AREA), 0);
  const head = area.subarray(0, 32), ent = area.subarray(32, 64);
  const areaLen = u32(head, 8), appLen = u32(ent, 8);
  check('app_area_head entry', crc16(head.subarray(2, 32)), u16(head, 0));
  check('app_area_head data', crc16(area.subarray(0x20, areaLen)), u16(head, 2));
  check('app.bin entry', crc16(ent.subarray(2, 32)), u16(ent, 0));
  const slot = area.subarray(APP_BIN_OFFSET, APP_BIN_OFFSET + appLen);
  check('app.bin data', crc16(slot), u16(ent, 2));
  let ok = res.every(r => r[1]) && areaLen === APP_BIN_OFFSET + appLen;
  if (app) {
    const same = eq(slot.subarray(0, app.length), app) && slot.subarray(app.length).every(x => x === 0xFF);
    res.push(['app.bin slot equals the build', same]);
    ok &&= same;
  }
  return { ok, res };
}

// --- tools/uv5r_flasher.py area_report / check_image
export function areaReport(flash) {
  const bad = [];
  try {
    const ah = appAreaCrypt(flash.subarray(APP_AREA, APP_AREA + 64), 0);
    const head = ah.subarray(0, 32), ent = ah.subarray(32, 64);
    if (latin1(head.subarray(16, 29)) !== 'app_area_head' || latin1(ent.subarray(16, 23)) !== 'app.bin') {
      return [false, null, ['no app area directory (erased or damaged)']];
    }
    const areaLen = u32(head, 8), appLen = u32(ent, 8);
    if (!(APP_BIN_OFFSET <= areaLen && areaLen <= flash.length - APP_AREA && APP_BIN_OFFSET + appLen <= areaLen)) {
      return [false, null, ['app area lengths out of range']];
    }
    const nn = Math.ceil(areaLen / 32) * 32;
    const area = appAreaCrypt(flash.subarray(APP_AREA, APP_AREA + nn), 0);
    const app = area.slice(APP_BIN_OFFSET, APP_BIN_OFFSET + appLen);
    if (crc16(head.subarray(2, 32)) !== u16(head, 0)) bad.push('app_area_head entry CRC');
    if (crc16(area.subarray(0x20, areaLen)) !== u16(head, 2)) bad.push('app area data CRC');
    if (crc16(ent.subarray(2, 32)) !== u16(ent, 0)) bad.push('app.bin entry CRC');
    if (crc16(app) !== u16(ent, 2)) bad.push('app.bin data CRC');
    return [!bad.length, app, bad];
  } catch (e) {
    return [false, null, ['unreadable app area: ' + e.message]];
  }
}

export async function checkImage(img, stock) {
  const res = [];
  let f, fs, hdr;
  try {
    ({ hdr, files: f } = ufwFiles(img));
    ({ files: fs } = ufwFiles(stock));
  } catch (e) {
    return [['Container', false, 'not a .ufw: ' + e.message]];
  }
  const names = Object.keys(f).sort();
  res.push(['Container', names.join() === 'flash.bin,ota.bin' && Object.values(f).every(x => x.off + x.len <= img.length), names.join(', ')]);
  if (!res[0][1]) return res;
  let crcOk = crc16(hdr.subarray(2, 0x40)) === u16(hdr, 0) &&
    crc16(img.subarray(0x40, 0x40 + 0x50 * names.length)) === u16(hdr, 2) &&
    Object.values(f).every(x => crc16(img.subarray(x.off, x.off + x.len)) === x.crc);
  const fl = img.subarray(f['flash.bin'].off, f['flash.bin'].off + f['flash.bin'].len);
  const fh = jlCrypt(fl.subarray(0, 32));
  crcOk &&= crc16(fh.subarray(2, 32)) === u16(fh, 0);
  const [aOk, app, aBad] = areaReport(fl);
  res.push(['CRCs', crcOk && aOk, crcOk && aOk ? 'all good' : (aBad.join(', ') || 'container CRC')]);
  const ota = img.subarray(f['ota.bin'].off, f['ota.bin'].off + f['ota.bin'].len);
  const lo = u32(ota, 4), ll = u32(ota, 8);
  const lsha = await sha256(ota.subarray(lo, lo + ll));
  res.push(['Loader', latin1(ota.subarray(16, 29)) === 'uart_user.bin' && lsha in PINNED_L2,
    PINNED_L2[lsha] || 'not a pinned nrf-v0.27 (L2) loader: ' + lsha.slice(0, 16)]);
  res.push(['Area', FLASH_SOC + fl.length <= AREA_END, `flash.bin SoC 0x${FLASH_SOC.toString(16)}..0x${(FLASH_SOC + fl.length).toString(16)}`]);
  const sfl = stock.subarray(fs['flash.bin'].off, fs['flash.bin'].off + fs['flash.bin'].len);
  let out = -1;
  for (let i = 0; i < Math.min(fl.length, sfl.length); i++) {
    if (fl[i] !== sfl[i] && !WINDOWS.some(([a, b]) => a <= FLASH_SOC + i && FLASH_SOC + i < b)) { out = i; break; }
  }
  res.push(['Stock bytes', fl.length === sfl.length && out < 0,
    out < 0 ? 'only the app window differs' : 'differs at SoC 0x' + (FLASH_SOC + out).toString(16)]);
  const [, sapp] = areaReport(sfl);
  const [kind, detail] = updateModeStatus(app, sapp);
  res.push(['Update mode', kind !== 'none', detail]);
  return res;
}

export function needsRescueWarning(checks) {
  return checks.some(([c, ok, d]) => c === 'Update mode' && ok && d.startsWith('Open-UV5R'));
}

// The whole local step: user's stock file + to-stock patch + our app -> checked flashable image.
// Refuses anything but stock nrf-v0.27 (the NRF kind; NRFB has a different layout and is not built yet).
export async function prepareImage(stock, patch, app) {
  const s = await sha256(stock);
  if (s !== STOCK_SHA) {
    throw new UfwError('This stock file is not stock-nrf-v0.27.ufw (NRF radios, Hardware V1.7). Only that kind is ' +
      'supported: NRFB radios (stock V0.04) have a different flash layout and must never get an NRF image.');
  }
  if (patch.base !== 'nrf-v0.27') throw new UfwError(`the patch is for base ${patch.base}, not nrf-v0.27`);
  const base = await applyPatch(patch, stock);
  const img = mkufw(base, app);
  const crcs = ufwcheck(img, app);
  if (!crcs.ok) throw new UfwError('the combined .ufw failed its CRC check');
  const checks = await checkImage(img, stock);
  // the app slot's size: the stock app area less its 0x100-byte head (what mkufw pads the app to)
  const { files } = ufwFiles(base);
  const fl = base.subarray(files['flash.bin'].off, files['flash.bin'].off + files['flash.bin'].len);
  const head = appAreaCrypt(fl.subarray(APP_AREA, APP_AREA + 32), 0);
  const slot = u32(head, 8) - APP_BIN_OFFSET;
  return { img, crcs, checks, slot };
}
