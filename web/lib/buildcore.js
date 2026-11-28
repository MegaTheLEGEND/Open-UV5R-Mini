// One compile or the link, with the WebAssembly toolchain (llvm.wasm) and an in-memory file system.
// Used by build-worker.js in the browser and directly by tests/web_build.mjs in Node.
import { MemFS, runWasi } from './wasi.js';

export const RESOURCE_DIR = '/clang';

// files: {path: string | Uint8Array}; clangInclude: {relative path under include/: text}
export function baseFS(files, clangInclude) {
  const fs = new MemFS();
  for (const [p, d] of Object.entries(files)) fs.write('/' + p, d);
  for (const [p, d] of Object.entries(clangInclude)) fs.write(`${RESOURCE_DIR}/include/${p}`, d);
  fs.mkdirp('/build/fw');
  fs.mkdirp('/tmp');                  // clang preprocesses .S files into a temporary file
  return fs;
}

function run(module, fs, tool, args) {
  const log = [];
  const code = runWasi(module, { args: [tool, ...args], env: ['TMPDIR=/tmp'], fs, stdout: s => log.push(s), stderr: s => log.push(s) });
  return { code, log };
}

// args: the plan's clang arguments (exactly make's); we add only where the compiler's own headers are.
// WASI cannot start processes. clang runs its compile step (-cc1) in-process by itself, but for a .S file it would
// start a second step (the preprocessor, then the assembler, -cc1as): so for those we ask clang for its steps (-###)
// and run each one here.
export function compile(module, fs0, job) {
  const fs = fs0.fork();
  fs.mkdirp('/' + job.obj.split('/').slice(0, -1).join('/'));
  const args = ['-resource-dir', RESOURCE_DIR, '-fno-color-diagnostics', ...job.args];
  let r;
  if (!job.src.endsWith('.S')) {
    r = run(module, fs, 'clang', args);
  } else {
    const plan = run(module, fs, 'clang', ['-###', ...args]);
    const steps = plan.log.filter(l => l.startsWith(' "')).map(parseQuoted);
    if (plan.code || !steps.length) return { code: plan.code || 1, log: plan.log, obj: null };
    r = { code: 0, log: [] };
    for (const st of steps) {
      // a step reads "<this program>" "clang" "-cc1" ...: the multicall program's own name, then the tool's
      const one = run(module, fs, 'clang', st.slice(st[1] === 'clang' ? 2 : 1));
      r.log.push(...one.log);
      if ((r.code = one.code)) break;
    }
  }
  return { code: r.code, log: r.log, obj: r.code === 0 ? fs.read('/' + job.obj)?.slice() : null };
}

// one line of clang -### output: "arg" "arg" ... (\\ and \" escaped)
function parseQuoted(line) {
  const out = []; const re = /"((?:[^"\\]|\\.)*)"/g; let m;
  while ((m = re.exec(line))) out.push(m[1].replace(/\\(.)/g, '$1'));
  return out;
}

// objs: {path: bytes}. Returns {code, log, elf, bin}
export function link(module, fs0, plan, objs) {
  const fs = fs0.fork();
  for (const [p, d] of Object.entries(objs)) fs.write('/' + p, d);
  const l = run(module, fs, 'ld.lld', plan.link);
  if (l.code) return { code: l.code, log: l.log };
  const o = run(module, fs, 'llvm-objcopy', plan.objcopy);
  return {
    code: o.code, log: [...l.log, ...o.log],
    elf: fs.read('/build/fw/firmware.elf')?.slice(), bin: o.code ? null : fs.read('/build/fw/firmware.bin')?.slice(),
  };
}
