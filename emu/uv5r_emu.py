#!/usr/bin/env python3
"""UV-5R Mini emulator: runs the stock (or our) app.bin on an emulated JieLi AC695N.

    python emu/uv5r_emu.py --isa E:/pi32v2-isa [--app build/stock/unpacked/app.bin] [--steps N]
        [--extflash dump.bin] [--watch 1e1d00-1e1e00] [--trace N]

WORK IN PROGRESS: boots part way (docs/EMULATOR.md has the status and the road map).

CPU: the pi32v2 simulator of pi32v2-isa (tools/llvm/sim.py, MIT-0), whose instruction effects are models fitted to
real hardware. This file adds the AC695N around it: the memory map, the peripherals found so far, the mask-ROM
routines the app calls (high-level stand-ins), and the radio's parts (ST7735S LCD and external SPI flash so far;
keypad and BK4819 still to do). A screenshot of the emulated LCD is written to build/emu-screen.png.

Memory map (as the app uses it):
  0x00000000..0x0003FFFF  RAM (code copied to 0 at start-up, stack top 0xD910, .bss from 0xD960)
  0x00800000..            second RAM window (.data, empty in v0.27)
  0x001E0000..0x001FFFFF  peripheral registers (SFR)
  0x01E00000..            flash cache window: the decrypted app area (app.bin at +0x100)
  0x02000000..            mask ROM (routines emulated in ROM_HLE)
"""
import argparse
import os
import struct
import sys
import time
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

M = 0xFFFFFFFF
FLASH_BASE = 0x01E00000
ROM_BASE = 0x02000000
SFR_LO, SFR_HI = 0x1E0000, 0x200000
NOR_WINDOW = 0x05000000   # emulator-only window onto the raw internal flash (what ROM 0x020004AC maps to here)


class Bus:
    """The memory map. read/write take any address and length (the CPU core works in bytes and words)."""

    def __init__(self, area, periph, nor=None):
        self.ram = bytearray(0x40000)
        self.ram2 = bytearray(0x10000)
        self.flash = bytearray(area)
        self.nor = nor if nor is not None else bytearray(b'\xFF' * 0x100000)   # internal flash, raw
        self.periph = periph
        self.unmapped = Counter()

    def _region(self, a):
        if a < len(self.ram):
            return self.ram, a
        if 0x800000 <= a < 0x800000 + len(self.ram2):
            return self.ram2, a - 0x800000
        if FLASH_BASE <= a < FLASH_BASE + len(self.flash):
            return self.flash, a - FLASH_BASE
        if NOR_WINDOW <= a < NOR_WINDOW + len(self.nor):
            return self.nor, a - NOR_WINDOW
        return None, a

    def read(self, a, n):
        a &= M
        if SFR_LO <= a < SFR_HI or 0x100000 <= a < 0x110000:
            return self.periph.read(a, n)
        buf, o = self._region(a)
        if buf is not None and o + n <= len(buf):
            return bytes(buf[o:o + n])
        self.unmapped['r%08x' % a] += 1
        return bytes(n)

    def write(self, a, data):
        a &= M
        if SFR_LO <= a < SFR_HI or 0x100000 <= a < 0x110000:
            return self.periph.write(a, bytes(data))
        buf, o = self._region(a)
        if buf is not None and buf is not self.flash and o + len(data) <= len(buf):
            buf[o:o + len(data)] = data
            return
        self.unmapped['w%08x' % a] += 1


class Peripherals:
    """Register file for the SFR space with per-register hooks. Unknown registers read back what was written."""

    def __init__(self, log):
        self.regs = defaultdict(int)
        self.log = log
        self.read_hooks = {}
        self.write_hooks = {}
        self.touched = Counter()
        self.watch = None
        self.watched = 0
        self.cpu = None

    def read(self, a, n):
        base = a & ~3
        self.touched['r%06x' % base] += 1
        v = self.read_hooks[base](base) if base in self.read_hooks else self.regs[base]
        b = struct.pack('<I', v & M)
        sh = a - base
        return (b + b)[sh:sh + n]

    def write(self, a, data):
        base = a & ~3
        sh = a - base
        old = struct.pack('<I', self.regs[base] & M)
        new = bytearray(old)
        new[sh:sh + len(data)] = data[:4 - sh]
        v = struct.unpack('<I', bytes(new))[0]
        self.touched['w%06x' % base] += 1
        if self.watch and self.watch[0] <= base < self.watch[1] and self.watched < 60:
            self.watched += 1
            self.log('  w %06x = %08x  pc %x' % (base, v, self.cpu.pc if self.cpu else 0))
        self.regs[base] = v
        if base in self.write_hooks:
            self.write_hooks[base](base, v)


