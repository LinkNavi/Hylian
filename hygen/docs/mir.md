# libhymir

Typed IR between Hylian's frontend IR and the backend encoder. Every value
carries a `MIRType` (width + signed/unsigned/float) so casting, div/mod,
and shifts are correct by construction instead of guessed at codegen time.

## Types
`MIR_I8/I16/I32/I64`, `MIR_U8/U16/U32/U64`, `MIR_F32/F64`, `MIR_PTR`.

## Values
`MIRValue` is a tagged union: vreg, int imm, float imm, label, or global.
Vregs are not real registers yet — regalloc assigns those later.

## Key ops
- `MIR_SDIV/UDIV`, `MIR_SMOD/UMOD`, `MIR_SHR_A/SHR_L` — split by signedness.
- `MIR_ADD/SUB/MUL` (int) vs `MIR_FADD/FSUB/FMUL/FDIV` (float) — never shared.
- `MIR_SEXT/ZEXT/TRUNC/I2F/F2I/F2F/BITCAST` — replaces the old no-op cast.
- `MIR_ASM_RAW` — pre-encoded raw bytes spliced in untouched (escape hatch).
- `MIR_CLI/STI/WRMSR/OUTB/...` — dedicated ops for common privileged instrs.

## Building
```
meson setup build
meson test -C build
```

## Status
- [x] Types, values, opcodes, construction API, dump
- [x] ir_to_mir (lowering from existing Hylian IR) — full opcode coverage:
      consts, vars, arithmetic (int/float split), div/mod/shift, cast
      (real conversions, not bit-copy), control flow, calls, OOP
      (new/get_field/set_field), arrays, privileged/kernel intrinsics,
      strings (interned, deduped), print/println. Only interp-strings and
      inline asm blocks are stubbed (same known gaps as codegen_elf.c).
- [x] mir_verify — catches use-before-def, missing terminators, float/int
      op-type mismatches. Caught a real bug in test_ir_to_mir_full.c.
- [x] libhyregalloc — real CFG + backward dataflow liveness (not a naive
      linear instruction scan, so loop back-edges are handled correctly)
      feeding classic Poletto-Sarkar linear scan, run separately per
      register class (int/gpr vs float/xmm). Target only supplies register
      counts - regalloc knows nothing about x86. Spilling verified against
      both a starved (1 reg) and roomy (4 reg) target on a loop.
- [x] libhyx64 — direct x86-64 byte encoder, no text/nasm anywhere. Covers
      MOV, ALU (add/sub/and/or/xor/cmp), imul, signed+unsigned div/mod,
      neg/not, shifts, all comparisons (signed+unsigned via setcc/movzx),
      load/store (global via RIP-relative, and register-indirect for raw
      pointers), lea, calls (SysV, up to 6 register args), labels/jumps
      with fixups, privileged/kernel ops (cli/sti/iret/wrmsr/rdmsr/in/out/
      mov-crN), memset/memcpy (rep stosb/movsb). Reserves RAX/RDX/RCX/R11
      as fixed scratch so operand materialization never risks clobbering a
      live vreg (costs some redundant movs - a later peephole pass can
      clean those up; correctness over optimality for now).
      Verified two ways: exact byte-for-byte match against a hand-computed
      encoding for a trivial function, and objdump disassembly of the
      loop-sum function confirmed instruction-by-instruction correct.
      Known gaps (explicit, not silent): float/xmm ops, call args beyond 6
      registers, the parallel-move hazard when an arg's source vreg already
      sits in a register another arg needs, WRMSR/RDMSR only handling
      values that fit 32 bits. Inline asm blocks still need the
      mini-assembler (not started) to turn raw asm text into bytes.
- [x] libhyobj — ELF64 relocatable object (.o) writer, format-agnostic
      ObjModule (funcs/rodata/data/bss/relocs) so a PE or Mach-O writer
      later reuses the same builder API, only obj_write_elf() is
      Linux-specific. No system <elf.h> dependency - structs defined
      locally. Verified for real: emitted object was linked with `cc`
      against a genuine C caller and *executed* - returned 42 as expected.
      Full pipeline (MIR -> regalloc -> x64 bytes -> ELF -> real linker ->
      real execution) is proven end-to-end.
