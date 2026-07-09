#ifndef MINIASM_H
#define MINIASM_H

#include "hyx64.h"

/* Assembles pre-substituted x86-64 text (no more {N} placeholders - the
   caller already replaced those with real register names or resolved
   them via miniasm_resolve, see lower_x64.c's MIR_ASM_TEXT case) straight
   to bytes, appending to `out` and recording relocations for any call/jmp
   targets that aren't a label defined within this same block.

   Scope (matches "basic, well-used instructions" - not a general assembler):
     - 64-bit GPRs only (rax..r15), no 32/16/8-bit sub-registers
     - mov/add/sub/and/or/xor/cmp: reg,reg and reg,imm32 (real hardware
       encodings for both - no synthesized scratch registers, since user
       asm can legally reference any register including ones the rest of
       the backend treats as reserved scratch)
     - imul reg,reg; neg/not/push/pop reg; shl/shr/sar reg,cl (literal
       "cl" required, no immediate shift-count form yet)
     - mov reg,[reg±disp]  and  mov [reg±disp],reg  (no SIB/index, no
       rip-relative to globals from inside an asm block yet)
     - lea reg,[reg±disp]
     - jmp/jcc/call to a local label (defined via "name:" in the same
       block) or an external symbol (becomes a relocation)
     - ret, syscall, cli, sti, iretq, wrmsr, rdmsr, in al,dx / out dx,al,
       mov reg,crN / mov crN,reg
     - setcc reg8 (low byte of a 64-bit register name, e.g. "sete rax"
       sets al and the rest of rax is left as-is - no implicit movzx,
       unlike MIR's own comparison lowering)

   Returns 1 on success. On failure returns 0 and writes a message into
   err (if err_cap > 0) - never silently drops an unrecognized line. */
int miniasm_assemble(const char *text, X64Buf *out, X64RelocList *out_relocs,
                      char *err, size_t err_cap);

#endif
