#!/usr/bin/env python3
"""Rewrite the lifted stock functions as readable C with a local LLM (Ollama), each one checked by test.

    python tools/lift/local_rewrite.py [--model qwen3-coder:30b] [--only sub_01E0019C,...] [--tries 3]

Runs on your PC, unattended, as long as you like; stop it any time and start it again: it resumes.

For every function in build/lift/lifted.c (smallest first, or --only):
  1. the model is asked for a readable rewrite with the same interface (S.r[], rd/wr, lift_call)
  2. original and rewrite are compiled into a test program (tools/lift/eqtest_rt.c) and run on a few hundred
     random states: return values, sp, every byte written outside the stack frame, and every call made
     (address and arguments) must agree with the lifted original, which matches the stock code
  3. on a failure the error goes back to the model, up to --tries times; a function that never passes keeps
     its lifted version
Results: build/readable/<function>.c for each pass, build/readable/progress.json, build/readable/log.txt.

Keys while it runs (Windows console): p pause/resume, s skip the current function, q quit (after the current
step). A watchdog cuts off a model call (--llm-timeout) or a test (--test-timeout) that hangs, and stops the run if
Ollama stops answering several times in a row.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
LIFT = os.path.join(ROOT, 'build', 'lift')
OUT = os.path.join(ROOT, 'build', 'readable')
TMP = os.path.join(OUT, 'work', 'tmp')        # compiler temp files stay next to the repo, not on C:
ENV = dict(os.environ, TMP=TMP, TEMP=TMP, TMPDIR=TMP)

PROMPT = """You rewrite machine-lifted C into readable C. The input is one function lifted from JieLi pi32v2 \
firmware. It works on a CPU state S (S.r[0..15] registers, S.sp stack pointer, S.rets return address) and memory \
accessors rd8/rd16/rd32(addr) and wr8/wr16/wr32(addr, value). Calls are made by setting S.r[0..3] (arguments; more \
on the stack at S.sp) and calling lift_call(address); the result comes back in S.r[0] (and S.r[1] for 64-bit).

