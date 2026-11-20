#!/usr/bin/env python3
"""Lift the stock app (pi32v2 machine code) to compilable C, one C function per stock function.

    python tools/lift/lift.py [--variant nrf|nrfb] [--isa E:/pi32v2-isa] [--out build/lift]

--variant nrfb lifts the NRFB (firmware V0.04) app instead: build/stock-nrfb/unpacked/app.bin and build/nrfb/app.dis
to build/nrfb/lift/ (unpack it with tools/setup.py, disassemble it with tools/disasm.py, see docs/LIFTER.md).

The output is "register-level" C: every instruction becomes one C statement on a CPU state (S.r[0..15], S.sp,
S.rets) and memory accessors (rd8/rd16/rd32, wr8/wr16/wr32). It is ugly but it is the whole firmware, it compiles,
and each function can be checked against the stock one (tools/lift/insn_test.py per instruction,
tools/lift/func_test.py per function, running the stock code in the emulator). Cleaning a function up into
readable C is then a refactor under a test that runs in milliseconds.

The generated C is derived from the Baofeng firmware: it goes to build/ and is never committed.

How instructions map (pi32v2, as the ISA's models define them; tools/lift/insn_test.py checks every form):
  * the text form of each instruction (pi32v2isa.codec.format) is already close to C; this file parses it
  * `A  #` then B is a parallel pair: B reads the registers as they were before A, A's result wins
  * `if (c) {` opens a block of t instructions, then an else part of e instructions (a pair counts as one)
  * `rep n rX {` runs the next n bytes min(rX, 32) times and counts rX down; `rep b n {` runs them n times
  * tbb/tbh jump to the end of the instruction + 2 * a table entry
"""
import argparse
import hashlib
import json
import os
import pickle
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from special import SPECIAL  # noqa: E402

FLASH_BASE = 0x01E00000
M = 0xFFFFFFFF

SR_LIST = 'reti rete retx rets sr4 psr cnum uspn sspn icfgn icfgs icfg usp ssp sp pc'.split()
SR_NAMES = {n: 'S.sr[%d]' % k for k, n in enumerate(SR_LIST) if n != 'pc'}
SR_NAMES.update({'sp': 'S.sp', 'rets': 'S.rets', 'icfg': 'S.icfg', 'psr': 'S.flags'})


class Unimpl(Exception):
    pass


def load_isa(isa):
    sys.path.insert(0, os.path.join(isa, 'tools', 'llvm'))
    sys.path.insert(0, os.path.join(isa, 'tools', 'hwtest'))
    sys.path.insert(0, isa)
    from pi32v2isa import codec, table  # noqa: E402
    import check  # noqa: E402
    return codec, table, check


def h32(v):
    return '0x%08Xu' % (v & M)


# ---------------------------------------------------------------- operand expressions

REG = r'r(\d+)'


def num(s):
    s = s.strip()
    return int(s, 16) if re.fullmatch(r'-?0x[0-9a-fA-F]+', s) else int(s)


def reg(n):
    return 'S.r[%d]' % int(n)


def pair_get(hi, lo):
    return '(((uint64_t)S.r[%d] << 32) | S.r[%d])' % (int(hi), int(lo))


def val(tok):
    """A register, half, pair, special register or number, as a C rvalue (uint32_t unless a pair)."""
    t = tok.strip()
    m = re.fullmatch(r'r(\d+)_r(\d+)', t)
    if m:
        return pair_get(*m.groups())
    m = re.fullmatch(r'r(\d+)\.(l|h|b0|b1|b2|b3)', t)
    if m:
        r, part = m.groups()
        sh = {'l': 0, 'h': 16, 'b0': 0, 'b1': 8, 'b2': 16, 'b3': 24}[part]
        mask = 0xFFFF if part in 'lh' else 0xFF
        return '((S.r[%s] >> %d) & 0x%X)' % (r, sh, mask)
    m = re.fullmatch(REG, t)
    if m:
        return reg(m.group(1))
    if t in SR_NAMES:
        return SR_NAMES[t]
    if re.fullmatch(r'-?(0x[0-9a-fA-F]+|\d+)', t):
        return h32(num(t))
    raise Unimpl('operand ' + t)


def address(inner):
    """[...] contents -> (address expression, code before the access, code after it)."""
    s = inner.replace(' ', '')
    if s == 'sp':
        return 'S.sp', '', ''
    m = re.fullmatch(r'sp\+?(-?\d+)', s)
    if m:
        return '(S.sp + %s)' % h32(int(m.group(1))), '', ''
    m = re.fullmatch(r'r(\d+)\+(-?(?:0x[0-9a-fA-F]+|\d+))', s)
    if m:
        return '(%s + %s)' % (reg(m.group(1)), h32(num(m.group(2)))), '', ''
    m = re.fullmatch(r'r(\d+)\+r(\d+)<<(\d+)', s)
    if m:
        return '(%s + (%s << %s))' % (reg(m.group(1)), reg(m.group(2)), m.group(3)), '', ''
    m = re.fullmatch(r'r(\d+)\+r(\d+)', s)
    if m:
        return '(%s + %s)' % (reg(m.group(1)), reg(m.group(2))), '', ''
    m = re.fullmatch(r'r(\d+)\+\+=(r\d+|-?\d+)', s)            # post-increment
    if m:
        b = reg(m.group(1))
        return '_a', '_a = %s;' % b, '%s = %s + %s;' % (b, b, val(m.group(2)))
    m = re.fullmatch(r'\+\+r(\d+)=(r\d+|-?\d+)', s)            # pre-increment
    if m:
        b = reg(m.group(1))
        return '_a', '%s = %s + %s; _a = %s;' % (b, b, val(m.group(2)), b), ''
    raise Unimpl('address [' + inner + ']')


def load(width, inner, ext):
    a, pre, post = address(inner)
    if width == 'd':
        raise Unimpl('64-bit load as value')
    fn = {'b': 'rd8', 'h': 'rd16', '': 'rd32'}[width]
    e = '%s(%s)' % (fn, a)
    if ext == 's':
        e = {'b': '(uint32_t)(int32_t)(int8_t)', 'h': '(uint32_t)(int32_t)(int16_t)'}[width] + e
    return e, pre, post


MEMRX = re.compile(r'^(b|h|d)?\[(.+)\](?: \((u|s)\))?$')


def lhs_info(lhs):
    """-> (kind, data): 'reg' n, 'pair' (hi, lo), 'half' (n, part), 'sr' name, 'mem' (width, inner)."""
    l = lhs.strip()
    m = re.fullmatch(r'r(\d+)_r(\d+)', l)
    if m:
        return 'pair', (int(m.group(1)), int(m.group(2)))
    m = re.fullmatch(r'r(\d+)\.(l|h|b0|b1|b2|b3)', l)
    if m:
        return 'half', (int(m.group(1)), m.group(2))
    m = re.fullmatch(REG, l)
    if m:
        return 'reg', int(m.group(1))
    if l in SR_NAMES:
        return 'sr', l
    m = MEMRX.match(l)
    if m:
        return 'mem', (m.group(1) or '', m.group(2))
    raise Unimpl('lhs ' + l)


