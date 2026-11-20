"""Instruction forms the text translator in lift.py does not cover, by insn id, with operands from
pi32v2isa (check.operands). Each follows the pi32v2-isa model of the form (tools/hwtest/semantics), and
tools/lift/insn_test.py checks it against that model where one exists.

A handler returns (kind, code, cond) for Unit: kind 'op' (data only), 'ret' (code then return), 'if' / 'br' (cond).
"""

M = 0xFFFFFFFF


def R(n):
    return 'S.r[%d]' % n


def P(lo):
    """a register pair, low register lo"""
    return '(((uint64_t)S.r[%d] << 32) | S.r[%d])' % (lo + 1, lo)


def setp(lo, v):
    return '{ uint64_t _q = %s; S.r[%d] = (uint32_t)_q; S.r[%d] = (uint32_t)(_q >> 32); }' % (v, lo, lo + 1)


def h32(v):
    return '0x%08Xu' % (v & M)


def mulmem(kind, acc, sign, w1, w2):
    """mul/madd/msub.mem1/mem2 (semantics/d2.py): loads through post-incremented pointers, 32x32 -> 64. Every
    operand is read before anything is written; a shared base keeps the first operand's increment, and the pair
    result wins over a pointer update to the same register."""
    def f(op, text):
        s, d = op['s'], op['r']
        ld = {16: 'rd16', 32: 'rd32'}
        ext = {16: '(int64_t)(int16_t)', 32: '(int64_t)(int32_t)'} if sign else {16: '(uint64_t)', 32: '(uint64_t)'}
        code = '{ uint64_t _o = %s; uint32_t _s0 = S.r[%d]; uint32_t _x = %s(_s0);' % (P(d), s, ld[w1])
        if kind == 'mem1':
            if 'n' in op:
                b, inc = R(op['t']), h32(op['n'])
            else:
                b, inc = R(op['u']), R(op['t'])
            code += ' uint32_t _y = %s, _ia = %s;' % (b, inc)
            upd = ' S.r[%d] = _s0 + _ia;' % s
        else:
            if 'n' in op:
                second, ia, ib = op['t'], h32(op['n']), h32(op['m'])
            else:
                second = op['u']
                ia = '(uint32_t)(int32_t)(int16_t)(S.r[%d] >> 16)' % op['t']
                ib = '(uint32_t)(int32_t)(int16_t)S.r[%d]' % op['t']
            code += ' uint32_t _t0 = S.r[%d], _ia = %s, _ib = %s; uint32_t _y = %s(_t0);' % (second, ia, ib, ld[w2])
            upd = ' S.r[%d] = _t0 + _ib; S.r[%d] = _s0 + _ia;' % (second, s)
        code += ' uint64_t _pr = (uint64_t)(%s_x * %s_y);' % (ext[w1], ext[w2])
        if acc > 0:
            code += ' uint64_t _rs = _o + _pr; S.flags = (S.flags & ~2u) | ((_rs < _o) ? 2u : 0u);'
        elif acc < 0:
            code += ' uint64_t _rs = _o - _pr; S.flags = (S.flags & ~2u) | ((_o >= _pr) ? 2u : 0u);'
        else:
            code += ' uint64_t _rs = _pr; (void)_o;'
        code += upd + ' %s }' % setp(d, '_rs')
        return 'op', code, None
    return f


def mulp(sign, acc):
    def f(op, text):
        a, b = R(op['s']), R(op['t'])
        pr = ('(uint64_t)((int64_t)(int32_t)%s * (int64_t)(int32_t)%s)' % (a, b)) if sign else \
            ('((uint64_t)%s * (uint64_t)%s)' % (a, b))
        d = op['r']
        if acc > 0:
            code = '{ uint64_t _pr = %s, _o = %s, _rs = _o + _pr; S.flags = (S.flags & ~2u) | ((_rs < _o) ? 2u : 0u); %s }' % (
                pr, P(d), setp(d, '_rs'))
        else:
            code = '{ uint64_t _pr = %s, _o = %s, _rs = _o - _pr; S.flags = (S.flags & ~2u) | ((_o >= _pr) ? 2u : 0u); %s }' % (
                pr, P(d), setp(d, '_rs'))
        return 'op', code, None
    return f