Write the same function as readable C:
- Same name and signature: void {name}(void). Arguments come in S.r[0..3] (and the stack); put the result in \
S.r[0] (S.r[1] too if the original sets it). Keep S.sp the same at return as at entry.
- Use local variables with meaningful names instead of registers, and if/else, for, while, switch instead of goto.
- Keep EVERY memory read and write, every lift_call (same address, same S.r[0..3] at the call, same S.sp at the call) \
and their order. Memory below the entry S.sp is the function's own stack frame: you may use C locals instead of it \
unless its address is passed to a call.
- Use only: stdint types, rd8/rd16/rd32, wr8/wr16/wr32, lift_call, S, and the lift_* helpers the original uses. \
No other functions, no headers, no globals.
- Add a short comment saying what the function seems to do.
Answer with only the C function in one ```c block.

{extra}Original:
```c
{code}
```"""


def read_functions(path):
    src = open(path, encoding='utf-8').read()
    out = {}
    for m in re.finditer(r'^void (f_[0-9A-F]{8})\(void\)\n\{\n(.*?)^\}\n', src, re.S | re.M):
        out[m.group(1)] = m.group(0)
    return out


def ollama(url, model, prompt, timeout):
    body = json.dumps({'model': model, 'prompt': prompt, 'stream': False,
                       'options': {'temperature': 0.2, 'num_ctx': 16384}}).encode()
    req = urllib.request.Request(url.rstrip('/') + '/api/generate', body, {'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read())['response']


def extract_c(text, name):
    m = re.search(r'```(?:c|C)?\s*\n(.*?)```', text, re.S)
    code = m.group(1) if m else text
    code = re.sub(r'<think>.*?</think>', '', code, flags=re.S)
    if not re.search(r'void\s+%s\s*\(\s*void\s*\)' % name, code):
        return None
    return code.strip() + '\n'


def helpers_inc():
    src = open(os.path.join(HERE, 'runtime.c'), encoding='utf-8').read()
    a = src.index('uint32_t lift_shl3(')
    head = src[src.index('#define FNAN'):src.index('uint32_t lift_ftoi(')]
    body = src[a:]
    body = body.replace(head, '')
    return '#include <math.h>\n' + head + body


def test(name, orig, cand, work, timeout):
    """-> (ok, message)"""
    c = os.path.join(work, 'eq_%s.c' % name)
    exe = os.path.join(work, 'eq_%s.exe' % name)
    o = orig.replace('void %s(void)' % name, 'void f_orig(void)', 1)
    n = cand.replace('void %s(void)' % name, 'void f_new(void)', 1)
    with open(c, 'w', encoding='utf-8') as f:
        f.write('#include "lift.h"\n#include <stdint.h>\n%s\n%s\n' % (o, n))
    r = subprocess.run(['gcc', '-O1', '-w', '-I', LIFT, '-I', work, os.path.join(HERE, 'eqtest_rt.c'), c, '-o', exe,
                        '-lm'], capture_output=True, text=True, timeout=120, env=ENV)
    if r.returncode:
        errs = [l for l in r.stderr.splitlines() if 'error' in l][:6]
        return False, 'does not compile:\n' + '\n'.join(errs)
    try:
        r = subprocess.run([exe, '300'], capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, 'the test hung (an endless loop?) on some input'
    msg = (r.stdout or r.stderr).strip()[:600]
    return r.returncode == 0, msg


class Keys(threading.Thread):
    """p pause, s skip, q quit (Windows console)."""
    def __init__(self):
        super().__init__(daemon=True)
        self.pause = self.skip = self.quit = False

    def run(self):
        try:
            import msvcrt
        except ImportError:
            return
        while True:
            if msvcrt.kbhit():
                k = msvcrt.getwch().lower()
                if k == 'p':
                    self.pause = not self.pause
                elif k == 's':
                    self.skip = True
                elif k == 'q':
                    self.quit = True
            time.sleep(0.1)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--model', default='qwen3-coder:30b')
    ap.add_argument('--url', default='http://localhost:11434')
    ap.add_argument('--only', default='', help='comma-separated sub_XXXXXXXX or f_XXXXXXXX names')
    ap.add_argument('--tries', type=int, default=3)
    ap.add_argument('--max-lines', type=int, default=400, help='skip lifted functions longer than this (for now)')
    ap.add_argument('--llm-timeout', type=int, default=600)
    ap.add_argument('--test-timeout', type=int, default=20)
    ap.add_argument('--retry-failed', action='store_true', help='also redo functions that failed before')
    a = ap.parse_args()

    funcs = read_functions(os.path.join(LIFT, 'lifted.c'))
    os.makedirs(OUT, exist_ok=True)
    work = os.path.join(OUT, 'work')
    os.makedirs(TMP, exist_ok=True)
    with open(os.path.join(work, 'lift_helpers.inc'), 'w', encoding='utf-8') as f:
        f.write(helpers_inc())
    prog_path = os.path.join(OUT, 'progress.json')
    prog = json.load(open(prog_path)) if os.path.exists(prog_path) else {}
    log = open(os.path.join(OUT, 'log.txt'), 'a', encoding='utf-8')

    only = {x.replace('sub_', 'f_') for x in a.only.split(',') if x}
    todo = [n for n in sorted(funcs, key=lambda n: funcs[n].count('\n')) if (not only or n in only)]
    todo = [n for n in todo if funcs[n].count('\n') <= a.max_lines]
    todo = [n for n in todo if prog.get(n, {}).get('status') != 'pass' and
            (a.retry_failed or prog.get(n, {}).get('status') != 'fail')]

    keys = Keys()
    keys.start()
    t0 = time.time()
    stats = {'pass': 0, 'fail': 0, 'llm_error': 0, 'compile_error': 0, 'test_fail': 0, 'tries': 0}
    done_before = sum(1 for v in prog.values() if v.get('status') == 'pass')
    llm_down = 0

    def status(cur, step):
        n = stats['pass'] + stats['fail']
        el = time.time() - t0
        eta = (el / n * (len(todo) - n)) if n else 0
        err = 100.0 * stats['fail'] / n if n else 0.0
        line = ('[%d/%d] pass %d fail %d (%.0f%% fail) | tries %d, compile err %d, test fail %d, llm err %d | '
                'total readable %d/%d | %s %s | %s, eta %s%s' % (
                    n, len(todo), stats['pass'], stats['fail'], err, stats['tries'], stats['compile_error'],
                    stats['test_fail'], stats['llm_error'], done_before + stats['pass'], len(funcs), cur, step,
                    fmt_t(el), fmt_t(eta), '  [PAUSED: p to resume]' if keys.pause else ''))
        sys.stdout.write('\r' + line[:230].ljust(230))
        sys.stdout.flush()

    print('%d functions to do (%d lifted, %d already readable). Model %s. Keys: p pause, s skip, q quit.' % (
        len(todo), len(funcs), done_before, a.model))
    for name in todo:
        if keys.quit:
            break
        orig = funcs[name]
        extra = ''
        result = None
        for k in range(a.tries):
            while keys.pause and not keys.quit:
                status(name, 'paused')
                time.sleep(0.5)
            if keys.skip or keys.quit:
                break
            status(name, 'asking the model (try %d)' % (k + 1))
            stats['tries'] += 1
            try:
                reply = ollama(a.url, a.model, PROMPT.format(name=name, code=orig, extra=extra), a.llm_timeout)
                llm_down = 0
            except (urllib.error.URLError, TimeoutError, OSError, KeyError, ValueError) as e:
                stats['llm_error'] += 1
                llm_down += 1
                log.write('%s llm error: %r\n' % (name, e))
                log.flush()
                if llm_down >= 5:
                    print('\nwatchdog: Ollama failed 5 times in a row (%r); is it running? Stopping.' % e)
                    keys.quit = True
                    break
                time.sleep(10)
                continue
            cand = extract_c(reply, name)
            if cand is None:
                stats['compile_error'] += 1
                extra = 'Your last answer did not contain the function void %s(void). ' % name
                continue
            status(name, 'testing (try %d)' % (k + 1))
            try:
                ok, msg = test(name, orig, cand, work, a.test_timeout)
            except subprocess.TimeoutExpired:
                ok, msg = False, 'the compiler hung'
            if ok:
                result = cand
                break
            if msg.startswith('does not compile'):
                stats['compile_error'] += 1
            else:
                stats['test_fail'] += 1
            extra = ('Your last rewrite was wrong. Test result:\n%s\nFix it.\nYour last rewrite:\n```c\n%s```\n\n'
                     % (msg, cand))
            log.write('%s try %d: %s\n' % (name, k + 1, msg.splitlines()[0] if msg else '?'))
            log.flush()
        if keys.skip:
            keys.skip = False
            prog[name] = {'status': 'skipped'}
        elif result:
            open(os.path.join(OUT, name.replace('f_', 'sub_') + '.c'), 'w', encoding='utf-8').write(result)
            prog[name] = {'status': 'pass', 'tries': k + 1}
            stats['pass'] += 1
        elif not keys.quit:
            prog[name] = {'status': 'fail'}
            stats['fail'] += 1
        json.dump(prog, open(prog_path, 'w'), indent=0)
        status(name, 'done')
    print('\nstopped. %d passed, %d failed this run; %d of %d functions readable in build/readable/' % (
        stats['pass'], stats['fail'], done_before + stats['pass'], len(funcs)))


def fmt_t(s):
    s = int(s)
    return '%dh%02dm' % (s // 3600, s % 3600 // 60) if s >= 3600 else '%dm%02ds' % (s // 60, s % 60)


if __name__ == '__main__':
    main()
