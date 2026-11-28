// features.mk -> the switches, in file order: [{name, default, help, values?}]. The same rules as
// web/tools/mksite.py parse_features (tests/web_make_host.py checks they agree). A version without features.mk has none.

export function parseFeatures(text) {
  const out = [];
  let comment = [];
  for (const line of (text || '').replace(/\r\n?/g, '\n').split('\n')) {
    const m = line.match(/^([A-Z_]+)\s*\?=\s*(\d+)/);
    if (m) {
      if (comment.length && comment[0].startsWith('Apps (MENU')) {   // the Apps section's intro, not this switch's
        const end = comment.findIndex(c => c.includes('section itself goes'));
        comment = end >= 0 ? comment.slice(end + 1) : comment;
      }
      const item = { name: m[1], default: +m[2], help: comment.join(' ') };
      if (item.name === 'REGION') item.values = { 0: 'chosen in the menu', 1: 'US', 2: 'EU/UK', 3: 'AU' };
      out.push(item);
      comment = [];
    } else if (line.startsWith('#')) {
      const t = line.replace(/^#+/, '').trim();
      if (t) comment.push(t); else comment = [];
    } else if (!line.trim()) {
      comment = [];
    }
  }
  return out;
}
