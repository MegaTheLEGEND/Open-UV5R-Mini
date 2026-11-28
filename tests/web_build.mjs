// Node half of tests/web_build_host.py: build firmware.bin with the site's WebAssembly toolchain, as the page does.
// usage: node tests/web_build.mjs SITE_DIR SHA SWITCHES_JSON FW_BUILD OUT_BIN  (SHA: one of the site's versions/)
import { readFileSync, writeFileSync } from 'node:fs';
import { gunzipSync } from 'node:zlib';
import { cpus } from 'node:os';
import { Worker } from 'node:worker_threads';
import { makePlan, SDK_LIBS, BUILTINS } from '../web/lib/plan.js';
import { baseFS, link } from '../web/lib/buildcore.js';

const [site, sha, swJson, fwBuild, out] = process.argv.slice(2);
const rd = p => new Uint8Array(readFileSync(`${site}/${p}`));
const t0 = Date.now();
const module = await WebAssembly.compile(gunzipSync(readFileSync(`${site}/toolchain/llvm.wasm.gz`)));
const ver = JSON.parse(readFileSync(`${site}/versions/${sha}.json`, 'utf8'));
const inc = JSON.parse(readFileSync(`${site}/toolchain/clang-include.json`, 'utf8'));
const files = { ...ver.files, [SDK_LIBS[0]]: rd('sdk/cpu.a'), [SDK_LIBS[1]]: rd('sdk/media.a'),
  [SDK_LIBS[2]]: rd('sdk/btctrler.a'), [BUILTINS]: rd('toolchain/libclang_rt.builtins.a') };
const plan = makePlan(ver.files, JSON.parse(swJson), fwBuild);
console.log(`compiler ready in ${((Date.now() - t0) / 1000).toFixed(1)} s; ${plan.compile.length} files`);

// the page's worker pool, with Node's workers running the same buildcore.compile
const code = `
import { parentPort, workerData } from 'node:worker_threads';
import { baseFS, compile } from '${new URL('../web/lib/buildcore.js', import.meta.url).href}';
const fs = baseFS(workerData.files, workerData.inc);
parentPort.on('message', job => { const r = compile(workerData.module, fs, job); parentPort.postMessage(r); });`;
const n = Math.min(cpus().length, 8);
const workers = Array.from({ length: n }, () => new Worker(new URL('data:text/javascript,' + encodeURIComponent(code)),
  { workerData: { module, files, inc } }));
const queue = plan.compile.slice(); const objs = {}; let failed = false;
const t1 = Date.now();
await Promise.all(workers.map(w => (async () => {
  while (queue.length && !failed) {
    const job = queue.shift();
    const r = await new Promise(res => { w.once('message', res); w.postMessage(job); });
    for (const l of r.log) console.log('  ' + l);
    if (r.code) { console.log(`FAILED ${job.src} (exit ${r.code})`); failed = true; break; }
    objs[job.obj] = r.obj;
  }
})()));
workers.forEach(w => w.terminate());
if (failed) process.exit(1);
console.log(`compiled in ${((Date.now() - t1) / 1000).toFixed(1)} s`);
const t2 = Date.now();
const r = link(module, baseFS(files, inc), plan, objs);
for (const l of r.log) console.log('  ' + l);
if (r.code) { console.log(`link FAILED (exit ${r.code})`); process.exit(1); }
console.log(`linked in ${((Date.now() - t2) / 1000).toFixed(1)} s: firmware.bin ${r.bin.length} bytes`);
writeFileSync(out, r.bin);