def iff(cond):
    """Float compare-and-branch / -block. Not modelled by pi32v2-isa (only iff.ne): IEEE ordering, the u forms
    also true when unordered (a NaN)."""
    def f(op, text):
        return ('if' if text.rstrip().endswith('{') else 'br'), '', \
            'lift_fcmp_%s(%s, %s)' % (cond, R(op['r']), R(op['s']))
    return f


def sysret(bank):
    def f(op, text):
        return 'ret', 'lift_sysret(%d);' % bank, None
    return f


SR_SLOT = {n: 'S.sr[%d]' % n for n in range(16)}
SR_SLOT.update({3: 'S.rets', 5: 'S.flags', 11: 'S.icfg', 14: 'S.sp'})
del SR_SLOT[15]


def push_srl(op, text):
    """[--sp] = {special registers}: register n at the n-th lowest address (sim.py special_list)."""
    m = op['m'] if 'm' in op else op['n']
    regs = [k for k in range(16) if m >> k & 1]
    if any(r not in SR_SLOT or r == 14 for r in regs):
        return None
    code = 'S.sp -= %d;' % (4 * len(regs))
    for k, r in enumerate(regs):
        code += ' wr32(S.sp + %d, %s);' % (4 * k, SR_SLOT[r])
    return 'op', code, None


def pop_srl(op, text):
    m = op['m'] if 'm' in op else op['n']
    regs = [k for k in range(16) if m >> k & 1]
    if any(r not in SR_SLOT and r != 15 or r == 14 for r in regs):
        return None
    code = '{ uint32_t _b = S.sp;'
    ret = False
    for k, r in enumerate(regs):
        if r == 15:
            ret = True
            continue
        v = 'rd32(_b + %d)' % (4 * k)
        if r == 3:
            v += ' & 0x07FFFFFEu'
        elif r == 5:
            v += ' & 0xFu'
        elif r in (0, 1, 2):
            v += ' & 0x07FFFFFEu'
        code += ' %s = %s;' % (SR_SLOT[r], v)
    code += ' S.sp = _b + %d; }' % (4 * len(regs))
    return ('ret' if ret else 'op'), code, None


def mov_r_sr(op, text):
    s = op['s']
    if s not in SR_SLOT:
        return None
    return 'op', '%s = %s;' % (R(op['r']), SR_SLOT[s]), None


def carry_op(kind):
    """addc/subc: the carry flag comes from the instruction before (lift.py makes that one compute it)."""
    def f(op, text):
        a, b, d = R(op['s']), R(op['t']), R(op['r'])
        if kind == 'addc':
            code = ('{ uint64_t _s = (uint64_t)%s + %s + ((S.flags >> 1) & 1u); %s = (uint32_t)_s; '
                    'S.flags = (S.flags & ~2u) | (uint32_t)((_s >> 32) << 1); }' % (a, b, d))
        else:
            code = ('{ uint64_t _s = (uint64_t)%s + (uint32_t)~%s + ((S.flags >> 1) & 1u); %s = (uint32_t)_s; '
                    'S.flags = (S.flags & ~2u) | (uint32_t)((_s >> 32) << 1); }' % (a, b, d))
        return 'op', code, None
    return f


def simple(fmt):
    def f(op, text):
        return 'op', fmt.format(**{k: (R(v) if isinstance(v, int) and k in 'rst' else v) for k, v in op.items()
                                   if k != 'raw'}, **{'n_': op.get('n'), 'p_': op.get('p')}), None
    return f


def addsub_pqq(op, text):
    (a0, a1), (b0, b1) = op['s'], op['m']
    s1, s2 = op['n'].split('|')
    sat = 'lift_ssat64' if 'ssat' in op['p'] else 'lift_usat64'
    sg = '(int64_t)(int32_t)' if 'ssat' in op['p'] else '(int64_t)(uint64_t)'
    hi = '%s(%s%s %s %s%s)' % (sat, sg, R(a0), s1, sg, R(b0))
    lo = '%s(%s%s %s %s%s)' % (sat, sg, R(a1), s2, sg, R(b1))
    return 'op', '{ uint32_t _h = %s, _l = %s; S.r[%d] = _l; S.r[%d] = _h; }' % (hi, lo, op['r'], op['r'] + 1), None


