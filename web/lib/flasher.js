// The cable update session, ported from tools/uv5r_flasher.py flash_radio (same frames, timings and guards).
// The radio drives it: the host pings until the stock or our update mode answers, then only answers the radio's
// reads with bytes of the checked image (an outgoing guard refuses anything else) and echoes its frames.
// link: {read(ms) -> Uint8Array, write(bytes), setBaud(baud), close(), radioOff?()}. WebSerialLink is the browser's.
import { crc16, ufwFiles, AREA_START, FLASH_SOC } from './ufw.js';

export const CMD_BAUD = 1, CMD_DATA = 2, CMD_RESULT = 3, CMD_LENGTH = 4, CMD_PING = 6;
export const APP_BAUD = 9600;
const MAX_READ = 0x1000, DUP_WINDOW = 0.1;
const T_LISTEN = 2.0, T_PING = 1.0, T_PING_TOTAL = 30.0, T_SETTLE = 0.3, T_IDLE = 60.0;

export const RESULT_TEXT = {
  0x00: 'Done. The radio restarts by itself; check its power-on screen.',
  0x01: "Nothing was written: the image is not for this radio's key. Switch the radio off and on.",
  0x83: 'The radio refused the file (not for this firmware).',
  0x86: "This firmware is already on the radio, or it can't be replaced by this image. Nothing was erased.",
  0x87: 'Nothing was written; the radio boots the installed app.',
  0x88: 'The loader gave up part-way through writing (0x88).',
  0x8A: 'Nothing was written: the image is for another stock version.',
  0x8F: "The file isn't for this radio's flash layout; the radio refused it. Nothing installed.",
  0x90: 'The image runs past the area the radio can install. Nothing was erased.',
};
export const RECOVER = 'The update did not finish. Leave the cable in, switch the radio off and on with NO keys held ' +
  '(the screen stays dark), then press Flash again with the same files: the loader continues.';

const now = () => (globalThis.performance ? performance.now() : Date.now()) / 1000;
const sleep = s => new Promise(r => setTimeout(r, s * 1000));
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
const le32 = v => new Uint8Array([v & 0xFF, (v >>> 8) & 0xFF, (v >>> 16) & 0xFF, (v >>> 24) & 0xFF]);
const hexs = b => Array.from(b, x => x.toString(16).padStart(2, '0')).join(' ');

export function frame(cmd, data = new Uint8Array(0)) {
  const body = new Uint8Array(5 + data.length);
  body.set([0xAA, 0x55, (1 + data.length) & 0xFF, ((1 + data.length) >> 8) & 0xFF, cmd]);
  body.set(data, 5);
  const c = crc16(body);
  const out = new Uint8Array(body.length + 2);
  out.set(body); out[body.length] = c & 0xFF; out[body.length + 1] = c >> 8;
  return out;
}

export class FrameParser {
  constructor() { this.buf = new Uint8Array(0); }
  feed(b) {
    const nb = new Uint8Array(this.buf.length + b.length); nb.set(this.buf); nb.set(b, this.buf.length); this.buf = nb;
    const out = [];
    for (;;) {
      let i = -1;
      for (let k = 0; k + 1 < this.buf.length; k++) if (this.buf[k] === 0xAA && this.buf[k + 1] === 0x55) { i = k; break; }
      if (i < 0) { this.buf = this.buf.length && this.buf[this.buf.length - 1] === 0xAA ? this.buf.slice(-1) : new Uint8Array(0); return out; }
      this.buf = this.buf.slice(i);
      if (this.buf.length < 4) return out;
      const ln = this.buf[2] | (this.buf[3] << 8);
      if (ln === 0) { this.buf = this.buf.slice(2); out.push([null, new Uint8Array(0)]); continue; }
      const total = ln + 6;
      if (this.buf.length < total) return out;
      const f = this.buf.slice(0, total); this.buf = this.buf.slice(total);
      if ((f[total - 2] | (f[total - 1] << 8)) !== crc16(f.subarray(0, total - 2))) out.push([null, f]);
      else out.push([f[4], f.slice(5, total - 2)]);
    }
  }
}

