// A small GNU make, enough for this project's Makefile at any commit: it reads the Makefile (and what it includes)
// from a set of files, applies command-line variables as make does, and lists the commands `make -n -B all` would
// print. The web builder runs the clang, ld.lld and llvm-objcopy ones. tests/web_make_host.py compares it with real
// make across commits and switch sets.
//
// Supported: = := ::= ?= += and override; command-line variables; ifeq/ifneq (both syntaxes), ifdef/ifndef, else,
// else-if chains, endif; include/-include; explicit and %-pattern rules with recipes; target-specific variables;
// $(VAR:a=b); $@ $< $^ $*; functions if, filter, filter-out, wildcard, dir, notdir, addprefix, addsuffix, subst,
// patsubst, strip, sort, word, words, firstword, foreach, shell (returns ""), warning/info/error.

export class MakeError extends Error {}

const isFuncName = n => FUNCS.has(n);
const FUNCS = new Set(['if', 'filter', 'filter-out', 'wildcard', 'dir', 'notdir', 'addprefix', 'addsuffix', 'subst',
  'patsubst', 'strip', 'sort', 'word', 'words', 'firstword', 'lastword', 'foreach', 'shell', 'warning', 'info', 'error',
  'basename', 'suffix', 'or', 'and', 'findstring', 'join', 'realpath', 'abspath']);

const words = s => s.split(/\s+/).filter(Boolean);
function pat2re(p) {
  const i = p.indexOf('%');
  if (i < 0) return { re: null, lit: p };
  return { re: new RegExp('^' + esc(p.slice(0, i)) + '(.*)' + esc(p.slice(i + 1)) + '$') };
}
const esc = s => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
function patsubst(pat, rep, w) {
  const p = pat2re(pat);
  if (!p.re) return w === p.lit ? rep : w;
  const m = w.match(p.re);
  return m ? (rep.includes('%') ? rep.replace('%', m[1]) : rep) : w;
}
function normPath(p) {
  const abs = p.startsWith('/'); const out = [];
  for (const s of p.split('/')) {
    if (!s || s === '.') continue;
    if (s === '..') { if (out.length && out[out.length - 1] !== '..') out.pop(); else if (!abs) out.push('..'); }
    else out.push(s);
  }
  return (abs ? '/' : '') + out.join('/');
}