def s32(e):
    return '(int32_t)(%s)' % e


BINOPS = ['>>>', '<<<', '<<', '>>', '+', '-', '*', '/', '&', '|', '^']


def rhs_expr(r, lpair=False):
    """Right-hand side -> (C expression, pre code, post code). lpair: the destination is a register pair."""
    r = r.strip()
    suf = None
    m = re.fullmatch(r'(.*?) \((u|s|f|trunc)\)', r)
    if m and not MEMRX.match(r):
        r, suf = m.group(1).strip(), m.group(2)
    m = MEMRX.match(r if suf is None else r + ' (%s)' % suf)
    if m and m.group(1) != 'd':
        return load(m.group(1) or '', m.group(2), m.group(3))
    m = re.fullmatch(r'(r\d+)\.(b0|l) ', r + ' ')
    if m and suf in ('u', 's'):
        n = re.fullmatch(REG, m.group(1)).group(1)
        cast = {('b0', 'u'): '(uint8_t)', ('b0', 's'): '(uint32_t)(int32_t)(int8_t)',
                ('l', 'u'): '(uint16_t)', ('l', 's'): '(uint32_t)(int32_t)(int16_t)'}[(m.group(2), suf)]
        return '%s%s' % (cast, reg(n)), '', ''
    m = re.fullmatch(r'(u|s)extra\((r\d+), p:(\d+), l:(\d+)\)', r)
    if m:
        k, s, p, n = m.group(1), val(m.group(2)), int(m.group(3)), int(m.group(4))
        if k == 'u':
            return '((%s >> %d) & %s)' % (s, p, h32((1 << n) - 1 if n < 32 else M)), '', ''
        return '(uint32_t)((int32_t)(%s << %d) >> %d)' % (s, 32 - p - n, 32 - n), '', ''
    m = re.fullmatch(r'(umin|smin|umax|smax)\((r\d+), (r\d+)\)', r)
    if m:
        f, a, b = m.groups()
        a, b = val(a), val(b)
        if f[0] == 's':
            a, b = s32(a), s32(b)
        op = '<' if 'min' in f else '>'
        return '(uint32_t)((%s %s %s) ? %s : %s)' % (a, op, b, a, b), '', ''
    m = re.fullmatch(r'clz\((r\d+)\)', r)
    if m:
        return 'lift_clz(%s)' % val(m.group(1)), '', ''
    m = re.fullmatch(r'abs\((r\d+)\)', r)
    if m:
        return 'lift_abs(%s)' % val(m.group(1)), '', ''
    m = re.fullmatch(r'sat16\((r\d+)\)', r)
    if m and suf == 's':
        return 'lift_sat16(%s)' % val(m.group(1)), '', ''
    m = re.fullmatch(r'ftoi\((r\d+)\)', r)
    if m and suf == 'trunc':
        return 'lift_ftoi(%s)' % val(m.group(1)), '', ''
    m = re.fullmatch(r'itof\((r\d+)\)', r)
    if m:
        return 'lift_itof(%s)' % val(m.group(1)), '', ''
    m = re.fullmatch(r'~(\S+)', r)
    if m:
        return '(~%s)' % val(m.group(1)), '', ''
    m = re.fullmatch(r'(\S+) & ~\(1 << (\S+)\)', r)
    if m:
        return '(%s & ~(1u << (%s & 31)))' % (val(m.group(1)), val(m.group(2))), '', ''
    m = re.fullmatch(r'(\S+) & \(1 << (\S+)\)', r)
    if m:
        return '(%s & (1u << (%s & 31)))' % (val(m.group(1)), val(m.group(2))), '', ''
    m = re.fullmatch(r'(\S+) & ~(\S+)', r)
    if m:
        return '(%s & ~%s)' % (val(m.group(1)), val(m.group(2))), '', ''
    for op in BINOPS:
        parts = r.split(' %s ' % op)
        if len(parts) == 2:
            return binop(parts[0], op, parts[1], suf, lpair), '', ''
    if suf is None:
        return val(r), '', ''
    raise Unimpl('rhs ' + r + ' (%s)' % suf)


def binop(a, op, b, suf, lpair=False):
    A, B = val(a), val(b)
    apair = '_r' in a
    if suf == 'f':
        f = {'+': 'add', '-': 'sub', '*': 'mul', '/': 'div'}[op]
        return 'lift_f%s(%s, %s)' % (f, A, B)
    if op == '/' and lpair and apair and suf in ('s', 'u'):
        return 'lift_div64%s(%s, %s)' % (suf, A, B)
    if op == '*' and lpair:
        if suf == 's':
            return '(uint64_t)((int64_t)(int32_t)%s * (int64_t)(int32_t)%s)' % (A, B)
        if suf == 'u':
            return '((uint64_t)%s * (uint64_t)%s)' % (A, B)
        raise Unimpl('pair multiply')
    if apair:
        if op == '>>' and suf == 's':
            return 'lift_sar64sat(%s, %s)' % (A, B)
        raise Unimpl('pair op ' + op)
    if op == '/':
        if suf == 's':
            return 'lift_divs(%s, %s)' % (A, B)
        return 'lift_divu(%s, %s)' % (A, B)
    if suf not in (None,):
        raise Unimpl('suffix ' + str(suf))
    if op == '>>>':
        return 'lift_asr(%s, %s)' % (A, B)
    if op == '<<<':
        return 'lift_shl3(%s, %s)' % (A, B)
    if op in ('<<', '>>'):
        return 'lift_%s(%s, %s)' % ('shl' if op == '<<' else 'shr', A, B)
    return '(uint32_t)(%s %s %s)' % (A, op, B)


def assign(lhs, value, lpair_ok=False):
    kind, d = lhs_info(lhs)
    if kind == 'reg':
        return '%s = %s;' % (reg(d), value)
    if kind == 'sr':
        return '%s = %s;' % (SR_NAMES[d], value)
    if kind == 'half':
        n, part = d
        sh = {'l': 0, 'h': 16, 'b0': 0, 'b1': 8, 'b2': 16, 'b3': 24}[part]
        mask = (0xFFFF if part in 'lh' else 0xFF) << sh
        return '%s = (%s & %s) | (((uint32_t)(%s) << %d) & %s);' % (reg(n), reg(n), h32(~mask), value, sh,
                                                                     h32(mask))
    if kind == 'pair':
        hi, lo = d
        return '{ uint64_t _p = %s; S.r[%d] = (uint32_t)_p; S.r[%d] = (uint32_t)(_p >> 32); }' % (value, lo, hi)
    raise Unimpl('assign to ' + lhs)


def dest_regs(lhs):
    kind, d = lhs_info(lhs)
    if kind in ('reg', 'half'):
        return [d if kind == 'reg' else d[0]]
    if kind == 'pair':
        return list(d)
    return []