class Rom:
    """High-level stand-ins for the mask-ROM routines the app calls. Each takes the CPU, returns r0 (or None)."""

    LIBC = {  # cpu/br23/maskrom_stubs.ld of the JieLi br23 SDK, at ROM 0x02000000 + its address
        0x021127A4: 'memmem', 0x021127A8: 'memcpy', 0x021127AC: 'memmove', 0x021127B0: 'memcmp',
        0x021127B4: 'memset', 0x021127B8: 'strcmp', 0x021127BC: 'strcpy', 0x021127C0: 'strlen',
        0x021127C4: 'strncmp', 0x021127C8: 'strstr', 0x021127CC: 'flush_dcache', 0x021127D0: 'flushinv_dcache',
        0x021127D4: 'sfc_suspend', 0x021127D8: 'sfc_resume', 0x021127E0: 'chip_crc16',
        0x021127F0: 'nvram_set_boot_state',
    }
    NAMES = {  # worked out from the call sites (docs/EMULATOR.md)
        0x02000058: 'p33_rx', 0x020000A0: 'p33_tx', 0x02000156: 'p33_or', 0x0200084A: 'p33_and',
        0x020012BE: 'irq_save', 0x020012DA: 'irq_restore', 0x02000040: 'udelay?',
        # internal-flash driver (docs/UPDATE-MODE.md): called by sub_01E421F6 read, sub_01E4256C write,
        # sub_01E42264 ioctl (erase 0xC8/0xC9/0xCC, JEDEC id)
        0x0200048C: 'sfc_read', 0x020004AC: 'sfc_map', 0x02000772: 'sfc_program', 0x020006BC: 'sfc_erase',
        0x02000526: 'sfc_read_id',
        # RTOS semaphore calls the flash driver brackets itself with (single task here: always free)
        0x02002824: 'sem_pend', 0x0200288E: 'sem_post',
    }
    ERASE = {0xC9: 0x10000, 0xC8: 0x1000, 0xCC: 0x100}

    def __init__(self, bus, log):
        self.bus, self.log = bus, log
        self.p33 = defaultdict(int)
        self.calls = Counter()
        self.unknown = Counter()
        self.flash_log = []

    def name(self, a):
        return self.LIBC.get(a) or self.NAMES.get(a) or 'rom_%07x' % a

    def _cstr(self, a):
        out = bytearray()
        while len(out) < 4096:
            c = self.bus.read(a + len(out), 1)[0]
            if not c:
                break
            out.append(c)
        return bytes(out)

    def call(self, cpu):
        a = cpu.pc
        n = self.name(a)
        self.calls[n] += 1
        r0, r1, r2 = cpu.R[0], cpu.R[1], cpu.R[2]
        rd, wr = self.bus.read, self.bus.write
        ret = 0
        if n in ('memcpy', 'memmove'):
            wr(r0, rd(r1, r2)) if r2 else None
            ret = r0
        elif n == 'memset':
            if r2:
                wr(r0, bytes([r1 & 0xFF]) * r2)
            ret = r0
        elif n == 'memcmp':
            x, y = rd(r0, r2), rd(r1, r2)
            ret = 0 if x == y else (1 if x > y else M)
        elif n == 'strlen':
            ret = len(self._cstr(r0))
        elif n in ('strcmp', 'strncmp'):
            x, y = self._cstr(r0), self._cstr(r1)
            if n == 'strncmp':
                x, y = x[:r2], y[:r2]
            ret = 0 if x == y else (1 if x > y else M)
        elif n == 'strcpy':
            wr(r0, self._cstr(r1) + bytes(1))
            ret = r0
        elif n == 'p33_rx':
            ret = self.p33[r0 & 0xFF]
        elif n == 'p33_tx':
            self.p33[r0 & 0xFF] = r1 & 0xFF
        elif n == 'p33_or':
            self.p33[r0 & 0xFF] |= r1 & 0xFF
        elif n == 'p33_and':
            self.p33[r0 & 0xFF] &= r1 & 0xFF
        elif n == 'sfc_read':                       # (buf, addr, len)
            nor = self.bus.nor
            wr(r0, bytes(nor[r1 % len(nor):r1 % len(nor) + r2]))
            self.flash_log.append(('read', r1, r2))
            ret = r2
        elif n == 'sfc_map':                        # (addr) -> pointer to read it through
            ret = NOR_WINDOW + (r0 % len(self.bus.nor))
        elif n == 'sfc_program':                    # (buf, addr, len): NOR programming only clears bits
            nor = self.bus.nor
            data = rd(r0, r2)
            for i, b in enumerate(data):
                nor[(r1 + i) % len(nor)] &= b
            self.flash_log.append(('program', r1, r2))
        elif n == 'sfc_erase':                      # (cmd, addr)
            size = self.ERASE.get(r0 & 0xFF)
            if size:
                a0 = r1 & ~(size - 1)
                self.bus.nor[a0:a0 + size] = b'\xFF' * size
                self.flash_log.append(('erase', a0, size))
            else:
                self.log('ROM sfc_erase: unknown command %#x at %#x' % (r0, r1))
        elif n == 'sfc_read_id':                    # JEDEC id 20 40 14 kept as 0x204014: low byte 0x14 -> 1 MiB
            self.bus.write(0x198C0 + 353 * 4, struct.pack('<I', 0x00204014))
            ret = 0x00204014
        elif n in ('irq_save', 'irq_restore', 'udelay?', 'flush_dcache', 'flushinv_dcache', 'sfc_suspend',
                   'sfc_resume', 'nvram_set_boot_state', 'sem_pend', 'sem_post'):
            pass
        else:
            if not self.unknown[n]:
                self.log('ROM %s(%s) from %#x: not emulated, returns 0' % (
                    n, ', '.join('%#x' % r for r in cpu.R[:4]), cpu.rets))
            self.unknown[n] += 1
        cpu.R[0] = ret & M
        cpu.pc = cpu.rets
        cpu.steps += 1
        if cpu.after_then is not None:   # a call is always last in an if block
            cpu.check_then()


