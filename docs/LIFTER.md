# The lifter: stock machine code to C, checked against the stock code

`tools/lift/` turns the whole stock v0.27 app into compilable C in seconds, then checks the C against the stock
code automatically. The goal is the fastest route to a full C firmware: start from C that is correct but ugly,
then clean it up one function at a time, with a test that runs in milliseconds after every edit.

The generated C is derived from the Baofeng firmware. It goes to `build/lift/` and is never committed. Only the
tools are committed.

## Pieces

| File | What it does |
| --- | --- |
| `tools/lift/lift.py` | Turns every instruction into one C statement on a CPU state (`S.r[0..15]`, `S.sp`, `S.rets`) through memory accessors. One C function per stock function. Entry points come from the disassembly plus every call or jump target reachable from real code. Decodes are cached, so a re-lift takes ~5 s. |
| `tools/lift/runtime.c` | Host runtime: memory and mask-ROM calls go back to the caller, and arithmetic edge cases (shifts, saturation, divide by zero, float NaN) behave the way the ISA models measured them. |
| `tools/lift/insn_test.py` | Per instruction form: up to 12 instances from the app, each run on 16 random register and memory states, compared with the pi32v2-isa models (fitted to real hardware). |
| `tools/lift/func_test.py` | Per function, lockstep: the emulator boots the stock app and records each call (registers, every memory access tagged with the instruction-execution number, mask-ROM calls with their results). The lifted C replays each call: every read is served from the recording, and every write must be the same byte at the same instruction execution. Registers must match at the return. The first difference names the stock instruction. |

```
python tools/lift/lift.py                       # build/lift/lifted.c (+ lift.h)
python tools/lift/insn_test.py                  # every instruction form vs the ISA models
python tools/lift/func_test.py --steps 3000000  # lifted functions vs the stock code, in lockstep
```

## Status (2026-11-19)

- **Lift (update):** 100% of the code is lifted: 2,032 functions, 75,630 instructions, counted from the entry point through calls, jumps and function pointers (data the disassembly swept as code is left out). New forms in `tools/lift/special.py`; `copex`, `swi` and float compare-branches (no ISA model) are runtime hooks or IEEE C, unverified.
- **Instruction forms:** 641 of 646 forms used by the app agree with the ISA models. The other 5 have no usable
  model here (host-defined special registers). The hardware facts this pinned down:
  - shift amounts use the low 8 bits of the register
  - `<<<` saturates
  - `abs(INT_MIN)` saturates
  - divide by zero gives 0
  - `(s)` pair shifts saturate to 32 bits
  - any float NaN becomes 0x7F800001
- **Functions:** all 129 functions the emulated boot reaches match the stock code on every recorded call (212
  calls, checked instruction by instruction). Coverage is limited by how far the emulator boots: it has no
  interrupts, keypad or BK4819 yet ([EMULATOR.md](EMULATOR.md)).

## How a function becomes real source

1. Take a lifted function from `build/lift/lifted.c` and rewrite it in readable C (names, types, loops) in the
   firmware tree.
2. Run `func_test.py --only sub_XXXXXXXX`. It must still match the stock code on every recorded call.
3. Repeat. Every function that passes is one less piece of stock code, and the C firmware never stops matching
   the stock behaviour.

Next steps that raise coverage: interrupts and the remaining radio parts in the emulator (more functions
exercised), and the instruction gaps above.

## Readable C with a local model

`tools/lift/local_rewrite.py` runs unattended on your own PC with Ollama (default `qwen3-coder:30b`). It works
through the lifted functions, smallest first. For each one it asks the model for a readable rewrite, then compiles
the rewrite against the lifted original with `tools/lift/eqtest_rt.c`. The test runs 300 random states and compares
four things: r0 at return, sp, every byte written outside the stack frame, and every call (address, r0..r3, sp).
A failed test goes back to the model, up to 3 tries. Each rewrite that passes goes to
`build/readable/sub_XXXXXXXX.c`, which is not committed. The run resumes where it stopped.

```
python tools/lift/local_rewrite.py            # keys: p pause, s skip, q quit
```