# ---------------------------------------------------------------- instructions

class Unit:
    """One instruction (or a parallel pair) in C: data effect `code`, then control `kind`."""

    def __init__(self, pc, size, text):
        self.pc, self.size, self.text = pc, size, text
        self.code = ''          # C statements for the data effect
        self.cond = None        # C condition (branches, if-blocks)
        self.kind = 'op'        # op, br, goto, call, callr, gotor, ret, if, rep, tbb, unimpl
        self.target = None
        self.t = self.e = 0
        self.rep = None         # (count expression, body bytes)
        self.dests = []
        self.mask = M


REGLIST_ITEM = re.compile(r'r(\d+)-r(\d+)|r(\d+)|(rets|pc|psr|reti|retx|rete|icfg|sr\d+)')


def reglist(s):
    """'{rets, r5, r4}' -> sorted slot list: GPRs by number, then specials (rets, pc...)."""
    regs, specials = set(), []
    for m in REGLIST_ITEM.finditer(s):
        if m.group(1):
            a, b = int(m.group(1)), int(m.group(2))
            regs.update(range(min(a, b), max(a, b) + 1))
        elif m.group(3):
            regs.add(int(m.group(3)))
        else:
            specials.append(m.group(4))
    return sorted(regs), specials


def push_code(s):
    regs, sp = reglist(s)
    if any(x not in ('rets',) for x in sp):
        raise Unimpl('push of ' + ','.join(sp))
    slots = [reg(r) for r in regs] + ['S.rets' for _ in sp]
    code = 'S.sp -= %d;' % (4 * len(slots))
    for k, v in enumerate(slots):
        code += ' wr32(S.sp + %d, %s);' % (4 * k, v)
    return code


def pop_code(s):
    """-> (code, returns)"""
    regs, sp = reglist(s)
    if any(x not in ('rets', 'pc') for x in sp):
        raise Unimpl('pop of ' + ','.join(sp))
    code = ''
    k = 0
    for r in regs:
        code += ' %s = rd32(S.sp + %d);' % (reg(r), 4 * k)
        k += 1
    ret = False
    for x in sp:
        if x == 'rets':
            code += ' S.rets = rd32(S.sp + %d) & 0x07FFFFFEu;' % (4 * k)
        else:
            ret = True
        k += 1
    code += ' S.sp += %d;' % (4 * k)
    return code.strip(), ret


COND = re.compile(r'^(ifs|if|iff) \((.*)\) (goto (-?\d+)|\{)$')


def cond_c(kw, c):
    if kw == 'iff':
        raise Unimpl('float branch')
    m = re.fullmatch(r'--r(\d+) != 0', c)
    if m:
        return '(--%s != 0)' % reg(m.group(1))
    m = re.fullmatch(r'\((\S+) & (\S+)\) (==|!=) 0', c)
    if m:
        return '((%s & %s) %s 0)' % (val(m.group(1)), val(m.group(2)), m.group(3))
    m = re.fullmatch(r'(\S+) (==|!=|<=|>=|<|>) (\S+)', c)
    if m:
        a, op, b = val(m.group(1)), m.group(2), val(m.group(3))
        if kw == 'ifs':
            a, b = s32(a), s32(b)
        return '(%s %s %s)' % (a, op, b)
    raise Unimpl('condition ' + c)


NOPS = {'nop', 'ssync', 'csync', 'btbclr', 'idle', 'sevl', 'wfe', 'lockset', 'lockclr', 'bkpt', 'hbkpt'}