def add_rrre(op, text):
    a, b = R(op['s']), R(op['t'])
    if op['n'] == '(ssat)':
        v = 'lift_ssat64((int64_t)(int32_t)%s + (int64_t)(int32_t)%s)' % (a, b)
    else:
        v = 'lift_usat64((int64_t)(uint64_t)%s + (int64_t)(uint64_t)%s)' % (a, b)
    return 'op', '%s = %s;' % (R(op['r']), v), None


def shiftr(op, text):
    mode = {'(up)': 0, '(even)': 1, '(round)': 2, '(zero)': 3}.get(op['m'])
    if mode is None:
        return None
    arith = 1 if op['n'] == '>>>' else 0
    return 'op', '%s = lift_shiftr(%s, %s, %d, %d);' % (R(op['r']), P(op['s']), R(op['t']), mode, arith), None


def sat8_rhrh(op, text):
    if op['n'] != '.l' or op['m'] != '.l':
        return None
    d, s = op['r'], op['s']
    return 'op', ('{ int32_t _v = (int16_t)S.r[%d]; _v = _v > 127 ? 127 : _v < -128 ? -128 : _v; '
                  'S.r[%d] = (S.r[%d] & 0xFFFF0000u) | ((uint32_t)_v & 0xFFFFu); }' % (s, d, d)), None


def unpackb_s(op, text):
    s, d = R(op['s']), op['r']
    b = ['(uint32_t)(uint16_t)(int16_t)(int8_t)(%s >> %d)' % (s, 8 * k) for k in range(4)]
    return 'op', '{ uint32_t _l = %s | (%s << 16), _h = %s | (%s << 16); S.r[%d] = _l; S.r[%d] = _h; }' % (
        b[0], b[1], b[2], b[3], d, d + 1), None


def testset(op, text):
    x = R(op['x'])
    return 'op', '{ uint32_t _o = rd8(%s); wr8(%s, 0xFFu); S.flags = _o & 0xFu; }' % (x, x), None


def copex(op, text):
    # no model: the ISA project found its result depends on state it could not pin down (semantics/q.py)
    return 'op', '{ uint64_t _q = lift_copex(%s, %d); %s }' % (P(op['s']), op['n'], setp(op['r'], '_q')), None


def mul2_pair(acc):
    """r+1:r = two 16x16 lane products (semantics/r.py _mk_mul2), each saturated to 32 bits."""
    def f(op, text):
        mode = op['i'].strip('()').split(',')
        sg, x2 = 'ssat' in mode, 'x2' in mode
        def half(r, sel):
            v = '(S.r[%d] >> %d)' % (r, 16 if sel == '.h' else 0)
            return ('(int64_t)(int16_t)%s' % v) if sg else ('(int64_t)(uint16_t)%s' % v)
        hi = '(%s * %s * %d)' % (half(op['s'], op['n']), half(op['t'], op['p']), 2 if x2 else 1)
        lo = '(%s * %s * %d)' % (half(op['s'], op['m']), half(op['t'], op['q']), 2 if x2 else 1)
        sat = 'lift_ssat64' if sg else 'lift_usat64'
        base = (lambda r: '(int64_t)(int32_t)S.r[%d]' % r) if sg else (lambda r: '(int64_t)(uint64_t)S.r[%d]' % r)
        d = op['r']
        if acc:
            h = '%s(%s %s %s)' % (sat, base(d + 1), '+' if acc > 0 else '-', hi)
            l_ = '%s(%s %s %s)' % (sat, base(d), '+' if acc > 0 else '-', lo)
        else:
            h, l_ = '%s(%s)' % (sat, hi), '%s(%s)' % (sat, lo)
        return 'op', '{ uint32_t _h = %s, _l = %s; S.r[%d] = _l; S.r[%d] = _h; }' % (h, l_, d, d + 1), None
    return f