// files: {path: text} (only Makefile text is read; others only matter to $(wildcard) and include)
// exists: path -> bool for $(wildcard) and include (defaults to the files map)
export function runMake({ files, exists, cmdVars = {}, goal = 'all' }) {
  const has = exists || (p => Object.prototype.hasOwnProperty.call(files, normPath(p)));
  const vars = new Map();          // name -> {flavor:'simple'|'recursive', value, origin}
  const rules = [];                // {targets, prereqs, recipe:[], pattern}
  const targetVars = new Map();    // target -> [[name, op, value, override]]
  const notes = [];
  for (const [k, v] of Object.entries(cmdVars)) vars.set(k, { flavor: 'simple', value: String(v), origin: 'command' });
  vars.set('MAKEFILE_LIST', { flavor: 'simple', value: '', origin: 'default' });

  // --- expansion
  function lookup(name, local) {
    if (local && local.has(name)) return local.get(name);
    return vars.get(name);
  }
  function valueOf(name, local, auto) {
    if (auto && name in auto) return auto[name];
    const v = lookup(name, local);
    if (!v) return '';
    return v.flavor === 'recursive' ? expand(v.value, local, auto) : v.value;
  }
  // split "a,b,c" at top-level commas (n-1 splits)
  function splitArgs(s, n) {
    const out = []; let depth = 0, cur = '';
    for (let i = 0; i < s.length; i++) {
      const c = s[i];
      if (c === '(' || c === '{') depth++;
      else if (c === ')' || c === '}') depth--;
      if (c === ',' && depth === 0 && out.length < n - 1) { out.push(cur); cur = ''; continue; }
      cur += c;
    }
    out.push(cur);
    return out;
  }
  function wildcard(pattern) {
    const out = [];
    for (const p of words(pattern)) {
      if (!/[*?[]/.test(p)) { if (has(p)) out.push(p); continue; }
      // walk the known files: match the whole normalized path, return it in the pattern's own spelling
      const np = normPath(p);
      const re = new RegExp('^' + np.split('').map(c => c === '*' ? '[^/]*' : c === '?' ? '[^/]' : esc(c)).join('') + '$');
      const prefixLen = (() => { const i = p.search(/[*?[]/); return p.lastIndexOf('/', i) + 1; })();
      const nPrefix = normPath(p.slice(0, prefixLen) || '.');
      for (const f of Object.keys(files).sort()) {
        if (!re.test(f)) continue;
        const tail = nPrefix && nPrefix !== '.' ? f.slice(nPrefix.length + (nPrefix.endsWith('/') ? 0 : 1)) : f;
        out.push(p.slice(0, prefixLen) + tail);
      }
    }
    return out.join(' ');
  }
  function callFunc(name, argStr, local, auto) {
    const X = s => expand(s, local, auto);
    switch (name) {
      case 'if': { const [c, t = '', e = ''] = splitArgs(argStr, 3); return X(c).trim() ? X(t) : X(e); }
      case 'or': { for (const a of splitArgs(argStr, 99)) { const v = X(a).trim(); if (v) return v; } return ''; }
      case 'and': { let v = ''; for (const a of splitArgs(argStr, 99)) { v = X(a).trim(); if (!v) return ''; } return v; }
      case 'filter': case 'filter-out': {
        const [p, t] = splitArgs(argStr, 2); const pats = words(X(p));
        const hit = w => pats.some(q => { const r = pat2re(q); return r.re ? r.re.test(w) : w === r.lit; });
        return words(X(t)).filter(w => name === 'filter' ? hit(w) : !hit(w)).join(' ');
      }
      case 'wildcard': return wildcard(X(argStr));
      case 'dir': return words(X(argStr)).map(w => { const i = w.lastIndexOf('/'); return i < 0 ? './' : w.slice(0, i + 1); }).join(' ');
      case 'notdir': return words(X(argStr)).map(w => w.slice(w.lastIndexOf('/') + 1)).join(' ');
      case 'basename': return words(X(argStr)).map(w => { const i = w.lastIndexOf('.'); return i > w.lastIndexOf('/') ? w.slice(0, i) : w; }).join(' ');
      case 'suffix': return words(X(argStr)).map(w => { const i = w.lastIndexOf('.'); return i > w.lastIndexOf('/') ? w.slice(i) : ''; }).filter(Boolean).join(' ');
      case 'addprefix': { const [a, b] = splitArgs(argStr, 2); const p = X(a); return words(X(b)).map(w => p + w).join(' '); }
      case 'addsuffix': { const [a, b] = splitArgs(argStr, 2); const p = X(a); return words(X(b)).map(w => w + p).join(' '); }
      case 'subst': { const [a, b, c] = splitArgs(argStr, 3); return X(c).split(X(a)).join(X(b)); }
      case 'patsubst': { const [a, b, c] = splitArgs(argStr, 3); const pa = X(a).trim(), pb = X(b).trim(); return words(X(c)).map(w => patsubst(pa, pb, w)).join(' '); }
      case 'findstring': { const [a, b] = splitArgs(argStr, 2); const f = X(a); return X(b).includes(f) ? f : ''; }
      case 'strip': return words(X(argStr)).join(' ');
      case 'sort': return [...new Set(words(X(argStr)))].sort().join(' ');
      case 'words': return String(words(X(argStr)).length);
      case 'word': { const [n, t] = splitArgs(argStr, 2); return words(X(t))[+X(n) - 1] || ''; }
      case 'firstword': return words(X(argStr))[0] || '';
      case 'lastword': { const w = words(X(argStr)); return w[w.length - 1] || ''; }
      case 'join': { const [a, b] = splitArgs(argStr, 2); const A = words(X(a)), B = words(X(b)); return Array.from({ length: Math.max(A.length, B.length) }, (_, i) => (A[i] || '') + (B[i] || '')).join(' '); }
      case 'foreach': {
        const [v, l, t] = splitArgs(argStr, 3); const name2 = X(v).trim();
        return words(X(l)).map(w => { const loc = new Map(local || []); loc.set(name2, { flavor: 'simple', value: w }); return expand(t, loc, auto); }).join(' ');
      }
      case 'realpath': case 'abspath': return words(X(argStr)).map(normPath).join(' ');
      case 'shell': return '';        // git describe, the flags file: the caller passes FW_BUILD on the command line
      case 'warning': case 'info': notes.push(X(argStr).trim()); return '';
      case 'error': throw new MakeError(X(argStr).trim());
    }
    return '';
  }
  function expand(s, local, auto) {
    let out = '';
    for (let i = 0; i < s.length; i++) {
      const c = s[i];
      if (c !== '$') { out += c; continue; }
      const n = s[++i];
      if (n === undefined) break;
      if (n === '$') { out += '$'; continue; }
      if (n !== '(' && n !== '{') { out += valueOf(n, local, auto); continue; }
      const close = n === '(' ? ')' : '}';
      let depth = 1, j = i + 1;
      for (; j < s.length; j++) {
        if (s[j] === n) depth++;
        else if (s[j] === close && --depth === 0) break;
      }
      const inner = s.slice(i + 1, j);
      i = j;
      const sp = inner.search(/[\s]/);
      const fname = sp > 0 ? inner.slice(0, sp) : null;
      if (fname && isFuncName(fname)) { out += callFunc(fname, inner.slice(sp + 1).replace(/^\s+/, ''), local, auto); continue; }
      const name = expand(inner, local, auto);
      const sub = name.match(/^([^:]+):(.*)=(.*)$/);           // $(VAR:a=b)
      if (sub) {
        const val = valueOf(sub[1], local, auto);
        const a = sub[2], b = sub[3];
        out += words(val).map(w => a.includes('%') ? patsubst(a, b, w) : (w.endsWith(a) ? w.slice(0, w.length - a.length) + b : w)).join(' ');
        continue;
      }
      out += valueOf(name, local, auto);
    }
    return out;
  }

  // --- assignment
  function assign(map, name, op, value, override, isTarget) {
    const cur = map.get(name) || (isTarget ? vars.get(name) : undefined);
    if (!isTarget && cur && cur.origin === 'command' && !override) return;
    if (op === '?=') { if (cur) return; map.set(name, { flavor: 'recursive', value, origin: 'file' }); return; }
    if (op === ':=' || op === '::=') { map.set(name, { flavor: 'simple', value: expand(value), origin: override ? 'override' : 'file' }); return; }
    if (op === '=') { map.set(name, { flavor: 'recursive', value, origin: override ? 'override' : 'file' }); return; }
    if (op === '+=') {
      if (!cur) { map.set(name, { flavor: 'recursive', value, origin: 'file' }); return; }
      if (cur.flavor === 'simple') map.set(name, { ...cur, value: (cur.value ? cur.value + ' ' : '') + expand(value) });
      else map.set(name, { ...cur, value: (cur.value ? cur.value + ' ' : '') + value });
    }
  }

  // --- parsing
  const ASSIGN = /^(override\s+)?(export\s+)?([^\s:#=+?!]+)\s*(::=|:=|\?=|\+=|=)\s?(.*)$/;
  function parse(text, fname) {
    const raw = text.replace(/\r\n?/g, '\n').split('\n');
    const lines = [];                                       // join continuations
    for (let i = 0; i < raw.length; i++) {
      let l = raw[i];
      while (l.endsWith('\\') && i + 1 < raw.length) { l = l.slice(0, -1).replace(/\s+$/, '') + ' ' + raw[++i].replace(/^\s+/, ''); }
      lines.push(l);
    }
    const cond = [];                                        // stack of {active, done, parentActive}
    const active = () => cond.every(c => c.active);
    let rule = null;
    const stripComment = l => { let o = '', d = 0; for (let k = 0; k < l.length; k++) { const c = l[k]; if (c === '\\' && l[k + 1] === '#') { o += '#'; k++; continue; } if (c === '#' ) break; o += c; } return o; };
    function evalCond(kind, rest) {
      if (kind === 'ifdef' || kind === 'ifndef') {
        const v = lookup(expand(rest.trim()));
        const def = !!(v && (v.flavor === 'simple' ? v.value : v.value));
        return kind === 'ifdef' ? def : !def;
      }
      let a, b; rest = rest.trim();
      if (rest.startsWith('(')) {
        const inner = rest.slice(1, rest.lastIndexOf(')'));
        [a, b] = splitArgs(inner, 2);
      } else {
        const m = rest.match(/^(["'])(.*?)\1\s+(["'])(.*?)\3/);
        a = m ? m[2] : ''; b = m ? m[4] : '';
      }
      const eqv = expand(a).trim() === expand(b ?? '').trim();
      return kind === 'ifeq' ? eqv : !eqv;
    }
    for (const line of lines) {
      if (line.startsWith('\t') && rule) {                  // recipe line
        if (active()) rule.recipe.push(line.slice(1));
        continue;
      }
      const l = stripComment(line).trim();
      if (!l) continue;
      let m;
      if ((m = l.match(/^(ifeq|ifneq|ifdef|ifndef)\b\s*(.*)$/))) {
        const pa = active();
        const v = pa && evalCond(m[1], m[2]);
        cond.push({ active: v, done: v, parentActive: pa });
        continue;
      }
      if ((m = l.match(/^else\b\s*(.*)$/))) {
        const top = cond[cond.length - 1];
        if (!top) throw new MakeError(fname + ': else without if');
        const sub = m[1].match(/^(ifeq|ifneq|ifdef|ifndef)\b\s*(.*)$/);
        const outer = cond.slice(0, -1).every(c => c.active);
        if (top.done || !outer) top.active = false;
        else if (sub) { top.active = evalCond(sub[1], sub[2]); top.done = top.active; }
        else { top.active = true; top.done = true; }
        continue;
      }
      if (/^endif\b/.test(l)) { cond.pop(); continue; }
      if (!active()) continue;
      if ((m = l.match(/^(-?)include\s+(.*)$/))) {
        for (const f of words(expand(m[2]))) {
          const t = files[normPath(f)];
          if (typeof t === 'string') parse(t, f);
          else if (!m[1]) throw new MakeError(`${fname}: ${f}: no such file`);
        }
        rule = null;
        continue;
      }
      if ((m = l.match(/^(override\s+)?(export\s+)?([A-Za-z_.][\w.-]*)\s*(::=|:=|\?=|\+=|=)\s?(.*)$/))) {
        assign(vars, m[3], m[4], m[5].trim(), !!m[1], false);
        rule = null;
        continue;
      }
      // a rule: targets : prereqs   (or targets : VAR op value)
      const exp = expand(l);
      const ci = exp.search(/:(?!=)/);
      if (ci < 0) { if (exp.trim()) notes.push(`${fname}: ignored line: ${l}`); rule = null; continue; }
      const targets = words(exp.slice(0, ci));
      let rest = exp.slice(ci + 1).replace(/^:/, '');
      const tv = l.slice(l.search(/:(?!=)/) + 1).match(/^\s*(override\s+)?([A-Za-z_.][\w.-]*)\s*(::=|:=|\?=|\+=|=)\s?(.*)$/);
      if (tv) {
        for (const t of targets) {
          if (!targetVars.has(t)) targetVars.set(t, []);
          targetVars.get(t).push([tv[2], tv[3], tv[4].trim(), !!tv[1]]);
        }
        rule = null;
        continue;
      }
      let inlineRecipe = null;
      const semi = rest.indexOf(';');
      if (semi >= 0) { inlineRecipe = rest.slice(semi + 1).trim(); rest = rest.slice(0, semi); }
      rule = { targets, prereqs: words(rest), recipe: inlineRecipe ? [inlineRecipe] : [], pattern: targets.some(t => t.includes('%')) };
      rules.push(rule);
    }
  }

  const mk = files.Makefile ?? files.makefile ?? files.GNUmakefile;
  if (typeof mk !== 'string') throw new MakeError('no Makefile in this version');
  parse(mk, 'Makefile');

  // --- run the goal: every recipe in the order make -n -B prints them
  const explicit = new Map();
  for (const r of rules) if (!r.pattern) for (const t of r.targets) {
    const e = explicit.get(t);
    if (!e) explicit.set(t, { prereqs: [...r.prereqs], recipe: r.recipe.length ? r.recipe : [] });
    else { e.prereqs.push(...r.prereqs); if (r.recipe.length) e.recipe = r.recipe; }
  }
  const patterns = rules.filter(r => r.pattern);
  const phony = new Set((explicit.get('.PHONY') || { prereqs: [] }).prereqs);
  const done = new Set();
  const commands = [];
  function build(t, stack = []) {
    if (done.has(t)) return;
    if (stack.includes(t)) return;
    done.add(t);
    let e = explicit.get(t);
    let stem = '';
    let recipe = e && e.recipe.length ? e.recipe : null;
    let prereqs = e ? [...e.prereqs] : [];
    if (!recipe) {
      for (const r of patterns) {
        for (const pt of r.targets) {
          const p = pat2re(pt);
          const m = t.match(p.re);
          if (!m) continue;
          const pre = r.prereqs.map(q => q.replace('%', m[1]));
          // the pattern applies when its %-prerequisites exist or can be made (others, such as a file a $(shell)
          // writes, are taken as they come)
          const stemmed = r.prereqs.map((q, k) => q.includes('%') ? pre[k] : null).filter(Boolean);
          if (!stemmed.every(q => has(q) || explicit.has(q) || patterns.some(r2 => r2 !== r && r2.targets.some(x => pat2re(x).re?.test(q))))) continue;
          stem = m[1]; recipe = r.recipe; prereqs = [...pre, ...prereqs];
          break;
        }
        if (recipe) break;
      }
    }
    for (const p of prereqs) build(p, [...stack, t]);
    if (!recipe || !recipe.length) {
      // a plain file (or one a $(shell) writes); only the goal itself must have a rule
      const madeByPattern = patterns.some(r => r.targets.some(x => pat2re(x).re?.test(t)));
      if (!e && !has(t) && !phony.has(t) && (!stack.length || madeByPattern)) {
        throw new MakeError(`no rule to make ${t}` + (stack.length ? ` (needed by ${stack[stack.length - 1]})` : ''));
      }
      return;
    }
    const local = new Map();
    for (const [name, op, value, ov] of targetVars.get(t) || []) assign(local, name, op, value, ov, true);
    const auto = { '@': t, '<': prereqs[0] || '', '^': [...new Set(prereqs)].join(' '), '*': stem };
    for (const line of recipe) {
      const cmd = expand(line, local, auto).replace(/^[@+-]+/, '').trim();
      if (cmd) commands.push(cmd);
    }
  }
  build(goal);
  return { commands, notes, value: name => valueOf(name) };
}

// shell words: '...', "...", \x
export function shellWords(cmd) {
  const out = []; let cur = '', inW = false;
  for (let i = 0; i < cmd.length; i++) {
    const c = cmd[i];
    if (c === "'") { const j = cmd.indexOf("'", i + 1); cur += cmd.slice(i + 1, j < 0 ? undefined : j); i = j < 0 ? cmd.length : j; inW = true; continue; }
    if (c === '"') {
      let j = i + 1;
      for (; j < cmd.length && cmd[j] !== '"'; j++) { if (cmd[j] === '\\' && /["\\$`]/.test(cmd[j + 1])) j++; cur += cmd[j]; }
      i = j; inW = true; continue;
    }
    if (c === '\\' && i + 1 < cmd.length) { cur += cmd[++i]; inW = true; continue; }
    if (/\s/.test(c)) { if (inW) { out.push(cur); cur = ''; inW = false; } continue; }
    cur += c; inW = true;
  }
  if (inW) out.push(cur);
  return out;
}
