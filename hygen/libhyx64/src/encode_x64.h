#ifndef HYX64_ENCODE_H
#define HYX64_ENCODE_H

#include "hyx64.h"

/* All of these assume 64-bit operand size (REX.W=1) unless named otherwise -
   that's the only width this MVP needs since Hylian's `int` is i64. 32-bit
   forms can be added once the language grows sized integers. */

static inline uint8_t x64_rex(int w, int r_reg, int rm_reg) {
    return 0x40 | (w << 3) | (((r_reg >> 3) & 1) << 2) | ((rm_reg >> 3) & 1);
}
static inline uint8_t x64_modrm(int mod, int reg, int rm) {
    return (uint8_t)((mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

/* mov r/m64, r64 (dst is rm, src is reg) - dst = src */
void enc_mov_rr(X64Buf *b, X64Reg dst, X64Reg src);
void enc_mov_ri64(X64Buf *b, X64Reg dst, int64_t imm);
/* load/store to [base + disp32] */
void enc_mov_load(X64Buf *b, X64Reg dst, X64Reg base, int32_t disp);
void enc_mov_store(X64Buf *b, X64Reg base, int32_t disp, X64Reg src);
/* Sized variants, for dereferencing a real typed pointer rather than touching
   an 8-byte stack slot. `size` is 1/2/4/8 bytes; anything else is treated as 8.
   Using the 8-byte forms for a `*uint8` write is not merely wasteful, it is
   wrong: it scribbles over the 7 bytes that follow the target. Loads take
   `is_signed` and widen to the full 64-bit register accordingly, so the value
   the rest of the backend sees is always a correct full-width integer. */
void enc_mov_store_sz(X64Buf *b, X64Reg base, int32_t disp, X64Reg src, int size);
void enc_mov_load_sz(X64Buf *b, X64Reg dst, X64Reg base, int32_t disp, int size, int is_signed);
/* rip-relative lea/load - disp32 left as 0, caller records a relocation */
size_t enc_lea_rip(X64Buf *b, X64Reg dst);
void enc_lea_mem(X64Buf *b, X64Reg dst, X64Reg base, int32_t disp); /* lea dst, [base+disp] */
size_t enc_mov_load_rip(X64Buf *b, X64Reg dst);
size_t enc_mov_store_rip(X64Buf *b, X64Reg src);

/* two-operand ALU: dst = dst OP src (add/sub/and/or/xor/cmp share this shape) */
typedef enum { ALU_ADD = 0x01, ALU_OR = 0x09, ALU_AND = 0x21,
               ALU_SUB = 0x29, ALU_XOR = 0x31, ALU_CMP = 0x39 } AluOp;
void enc_alu_rr(X64Buf *b, AluOp op, X64Reg dst, X64Reg src);
void enc_alu_ri32(X64Buf *b, AluOp op, X64Reg dst, int32_t imm); /* dst = dst OP imm, real 0x81 /r encoding */

void enc_imul_rr(X64Buf *b, X64Reg dst, X64Reg src); /* dst = dst * src */
void enc_cqo(X64Buf *b);
void enc_idiv_r(X64Buf *b, X64Reg divisor);
void enc_div_r(X64Buf *b, X64Reg divisor);
void enc_neg_r(X64Buf *b, X64Reg reg);
void enc_not_r(X64Buf *b, X64Reg reg);

typedef enum { SHIFT_SHL = 4, SHIFT_SHR = 5, SHIFT_SAR = 7 } ShiftKind;
void enc_shift_cl(X64Buf *b, ShiftKind kind, X64Reg reg); /* reg = reg SHIFT cl */

/* condition codes for setcc/jcc, values match Intel's cc field */
typedef enum {
    CC_E = 0x4, CC_NE = 0x5, CC_L = 0xC, CC_LE = 0xE, CC_G = 0xF, CC_GE = 0xD,
    CC_B = 0x2, CC_BE = 0x6, CC_A = 0x7, CC_AE = 0x3, /* unsigned variants */
} CondCode;
void enc_setcc(X64Buf *b, CondCode cc, X64Reg dst8); /* dst8 = cc ? 1 : 0, full reg zero-extended by caller's movzx */
void enc_movzx_r64_r8(X64Buf *b, X64Reg dst, X64Reg src);
/* dst = low `src_size` bytes of src, widened to 64 bits (sign- or zero-extended).
   Backs MIR_SEXT / MIR_ZEXT / MIR_TRUNC. */
void enc_ext_rr(X64Buf *b, X64Reg dst, X64Reg src, int src_size, int is_signed);

void enc_push(X64Buf *b, X64Reg reg);
void enc_pop(X64Buf *b, X64Reg reg);
void enc_ret(X64Buf *b);
void enc_leave(X64Buf *b); /* mov rsp,rbp; pop rbp in one byte */

size_t enc_call_rel32(X64Buf *b); /* returns offset of the disp32 to relocate */
size_t enc_jmp_rel32(X64Buf *b);
size_t enc_jcc_rel32(X64Buf *b, CondCode cc);

void enc_syscall(X64Buf *b);
void enc_cli(X64Buf *b);
void enc_sti(X64Buf *b);
void enc_iretq(X64Buf *b);
void enc_hlt(X64Buf *b);
void enc_in_al_dx(X64Buf *b);
void enc_out_dx_al(X64Buf *b);
void enc_in_ax_dx(X64Buf *b);
void enc_out_dx_ax(X64Buf *b);
void enc_lgdt_mem(X64Buf *b, X64Reg addr);
void enc_lidt_mem(X64Buf *b, X64Reg addr);
void enc_invlpg_mem(X64Buf *b, X64Reg addr);
void enc_ltr_r16(X64Buf *b, X64Reg reg);
void enc_wrmsr(X64Buf *b);
void enc_rdmsr(X64Buf *b);
void enc_mov_r_from_crN(X64Buf *b, X64Reg dst, int crN);
void enc_mov_crN_from_r(X64Buf *b, int crN, X64Reg src);

void enc_rep_stosb(X64Buf *b); /* memset core: rdi=ptr, al=byte, rcx=count */
void enc_rep_movsb(X64Buf *b); /* memcpy core: rdi=dst, rsi=src, rcx=count */

/* ---- SSE2 scalar float (xmm register numbers are plain ints 0-15, same
   REX/ModRM bit scheme as GPRs - no separate register type needed) ---- */

void enc_movsd_xx(X64Buf *b, int dst_xmm, int src_xmm);
void enc_movsd_load(X64Buf *b, int dst_xmm, X64Reg base, int32_t disp);
void enc_movsd_store(X64Buf *b, X64Reg base, int32_t disp, int src_xmm);
void enc_addsd(X64Buf *b, int dst_xmm, int src_xmm);
void enc_subsd(X64Buf *b, int dst_xmm, int src_xmm);
void enc_mulsd(X64Buf *b, int dst_xmm, int src_xmm);
void enc_divsd(X64Buf *b, int dst_xmm, int src_xmm);
void enc_ucomisd(X64Buf *b, int a_xmm, int b_xmm);
void enc_cvtsi2sd(X64Buf *b, int dst_xmm, X64Reg src_gpr);  /* int64 -> double */
void enc_cvttsd2si(X64Buf *b, X64Reg dst_gpr, int src_xmm); /* double -> int64, truncating */

void enc_movss_xx(X64Buf *b, int dst_xmm, int src_xmm);
void enc_movss_load(X64Buf *b, int dst_xmm, X64Reg base, int32_t disp);
void enc_movss_store(X64Buf *b, X64Reg base, int32_t disp, int src_xmm);
void enc_addss(X64Buf *b, int dst_xmm, int src_xmm);
void enc_subss(X64Buf *b, int dst_xmm, int src_xmm);
void enc_mulss(X64Buf *b, int dst_xmm, int src_xmm);
void enc_divss(X64Buf *b, int dst_xmm, int src_xmm);
void enc_ucomiss(X64Buf *b, int a_xmm, int b_xmm);
void enc_cvtsi2ss(X64Buf *b, int dst_xmm, X64Reg src_gpr);  /* int64 -> float */
void enc_cvttss2si(X64Buf *b, X64Reg dst_gpr, int src_xmm); /* float -> int64, truncating */

void enc_cvtsd2ss(X64Buf *b, int dst_xmm, int src_xmm); /* double -> float */
void enc_cvtss2sd(X64Buf *b, int dst_xmm, int src_xmm); /* float -> double */

/* bit-level round-trip between a GPR and an xmm reg - used for FNEG (flip
   the sign bit via integer XOR, since there's no free rodata constant
   plumbed into this layer yet) and for BITCAST between same-width int/float */
void enc_movq_gpr_to_xmm(X64Buf *b, int dst_xmm, X64Reg src_gpr);
void enc_movq_xmm_to_gpr(X64Buf *b, X64Reg dst_gpr, int src_xmm);

#endif
