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
- [ ] libhyregalloc (linear scan)
- [ ] libhyx64 (byte encoder)
- [ ] libhyobj (ELF writer)

Note: `ir_to_mir.c`/`.h` depend on Hylian's `ir.h`/`ir.c` directly (they're
frontend glue, not a generic lib) — canonical copy lives in
`Hylian/compiler/`, mirrored here for tracking only.