def add_timers(periph):
    """JieLi timers: CON (+0) bit 0..1 mode (0 off), bit 14 write 1 = clear pending, bit 15 pending; CNT (+4);
    PRD (+8). Calibration code starts one and waits for pending: here a running timer is pending at once."""
    def con_w(a, v):
        if v & 0x4000:
            v &= ~0xC000
        if v & 3:
            v |= 0x8000
        periph.regs[a] = v
    for base in range(0x1E0400, 0x1E0500, 0x10):
        periph.write_hooks[base] = con_w


def add_misc(periph):
    """Small blocks found while booting (docs/EMULATOR.md)."""
    state = {'lock': 0}

    def lock_w(a, v):                       # 0x101044: hardware lock, each 0xE7 written flips bit 0
        if v & 0xFF == 0xE7:
            state['lock'] ^= 1
        periph.regs[a] = state['lock']
    periph.write_hooks[0x101044] = lock_w
    periph.read_hooks[0x101044] = lambda a: state['lock']


class ST7735S:
    """The LCD controller: commands and RGB565 pixels over SPI, D/C from PR1 (P33). 132x162 RAM, 128x128 shown."""

    W, H = 132, 162

    def __init__(self):
        self.ram = [[0] * self.W for _ in range(self.H)]
        self.dc = 0
        self.cmd = None
        self.args = []
        self.x0 = self.y0 = 0
        self.x1, self.y1 = self.W - 1, self.H - 1
        self.x = self.y = 0
        self.pix = []
        self.frames = 0
        self.cmds = Counter()
        self.madctl = 0

    def byte(self, b):
        if self.dc == 0:
            self.cmd, self.args = b, []
            self.cmds['%02X' % b] += 1
            if b == 0x2C:
                self.x, self.y, self.pix = self.x0, self.y0, []
                self.frames += 1
            return
        if self.cmd == 0x2C:
            self.pix.append(b)
            if len(self.pix) == 2:
                c = self.pix[0] << 8 | self.pix[1]
                self.pix = []
                if 0 <= self.y < self.H and 0 <= self.x < self.W:
                    self.ram[self.y][self.x] = c
                self.x += 1
                if self.x > self.x1:
                    self.x = self.x0
                    self.y += 1
            return
        self.args.append(b)
        if self.cmd == 0x2A and len(self.args) == 4:
            self.x0, self.x1 = self.args[0] << 8 | self.args[1], self.args[2] << 8 | self.args[3]
        elif self.cmd == 0x2B and len(self.args) == 4:
            self.y0, self.y1 = self.args[0] << 8 | self.args[1], self.args[2] << 8 | self.args[3]
        elif self.cmd == 0x36 and len(self.args) == 1:
            self.madctl = b

    def png(self, path, crop=None, scale=3):
        import zlib
        x0, y0, w, h = crop or (0, 0, self.W, self.H)
        rows = []
        for y in range(y0, y0 + h):
            line = bytearray()
            for x in range(x0, x0 + w):
                c = self.ram[y][x]
                rgb = bytes(((c >> 11) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31))
                line += rgb * scale
            rows += [bytes(1) + bytes(line)] * scale
        raw = b''.join(rows)

        def chunk(t, d):
            return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & M)
        data = (bytes.fromhex('89504e470d0a1a0a') + chunk(b'IHDR', struct.pack('>IIBBBBB', w * scale, h * scale, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))
        open(path, 'wb').write(data)


PORT = {'A': 0x1E5000, 'B': 0x1E5040, 'C': 0x1E5080, 'D': 0x1E50C0}   # app table at 0x01E00664; OUT at +0


class SpiFlash:
    """The external SPI NOR flash on SPI1 (CS = PA3). Its contents are not in the .ufw: unknown bytes read 0xFF.
    Give a dump with --extflash. Every read is logged so the needed areas are known."""

    def __init__(self, image=None):
        self.mem = bytearray(image) if image else bytearray(b'\xFF' * 0x100000)
        self.cmd = []
        self.addr = None
        self.reads = Counter()

    def select(self):
        self.cmd, self.addr = [], None

    def tx(self, b):
        self.cmd.append(b)
        if len(self.cmd) == 4 and self.cmd[0] in (0x03, 0x0B):
            self.addr = self.cmd[1] << 16 | self.cmd[2] << 8 | self.cmd[3]

    def rx(self, n):
        if self.addr is None:
            return bytes(b'\xFF' * n)
        a = self.addr % len(self.mem)
        self.reads[a & ~0xFFF] += n
        out = bytes(self.mem[a:a + n]).ljust(n, b'\xFF')
        self.addr += n
        return out


def add_spi1(periph, bus, lcd, ext):
    """SPI1 (0x1E1D00): CON +0 (bit 15 done, bit 14 clears it, bit 12 receive), BAUD +4, BUF +8, DMA ADR +12,
    DMA CNT +16. Two chips share it: the LCD (CS = PB5) and the external flash (CS = PA3), both active low."""
    B = 0x1E1D00

    def target():
        if not periph.regs[PORT['A']] & (1 << 3):
            return ext
        if not periph.regs[PORT['B']] & (1 << 5):
            return lcd
        return None

    def pa_w(a, v):
        if v & 8 == 0 and periph.regs.get('pa3', 8):
            ext.select()
        periph.regs['pa3'] = v & 8
    periph.write_hooks[PORT['A']] = pa_w

    def send(data):
        t = target()
        if t is ext:
            for b in data:
                ext.tx(b)
        elif t is lcd:
            for b in data:
                lcd.byte(b)

    def con_w(a, v):
        if v & 0x4000:
            v &= ~0xC000
        periph.regs[a] = v

    def buf_w(a, v):
        if periph.regs[B] & 0x1000:
            periph.regs[a] = ext.rx(1)[0] if target() is ext else 0xFF
        else:
            send([v & 0xFF])
        periph.regs[B] |= 0x8000

    def cnt_w(a, v):
        n = v & 0xFFFF
        if periph.regs[B] & 0x1000:
            bus.write(periph.regs[B + 12], ext.rx(n) if target() is ext else b'\xFF' * n)
        else:
            send(bus.read(periph.regs[B + 12], n))
        periph.regs[B] |= 0x8000
    periph.write_hooks[B] = con_w
    periph.write_hooks[B + 8] = buf_w
    periph.write_hooks[B + 16] = cnt_w


def load_core(isa):
    sys.path.insert(0, os.path.join(isa, 'tools', 'llvm'))
    sys.path.insert(0, isa)
    import sim  # noqa: E402  pi32v2-isa's simulator
    return sim


def make_core(sim):
    """sim.CPU plus the special registers the core leaves to its host (sp, ssp, usp, icfg, reti, ...)."""

    class Core(sim.CPU):
        def __init__(self):
            super().__init__()
            self.sr = [0] * 16
            self.special = {
                'li.sr_u32': self._li_sr, 'mov.sr_r': self._mov_sr_r, 'mov.r_sr': self._mov_r_sr,
                # idle (wait for an interrupt): no interrupts are modelled yet, so it falls straight through
                'idle': lambda op: None,
            }

        def get_sr(self, s):
            return {3: self.rets, 5: self.flags, 11: self.icfg, 14: self.sp, 15: self.pc}.get(s, self.sr[s])

        def set_sr(self, s, v):
            v &= M
            if s == 3:
                self.rets = v & 0x07FFFFFE
            elif s == 5:
                self.flags = v & 0xF
            elif s == 11:
                self.icfg = v
            elif s == 14:
                self.sp = v
            elif s in (0, 1, 2):
                self.sr[s] = v & 0x07FFFFFE
            else:
                self.sr[s] = v

        def _li_sr(self, op):
            self.set_sr(op['s'], op['n'])

        def _mov_sr_r(self, op):
            self.set_sr(op['s'], self.R[op['r']])

        def _mov_r_sr(self, op):
            self.R[op['r']] = self.get_sr(op['s']) & M

        def _rep(self, insn, raw, size):
            """rep / rep.i: run the next `body` bytes `count` times. The core's own model is a test pattern
            (it only bumps r7), so the loop is run here. The register form runs at most 32 times and counts its
            register down by the runs (hw/fm1-2026-11-15-excprobe); code loops back while it is non-zero."""
            op = sim.check.operands(insn, raw)
            if insn.id == 'rep':
                count, body = min(self.R[op['r']], 32), op['n']
                self.R[op['r']] = (self.R[op['r']] - count) & M
            else:
                count, body = op['n'], op['b']
            start = self.pc + size
            end = start + body
            if self.after_then is not None:
                self.after_then[0] -= 1
            self.steps += 1
            for _ in range(count):
                self.pc = start
                while self.pc != end:
                    super().step()
                    if not (start <= self.pc <= end):
                        raise RuntimeError('%#x: jump out of a rep body' % self.pc)
            self.pc = end
            self.seq = end
            self.check_then()

        def step(self):
            insn, raw, size = self.decode(self.pc)
            if insn.id in ('rep', 'rep.i'):
                return self._rep(insn, raw, size)
            h = self.special.get(insn.id)
            if h is None:
                return super().step()
            self.seq = self.pc + size
            if self.after_then is not None:
                self.after_then[0] -= 1
            h(sim.check.operands(insn, raw))
            self.pc += size
            self.steps += 1
            self.check_then()

    return Core()


def decrypt_area(app_bin):
    """The flash window as the CPU sees it: app_area header (not needed by the app) and app.bin at +0x100."""
    return b'\xFF' * 0x100 + app_bin


class Machine:
    """CPU + memory map + peripherals + ROM stand-ins, ready to run from the app's start."""

    def __init__(self, isa, app_path, extflash=None, nor=None, log=print):
        self.sim = load_core(isa)
        self.log = log
        self.periph = periph = Peripherals(log)
        self.lcd = lcd = ST7735S()
        add_timers(periph)
        add_misc(periph)
        self.bus = bus = Bus(decrypt_area(open(app_path, 'rb').read()), periph, nor)
        self.ext = SpiFlash(open(extflash, 'rb').read() if extflash else None)
        add_spi1(periph, bus, lcd, self.ext)
        self.cpu = cpu = make_core(self.sim)
        # PC observers: app functions whose arguments the board model needs (they still run normally)
        self.observe = {
            0x01E5714E: lambda c: setattr(lcd, 'dc', c.R[1] & 1) if c.R[0] == 1 else None,   # P33_SetPrBit
        }
        self.rom = Rom(bus, log)
        periph.cpu = cpu
        cpu.mem = bus
        cpu.pc = FLASH_BASE + 0x100
        cpu.sp = 0

    def step(self):
        cpu = self.cpu
        if cpu.pc >= ROM_BASE:
            self.rom.call(cpu)
            return
        ob = self.observe.get(cpu.pc)
        if ob:
            ob(cpu)
        cpu.step()

    def call(self, addr, args, limit=20_000_000, stack=None):
        """Call an app function with r0..r3 = args (more go on the stack); returns r0."""
        cpu = self.cpu
        RET = 0x01FFFFF0
        if stack is not None:
            cpu.sp = stack
        regs, extra = list(args[:4]), list(args[4:])
        cpu.sp -= 4 * len(extra)
        for k, v in enumerate(extra):
            self.bus.write(cpu.sp + 4 * k, struct.pack('<I', v & M))
        for k, v in enumerate(regs):
            cpu.R[k] = v & M
        cpu.rets = RET
        cpu.pc = addr
        start = cpu.steps
        while cpu.pc != RET:
            self.step()
            if cpu.steps - start > limit:
                raise RuntimeError('call %#x: too many steps (pc %#x)' % (addr, cpu.pc))
        cpu.sp += 4 * len(extra)
        return cpu.R[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--isa', default=os.path.join(os.path.dirname(ROOT), 'pi32v2-isa'))
    ap.add_argument('--app', default=os.path.join(ROOT, 'build', 'stock', 'unpacked', 'app.bin'))
    ap.add_argument('--steps', type=int, default=200000)
    ap.add_argument('--extflash', help='dump of the external SPI flash, if you have one')
    ap.add_argument('--watch', help='LO-HI: print writes to these SFR addresses (hex)')
    ap.add_argument('--trace', type=int, default=0, help='print the last N pcs on a stop')
    a = ap.parse_args()

    m = Machine(a.isa, a.app, a.extflash)
    cpu, bus, rom, periph, lcd, ext, observe = m.cpu, m.bus, m.rom, m.periph, m.lcd, m.ext, m.observe
    log = m.log
    if a.watch:
        periph.watch = [int(x, 16) for x in a.watch.split('-')]
    hist = []
    t0 = time.time()
    err = None
    last_window_min = None
    try:
        while cpu.steps < a.steps:
            if a.trace:
                hist.append(cpu.pc)
                if len(hist) > a.trace:
                    hist.pop(0)
            if cpu.pc >= ROM_BASE:
                rom.call(cpu)
                continue
            ob = observe.get(cpu.pc)
            if ob:
                ob(cpu)
            cpu.step()
            if cpu.steps & 0xFFFFF == 0:
                window = set()
                for _ in range(2000):
                    window.add(cpu.pc)
                    if cpu.pc >= ROM_BASE:
                        rom.call(cpu)
                    else:
                        cpu.step()
                if len(window) < 12 and min(window) == last_window_min:
                    raise RuntimeError('stuck in a loop at %s' % ' '.join('%x' % p for p in sorted(window)))
                last_window_min = min(window)
    except Exception as e:  # noqa: BLE001
        err = e
    dt = time.time() - t0
    print('%d instructions in %.1f s (%.0f/s), pc=%#x' % (cpu.steps, dt, cpu.steps / max(dt, 1e-9), cpu.pc))
    if err:
        print('STOP:', repr(err))
    if hist:
        print('last pcs:', ' '.join('%x' % p for p in hist))
    print('SFR touched:', ', '.join('%s:%d' % kv for kv in sorted(periph.touched.items())[:80]))
    print('LCD: %d RAMWR, commands %s, MADCTL %02X' % (lcd.frames, dict(lcd.cmds), lcd.madctl))
    shot = os.path.join(ROOT, 'build', 'emu-screen.png')
    lcd.png(shot)
    print('screen saved to', shot)
    print('external flash reads (4 KiB pages):', ', '.join('%x:%d' % kv for kv in sorted(ext.reads.items())))
    print('stack:', ' '.join('%x' % int.from_bytes(bus.read(cpu.sp + 4 * k, 4), 'little') for k in range(24)))
    print('ROM calls:', rom.calls.most_common(30))
    if bus.unmapped:
        print('unmapped:', bus.unmapped.most_common(20))


if __name__ == '__main__':
    main()