def translate(text, pc, size, op, fid=None):
    """codec.format text of one instruction -> Unit."""
    u = Unit(pc, size, text)
    h = SPECIAL.get(fid) or (SPECIAL.get(fid[:-2]) if fid and fid.endswith('.p') else None)
    if h is not None:
        got = h(op, text)
        if got is not None:
            u.kind, u.code, u.cond = got
            if u.kind == 'if':
                u.t, u.e = op['t'], op['e']
            elif u.kind == 'br':
                u.target = pc + size + op['n']
            mm = re.match(r'^(r\d+)(?:_(r\d+))? ', text)
            if mm:
                u.dests = [int(x[1:]) for x in mm.groups() if x]
            return u
    t = re.sub(r'\s+#$', '', text.split('   ;')[0].strip()).strip()
    end = pc + size
    w = t.split(' ', 1)[0]
    if t in NOPS or w in ('pfetch', 'iflush', 'flush', 'flushinv', 'flushinv'):
        return u
    if t == 'rts':
        u.kind = 'ret'
        return u
    if t in ('rte', 'rti', 'rtss', 'syscall') or w in ('swi', 'testset'):
        raise Unimpl(t)
    if w in ('cli', 'sti'):
        if t == 'cli':
            u.code = 'S.icfg &= ~0x200u;'
        elif t == 'sti':
            u.code = 'S.icfg |= 0x200u;'
        else:
            r = val(t.split()[1])
            u.code = ('%s = S.icfg; S.icfg &= ~0x200u;' % r) if w == 'cli' else \
                ('S.icfg = (S.icfg & ~0x200u) | (%s & 0x200u);' % r)
        return u
    m = re.fullmatch(r'goto (-?\d+)', t)
    if m:
        u.kind, u.target = 'goto', end + int(m.group(1))
        return u
    m = re.fullmatch(r'(call|callns) (-?\d+)', t)
    if m:
        u.kind, u.target = 'call', end + int(m.group(2))
        return u
    m = re.fullmatch(r'(call|callns|goto) (r\d+)', t)
    if m:
        u.kind = 'gotor' if m.group(1) == 'goto' else 'callr'
        u.cond = val(m.group(2))
        return u
    m = re.fullmatch(r'(tbb|tbh) \[(r\d+)\]', t)
    if m:
        u.kind = 'tbb'
        f = 'rd8' if m.group(1) == 'tbb' else 'rd16'
        u.cond = '(%s + 2u * %s(%s + %s))' % (h32(end), f, h32(end), val(m.group(2)))
        return u
    m = COND.match(t)
    if m:
        u.cond = cond_c(m.group(1), m.group(2))
        if m.group(4) is not None:
            u.kind, u.target = 'br', end + int(m.group(4))
        else:
            u.kind, u.t, u.e = 'if', op['t'], op['e']
        return u
    m = re.fullmatch(r'rep (\d+) (r\d+) \{', t)
    if m:
        r = val(m.group(2))
        u.kind = 'rep'
        u.code = '_rc = %s < 32u ? %s : 32u; %s -= _rc;' % (r, r, r)
        u.rep = int(m.group(1))
        return u
    m = re.fullmatch(r'rep (\d+) (\d+) \{', t)
    if m:
        u.kind = 'rep'
        u.code = '_rc = %su;' % int(m.group(2))
        u.rep = int(m.group(1))
        return u
    m = re.fullmatch(r'\[--sp\] = (\{.*\}|rets)', t)
    if m:
        u.code = push_code(m.group(1))
        return u
    m = re.fullmatch(r'(\{.*\}|pc|rets) = \[sp\+\+\]', t)
    if m:
        u.code, ret = pop_code(m.group(1))
        if ret:
            u.kind = 'ret'
        return u
    m = re.fullmatch(r'(\{.*\}) = \[(--|-|)r(\d+)(\+\+|\+|)\]', t) or         re.fullmatch(r'\[(--|-|)r(\d+)(\+\+|\+|)\] = (\{.*\})', t)
    if m:
        # register-list load/store (the ISA model): lowest register at the lowest address; [r+] at r, [r++] at r
        # and base += 4n, [-r] at r - 4n, [--r] at r - 4n and base -= 4n; a load into the base keeps the loaded value
        store = t.startswith('[')
        lst, pre, base, post = (m.group(4), m.group(1), int(m.group(2)), m.group(3)) if store else             (m.group(1), m.group(2), int(m.group(3)), m.group(4))
        regs, sp = reglist(lst)
        if sp:
            raise Unimpl(t)
        n = len(regs)
        b = reg(base)
        code = '{ uint32_t _b = %s, _s = %s;' % (b, '_b - %du' % (4 * n) if pre else '_b')
        for k, r in enumerate(regs):
            code += (' wr32(_s + %du, %s);' % (4 * k, reg(r))) if store else (' %s = rd32(_s + %du);' % (reg(r), 4 * k))
        if pre == '--' or post == '++':
            if store or base not in regs:
                code += ' %s = _b %s %du;' % (b, '-' if pre else '+', 4 * n)
        u.code = code + ' }'
        u.dests = regs + [base] if not store else [base]
        return u
    m = re.fullmatch(r'sp \+= (-?\d+)', t)
    if m:
        u.code = 'S.sp += %s;' % h32(int(m.group(1)))
        return u
    m = re.fullmatch(r'(\S+) = sp \+ (-?\d+)', t)
    if m:
        u.code = assign(m.group(1), '(S.sp + %s)' % h32(int(m.group(2))))
        u.dests = dest_regs(m.group(1))
        return u
    m = re.fullmatch(r'(r\d+) <= insert\((r\d+), p:(\d+), l:(\d+)\)', t)
    if m:
        d, s, p, n = val(m.group(1)), val(m.group(2)), int(m.group(3)), int(m.group(4))
        mask = ((1 << n) - 1) << p & M
        u.code = '%s = (%s & %s) | ((%s << %d) & %s);' % (d, d, h32(~mask), s, p, h32(mask))
        u.dests = dest_regs(m.group(1))
        return u
    # assignments
    m = re.fullmatch(r'(.+?) (=|\+=|-=|\*=|&=|\|=|\^=|<<=|>>=|>>>=|<<<=) (.+)', t)
    if not m:
        raise Unimpl(t)
    lhs, aop, rhs = m.groups()
    kind, d = lhs_info(lhs)
    if kind == 'mem':
        width, inner = d
        a, pre, post = address(inner)
        if width == 'd':
            if aop != '=':
                raise Unimpl(t)
            pm = re.fullmatch(r'r(\d+)_r(\d+)', rhs.strip())
            if not pm:
                raise Unimpl(t)
            u.code = '%s wr32(%s, S.r[%s]); wr32(%s + 4u, S.r[%s]); %s' % (pre, a, pm.group(2), a, pm.group(1), post)
            return u
        fn = {'b': 'wr8', 'h': 'wr16', '': 'wr32'}[width]
        rd = {'b': 'rd8', 'h': 'rd16', '': 'rd32'}[width]
        v = val(rhs) if not rhs.startswith(('~', '1 <<', '~(')) else None
        if aop == '=':
            u.code = '{ uint32_t _v = %s; %s %s(%s, _v); %s }' % (val(rhs), pre, fn, a, post)
        else:
            op = aop[:-1]
            r = rhs.strip()
            mm = re.fullmatch(r'~\(1 << (r\d+)\)', r)
            if mm:
                v = '~(1u << (%s & 31))' % val(mm.group(1))
            elif re.fullmatch(r'1 << (r\d+)', r):
                v = '(1u << (%s & 31))' % val(r.split('<<')[1])
            elif r.startswith('~'):
                v = '~%s' % val(r[1:])
            else:
                v = val(r)
            u.code = '%s { uint32_t _m = %s(%s); %s(%s, _m %s %s); } %s' % (pre, rd, a, fn, a, op, v, post)
        u.code = u.code.strip()
        return u
    # register destination
    pair = kind == 'pair'
    if aop == '=':
        if pair and rhs.strip() == '0':
            expr, pre, post = '0ull', '', ''
        elif pair and re.fullmatch(r'r\d+_r\d+', rhs.strip()):
            expr, pre, post = val(rhs), '', ''
        elif pair:
            mm = MEMRX.match(rhs.strip())
            if mm and mm.group(1) == 'd':
                a, pre, post = address(mm.group(2))
                u.code = '%s { uint32_t _lo = rd32(%s), _hi = rd32(%s + 4u); %s S.r[%d] = _lo; S.r[%d] = _hi; }' % (
                    pre, a, a, post, d[1], d[0])
                u.dests = list(d) + base_regs(mm.group(2))
                return u
            expr, pre, post = rhs_expr(rhs, lpair=True)
        else:
            if kind == 'half' and not re.fullmatch(r'-?(0x[0-9a-fA-F]+|\d+)', rhs.strip()):
                raise Unimpl('half arithmetic ' + t)
            expr, pre, post = rhs_expr(rhs)
        if post:      # a load with a post-increment: the loaded value is written last
            u.code = '%s { uint32_t _v = %s; %s %s }' % (pre, expr, post, assign(lhs, '_v'))
        else:
            u.code = '%s %s' % (pre, assign(lhs, expr))
        u.code = u.code.strip()
        mm = MEMRX.match(rhs.strip())
        u.dests = dest_regs(lhs) + (base_regs(mm.group(2)) if mm else [])
        return u
    op = aop[:-1]
    if kind == 'half':
        raise Unimpl('half arithmetic ' + t)
    if pair:
        if op in ('>>', '<<'):
            sh = val(rhs)
            u.code = assign(lhs, 'lift_%s64(%s, %s)' % ('shl' if op == '<<' else 'shr', val(lhs), sh))
            u.dests = dest_regs(lhs)
            return u
        raise Unimpl(t)
    expr = binop(lhs, op, rhs, None)
    u.code = assign(lhs, expr)
    u.dests = dest_regs(lhs)
    return u


def base_regs(inner):
    m = re.fullmatch(r'\+\+r(\d+)=.*|r(\d+)\+\+=.*', inner.replace(' ', ''))
    if m:
        return [int(m.group(1) or m.group(2))]
    return []


