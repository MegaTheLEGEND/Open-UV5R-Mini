// A small WASI (preview 1) runtime with an in-memory file system, enough to run clang, ld.lld and llvm-objcopy
// (web/toolchain/build-wasm.sh) in a browser or in Node. Our own code.
//
//   const fs = new MemFS(); fs.write('/src/a.c', bytes);
//   const code = runWasi(module, { args: ['clang', '-c', ...], fs, cwd: '/', stdout: s => ..., stderr: s => ... });

const E = { SUCCESS: 0, ACCES: 2, BADF: 8, EXIST: 20, INVAL: 28, IO: 29, ISDIR: 31, NOENT: 44, NOSYS: 52,
  NOTDIR: 54, NOTEMPTY: 55, SPIPE: 70, NOTSUP: 58 };
const FT = { UNKNOWN: 0, CHAR: 2, DIR: 3, FILE: 4 };

class ProcExit extends Error { constructor(code) { super('exit ' + code); this.code = code; } }

let inoNext = 1;
function mkdir() { return { type: 'dir', entries: new Map(), ino: inoNext++, mtime: 0n }; }
function mkfile(data = new Uint8Array(0)) { return { type: 'file', data, size: data.length, ino: inoNext++, mtime: 0n }; }

export class MemFS {
  constructor() { this.root = mkdir(); }
  split(path) { return path.split('/').filter(p => p && p !== '.'); }
  // node at an absolute path, or null
  lookup(path, from = this.root) {
    let stack = from === this.root ? [this.root] : [this.root, from];
    for (const p of this.split(path)) {
      const cur = stack[stack.length - 1];
      if (p === '..') { if (stack.length > 1) stack.pop(); continue; }
      if (cur.type !== 'dir') return null;
      const n = cur.entries.get(p);
      if (!n) return null;
      stack.push(n);
    }
    return stack[stack.length - 1];
  }
  mkdirp(path) {
    let cur = this.root;
    for (const p of this.split(path)) {
      let n = cur.entries.get(p);
      if (!n) { n = mkdir(); cur.entries.set(p, n); }
      cur = n;
    }
    return cur;
  }
  write(path, data) {
    if (typeof data === 'string') data = new TextEncoder().encode(data);
    const parts = this.split(path);
    const dir = this.mkdirp(parts.slice(0, -1).join('/'));
    const f = mkfile(data);
    dir.entries.set(parts[parts.length - 1], f);
    return f;
  }
  read(path) {
    const n = this.lookup(path);
    return n && n.type === 'file' ? n.data.subarray(0, n.size) : null;
  }
  // a cheap copy: new directories, files shared until written (writes replace the file's data array)
  fork() {
    const copy = d => {
      const nd = mkdir();
      for (const [k, v] of d.entries) nd.entries.set(k, v.type === 'dir' ? copy(v) : { ...v });
      return nd;
    };
    const f = new MemFS();
    f.root = copy(this.root);
    return f;
  }
}

