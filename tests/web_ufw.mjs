// Node half of tests/web_ufw_host.py: the web flasher's .ufw code (web/lib/ufw.js) on real files.
// usage: node tests/web_ufw.mjs STOCK_NRF STOCK_NRFB PATCH APP OUT_UFW
import { readFileSync, writeFileSync } from 'node:fs';
import { prepareImage, applyPatch, mkufw, ufwcheck, UfwError } from '../web/lib/ufw.js';

const [stockP, nrfbP, patchP, appP, outP] = process.argv.slice(2);
const stock = new Uint8Array(readFileSync(stockP));
const patch = JSON.parse(readFileSync(patchP, 'utf8'));
const app = new Uint8Array(readFileSync(appP));
let fails = 0;
const ok = (what, cond, more = '') => { console.log(`${cond ? 'ok  ' : 'BAD '} ${what} ${more}`); if (!cond) fails++; };

const { img, crcs, checks } = await prepareImage(stock, patch, app);
ok('combined .ufw passes its CRC check', crcs.ok);
for (const [c, good, d] of checks) ok('image check ' + c, good, d);
writeFileSync(outP, img);

const base = await applyPatch(patch, stock);
writeFileSync(outP + '.base', base);

async function refuses(what, fn, word) {
  try { await fn(); ok(what, false, '(accepted)'); } catch (e) { ok(what, e instanceof UfwError && e.message.includes(word), e.message.slice(0, 70)); }
}
await refuses('NRFB stock file refused', () => prepareImage(new Uint8Array(readFileSync(nrfbP)), patch, app), 'NRFB');
const noMode = app.slice();
for (const m of ['UPDATE\0', 'OPENUV5R-UPDATE-MODE/1 writes=']) {
  const b = new TextEncoder().encode(m);
  for (let i = 0; i + b.length <= noMode.length; i++) {
    let hit = true;
    for (let j = 0; j < b.length; j++) if (noMode[i + j] !== b[j]) { hit = false; break; }
    if (hit) noMode[i] ^= 0x20;
  }
}
await refuses('app without an update mode refused (mkufw guard)', () => mkufw(base, noMode), 'refusing');
const broken = img.slice(); broken[0x5000] ^= 1;
ok('ufwcheck finds a flipped byte', !ufwcheck(broken).ok);
await refuses('app too big for the slot refused', () => { const big = new Uint8Array(600000); big.set(app); return mkufw(base, big); }, 'slot');
process.exit(fails ? 1 : 0);
