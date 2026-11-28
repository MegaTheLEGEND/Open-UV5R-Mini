// Web Worker: holds the toolchain and the sources, runs compiles and the link for build.js.
import { baseFS, compile, link } from './buildcore.js';

let module, fs;
self.onmessage = e => {
  const m = e.data;
  try {
    if (m.type === 'init') {
      module = m.module;
      fs = baseFS(m.files, m.clangInclude);
      self.postMessage({ type: 'ready' });
    } else if (m.type === 'compile') {
      const r = compile(module, fs, m.job);
      self.postMessage({ type: 'done', id: m.id, ...r }, r.obj ? [r.obj.buffer] : []);
    } else if (m.type === 'link') {
      const r = link(module, fs, m.plan, m.objs);
      self.postMessage({ type: 'done', id: m.id, ...r });
    }
  } catch (err) {
    self.postMessage({ type: 'done', id: m.id, code: 1, log: ['worker: ' + (err && err.stack || err)] });
  }
};
