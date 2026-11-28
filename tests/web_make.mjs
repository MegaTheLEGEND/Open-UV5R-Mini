// Node half of tests/web_make_host.py: stdin {files, sets, tc} -> stdout {features, runs: [{commands} | {error}]}
// (features: web/lib/features.js's reading of features.mk, one entry per switch set).
import { runMake } from '../web/lib/make.js';
import { parseFeatures } from '../web/lib/features.js';

let input = '';
process.stdin.on('data', d => { input += d; });
process.stdin.on('end', () => {
  const { files, sets, tc } = JSON.parse(input);
  const out = sets.map(sw => {
    try {
      const r = runMake({ files, cmdVars: { ...sw, TOOLCHAIN: tc, FW_BUILD: 'web' } });
      return { commands: r.commands, notes: r.notes };
    } catch (e) {
      return { error: e.message };
    }
  });
  process.stdout.write(JSON.stringify({ features: files['features.mk'] ? parseFeatures(files['features.mk']) : [], runs: out }));
});