// opts: {loaderBaud (38400), replyDelay (s, 0.01), progress(p), log(s), cancelled(), trace(line)}
export async function flashRadio(link, image, opts = {}) {
  let LOADER_BAUD = opts.loaderBaud ?? 38400;
  const REPLY_DELAY = opts.replyDelay ?? 0.01;
  const log = opts.log || (() => {}), progress = opts.progress || (() => {});
  const t0 = now();
  const tr = (what, s) => opts.trace && opts.trace(`${(now() - t0).toFixed(3).padStart(9)} ${what.padEnd(5)} ${s}`);
  const { files } = ufwFiles(image);
  const areaLo = files['flash.bin'].off + (AREA_START - FLASH_SOC);
  const parser = new FrameParser();
  let rate = APP_BAUD, phase = 'listen', reads = 0, areaLen = null, touched = false;

  async function send(cmd, data = new Uint8Array(0)) {
    if (cmd === CMD_DATA) {                       // outgoing guard: only image bytes the radio asked for
      const off = u32(data, 0), n = u32(data, 4);
      const want = image.subarray(off, off + n), got = data.subarray(8);
      if (n > MAX_READ || off + n > image.length || got.length !== n || got.some((x, i) => x !== want[i])) {
        throw new Error(`refusing to send: read 0x${off.toString(16)}+0x${n.toString(16)} is outside the image`);
      }
      tr('out', `cmd 2 0x${off.toString(16)}+${n} @${rate}`);
    } else tr('out', `cmd ${cmd} ${hexs(data)} @${rate}`);
    await link.write(frame(cmd, data));
  }
  async function frames(timeout) {
    if (opts.cancelled && opts.cancelled()) throw new Error('cancelled');
    const raw = await link.read(timeout * 1000);
    if (raw.length) tr('in', hexs(raw));
    const out = [];
    for (const [cmd, d] of parser.feed(raw)) { if (cmd === null) log('  (bad frame ignored)'); else out.push([cmd, d]); }
    return out;
  }

  log(`Listening ${T_LISTEN} s for a loader that is part-way through an update...`);
  let pending = [];
  let end = now() + T_LISTEN;
  while (now() < end && !pending.length) pending = (await frames(0.1)).filter(x => x[0] === CMD_BAUD);
  if (pending.length) {
    log(`The radio was part-way through an update: continuing it at ${APP_BAUD}.`);
    LOADER_BAUD = APP_BAUD;
    phase = 'loader'; touched = true;
    await send(CMD_BAUD, le32(LOADER_BAUD));
    await link.setBaud(LOADER_BAUD); rate = LOADER_BAUD;
    pending = [];
  } else {
    log('Pinging the radio (hold PTT-A and = while switching on, release at UPDATE...)');
    phase = 'ping';
    end = now() + T_PING_TOTAL;
    while (phase === 'ping') {
      if (now() > end) return [false, 'No radio in update mode answered. Nothing was sent that the radio acted on.'];
      await send(CMD_PING);
      const t = now() + T_PING;
      while (now() < t) {
        if ((await frames(0.1)).some(([c, d]) => c === CMD_BAUD && !d.length)) { phase = 'app'; break; }
      }
    }
    const settle = now() + T_SETTLE;
    while (now() < settle) await frames(0.05);
    log('Radio in update mode answered. App stage at 9600 (the radio is not changed yet).');
    await send(CMD_BAUD, le32(APP_BAUD));
  }

  let last = now(), switchedAt = null, fallbacks = 0;
  const blocks = new Map(); let asked = null, quietLogged = false, lastReq = null;
  for (;;) {
    const got = pending.length ? pending : await frames(0.25);
    pending = [];
    if (!got.length) {
      if (phase === 'loader' && areaLen && !quietLogged && now() - last > 3) {
        quietLogged = true;
        log(`  the loader went quiet at ${rate} baud: ${blocks.size} of ${Math.ceil(areaLen / 0x200)} blocks done`);
      }
      if (switchedAt && now() - switchedAt > 1.5 && fallbacks < 5) {
        fallbacks++;
        log(`No hello at ${LOADER_BAUD}: back to ${APP_BAUD} for the next loader hello.`);
        await link.setBaud(APP_BAUD); rate = APP_BAUD; switchedAt = null;
      }
      if (now() - last > T_IDLE || (link.radioOff && link.radioOff())) {
        return [false, touched ? 'The radio stopped talking. ' + RECOVER : 'The radio stopped talking.'];
      }
      continue;
    }
    last = now(); switchedAt = null; quietLogged = false;
    const asks = got.map(([c, d], i) => [i, c === CMD_DATA && d.length >= 8 ? [u32(d, 0), u32(d, 4)] : null]).filter(x => x[1]);
    for (let i = 0; i < got.length; i++) {
      const [cmd, d] = got[i];
      if (cmd === CMD_BAUD) {
        if (phase === 'app' && reads) {
          log(`Hand-over: the radio reset into the loader. Switching to ${LOADER_BAUD}.`);
          phase = 'loader'; touched = true;
          await send(CMD_BAUD, le32(LOADER_BAUD));
          await link.setBaud(LOADER_BAUD); rate = LOADER_BAUD; switchedAt = now();
        } else if (phase === 'loader') {
          await send(CMD_BAUD, le32(LOADER_BAUD));
          if (rate !== LOADER_BAUD) { await link.setBaud(LOADER_BAUD); rate = LOADER_BAUD; switchedAt = now(); }
        }
      } else if (cmd === CMD_DATA && d.length >= 8) {
        const off = u32(d, 0), n = u32(d, 4);
        const t = now();
        if (lastReq && lastReq[0] === off && lastReq[1] === n && t - lastReq[2] < DUP_WINDOW) {
          tr('note', `duplicate request 0x${off.toString(16)}+${n} not answered`); continue;
        }
        lastReq = [off, n, t];
        if (asks.some(([j, a]) => j > i && (a[0] !== off || a[1] !== n))) {
          tr('note', `request 0x${off.toString(16)}+${n} not answered: a newer one came with it`); continue;
        }
        if (phase === 'loader' && REPLY_DELAY) {
          await sleep(REPLY_DELAY);
          const more = await frames(0);
          pending.push(...more);
          if (more.some(([c, x]) => c === CMD_DATA && x.length >= 8 && (u32(x, 0) !== off || u32(x, 4) !== n))) {
            tr('note', `request 0x${off.toString(16)}+${n} not answered: a newer one came during the pause`); continue;
          }
        }
        const ans = new Uint8Array(8 + n); ans.set(d.subarray(0, 8)); ans.set(image.subarray(off, off + n), 8);
        await send(CMD_DATA, ans);
        if (phase === 'loader' && !areaLen) log(`  loader read 0x${off.toString(16)}, ${n} bytes`);
        reads++;
        if (phase === 'loader' && areaLen && off >= areaLo) {
          if ((asked && asked[0] === off && asked[1] === n) || blocks.has(off)) log(`  loader asked again for 0x${off.toString(16)} (+${n}): an answer was lost`);
          blocks.set(off, n); asked = [off, n];
          let sum = 0; for (const v of blocks.values()) sum += v;
          progress(Math.min(0.99, sum / areaLen));
        }
      } else if (cmd === CMD_LENGTH && phase === 'loader' && d.length >= 4) {
        areaLen = u32(d, 0);
        log(`The radio is erasing its firmware area (0x${areaLen.toString(16)} bytes) and writing...`);
        await send(CMD_LENGTH, d);
      } else if (cmd === CMD_RESULT && d.length) {
        const result = d[0];
        if (phase === 'loader') await send(CMD_RESULT, d);
        let txt = RESULT_TEXT[result] || `Result 0x${result.toString(16)}.`;
        if (result !== 0 && phase === 'loader' && areaLen) txt += ' ' + RECOVER;
        progress(result === 0 ? 1 : 0);
        return [result === 0, txt];
      } else if (cmd === 0x7F && d.length >= 36) {
        log('  radio debug frame: ' + hexs(d.subarray(0, 36)));
      } else {
        log(`  (frame cmd ${cmd} ignored)`);
      }
    }
  }
}