- [x] mini-assembler for inline asm blocks (libhyx64/src/miniasm.c). Scope:
      64-bit GPRs only, mov/alu(reg,reg and reg,imm32 - real hardware
      encodings, no synthesized scratch)/imul/neg/not/push/pop/shifts(reg,cl
      only)/lea/mov±[reg+disp]/jmp/jcc/call/setcc/ret/syscall/cli/sti/iretq/
      wrmsr/rdmsr/in-out/mov±crN. Local labels + external symbol relocations
      for jmp/call/jcc targets.
      {varname} handling: ir_to_mir.c's IR_ASM_BLOCK case rewrites {varname}
      -> {N} and loads each referenced variable's current value into a
      fresh vreg (MIR_LOAD + MIR_ASM_TEXT's args[]), so MIR itself never
      sees Hylian variable names - stays frontend-blind like everything
      else in this pipeline. Post-regalloc, lower_x64.c's MIR_ASM_TEXT case
      resolves {N} to a real register name (direct if regalloc put it in a
      register, or a load into reserved scratch if spilled - reused across
      repeated {N} in the same block) and hands the fully-substituted text
      to miniasm_assemble. v1 is read-only: {varname} always means "current
      value in", never an implicit write-back target.
      Verified two ways: test_miniasm.c builds a MIR_ASM_TEXT block
      computing 40+2 via {0}/{1} where one operand lands in a register
      regalloc chose (not hardcoded) - full pipeline to a real linked,
      executed binary, exit code 42 confirms the substitution and encoding
      were both correct. Also confirmed ir_to_mir.c compiles clean against
      the real Hylian ir.h/ir.c.
      Known gaps: no rip-relative-to-globals addressing from inside an asm
      block yet, max 4 simultaneously-spilled operands per block (cycles
      through the same reserved scratch set as the rest of lower_x64.c),
      no write-back (an asm block can read a variable's value but can't
      assign a new value to it - the block's own dest, if any, isn't wired
      up in v1 either).
- [x] MIR_SYSCALL — raw kernel calls via a dedicated MIR op instead of the
      general asm escape hatch. Correct Linux syscall ABI (arg4 in r10, not
      rcx - different from the SysV call ABI, easy to get wrong). Verified
      as strongly as possible: built a fully freestanding `_start` (no
      libc, no crt0) doing write(1,"hi\n",3) + exit(0) via raw syscalls,
      linked with plain `ld` (no cc driver), executed it - printed "hi",
      exited 0. This is the primitive layer the stdlib rewrite needs;
      most of the current asm{} blocks in sys.hy can now route through
      this instead of needing the mini-assembler at all.
      Known gap: same parallel-move hazard as MIR_CALL, and r10 (syscall's
      4th arg register) is still in the regalloc-allocatable pool rather
      than reserved, so it's the most exposed case of that hazard.
- [ ] wire ir_to_mir.c's IR_ASM_BLOCK/IR_INTERP_STR stubs to something real
- [x] recognize a generic `syscall(nr, a0, a1, ...)` builtin in
      ir_to_mir.c's IR_CALL case and lower it straight to MIR_SYSCALL - one
      builtin at any arity, not five special-cased wrapper names. sys.hy's
      whole family of naked asm wrappers (_sc3/_sc5/_sc_pread/_sc_pwrite/
      _sc_mmap) becomes unnecessary once sys.hy calls syscall() directly -
      no fixed-shape wrapper, no reshaping hacks for things like mmap's
      6-arg signature, no more silently-dropped arguments like the
      pre-existing sys_mount bug (passes 6 args to a 5-param _sc5).
      Verified with test_builtin_syscall.c: fed it the IR shape
      sys_write/sys_exit would generate calling syscall() directly,
      confirmed zero dead calls survive, confirmed the resulting binary
      actually links (plain ld, no libc) and runs correctly.
- [ ] stdlib split into platform-agnostic primitives + platform/*.hy backends

## Local variables / stack frames (real per-call-frame storage)
Fixed a real correctness bug: LOAD_VAR/STORE_VAR used to lower through the
same mechanism as true globals (a named symbol), meaning two different
functions with a same-named local - or, worse, two recursive calls to the
*same* function - would alias onto one shared storage location. Added:
- `MIRV_LOCAL` value kind + `mir_local()`/`mir_new_local()` - a per-function
  slot id, resolved to a real rbp-relative offset only in the backend.
- `MIRFunc.local_count`/`param_count` - params now get real local slots too.
- ir_to_mir.c's TypeEnv tracks is_local/local_id per variable name; ALLOCA
  and function params allocate real slots; LOAD_VAR/STORE_VAR/ADDROF/the
  asm-block {varname} substitution all route through one `var_location()`
  helper so they can't drift out of sync with each other. True globals
  (IR_STATIC_VAR) are unaffected, still go through the symbol mechanism.
- lower_x64.c: frame layout is now [locals][int spills][float spills],
  each region computed from fn->local_count and ra's spill counts - no
  overlap. x64_lower_func computes total frame size internally instead of
  trusting an external parameter (removed that parameter entirely - a
  wrong caller-supplied frame size used to be a silent corruption risk).
  Non-naked functions now receive their incoming SysV-register params into
  local slots in the prologue - functions didn't actually receive
  parameters at all before this.
Verified with a genuine proof, not a toy case: recursive `fact(5)` built
from real Hylian-shaped IR (IR_LOAD_VAR/STORE_VAR/IR_CALL, not hand-built
MIR) - linked with plain `ld`, executed, returned 120. Recursion is about
as strong a test of real per-call-frame storage as exists: the old
aliased-global design would have corrupted this immediately since every
nested call would stomp the same shared storage for `n`.
Also fixed a real bug this surfaced: `mir_verify` trusted `fn->vreg_count`
blindly (crashed with a heap overflow on IR that assigns temp ids without
routing through `mir_new_vreg`, which is most of ir_to_mir.c's own output) -
now does the same defensive max-vreg scan `regalloc_run` already did.
Also: naked functions now skip the terminator requirement in mir_verify
(a naked function may legitimately end via a non-returning syscall).

## Float / xmm codegen
MIR_FADD/FSUB/FMUL/FDIV/FNEG/FCMP_*/I2F/F2I/F2F/BITCAST existed as MIR ops
since early in this session but lower_x64.c never implemented any of them -
any real float arithmetic beyond a literal would have silently hit the
unhandled-opcode fallback. Also fixed a related bug: MIR_MOV unconditionally
used the int operand path even when the destination was float-typed, which
would have misinterpreted an xmm-pool register index as a GPR-pool index.
Added: SSE2 scalar double AND single encoders (both genuinely supported,
not just F64 - the language only exposes F64 today but nothing here assumes
that), an xmm register pool (xmm0-13 allocatable, xmm14/15 reserved
scratch, mirroring the GPR reservation pattern), float-aware
load_operand_f/store_result_f, and a proper 3-region frame layout so float
spills don't collide with int spills or locals.
FNEG has no dedicated instruction - implemented via an integer XOR
round-trip through a GPR to flip the sign bit (no rodata constant plumbed
into this layer to use xorps instead). FCMP_* uses ucomisd/ucomiss flags
directly without the extra PF/unordered check IEEE-correct NaN handling
needs - flagged, not silently "correct" for NaN operands.
Verified with real arithmetic AND a forced spill: 1.5+2.5+3.0, negated
twice (round-trip), compared for equality, converted to int, linked,
executed - exit code 7, with 3 genuinely spilled floats (only 2 registers
made available) correctly round-tripping through their spill slots.

## String interpolation (IR_INTERP_STR)
Checked the actual data available: InterpSegment's expression segments
carry raw UNPARSED expression source text (see ast.h) - lower.c hands it
straight through without ever evaluating it into IR. ir_to_mir.c has no
parser/typechecker available to it (that's lower.c's exclusive machinery),
so it cannot evaluate arbitrary expression text itself - this is a real
gap in lower.c, not something fixable from this layer.
What IS fixed for real: literal-only segments are concatenated at compile
time and interned as one ordinary string constant (same as a plain string
literal - no runtime buffer/copy needed, since every literal segment's
content is already known at compile time). Verified by executing the
result: a real linked binary printed the literal-only concatenation
correctly via a raw write() syscall.
Expression segments get a specific, loud diagnostic naming exactly which
segment couldn't be evaluated and explaining the lower.c gap, rather than
being silently dropped, silently wrong, or crashing - each is cleanly
omitted from the result and the caller is told the result is incomplete.

Note: `ir_to_mir.c`/`.h` depend on Hylian's `ir.h`/`ir.c` directly (they're
frontend glue, not a generic lib) — canonical copy lives in
`Hylian/compiler/`, mirrored here for tracking only.