def carry_code(text):
    """C for an add/subtract that also sets the carry flag (bit 1 of S.flags) as the ISA does: add = carry out,
    subtract = no borrow. None if the text is not such a form."""
    t = re.sub(r'\s+#$', '', text.split('   ;')[0].strip())
    m = re.fullmatch(r'(r\d+) = (r\d+|-?\d+|0x[0-9A-Fa-f]+) ([+-]) (r\d+|-?\d+|0x[0-9A-Fa-f]+)', t)
    if m:
        d, a, op, b = m.groups()
    else:
        m = re.fullmatch(r'(r\d+) ([+-])= (r\d+|-?\d+|0x[0-9A-Fa-f]+)', t)
        if not m:
            return None
        d, op, b = m.groups()
        a = d
    A, B, D = val(a), val(b), val(d)
    if op == '+':
        return '{ uint32_t _x = %s, _y = %s; %s = _x + _y; S.flags = (S.flags & ~2u) | ((%s < _x) ? 2u : 0u); }' % (
            A, B, D, D)
    return '{ uint32_t _x = %s, _y = %s; %s = _x - _y; S.flags = (S.flags & ~2u) | ((_x >= _y) ? 2u : 0u); }' % (
        A, B, D)


HALF_MASK = {'l': 0xFFFF, 'h': 0xFFFF0000, 'b0': 0xFF, 'b1': 0xFF00, 'b2': 0xFF0000, 'b3': 0xFF000000}


def pair_units(a, b):
    """A (the `#` instruction) and B as one unit: B sees the registers before A; A's writes win."""
    if a.kind != 'op' or not a.dests and a.code:
        if a.kind != 'op':
            raise Unimpl('pair with a control instruction first')
    u = Unit(a.pc, a.size + b.size, a.text + ' | ' + b.text)
    for k in ('kind', 'cond', 'target', 't', 'e', 'rep'):
        setattr(u, k, getattr(b, k))
    acode = a.code
    temps = []
    for r in sorted(set(a.dests)):
        acode = acode.replace('S.r[%d]' % r, '_p%d' % r)
        temps.append(r)
    mh = re.match(r'^r(\d+)\.(l|h) ', a.text)      # as the ISA's pair model: only .l/.h merge
    mask = HALF_MASK[mh.group(2)] if mh else M
    decl = ' '.join('uint32_t _p%d = S.r[%d];' % (r, r) for r in temps)
    commit = ' '.join('S.r[%d] = (S.r[%d] & %s) | (_p%d & %s);' % (r, r, h32(~mask), r, h32(mask)) if mask != M
                      else 'S.r[%d] = _p%d;' % (r, r) for r in temps)
    if b.cond is not None and b.kind in ('br', 'if'):
        u.cond = '_pc'
        u.code = '{ %s %s int _pcv = %s; %s %s _pc = _pcv; }' % (decl, acode, b.cond, b.code, commit)
    elif b.kind in ('callr', 'gotor', 'tbb'):
        u.cond = '_pt'
        u.code = '{ %s %s uint32_t _ptv = %s; %s %s _pt = _ptv; }' % (decl, acode, b.cond, b.code, commit)
    else:
        u.code = '{ %s %s %s %s }' % (decl, acode, b.code, commit)
    u.dests = sorted(set(a.dests) | set(b.dests))
    return u


# ---------------------------------------------------------------- whole program

def read_functions(dis_path):
    """build/app.dis -> [(name, start, [instruction addresses])]"""
    funcs, cur = [], None
    for line in open(dis_path, encoding='utf-8'):
        m = re.match(r'^(sub_[0-9A-F]{8}):', line)
        if m:
            cur = (m.group(1), int(m.group(1)[4:], 16), [])
            funcs.append(cur)
            continue
        m = re.match(r'^([0-9a-f]{8}):\s', line)
        if m and cur is not None:
            cur[2].append(int(m.group(1), 16))
    return funcs


class Lifter:
    def __init__(self, isa, app_bin, cache_dir=None):
        self.codec, table, self.check = load_isa(isa)
        self.tab = table.load()
        sys.path.insert(0, os.path.join(ROOT, 'emu'))
        import uv5r_emu as E
        self.area = E.decrypt_area(open(app_bin, 'rb').read())
        self.cache = {}
        # decoding is the slow part: decoded instructions are kept in build/lift/decode.pickle, keyed by the app
        self.disk_path = os.path.join(cache_dir or os.path.join(ROOT, 'build', 'lift'), 'decode.pickle')
        self.disk, self.disk_dirty = {}, False
        key = hashlib.sha256(self.area).hexdigest()
        try:
            with open(self.disk_path, 'rb') as f:
                k, d = pickle.load(f)
            if k == key:
                self.disk = d
        except (OSError, EOFError, pickle.PickleError, ValueError):
            pass
        self.disk_key = key

    def rd(self, a, n):
        o = a - FLASH_BASE
        return int.from_bytes(self.area[o:o + n], 'little')

    def save(self):
        if self.disk_dirty:
            os.makedirs(os.path.dirname(self.disk_path), exist_ok=True)
            with open(self.disk_path, 'wb') as f:
                pickle.dump((self.disk_key, self.disk), f)
            self.disk_dirty = False

    def decode(self, a):
        got = self.cache.get(a)
        if got is not None:
            return got or None
        d = self.disk.get(a)
        if d is None:
            off = a - FLASH_BASE
            r = self.codec.decode(self.tab, self.area[off:off + 6])
            if r is None:
                d = ()
            else:
                insn, raw, size = r
                d = (insn.id, raw, size, self.codec.format(insn, raw), self.check.operands(insn, raw))
            self.disk[a] = d
            self.disk_dirty = True
        if not d:
            self.cache[a] = ()
            return None
        got = (self.tab.by_id(d[0]),) + tuple(d[1:])
        self.cache[a] = got
        return got

    def unit_at(self, a):
        """-> (Unit, bytes consumed) or raises Unimpl."""
        d = self.decode(a)
        if d is None:
            raise Unimpl('undecodable')
        insn, raw, size, text, op = d
        u = translate(text, a, size, op, insn.id)
        if insn.id.endswith('.p'):
            d2 = self.decode(a + size)
            if d2 is None:
                raise Unimpl('pair without a second instruction')
            u2 = translate(d2[3], a + size, d2[2], d2[4], d2[0].id)
            u = pair_units(u, u2)
        return u

    def carry_fix(self, units):
        """addc/subc read the carry flag: make the instruction before each one compute it."""
        for k, u in enumerate(units):
            d = self.decode(u.pc)
            if not d or d[0].id not in ('addc.rrr', 'subc.rrr') or k == 0:
                continue
            prev = units[k - 1]
            c = carry_code(prev.text)
            if c is not None and prev.kind == 'op':
                prev.code = c

    def units(self, start, end):
        """Linear sweep [start, end) -> list of Unit (unimplemented forms become kind 'unimpl')."""
        out, a = [], start
        while a < end:
            try:
                u = self.unit_at(a)
            except Unimpl as e:
                d = self.decode(a)
                size = d[2] if d else 2
                if d and d[0].id.endswith('.p'):
                    d2 = self.decode(a + size)
                    size += d2[2] if d2 else 0
                u = Unit(a, size, (d[3] if d else '??') + '   /* ' + str(e) + ' */')
                u.kind = 'unimpl'
            out.append(u)
            a += u.size
            if u.kind == 'tbb':
                # the jump table: data up to its first target, then code again
                targets = tbb_targets(u, end, self.rd)
                if targets:
                    t_end = min(targets)
                    if t_end > a:
                        dt = Unit(a, t_end - a, 'jump table: %s' % ' '.join('%x' % t for t in targets))
                        dt.kind = 'data'
                        out.append(dt)
                        a = t_end
        self.carry_fix(out)
        return out


