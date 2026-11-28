// What to run for one version and one set of switches: that version's own Makefile, read by make.js, with the
// switches as command-line variables (as `make ENABLE_X=0` does). Of its commands the page runs clang, ld.lld and
// llvm-objcopy; the rest (mkdir, llvm-size) are not needed.

import { runMake, shellWords, MakeError } from './make.js';
export { MakeError };

// Where the page puts the toolchain's own files in its file system (TOOLCHAIN for the Makefile).
export const TOOLCHAIN = '/tc/bin';
export const BUILTINS = '/tc/lib/clang/23/lib/pi32v2/libclang_rt.builtins.a';
export const SDK_LIBS = ['build/sdk/cpu.a', 'build/sdk/media.a', 'build/sdk/btctrler.a'];
const TOOLS = new Set(['clang', 'ld.lld', 'llvm-objcopy']);

// files: {path: text} of the version; switches: {ENABLE_X: 0|1, ...}
// -> {steps: [{tool, args}], compile: [{src, obj, args}], after: [{tool, args}], output, notes}
export function makePlan(files, switches, fwBuild) {
  const vfiles = { ...files };                      // the libraries only need to exist for $(wildcard)
  for (const l of SDK_LIBS) vfiles[l] = 1;
  vfiles[BUILTINS] = 1;
  const r = runMake({ files: vfiles, cmdVars: { ...switches, TOOLCHAIN, FW_BUILD: fwBuild } });
  const steps = [];
  for (const cmd of r.commands) {
    if (/[;&|<>`]/.test(cmd.replace(/'[^']*'|"[^"]*"/g, ''))) { r.notes.push('skipped a shell command: ' + cmd); continue; }
    const w = shellWords(cmd);
    const tool = (w[0] || '').split('/').pop();
    if (TOOLS.has(tool)) steps.push({ tool, args: w.slice(1) });
  }
  const after = [], compile = [];
  for (const s of steps) {
    const c = s.args.indexOf('-c'), o = s.args.indexOf('-o');
    if (s.tool === 'clang' && c >= 0 && o >= 0) compile.push({ src: s.args[c + 1], obj: s.args[o + 1], args: s.args });
    else after.push(s);
  }
  const last = after[after.length - 1];
  const output = last && last.tool === 'llvm-objcopy' ? last.args[last.args.length - 1] : null;
  if (!compile.length || !output) throw new MakeError('this version\'s Makefile does not compile and objcopy a firmware image');
  return { steps, compile, after, output, notes: r.notes };
}
