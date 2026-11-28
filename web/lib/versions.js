// Which firmware version to build: the releases and recent commits this site carries (versions/, made by
// web/tools/mksite.py from git), or any other commit, branch or tag, fetched from GitHub by the page itself.
// A version is {sha, date, subject, files: {path: text}, features: [...], from: 'site' | 'github'}.
import { parseFeatures } from './features.js';

// the same files mksite.py takes from a commit: the Makefile and .mk files at the top, src/'s sources
const BUILD_EXT = /\.(c|h|S|s|ld|inc|mk)$/;
export function isBuildFile(path) {
  if (!path.includes('/')) return path === 'Makefile' || path.endsWith('.mk');
  return path.startsWith('src/') && BUILD_EXT.test(path);
}

export async function loadIndex(base) {
  const r = await fetch(base + 'versions/index.json');
  if (!r.ok) throw new Error('versions/index.json: ' + r.status);
  return r.json();
}

// The version picked by default: the newest release, else the commit the site was made from (main's newest)
export function defaultSha(index) {
  return index.releases.length ? index.releases[0].sha : index.head;
}

// What the firmware calls itself (FW_BUILD; the Makefile uses git describe): a release's tag, else the short hash
export function buildName(index, v) {
  const rel = index.releases.find(r => r.sha === v.sha);
  return (rel ? rel.tag : v.sha.slice(0, 7)) + '-web';
}

const cache = new Map();

// ref: a hash (whole or at least 4 characters), a branch or a tag
export async function loadVersion(base, index, ref, log = () => {}) {
  ref = ref.trim();
  if (!ref) throw new Error('no version given');
  const local = findLocal(index, ref);
  if (local) return fromSite(base, local);
  if (!index.repo) throw new Error(`${ref} is not one of the versions on this site`);
  const info = await api(index.repo, `commits/${encodeURIComponent(ref)}`);
  const known = index.commits.find(c => c.sha === info.sha);
  if (known) return fromSite(base, known);
  return fromGitHub(index.repo, info, log);
}

function findLocal(index, ref) {
  const r = ref.toLowerCase();
  const rel = index.releases.find(x => x.tag === ref);
  if (rel) return index.commits.find(c => c.sha === rel.sha);
  const tip = index.commits.filter(c => (c.tips || []).includes(ref));
  if (tip.length === 1) return tip[0];
  if (/^[0-9a-f]{4,40}$/.test(r)) {
    const hits = index.commits.filter(c => c.sha.startsWith(r));
    if (hits.length === 1) return hits[0];
  }
  return null;
}

async function fromSite(base, c) {
  if (cache.has(c.sha)) return cache.get(c.sha);
  const r = await fetch(base + 'versions/' + c.sha + '.json');
  if (!r.ok) throw new Error(`versions/${c.sha}.json: ${r.status}`);
  const j = await r.json();
  const v = { sha: c.sha, date: c.date, subject: c.subject, files: j.files, features: j.features, from: 'site' };
  cache.set(c.sha, v);
  return v;
}

async function api(repo, path) {
  const r = await fetch(`https://api.github.com/repos/${repo}/${path}`, { headers: { Accept: 'application/vnd.github+json' } });
  if (r.status === 404 || r.status === 422) throw new Error('GitHub has no such commit, branch or tag in ' + repo);
  if (r.status === 403 || r.status === 429) {
    const reset = +r.headers.get('x-ratelimit-reset');
    const when = reset ? ' Try again after ' + new Date(reset * 1000).toLocaleTimeString() + '.' : '';
    throw new Error('GitHub\'s limit for pages like this one (60 requests an hour) is used up.' + when);
  }
  if (!r.ok) throw new Error(`GitHub: ${r.status}`);
  return r.json();
}

async function fromGitHub(repo, info, log) {
  const sha = info.sha;
  if (cache.has(sha)) return cache.get(sha);
  log(`Fetching ${sha.slice(0, 7)} from GitHub...`);
  const tree = await api(repo, `git/trees/${sha}?recursive=1`);
  if (tree.truncated) throw new Error('GitHub sent only part of this commit\'s file list');
  const paths = tree.tree.filter(t => t.type === 'blob' && t.mode !== '120000' && isBuildFile(t.path)).map(t => t.path);
  if (!paths.includes('Makefile')) throw new Error(`${sha.slice(0, 7)} has no Makefile: it is from before the firmware could be built`);
  const files = {};
  let next = 0, done = 0;
  await Promise.all(Array.from({ length: 16 }, async () => {
    while (next < paths.length) {
      const p = paths[next++];
      const r = await fetch(`https://raw.githubusercontent.com/${repo}/${sha}/${p.split('/').map(encodeURIComponent).join('/')}`);
      if (!r.ok) throw new Error(`${p}: ${r.status}`);
      files[p] = await r.text();
      if (++done % 50 === 0) log(`  ${done}/${paths.length} files`);
    }
  }));
  const c = info.commit || {};
  const v = {
    sha, date: (c.committer?.date || '').slice(0, 10), subject: (c.message || '').split('\n')[0],
    files, features: parseFeatures(files['features.mk']), from: 'github',
  };
  cache.set(sha, v);
  return v;
}