def c_function(name, start, end, units, entries, trace_pc=True):
    """Emit one C function. entries: set of all function entry addresses (for tail calls)."""
    body = []
    labels = {u.pc for u in units}
    pre = {}            # address -> code emitted just before that address's label (block ends, rep tails)
    idx = {u.pc: k for k, u in enumerate(units)}
    nblk = 0
    uses_dispatch = False
    unimpl = 0

    def after(k, n):
        """address after n units starting at index k"""
        j = k + n
        return units[j].pc if j < len(units) else end

    ok_end = labels | {end}
    for k, u in enumerate(units):
        if u.kind == 'if':
            a_else = after(k + 1, u.t)
            a_end = after(k + 1, u.t + u.e)
            if k + 1 + u.t + u.e > len(units) + 1:
                u.kind, u.text = 'unimpl', u.text + '   /* block runs past the function */'
                continue
            nblk += 1
            u._blk = nblk
            ins = ('goto B%d_end;' % nblk if u.e else '') + ' B%d_else:;' % nblk
            pre.setdefault(a_else, []).insert(0, ins)
            if u.e:
                pre.setdefault(a_end, []).insert(0, 'B%d_end:;' % nblk)
        elif u.kind == 'rep':
            a_end = u.pc + u.size + u.rep
            if a_end not in ok_end:
                u.kind, u.text = 'unimpl', u.text + '   /* rep body ends inside an instruction */'
                continue
            nblk += 1
            pre.setdefault(a_end, []).insert(0, 'if (--_rc) goto B%d_body; B%d_skip:;' % (nblk, nblk))
            u._blk = nblk

    def jump(target):
        if target in labels:
            return 'goto L_%08X;' % target
        if target in entries or target >= 0x02000000:      # a tail call (into a function or the mask ROM)
            return '{ lift_call(%s); return; }' % h32(target)
        return 'lift_unimpl(%s, "jump out of the function");' % h32(target)

    for k, u in enumerate(units):
        for p in pre.get(u.pc, []):
            body.append('\t' + p)
        body.append('L_%08X:; /* %s */' % (u.pc, u.text.replace('*/', '* /')))
        if trace_pc:
            body.append('	LIFT_INSN(%s);' % h32(u.pc))
        if u.kind == 'data':
            continue
        if u.kind == 'unimpl':
            unimpl += 1
            body.append('\tlift_unimpl(%s, "unimplemented instruction");' % h32(u.pc))
            continue
        code = u.code
        end_pc = u.pc + u.size
        if u.kind == 'op':
            body.append('\t' + code)
        elif u.kind == 'br':
            body.append('\t%s if (%s) %s' % (code, u.cond, jump(u.target)))
        elif u.kind == 'goto':
            body.append('\t%s %s' % (code, jump(u.target)))
        elif u.kind == 'call':
            body.append('\t%s S.rets = %s; lift_call(%s);' % (code, h32(end_pc), h32(u.target)))
        elif u.kind == 'callr':
            body.append('\t{ uint32_t _t = %s; %s S.rets = %s; lift_call(_t); }' % (u.cond, code, h32(end_pc)) if
                        u.cond != '_pt' else '\t%s S.rets = %s; lift_call(_pt);' % (code, h32(end_pc)))
        elif u.kind in ('gotor', 'tbb'):
            uses_dispatch = True
            if u.cond == '_pt':
                body.append('\t%s _jt = _pt; goto dispatch;' % code)
            else:
                body.append('\t_jt = %s; %s goto dispatch;' % (u.cond, code))
        elif u.kind == 'ret':
            body.append('\t%s return;' % code)
        elif u.kind == 'if':
            blk = u._blk
            if u.cond == '_pc':
                body.append('\t%s if (!_pc) goto B%d_else;' % (code, blk))
            else:
                body.append('\t%s if (!%s) goto B%d_else;' % (code, u.cond, blk))
        elif u.kind == 'rep':
            body.append('\t%s if (!_rc) goto B%d_skip; B%d_body:;' % (code, u._blk, u._blk))
    tail = units[-1] if units else None
    for p in pre.get(end, []):
        body.append('\t' + p)
    if tail is None or tail.kind not in ('ret', 'goto', 'gotor', 'tbb', 'unimpl'):
        if end in entries:          # the code runs on into the next function: a tail call
            body.append('\t{ lift_call(%s); return; }' % h32(end))
        else:
            body.append('\tlift_unimpl(%s, "ran off the end");' % h32(end))
    out = ['void %s(void)' % name.replace('sub_', 'f_'), '{',
           '\tuint32_t _a = 0, _rc = 0, _jt = 0, _pt = 0; int _pc = 0;',
           '\t(void)_a; (void)_rc; (void)_jt; (void)_pt; (void)_pc;']
    out += body
    if uses_dispatch:
        out.append('dispatch:')
        out.append('\tswitch (_jt) {')
        for u in units:
            out.append('\tcase %s: goto L_%08X;' % (h32(u.pc), u.pc))
        out.append('\tdefault: lift_call(_jt); return;')
        out.append('\t}')
    out.append('}')
    return '\n'.join(out), unimpl


