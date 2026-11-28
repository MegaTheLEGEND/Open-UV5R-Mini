// The in-browser build: fetch the toolchain and sources (cached by the browser), compile every file in a pool of
// Web Workers, link, and hand back firmware.bin. The same steps as `make` (plan.js; tests/web_plan_host.py).
import { makePlan } from './plan.js';

const cache = {};
async function fetchBytes(url, progress) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url}: ${r.status}`);
  const total = +r.headers.get('content-length') || 0;
  if (!r.body || !progress) return new Uint8Array(await r.arrayBuffer());
  const reader = r.body.getReader();
  const parts = []; let got = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    parts.push(value); got += value.length;
    progress(got, total);
  }
  const out = new Uint8Array(got); let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
}

// Gunzip with the browser's own DecompressionStream (llvm.wasm.gz: GitHub Pages does not compress .wasm).
async function gunzip(bytes) {
  const s = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'));
  return new Uint8Array(await new Response(s).arrayBuffer());
}

export async function loadToolchain(base, log) {
  if (cache.tc) return cache.tc;
  log('Downloading the compiler (once; the browser keeps it)...');
  let last = 0;
  const gz = await fetchBytes(base + 'toolchain/llvm.wasm.gz', (g, t) => {
    const p = t ? Math.floor(g * 100 / t) : Math.floor(g / 1e6);
    if (p >= last + 10) { last = p; log(t ? `  ${p}%` : `  ${p} MB`); }
  });
  log('Starting the compiler...');
  const module = await WebAssembly.compile(await gunzip(gz));
  const [src, inc, cpu, media, btc, rt] = await Promise.all([
    fetch(base + 'sources.json').then(r => r.json()),
    fetch(base + 'toolchain/clang-include.json').then(r => r.json()),
    fetchBytes(base + 'sdk/cpu.a'), fetchBytes(base + 'sdk/media.a'), fetchBytes(base + 'sdk/btctrler.a'),
    fetchBytes(base + 'toolchain/libclang_rt.builtins.a'),
  ]);
  const files = { ...src.files, 'sdk/cpu.a': cpu, 'sdk/media.a': media, 'sdk/btctrler.a': btc,
    'lib/libclang_rt.builtins.a': rt };
  cache.tc = { module, files, clangInclude: inc, meta: src.meta };
  return cache.tc;
}

function startWorker(tc) {
  const w = new Worker(new URL('./build-worker.js', import.meta.url), { type: 'module' });
  const pending = new Map(); let next = 1;
  const ready = new Promise((res, rej) => {
    w.onmessage = e => {
      if (e.data.type === 'ready') return res();
      const p = pending.get(e.data.id); pending.delete(e.data.id); p(e.data);
    };
    w.onerror = e => rej(new Error('worker: ' + e.message));
  });
  w.postMessage({ type: 'init', module: tc.module, files: tc.files, clangInclude: tc.clangInclude });
  const call = (type, msg) => new Promise(res => { const id = next++; pending.set(id, res); w.postMessage({ type, id, ...msg }); });
  return { ready, call, stop: () => w.terminate() };
}

// features: {ENABLE_x: 0|1, REGION: n}. Returns {bin, plan, seconds}
export async function buildFirmware(base, features, log) {
  const tc = await loadToolchain(base, log);
  const defaults = Object.fromEntries(tc.meta.features.map(f => [f.name, f.default]));
  const plan = makePlan(features, defaults, tc.meta.build + '-web');
  for (const n of plan.notes) log('Note: ' + n);
  const t0 = performance.now();
  const n = Math.max(1, Math.min(navigator.hardwareConcurrency || 4, 8, plan.compile.length));
  log(`Compiling ${plan.compile.length} files with ${n} workers...`);
  const workers = Array.from({ length: n }, () => startWorker(tc));
  try {
    await Promise.all(workers.map(w => w.ready));
    const queue = plan.compile.slice();
    const objs = {}; let failed = null, done = 0;
    await Promise.all(workers.map(async w => {
      while (queue.length && !failed) {
        const job = queue.shift();
        const r = await w.call('compile', { job });
        for (const l of r.log) log('  ' + l);
        if (r.code) { failed = `${job.src}: the compiler stopped (exit ${r.code})`; break; }
        objs[job.obj] = r.obj;
        if (++done % 10 === 0) log(`  ${done}/${plan.compile.length}`);
      }
    }));
    if (failed) throw new Error(failed);
    log('Linking (with JieLi\'s libraries, link-time optimised)...');
    const r = await workers[0].call('link', { plan, objs });
    for (const l of r.log) log('  ' + l);
    if (r.code || !r.bin) throw new Error(`the linker stopped (exit ${r.code})`);
    const seconds = (performance.now() - t0) / 1000;
    log(`Built firmware.bin: ${r.bin.length} bytes in ${seconds.toFixed(1)} s (build ${tc.meta.build}-web).`);
    return { bin: r.bin, plan, seconds, build: tc.meta.build + '-web' };
  } finally {
    workers.forEach(w => w.stop());
  }
}