SPECIAL = {
    'mul2.prhrhrhrhe': mul2_pair(0), 'madd.mul2.prhrhrhrhe': mul2_pair(1), 'msub.mul2.prhrhrhrhe': mul2_pair(-1),
    'xchg.pr.sat': lambda op, text: ('op', setp(op['r'], 'lift_xchg64_sat(%s, S.r[%d])' % (P(op['r']), op['s'])), None),
    'asr.pair.i': lambda op, text: ('op', setp(op['r'], '(uint64_t)((int64_t)%s >> %d)' % (P(op['r']), min(op['n'], 63))),
                                    None),
    'lsr.u.rpi': lambda op, text: ('op', '%s = lift_usat64((int64_t)(%s >> %d) < 0 ? INT64_MAX : (int64_t)(%s >> %d));' % (
        R(op['r']), P(op['s']), min(op['n'], 63), P(op['s']), min(op['n'], 63)) if op['n'] < 64 else '%s = 0;' % R(op['r']),
        None),
    'ftou.trunc.rr': simple('{r} = lift_ftou({s});'),
    'or.bit.rrr': simple('{r} = {s} | (1u << ({t} & 31));'),
    'xor.bit.rrr': simple('{r} = {s} ^ (1u << ({t} & 31));'),
    'rev8.rr': simple('{r} = __builtin_bswap32({s});'),
    'ror.rri': simple('{r} = lift_ror({s}, {n_});'),
    'ror.rrr': simple('{r} = lift_ror({s}, {t});'),
    'xchg.rrr.sat': simple('{r} = lift_xchg_sat({s}, {t});'),
    'xchg.rrr': simple('{r} = lift_xchg({s}, {t});'),
    'utof.rr': simple('{r} = lift_utof({s});'),
    'ftoi.floor.rr': simple('{r} = lift_ftoi_floor({s});'),
    'fmin.rrr': simple('{r} = lift_fmin({s}, {t});'),
    'fmax.rrr': simple('{r} = lift_fmax({s}, {t});'),
    'madd.f.rrr': simple('{r} = lift_fadd({r}, lift_fmul({s}, {t}));'),
    'msub.f.rrr': simple('{r} = lift_fsub({r}, lift_fmul({s}, {t}));'),
    'uextra.rrriirii': simple('{r} = lift_uextra_r({s}, {t});'),
    'sextra.rrriirii': simple('{r} = lift_sextra_r({s}, {t});'),
    'add.rrre': add_rrre,
    'addsub.pqq.sat': addsub_pqq,
    'shiftr.rpr.mode': shiftr,
    'sat8.s.rhrh': sat8_rhrh,
    'unpackb.s.pr': unpackb_s,
    'testset.b': testset,
    'copex.ppi': copex,
    'addc.rrr': carry_op('addc'),
    'subc.rrr': carry_op('subc'),
    'madd.s.prr': mulp(True, 1),
    'madd.u.prr': mulp(False, 1),
    'msub.s.prr': mulp(True, -1),
    'msub.u.prr': mulp(False, -1),
    'push.srl': push_srl,
    'pop.srl': pop_srl,
    'mov.r_sr': mov_r_sr,
    'swi': lambda op, text: ('op', 'lift_swi(%d);' % op['n'], None),
    'trigger': lambda op, text: ('op', 'lift_trigger();', None),
    'rti': sysret(0), 'rte': sysret(1), 'rtx': sysret(2), 'rthw': sysret(2), 'rtss': sysret(3),
    'iff.gt.rr': iff('gt'), 'iff.lt.rr': iff('lt'), 'iff.le.rr': iff('le'), 'iff.ge.rr': iff('ge'),
    'iff.eq.rr': iff('eq'), 'iff.uge.rr': iff('uge'), 'iff.ule.rr': iff('ule'), 'iff.ult.rr': iff('ult'),
    'iff.ugt.rr': iff('ugt'), 'bfule.rr': iff('ule'), 'bfuge.rr': iff('uge'), 'bfult.rr': iff('ult'),
    'bfugt.rr': iff('ugt'), 'bfgt.rr': iff('gt'), 'bflt.rr': iff('lt'), 'bfge.rr': iff('ge'),
    'bfle.rr': iff('le'),
}
for _op, _acc in (('mul', 0), ('madd', 1), ('msub', -1)):
    for _sg in ('u', 's'):
        for _sz, _w in (('h', 16), ('w', 32)):
            for _ad in ('i', 'r'):
                SPECIAL['%s.mem1.%s.%s.%s' % (_op, _sz, _ad, _sg)] = mulmem('mem1', _acc, _sg == 's', _w, 32)
        for _sz, (_w1, _w2) in (('hh', (16, 32)), ('hw', (16, 32)), ('ww', (32, 32))):
            for _ad in ('i', 'rh'):
                SPECIAL['%s.mem2.%s.%s.%s' % (_op, _sz, _ad, _sg)] = mulmem('mem2', _acc, _sg == 's', _w1, _w2)