RUNTIME_H = r'''/* Generated by tools/lift/lift.py: the lifted stock app's runtime interface. */
#ifndef LIFT_H
#define LIFT_H
#include <stdint.h>
typedef struct { uint32_t r[16]; uint32_t sp, rets, flags, icfg; uint32_t sr[3]; } LiftState;  /* sr: reti rete retx */
extern LiftState S;
extern uint32_t lift_pc;          /* the stock instruction being run (for tests and fault reports) */
extern uint32_t lift_icount;      /* instructions run (tests run the lifted code in lockstep with the stock) */
#define LIFT_INSN(pc) (lift_pc = (pc), lift_icount++)
uint32_t rd8(uint32_t a);
uint32_t rd16(uint32_t a);
uint32_t rd32(uint32_t a);
void wr8(uint32_t a, uint32_t v);
void wr16(uint32_t a, uint32_t v);
void wr32(uint32_t a, uint32_t v);
void lift_call(uint32_t addr);
void lift_unimpl(uint32_t pc, const char *why);
/* shift amounts are the low 8 bits of the register (checked against the ISA models by insn_test.py) */
static inline uint32_t lift_shl(uint32_t a, uint32_t n) { n &= 0xFF; return n > 31 ? 0 : a << n; }
static inline uint32_t lift_shr(uint32_t a, uint32_t n) { n &= 0xFF; return n > 31 ? 0 : a >> n; }
static inline uint32_t lift_asr(uint32_t a, uint32_t n) { n &= 0xFF; return (uint32_t)((int32_t)a >> (n > 31 ? 31 : n)); }
static inline uint64_t lift_shl64(uint64_t a, uint32_t n) { n &= 0xFF; return n > 63 ? 0 : a << n; }
static inline uint64_t lift_shr64(uint64_t a, uint32_t n) { n &= 0xFF; return n > 63 ? 0 : a >> n; }
uint32_t lift_sar64sat(uint64_t a, uint32_t n);
uint32_t lift_shl3(uint32_t a, uint32_t n);
uint32_t lift_divu(uint32_t a, uint32_t b);
uint32_t lift_divs(uint32_t a, uint32_t b);
uint64_t lift_div64s(uint64_t a, uint32_t b);
uint64_t lift_div64u(uint64_t a, uint32_t b);
uint32_t lift_clz(uint32_t a);
uint32_t lift_abs(uint32_t a);
uint32_t lift_sat16(uint32_t a);
uint32_t lift_ftoi(uint32_t a);
uint32_t lift_itof(uint32_t a);
uint32_t lift_fadd(uint32_t a, uint32_t b);
uint32_t lift_fsub(uint32_t a, uint32_t b);
uint32_t lift_fmul(uint32_t a, uint32_t b);
uint32_t lift_fdiv(uint32_t a, uint32_t b);
uint32_t lift_ror(uint32_t a, uint32_t n);
uint64_t lift_xchg64_sat(uint64_t a, uint32_t n);
uint32_t lift_ftou(uint32_t a);
uint32_t lift_xchg(uint32_t a, uint32_t n);
uint32_t lift_xchg_sat(uint32_t a, uint32_t n);
uint32_t lift_utof(uint32_t a);
uint32_t lift_ftoi_floor(uint32_t a);
uint32_t lift_fmin(uint32_t a, uint32_t b);
uint32_t lift_fmax(uint32_t a, uint32_t b);
uint32_t lift_uextra_r(uint32_t a, uint32_t t);
uint32_t lift_sextra_r(uint32_t a, uint32_t t);
uint32_t lift_ssat64(int64_t v);
uint32_t lift_usat64(int64_t v);
uint32_t lift_shiftr(uint64_t x, uint32_t n, int mode, int arith);
int lift_fcmp_gt(uint32_t a, uint32_t b);
int lift_fcmp_lt(uint32_t a, uint32_t b);
int lift_fcmp_ge(uint32_t a, uint32_t b);
int lift_fcmp_le(uint32_t a, uint32_t b);
int lift_fcmp_eq(uint32_t a, uint32_t b);
int lift_fcmp_ugt(uint32_t a, uint32_t b);
int lift_fcmp_ult(uint32_t a, uint32_t b);
int lift_fcmp_uge(uint32_t a, uint32_t b);
int lift_fcmp_ule(uint32_t a, uint32_t b);
/* system hooks: what these do depends on the chip around the code (interrupts, exceptions) */
uint64_t lift_copex(uint64_t x, int n);
void lift_swi(int n);
void lift_trigger(void);
void lift_sysret(int bank);
#endif
'''


def tbb_targets(u, end, rd):
    """A tbb/tbh table follows the instruction; it ends where the first target starts."""
    w = 2 if u.text.startswith('tbh') else 1
    tend = u.pc + u.size
    out, i = [], 0
    while True:
        pos = tend + i * w
        if pos >= min(out + [end]):
            break
        t = tend + 2 * rd(pos, w)
        if t <= pos or t >= end:         # (a target inside the table is padding: the table has ended)
            break
        out.append(t)
        i += 1
    return out


def reachable(units, start, end, rd=None):
    """The units control can reach from the function's start (so data swept as code adds no bogus targets).
    tbb/tbh tables are followed when rd (read flash bytes) is given, else the rest of the function counts."""
    idx = {u.pc: k for k, u in enumerate(units)}
    seen, work = set(), [0] if units else []
    while work:
        k = work.pop()
        if k in seen or k >= len(units):
            continue
        seen.add(k)
        u = units[k]
        if u.kind == 'tbb':
            if rd is None:
                seen.update(range(k, len(units)))
                break
            work.extend(idx[t] for t in tbb_targets(u, end, rd) if t in idx)
            continue
        if u.kind in ('unimpl', 'data'):
            continue
        if u.kind in ('br', 'goto') and u.target in idx:
            work.append(idx[u.target])
        if u.kind not in ('goto', 'ret', 'gotor'):
            work.append(k + 1)
    return [units[k] for k in sorted(seen)]


def looks_like_code(L, a, end, limit=48, depth=0):
    """A pointer target is taken as code only if it decodes cleanly up to its first return or jump (data tables
    that code also loads as constants soon hit undecodable bytes or forms nothing uses)."""
    n = 0
    while a < end and n < limit:
        try:
            u = L.unit_at(a)
        except Unimpl:
            return False
        w = u.text.split(' ', 1)[0]
        if w in ('nop', 'bkpt', 'hbkpt', 'swi', 'rte', 'rti', 'rtss', 'rthw', 'idle', 'csync', 'ssync', 'trigger'):
            return False            # zero words and system instructions: a table, not code
        if u.kind == 'br' and L.decode(u.target) is None:
            return False
        if u.kind == 'tbb' and n == 0:
            return False            # a byte table 00 01 .. decodes as tbb
        if u.kind == 'goto' and n == 0:
            # a thunk, or a code pointer in a table decoding as goto: its target must look like code too
            return depth < 2 and u.target is not None and looks_like_code(L, u.target, end, limit, depth + 1)
        if u.kind in ('ret', 'goto', 'gotor', 'tbb'):
            return True
        a += u.size
        n += 1
    return n >= limit


