// Fetch the two outside files straight into the user's browser, from where the README and BUILDING.md link them.
// This site never holds them: the stock file comes from its MEGA link (decrypted here with the key in the link, as
// MEGA's own page does), the loader patch from MM9PDY's GitHub repo at a pinned commit. Each is then checked
// against its pinned SHA-256, so a changed or wrong file is refused like a hand-loaded one.
import { STOCK_SHA, sha256 } from './ufw.js';

export const STOCK_LINK = 'https://mega.nz/file/U5NSDIhZ#DqRVur6RPVay1ylKZFQsz2zrXnR3ZrAMBToPHgPnPvQ';
export const PATCH_URL = 'https://raw.githubusercontent.com/pskillen/mm9pdy-firmware/' +
  '6f23730a1edbcbce603252edcfa84ebb44760330/releases/baofeng-uv5rmini/to-stock/' +
  'uv5rmini-fw-to-stock-nrf-v0.27-hw1.7-1.1.0.patch.json';
export const PATCH_SHA = 'dd515ff91cd8372671c531327939cf846751b861f21221d919def91c2d0cdd30';

function b64url(s) {
  const b = atob(s.replace(/-/g, '+').replace(/_/g, '/') + '==='.slice((s.length + 3) % 4));
  return Uint8Array.from(b, c => c.charCodeAt(0));
}

// a public MEGA file link -> its bytes. progress(got, total)
export async function fetchMega(link, progress = () => {}) {
  const m = link.match(/mega\.nz\/file\/([\w-]+)#([\w-]+)/);
  if (!m) throw new Error('not a MEGA file link');
  const [, id, keyStr] = m;
  const r = await fetch('https://g.api.mega.co.nz/cs?id=' + Date.now(), {
    method: 'POST', body: JSON.stringify([{ a: 'g', g: 1, ssl: 2, p: id }]),
  });
  if (!r.ok) throw new Error('MEGA: ' + r.status);
  const [info] = await r.json();
  if (typeof info === 'number' || !info.g) throw new Error('MEGA refused the link (error ' + JSON.stringify(info) + ')');
  // the link's key: 32 bytes; the AES key is its two halves XORed, the counter starts at bytes 16..23
  const k = b64url(keyStr);
  if (k.length !== 32) throw new Error('MEGA link key has the wrong length');
  const aes = new Uint8Array(16), ctr = new Uint8Array(16);
  for (let i = 0; i < 16; i++) aes[i] = k[i] ^ k[i + 16];
  ctr.set(k.subarray(16, 24));
  const d = await fetch(info.g);
  if (!d.ok) throw new Error('MEGA download: ' + d.status);
  const parts = []; let got = 0;
  const reader = d.body.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    parts.push(value); got += value.length; progress(got, info.s);
  }
  const enc = new Uint8Array(got); let o = 0;
  for (const p of parts) { enc.set(p, o); o += p.length; }
  const key = await crypto.subtle.importKey('raw', aes, 'AES-CTR', false, ['decrypt']);
  return new Uint8Array(await crypto.subtle.decrypt({ name: 'AES-CTR', counter: ctr, length: 64 }, key, enc));
}

export async function fetchStock(progress) {
  const b = await fetchMega(STOCK_LINK, progress);
  if (await sha256(b) !== STOCK_SHA) throw new Error('the downloaded stock file is not stock-nrf-v0.27.ufw (SHA-256 differs)');
  return b;
}

export async function fetchPatch() {
  const r = await fetch(PATCH_URL);
  if (!r.ok) throw new Error('GitHub: ' + r.status);
  const bytes = new Uint8Array(await r.arrayBuffer());
  if (await sha256(bytes) !== PATCH_SHA) throw new Error('the downloaded loader patch is not the pinned 1.1.0 file (SHA-256 differs)');
  return JSON.parse(new TextDecoder().decode(bytes));
}