// Web Serial (Chrome, Edge): the K1 cable's USB serial adapter
export class WebSerialLink {
  constructor(port) { this.port = port; this.q = []; this.waiters = []; this.idleAt = 0; this.baud = APP_BAUD; }
  async open(baud = APP_BAUD) {
    this.baud = baud;
    await this.port.open({ baudRate: baud, dataBits: 8, stopBits: 1, parity: 'none', bufferSize: 8192 });
    try { await this.port.setSignals({ dataTerminalReady: false, requestToSend: false }); } catch { /* not all adapters */ }
    this.writer = this.port.writable.getWriter();
    this.reader = this.port.readable.getReader();
    this.loop = (async () => {
      try {
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) break;
          if (value && value.length) { this.q.push(value); this.waiters.splice(0).forEach(w => w()); }
        }
      } catch { /* closed */ }
    })();
  }
  async read(ms) {
    if (!this.q.length && ms > 0) {
      await new Promise(res => { const t = setTimeout(res, ms); this.waiters.push(() => { clearTimeout(t); res(); }); });
    }
    const parts = this.q.splice(0);
    const n = parts.reduce((a, p) => a + p.length, 0);
    const out = new Uint8Array(n); let o = 0;
    for (const p of parts) { out.set(p, o); o += p.length; }
    return out;
  }
  async write(b) {
    await this.writer.write(b);
    // the adapter still sends after the write returns: 10 bits a byte, plus a margin
    this.idleAt = now() + b.length * 10 / this.baud + 0.04;
  }
  async closePort() {
    try { await this.reader.cancel(); } catch { /* */ }
    await this.loop;
    this.reader.releaseLock(); this.writer.releaseLock();
    await this.port.close();
  }
  async setBaud(baud) {
    // changing the rate while the last frame is still going out garbles its tail
    const wait = this.idleAt - now();
    if (wait > 0) await sleep(wait);
    await this.closePort();
    this.q = [];                                   // bytes half-heard at the old rate are noise at the new one
    await this.open(baud);
  }
  async close() { try { await this.closePort(); } catch { /* */ } }
}