def discover(L, dis_funcs):
    """Function ranges: the disassembly's labels plus every direct call or jump target that lands outside its own
    function (code reached only from the middle of other code). -> ([(name, start, end)], {(start, end): units})"""
    entries = {f[1] for f in dis_funcs} | {FLASH_BASE + 0x100}     # (+ the app's entry point: start-up code)
    last = dis_funcs[-1]
    code_end = (last[2][-1] + 6) if last[2] else last[1]
    cache = {}
    # words in flash that may be code pointers (function-pointer tables, vectors)
    words = set()
    for o in range(0x100, len(L.area) - 3, 2):
        w = int.from_bytes(L.area[o:o + 4], 'little')
        if FLASH_BASE + 0x100 <= w < code_end and not w & 1:
            words.add(w)
    while True:
        starts = sorted(entries)
        ranges = [(st, starts[k + 1] if k + 1 < len(starts) else code_end) for k, st in enumerate(starts)]
        new = set()
        all_pcs = set()
        for st, en in ranges:
            units = cache.get((st, en))
            if units is None:
                units = cache[(st, en)] = L.units(st, en)
            all_pcs.update(u.pc for u in units if u.kind not in ('unimpl', 'data'))
        # code addresses used as values: a constant loaded by code (goto/call through a register, a callback
        # handed to the RTOS) or a word in a table in flash. Splitting a function there is harmless: the code
        # before it falls through into it as a tail call.
        consts = set(words)
        for st, en in ranges:
            for u in reachable(cache[(st, en)], st, en, L.rd):
                for m in re.finditer(r'= (\d+)', u.text):
                    consts.add(int(m.group(1)))
        new |= {c for c in consts if c in all_pcs and c not in entries and looks_like_code(L, c, code_end)}
        for st, en in ranges:
            units = cache[(st, en)]
            pcs = {u.pc for u in units}
            for u in reachable(units, st, en, L.rd):
                t = u.target
                if t is None or u.kind not in ('call', 'goto', 'br'):
                    continue
                if FLASH_BASE <= t < code_end and t not in entries and (u.kind == 'call' or t not in pcs)                         and L.decode(t) is not None:
                    new.add(t)
        if not new:
            return [('sub_%08X' % st, st, en) for st, en in ranges], cache
        entries |= new


def program_reachable(L, funcs, cache):
    """Entries of the functions that are code: reached from the app's entry point through direct calls and jumps,
    or whose address appears as a constant (a function pointer in code, or a word in a table in flash)."""
    starts = {st: (st, en) for _, st, en in funcs}
    words = set()
    area = L.area
    for o in range(0, len(area) - 3, 2):
        w = int.from_bytes(area[o:o + 4], 'little')
        if w in starts:
            words.add(w)
    live, work = set(), [FLASH_BASE + 0x100]
    while work:
        st = work.pop()
        if st in live or st not in starts:
            continue
        live.add(st)
        rng = starts[st]
        for u in reachable(cache[rng], rng[0], rng[1], L.rd):
            if u.target is not None:
                work.append(u.target)
            if rng[0] <= (u.target or 0) < rng[1]:
                continue
            for m in re.finditer(r'= (\d+)', u.text):
                v = int(m.group(1))
                if v in starts:
                    work.append(v)
        nxt = rng[1]
        units = cache[rng]
        reach = reachable(units, rng[0], rng[1], L.rd)
        if units and reach and reach[-1] is units[-1] and                 units[-1].kind not in ('ret', 'goto', 'gotor', 'tbb', 'unimpl', 'data') and nxt in starts:
            work.append(nxt)                # falls through into the next function
    return live


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--isa', default=os.path.join(os.path.dirname(ROOT), 'pi32v2-isa'))
    ap.add_argument('--variant', choices=['nrf', 'nrfb'], default='nrf',
                    help='which stock firmware: nrf (V0.27, default) or nrfb (V0.04)')
    ap.add_argument('--app')
    ap.add_argument('--dis')
    ap.add_argument('--out')
    a = ap.parse_args()
    nrfb = a.variant == 'nrfb'
    a.app = a.app or os.path.join(ROOT, 'build', 'stock-nrfb' if nrfb else 'stock', 'unpacked', 'app.bin')
    a.dis = a.dis or os.path.join(ROOT, 'build', 'nrfb', 'app.dis') if nrfb else a.dis or os.path.join(ROOT, 'build', 'app.dis')
    a.out = a.out or os.path.join(ROOT, 'build', 'nrfb', 'lift') if nrfb else a.out or os.path.join(ROOT, 'build', 'lift')
    for f in (a.app, a.dis):
        if not os.path.exists(f):
            sys.exit('missing %s: run tools/setup.py, then tools/disasm.py (see docs/LIFTER.md)' % f)
    L = Lifter(a.isa, a.app, a.out)
    funcs, cache = discover(L, read_functions(a.dis))
    entries = {f[1] for f in funcs}
    os.makedirs(a.out, exist_ok=True)
    out = ['/* Generated by tools/lift/lift.py from the stock app. Derived from Baofeng firmware: do not commit. */',
           '#include "lift.h"', '']
    table = []
    for name, start, end in funcs:
        code, _ = c_function(name, start, end, cache[(start, end)], entries)
        out.append(code)
        out.append('')
        table.append(name)
    L.save()
    out.append('void lift_dispatch(uint32_t addr, int *found)\n{\n\t*found = 1;\n\tswitch (addr) {')
    for name in table:
        out.append('\tcase 0x%su: %s(); return;' % (name[4:], name.replace('sub_', 'f_')))
    out.append('\t}\n\t*found = 0;\n}')
    live = program_reachable(L, funcs, cache)
    total_units = total_unimpl = clean = 0
    gaps = []
    for name, start, end in funcs:
        if start not in live:
            continue
        reach = reachable(cache[(start, end)], start, end, L.rd)
        bad = [u for u in reach if u.kind == 'unimpl']
        total_units += len(reach)
        total_unimpl += len(bad)
        clean += not bad
        gaps += [(name, u) for u in bad]
    open(os.path.join(a.out, 'lifted.c'), 'w').write('\n'.join(out) + '\n')
    open(os.path.join(a.out, 'lift.h'), 'w').write(RUNTIME_H)
    # per-function facts for tools/lift/local_rewrite.py: is it real code, what it calls, which strings it uses
    info = {}
    bodies = {m.group(1): m.group(0) for m in re.finditer(r'^void (f_[0-9A-F]{8})\(void\)\n\{\n.*?^\}\n',
                                                          '\n'.join(out), re.S | re.M)}
    for name, start, end in funcs:
        fn = name.replace('sub_', 'f_')
        body = bodies.get(fn, '')
        strs = []
        for c in sorted(set(int(x, 16) for x in re.findall(r'0x(01E[0-9A-F]{5})u', body))):
            s = L.area[c - FLASH_BASE:c - FLASH_BASE + 80].split(b'\0')[0]
            if len(s) >= 3 and all(32 <= ch < 127 or ch in (9, 10, 13) for ch in s) and \
                    not any(t.endswith(s.decode('ascii')) for t in strs):
                strs.append(s.decode('ascii'))
        info[fn] = {'live': start in live, 'start': start, 'end': end, 'strings': strs,
                    'calls': sorted(set('f_' + x for x in re.findall(r'lift_call\(0x([0-9A-F]{8})u\)', body)))}
    json.dump(info, open(os.path.join(a.out, 'functions.json'), 'w'), indent=0)
    print('%d functions swept, %d of them code (reached from the entry point through calls, jumps and function '
          'pointers)' % (len(funcs), len(live)))
    print('code: %d instructions, %d not lifted (%.2f%% lifted); %d of %d functions fully lifted (%.1f%%)' % (
        total_units, total_unimpl, 100.0 * (total_units - total_unimpl) / max(total_units, 1), clean, len(live),
        100.0 * clean / max(len(live), 1)))
    for name, u in gaps[:20]:
        print('  not lifted: %s %#x %s' % (name, u.pc, u.text))


if __name__ == '__main__':
    main()