export function runWasi(module, { args, env = [], fs, stdout = () => {}, stderr = () => {} }) {
  let mem;
  const dv = () => new DataView(mem.buffer);
  const u8 = () => new Uint8Array(mem.buffer);
  const dec = new TextDecoder();
  const enc = new TextEncoder();
  const str = (p, n) => dec.decode(u8().subarray(p, p + n));
  const outBuf = { 1: '', 2: '' };
  const emit = (fd, bytes) => {
    outBuf[fd] += dec.decode(bytes, { stream: true });
    let i;
    while ((i = outBuf[fd].indexOf('\n')) >= 0) {
      (fd === 1 ? stdout : stderr)(outBuf[fd].slice(0, i));
      outBuf[fd] = outBuf[fd].slice(i + 1);
    }
  };
  // fds: 0-2 stdio, 3 = the preopened root "/"
  const fds = new Map([[0, { kind: 'stdio' }], [1, { kind: 'stdio' }], [2, { kind: 'stdio' }],
    [3, { kind: 'node', node: fs.root, path: '/', pos: 0, preopen: '/' }]]);
  let nextFd = 4;
  const now = () => BigInt(Date.now()) * 1000000n;

  function resolveParent(dirfd, path) {
    const d = fds.get(dirfd);
    if (!d || d.kind !== 'node' || d.node.type !== 'dir') return [E.BADF];
    const full = (path.startsWith('/') ? '' : d.path + '/') + path;
    const parts = fs.split(full);
    const stack = [];
    for (const p of parts) { if (p === '..') stack.pop(); else stack.push(p); }
    const name = stack.pop();
    const parent = fs.lookup('/' + stack.join('/'));
    if (!parent) return [E.NOENT];
    if (parent.type !== 'dir') return [E.NOTDIR];
    return [E.SUCCESS, parent, name, '/' + [...stack, name ?? ''].join('/')];
  }
  function statInto(node, p) {
    const v = dv();
    v.setBigUint64(p, 1n, true);
    v.setBigUint64(p + 8, BigInt(node.ino || 0), true);
    v.setUint8(p + 16, node.type === 'dir' ? FT.DIR : node.type === 'file' ? FT.FILE : FT.CHAR);
    v.setBigUint64(p + 24, 1n, true);
    v.setBigUint64(p + 32, BigInt(node.type === 'file' ? node.size : 0), true);
    v.setBigUint64(p + 40, node.mtime || 0n, true);
    v.setBigUint64(p + 48, node.mtime || 0n, true);
    v.setBigUint64(p + 56, node.mtime || 0n, true);
  }
  function ensureCap(node, n) {
    if (n > node.data.length || node.shared) {      // grow, or copy a file still shared with the template
      const nd = new Uint8Array(Math.max(n, node.data.length * 2, 4096));
      nd.set(node.data.subarray(0, node.size));
      node.data = nd;
      node.shared = false;
    }
  }
  function iovs(p, n) {
    const v = dv(); const out = [];
    for (let i = 0; i < n; i++) out.push([v.getUint32(p + 8 * i, true), v.getUint32(p + 8 * i + 4, true)]);
    return out;
  }
  function readInto(f, list, pos) {
    let total = 0;
    const data = f.node.data;
    for (const [b, l] of list) {
      const n = Math.max(0, Math.min(l, f.node.size - pos));
      u8().set(data.subarray(pos, pos + n), b);
      pos += n; total += n;
      if (n < l) break;
    }
    return total;
  }
  function writeFrom(f, list, pos) {
    let total = 0;
    for (const [b, l] of list) {
      ensureCap(f.node, pos + l);
      f.node.data.set(u8().subarray(b, b + l), pos);
      pos += l; total += l;
      if (pos > f.node.size) f.node.size = pos;
    }
    f.node.mtime = now();
    return total;
  }
  const strs = list => {
    const bufs = list.map(s => enc.encode(s + '\0'));
    return [bufs, bufs.reduce((a, b) => a + b.length, 0)];
  };

  const w = {
    args_sizes_get(c, s) { const [b, n] = strs(args); dv().setUint32(c, b.length, true); dv().setUint32(s, n, true); return 0; },
    args_get(pp, bp) {
      const [b] = strs(args);
      for (const x of b) { dv().setUint32(pp, bp, true); u8().set(x, bp); pp += 4; bp += x.length; }
      return 0;
    },
    environ_sizes_get(c, s) { const [b, n] = strs(env); dv().setUint32(c, b.length, true); dv().setUint32(s, n, true); return 0; },
    environ_get(pp, bp) {
      const [b] = strs(env);
      for (const x of b) { dv().setUint32(pp, bp, true); u8().set(x, bp); pp += 4; bp += x.length; }
      return 0;
    },
    clock_res_get(id, p) { dv().setBigUint64(p, 1000n, true); return 0; },
    clock_time_get(id, prec, p) {
      const t = id === 0 ? now() : BigInt(Math.round((globalThis.performance?.now() ?? Date.now()) * 1e6));
      dv().setBigUint64(p, t, true); return 0;
    },
    fd_advise() { return 0; },
    fd_allocate() { return 0; },
    fd_close(fd) { if (!fds.has(fd)) return E.BADF; fds.delete(fd); return 0; },
    fd_datasync() { return 0; },
    fd_sync() { return 0; },
    fd_fdstat_get(fd, p) {
      const f = fds.get(fd); if (!f) return E.BADF;
      const v = dv();
      v.setUint8(p, f.kind === 'stdio' ? FT.CHAR : f.node.type === 'dir' ? FT.DIR : FT.FILE);
      v.setUint16(p + 2, f.append ? 1 : 0, true);
      // stdio: a character device without seek/tell rights (what wasi-libc's isatty() takes for a terminal)
      const all = 0xFFFFFFFFFFFFFFFFn, noSeek = all & ~((1n << 2n) | (1n << 5n));
      v.setBigUint64(p + 8, f.kind === 'stdio' ? noSeek : all, true);
      v.setBigUint64(p + 16, all, true);
      return 0;
    },
    fd_fdstat_set_flags(fd, fl) { const f = fds.get(fd); if (!f) return E.BADF; f.append = !!(fl & 1); return 0; },
    fd_fdstat_set_rights() { return 0; },
    fd_filestat_get(fd, p) {
      const f = fds.get(fd); if (!f) return E.BADF;
      statInto(f.kind === 'stdio' ? { type: 'char' } : f.node, p); return 0;
    },
    fd_filestat_set_size(fd, sz) {
      const f = fds.get(fd); if (!f || f.kind !== 'node' || f.node.type !== 'file') return E.BADF;
      const n = Number(sz); ensureCap(f.node, n);
      if (n > f.node.size) f.node.data.fill(0, f.node.size, n);
      f.node.size = n; return 0;
    },
    fd_filestat_set_times() { return 0; },
    fd_pread(fd, iov, n, off, nr) {
      const f = fds.get(fd); if (!f || f.kind !== 'node' || f.node.type !== 'file') return E.BADF;
      dv().setUint32(nr, readInto(f, iovs(iov, n), Number(off)), true); return 0;
    },
    fd_pwrite(fd, iov, n, off, nw) {
      const f = fds.get(fd); if (!f || f.kind !== 'node' || f.node.type !== 'file') return E.BADF;
      dv().setUint32(nw, writeFrom(f, iovs(iov, n), Number(off)), true); return 0;
    },
    fd_read(fd, iov, n, nr) {
      const f = fds.get(fd); if (!f) return E.BADF;
      if (f.kind === 'stdio') { dv().setUint32(nr, 0, true); return 0; }
      if (f.node.type !== 'file') return E.ISDIR;
      const got = readInto(f, iovs(iov, n), f.pos); f.pos += got;
      dv().setUint32(nr, got, true); return 0;
    },
    fd_write(fd, iov, n, nw) {
      const f = fds.get(fd); if (!f) return E.BADF;
      const list = iovs(iov, n);
      if (f.kind === 'stdio') {
        let t = 0;
        for (const [b, l] of list) { emit(fd === 1 ? 1 : 2, u8().slice(b, b + l)); t += l; }
        dv().setUint32(nw, t, true); return 0;
      }
      if (f.node.type !== 'file') return E.ISDIR;
      if (f.append) f.pos = f.node.size;
      const t = writeFrom(f, list, f.pos); f.pos += t;
      dv().setUint32(nw, t, true); return 0;
    },
    fd_prestat_get(fd, p) {
      const f = fds.get(fd); if (!f || !f.preopen) return E.BADF;
      dv().setUint8(p, 0); dv().setUint32(p + 4, enc.encode(f.preopen).length, true); return 0;
    },
    fd_prestat_dir_name(fd, p, n) {
      const f = fds.get(fd); if (!f || !f.preopen) return E.BADF;
      u8().set(enc.encode(f.preopen).subarray(0, n), p); return 0;
    },
    fd_readdir(fd, buf, len, cookie, used) {
      const f = fds.get(fd); if (!f || f.kind !== 'node') return E.BADF;
      if (f.node.type !== 'dir') return E.NOTDIR;
      const list = [['.', f.node], ['..', f.node], ...f.node.entries];
      let p = buf; const end = buf + len;
      for (let i = Number(cookie); i < list.length && p < end; i++) {
        const [name, node] = list[i];
        const nb = enc.encode(name);
        const ent = new Uint8Array(24 + nb.length);
        const ev = new DataView(ent.buffer);
        ev.setBigUint64(0, BigInt(i + 1), true);
        ev.setBigUint64(8, BigInt(node.ino || 0), true);
        ev.setUint32(16, nb.length, true);
        ev.setUint8(20, node.type === 'dir' ? FT.DIR : FT.FILE);
        ent.set(nb, 24);
        const n = Math.min(ent.length, end - p);
        u8().set(ent.subarray(0, n), p); p += n;
      }
      dv().setUint32(used, p - buf, true); return 0;
    },
    fd_renumber(a, b) { const f = fds.get(a); if (!f) return E.BADF; fds.set(b, f); fds.delete(a); return 0; },
    fd_seek(fd, off, whence, p) {
      const f = fds.get(fd); if (!f) return E.BADF;
      if (f.kind === 'stdio') return E.SPIPE;
      const base = whence === 0 ? 0 : whence === 1 ? f.pos : f.node.size || 0;
      const np = base + Number(off); if (np < 0) return E.INVAL;
      f.pos = np; dv().setBigUint64(p, BigInt(np), true); return 0;
    },
    fd_tell(fd, p) { const f = fds.get(fd); if (!f) return E.BADF; dv().setBigUint64(p, BigInt(f.pos || 0), true); return 0; },
    path_create_directory(dirfd, pp, pl) {
      const [e, parent, name] = resolveParent(dirfd, str(pp, pl)); if (e) return e;
      if (parent.entries.has(name)) return E.EXIST;
      parent.entries.set(name, mkdir()); return 0;
    },
    path_filestat_get(dirfd, flags, pp, pl, p) {
      const [e, parent, name] = resolveParent(dirfd, str(pp, pl)); if (e) return e;
      const node = name === undefined ? parent : parent.entries.get(name);
      if (!node) return E.NOENT;
      statInto(node, p); return 0;
    },
    path_filestat_set_times() { return 0; },
    path_link() { return E.NOTSUP; },
    path_open(dirfd, dflags, pp, pl, oflags, rb, ri, fdflags, fdp) {
      const path = str(pp, pl);
      const [e, parent, name, full] = resolveParent(dirfd, path); if (e) return e;
      let node = name === undefined ? parent : parent.entries.get(name);
      if (node && (oflags & 4) && (oflags & 1)) return E.EXIST;
      if (!node) {
        if (!(oflags & 1)) return E.NOENT;
        node = mkfile(); parent.entries.set(name, node);
      }
      if ((oflags & 2) && node.type !== 'dir') return E.NOTDIR;
      if (node.type === 'file' && (oflags & 8)) { node.data = new Uint8Array(0); node.size = 0; }
      const fd = nextFd++;
      fds.set(fd, { kind: 'node', node, path: full, pos: 0, append: !!(fdflags & 1) });
      dv().setUint32(fdp, fd, true); return 0;
    },
    path_readlink() { return E.INVAL; },
    path_remove_directory(dirfd, pp, pl) {
      const [e, parent, name] = resolveParent(dirfd, str(pp, pl)); if (e) return e;
      const n = parent.entries.get(name); if (!n) return E.NOENT;
      if (n.type !== 'dir') return E.NOTDIR;
      if (n.entries.size) return E.NOTEMPTY;
      parent.entries.delete(name); return 0;
    },
    path_rename(fd1, p1, l1, fd2, p2, l2) {
      const [e1, par1, n1] = resolveParent(fd1, str(p1, l1)); if (e1) return e1;
      const [e2, par2, n2] = resolveParent(fd2, str(p2, l2)); if (e2) return e2;
      const node = par1.entries.get(n1); if (!node) return E.NOENT;
      par1.entries.delete(n1); par2.entries.set(n2, node); return 0;
    },
    path_symlink() { return E.NOTSUP; },
    path_unlink_file(dirfd, pp, pl) {
      const [e, parent, name] = resolveParent(dirfd, str(pp, pl)); if (e) return e;
      const n = parent.entries.get(name); if (!n) return E.NOENT;
      if (n.type === 'dir') return E.ISDIR;
      parent.entries.delete(name); return 0;
    },
    poll_oneoff(inp, out, n, ne) {
      const v = dv();
      for (let i = 0; i < n; i++) {
        v.setBigUint64(out + 32 * i, v.getBigUint64(inp + 48 * i, true), true);
        v.setUint16(out + 32 * i + 8, 0, true);
        v.setUint8(out + 32 * i + 10, v.getUint8(inp + 48 * i + 8));
      }
      v.setUint32(ne, n, true); return 0;
    },
    proc_exit(code) { throw new ProcExit(code); },
    proc_raise() { return E.NOSYS; },
    sched_yield() { return 0; },
    random_get(p, n) { globalThis.crypto.getRandomValues(u8().subarray(p, p + n)); return 0; },
    sock_accept() { return E.NOSYS; },
    sock_recv() { return E.NOSYS; },
    sock_send() { return E.NOSYS; },
    sock_shutdown() { return E.NOSYS; },
  };
  // files shared with the template FS must be copied before they are written
  const markShared = d => { for (const v of d.entries.values()) { if (v.type === 'dir') markShared(v); else v.shared = true; } };
  markShared(fs.root);

  const inst = new WebAssembly.Instance(module, { wasi_snapshot_preview1: w });
  mem = inst.exports.memory;
  let code = 0;
  try {
    inst.exports._start();
  } catch (e) {
    if (e instanceof ProcExit) code = e.code;
    else { stderr('wasm: ' + (e && e.message || e)); if (globalThis.WASI_STACK) stderr(e.stack); code = 134; }
  }
  for (const fd of [1, 2]) if (outBuf[fd]) (fd === 1 ? stdout : stderr)(outBuf[fd]);
  return code;
}
