// Node half of tests/web_flasher_host.py: run web/lib/flasher.js sessions over the TCP bridge to the emulated radio.
// usage: node tests/web_flasher.mjs PORT IMAGE.ufw SESSIONS
import net from 'node:net';
import { readFileSync } from 'node:fs';
import { flashRadio, APP_BAUD } from '../web/lib/flasher.js';

const [port, imgPath, sessions] = process.argv.slice(2);
const image = new Uint8Array(readFileSync(imgPath));

class BridgeLink {
  constructor(sock) {
    this.sock = sock; this.baud = APP_BAUD; this.q = []; this.waiters = []; this.off = false; let buf = '';
    sock.on('data', d => {
      buf += d.toString();
      let i;
      while ((i = buf.indexOf('\n')) >= 0) {
        const line = buf.slice(0, i); buf = buf.slice(i + 1);
        if (line === 'X') this.off = true;
        else if (line.startsWith('R ')) {
          const [, baud, hx] = line.split(' ');
          if (+baud === this.baud) this.q.push(Uint8Array.from(Buffer.from(hx, 'hex')));   // a UART at another rate hears noise
        }
        this.waiters.splice(0).forEach(w => w());
      }
    });
  }
  async read(ms) {
    if (!this.q.length && ms > 0) await new Promise(res => { const t = setTimeout(res, ms); this.waiters.push(() => { clearTimeout(t); res(); }); });
    const parts = this.q.splice(0);
    return Uint8Array.from(parts.flatMap(p => [...p]));
  }
  async write(b) { this.sock.write(`W ${this.baud} ${Buffer.from(b).toString('hex')}\n`); }
  async setBaud(b) { this.baud = b; this.q = []; }
  radioOff() { return this.off && !this.q.length; }
}

let ok = false;
for (let s = 0; s < +sessions; s++) {
  const sock = await new Promise((res, rej) => { const c = net.connect(+port, '127.0.0.1', () => res(c)); c.on('error', rej); });
  const link = new BridgeLink(sock);
  let lastP = -1;
  const [good, msg] = await flashRadio(link, image, {
    log: s2 => { if (!s2.startsWith('  loader')) console.log('  ' + s2); },
    progress: p => { const q = Math.floor(p * 4); if (q !== lastP) { lastP = q; console.log(`  ${Math.round(p * 100)}%`); } },
  });
  console.log(`session ${s + 1}: ${good ? 'ok' : 'not ok'}: ${msg}`);
  sock.end();
  ok = good;
  await new Promise(r => setTimeout(r, 300));
}
process.exit(ok ? 0 : 1);
