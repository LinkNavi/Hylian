#include "encode_x64.h"

void enc_mov_rr(X64Buf *b, X64Reg dst, X64Reg src) {
    x64_buf_push(b, x64_rex(1, src, dst));
    x64_buf_push(b, 0x89);
    x64_buf_push(b, x64_modrm(3, src, dst));
}

void enc_mov_ri64(X64Buf *b, X64Reg dst, int64_t imm) {
    x64_buf_push(b, x64_rex(1, 0, dst));
    x64_buf_push(b, (uint8_t)(0xB8 + (dst & 7)));
    x64_buf_write64(b, (uint64_t)imm);
}

void enc_mov_load(X64Buf *b, X64Reg dst, X64Reg base, int32_t disp) {
    x64_buf_push(b, x64_rex(1, dst, base));
    x64_buf_push(b, 0x8B);
    x64_buf_push(b, x64_modrm(2, dst, base));
    x64_buf_write32(b, (uint32_t)disp);
}

void enc_mov_store(X64Buf *b, X64Reg base, int32_t disp, X64Reg src) {
    x64_buf_push(b, x64_rex(1, src, base));
    x64_buf_push(b, 0x89);
    x64_buf_push(b, x64_modrm(2, src, base));
    x64_buf_write32(b, (uint32_t)disp);
}

void enc_mov_store_sz(X64Buf *b, X64Reg base, int32_t disp, X64Reg src, int size) {
    switch (size) {
    case 1:
        /* mov r/m8, r8. The REX prefix is emitted unconditionally even when
           no bit in it is set: without one, encodings 4-7 mean ah/ch/dh/bh
           rather than spl/bpl/sil/dil, so a store from rsi would silently
           become a store from dh. A bare 0x40 REX forces the modern reading. */
        x64_buf_push(b, x64_rex(0, src, base));
        x64_buf_push(b, 0x88);
        break;
    case 2:
        x64_buf_push(b, 0x66); /* operand-size override: 16-bit */
        x64_buf_push(b, x64_rex(0, src, base));
        x64_buf_push(b, 0x89);
        break;
    case 4:
        x64_buf_push(b, x64_rex(0, src, base));
        x64_buf_push(b, 0x89);
        break;
    default:
        x64_buf_push(b, x64_rex(1, src, base));
        x64_buf_push(b, 0x89);
        break;
    }
    x64_buf_push(b, x64_modrm(2, src, base));
    x64_buf_write32(b, (uint32_t)disp);
}

void enc_mov_load_sz(X64Buf *b, X64Reg dst, X64Reg base, int32_t disp, int size, int is_signed) {
    switch (size) {
    case 1:
        /* movzx/movsx r64, r/m8 */
        x64_buf_push(b, x64_rex(1, dst, base));
        x64_buf_push(b, 0x0F);
        x64_buf_push(b, is_signed ? 0xBE : 0xB6);
        break;
    case 2:
        x64_buf_push(b, x64_rex(1, dst, base));
        x64_buf_push(b, 0x0F);
        x64_buf_push(b, is_signed ? 0xBF : 0xB7);
        break;
    case 4:
        if (is_signed) {
            x64_buf_push(b, x64_rex(1, dst, base)); /* movsxd r64, r/m32 */
            x64_buf_push(b, 0x63);
        } else {
            /* a 32-bit mov zero-extends into the full 64-bit register, so the
               plain 32-bit load is already the correct zero-extending form */
            x64_buf_push(b, x64_rex(0, dst, base));
            x64_buf_push(b, 0x8B);
        }
        break;
    default:
        x64_buf_push(b, x64_rex(1, dst, base));
        x64_buf_push(b, 0x8B);
        break;
    }
    x64_buf_push(b, x64_modrm(2, dst, base));
    x64_buf_write32(b, (uint32_t)disp);
}

size_t enc_lea_rip(X64Buf *b, X64Reg dst) {
    x64_buf_push(b, x64_rex(1, dst, 0));
    x64_buf_push(b, 0x8D);
    x64_buf_push(b, x64_modrm(0, dst, 5)); /* mod=00 rm=101 -> RIP+disp32 */
    size_t off = b->len;
    x64_buf_write32(b, 0);
    return off;
}

size_t enc_mov_load_rip(X64Buf *b, X64Reg dst) {
    x64_buf_push(b, x64_rex(1, dst, 0));
    x64_buf_push(b, 0x8B);
    x64_buf_push(b, x64_modrm(0, dst, 5));
    size_t off = b->len;
    x64_buf_write32(b, 0);
    return off;
}

size_t enc_mov_store_rip(X64Buf *b, X64Reg src) {
    x64_buf_push(b, x64_rex(1, src, 0));
    x64_buf_push(b, 0x89);
    x64_buf_push(b, x64_modrm(0, src, 5));
    size_t off = b->len;
    x64_buf_write32(b, 0);
    return off;
}

void enc_alu_rr(X64Buf *b, AluOp op, X64Reg dst, X64Reg src) {
    x64_buf_push(b, x64_rex(1, src, dst));
    x64_buf_push(b, (uint8_t)op);
    x64_buf_push(b, x64_modrm(3, src, dst));
}

/* 0x81 /digit id32 - the ALU-with-32-bit-immediate form. /digit differs per
   op and isn't the same numbering as the reg,reg opcode byte, hence the map. */
void enc_alu_ri32(X64Buf *b, AluOp op, X64Reg dst, int32_t imm) {
    int digit;
    switch (op) {
        case ALU_ADD: digit = 0; break;
        case ALU_OR:  digit = 1; break;
        case ALU_AND: digit = 4; break;
        case ALU_SUB: digit = 5; break;
        case ALU_XOR: digit = 6; break;
        default:      digit = 7; break; /* ALU_CMP */
    }
    x64_buf_push(b, x64_rex(1, 0, dst));
    x64_buf_push(b, 0x81);
    x64_buf_push(b, x64_modrm(3, digit, dst));
    x64_buf_write32(b, (uint32_t)imm);
}

void enc_imul_rr(X64Buf *b, X64Reg dst, X64Reg src) {
    x64_buf_push(b, x64_rex(1, dst, src));
    x64_buf_push(b, 0x0F);
    x64_buf_push(b, 0xAF);
    x64_buf_push(b, x64_modrm(3, dst, src));
}

void enc_cqo(X64Buf *b) {
    x64_buf_push(b, 0x48);
    x64_buf_push(b, 0x99);
}

void enc_idiv_r(X64Buf *b, X64Reg divisor) {
    x64_buf_push(b, x64_rex(1, 0, divisor));
    x64_buf_push(b, 0xF7);
    x64_buf_push(b, x64_modrm(3, 7, divisor));
}

void enc_div_r(X64Buf *b, X64Reg divisor) {
    x64_buf_push(b, x64_rex(1, 0, divisor));
    x64_buf_push(b, 0xF7);
    x64_buf_push(b, x64_modrm(3, 6, divisor));
}

void enc_neg_r(X64Buf *b, X64Reg reg) {
    x64_buf_push(b, x64_rex(1, 0, reg));
    x64_buf_push(b, 0xF7);
    x64_buf_push(b, x64_modrm(3, 3, reg));
}

void enc_not_r(X64Buf *b, X64Reg reg) {
    x64_buf_push(b, x64_rex(1, 0, reg));
    x64_buf_push(b, 0xF7);
    x64_buf_push(b, x64_modrm(3, 2, reg));
}

void enc_shift_cl(X64Buf *b, ShiftKind kind, X64Reg reg) {
    x64_buf_push(b, x64_rex(1, 0, reg));
    x64_buf_push(b, 0xD3);
    x64_buf_push(b, x64_modrm(3, (int)kind, reg));
}

void enc_setcc(X64Buf *b, CondCode cc, X64Reg dst8) {
    /* always emit REX (even W=0) - needed to address spl/bpl/sil/dil as
       byte registers instead of falling back to ah/ch/dh/bh */
    x64_buf_push(b, x64_rex(0, 0, dst8));
    x64_buf_push(b, 0x0F);
    x64_buf_push(b, (uint8_t)(0x90 | cc));
    x64_buf_push(b, x64_modrm(3, 0, dst8));
}

void enc_movzx_r64_r8(X64Buf *b, X64Reg dst, X64Reg src) {
    x64_buf_push(b, x64_rex(1, dst, src));
    x64_buf_push(b, 0x0F);
    x64_buf_push(b, 0xB6);
    x64_buf_push(b, x64_modrm(3, dst, src));
}

/* dst = the low `src_size` bytes of src, widened to a full 64-bit register
   either sign- or zero-extended. This one primitive covers MIR_SEXT, MIR_ZEXT
   and MIR_TRUNC: truncation in a register-based backend IS "keep the low N
   bytes and re-widen", the only question being which extension to re-widen
   with, and that's exactly what `is_signed` selects. */
void enc_ext_rr(X64Buf *b, X64Reg dst, X64Reg src, int src_size, int is_signed) {
    switch (src_size) {
    case 1:
        x64_buf_push(b, x64_rex(1, dst, src));
        x64_buf_push(b, 0x0F);
        x64_buf_push(b, is_signed ? 0xBE : 0xB6);
        x64_buf_push(b, x64_modrm(3, dst, src));
        break;
    case 2:
        x64_buf_push(b, x64_rex(1, dst, src));
        x64_buf_push(b, 0x0F);
        x64_buf_push(b, is_signed ? 0xBF : 0xB7);
        x64_buf_push(b, x64_modrm(3, dst, src));
        break;
    case 4:
        if (is_signed) {
            x64_buf_push(b, x64_rex(1, dst, src)); /* movsxd r64, r/m32 */
            x64_buf_push(b, 0x63);
        } else {
            /* 32-bit mov already zero-extends into the upper 32 bits */
            x64_buf_push(b, x64_rex(0, dst, src));
            x64_buf_push(b, 0x8B);
        }
        x64_buf_push(b, x64_modrm(3, dst, src));
        break;
    default:
        if (dst != src) enc_mov_rr(b, dst, src);
        break;
    }
}

void enc_push(X64Buf *b, X64Reg reg) {
    if (reg >= 8) x64_buf_push(b, 0x41);
    x64_buf_push(b, (uint8_t)(0x50 + (reg & 7)));
}

void enc_push_imm32(X64Buf *b, int32_t imm) {
    x64_buf_push(b, 0x68);
    x64_buf_write32(b, (uint32_t)imm);
}

void enc_pop(X64Buf *b, X64Reg reg) {
    if (reg >= 8) x64_buf_push(b, 0x41);
    x64_buf_push(b, (uint8_t)(0x58 + (reg & 7)));
}

void enc_lea_mem(X64Buf *b, X64Reg dst, X64Reg base, int32_t disp) {
    x64_buf_push(b, x64_rex(1, dst, base));
    x64_buf_push(b, 0x8D);
    x64_buf_push(b, x64_modrm(2, dst, base)); /* mod=10 -> disp32, safe for any base incl rbp/r13/rsp/r12 */
    x64_buf_write32(b, (uint32_t)disp);
}

void enc_ret(X64Buf *b) { x64_buf_push(b, 0xC3); }
void enc_leave(X64Buf *b) { x64_buf_push(b, 0xC9); }

size_t enc_call_rel32(X64Buf *b) {
    x64_buf_push(b, 0xE8);
    size_t off = b->len;
    x64_buf_write32(b, 0);
    return off;
}

size_t enc_jmp_rel32(X64Buf *b) {
    x64_buf_push(b, 0xE9);
    size_t off = b->len;
    x64_buf_write32(b, 0);
    return off;
}

size_t enc_jcc_rel32(X64Buf *b, CondCode cc) {
    x64_buf_push(b, 0x0F);
    x64_buf_push(b, (uint8_t)(0x80 | cc));
    size_t off = b->len;
    x64_buf_write32(b, 0);
    return off;
}

void enc_syscall(X64Buf *b) { x64_buf_push(b, 0x0F); x64_buf_push(b, 0x05); }
void enc_cli(X64Buf *b) { x64_buf_push(b, 0xFA); }
void enc_sti(X64Buf *b) { x64_buf_push(b, 0xFB); }
void enc_hlt(X64Buf *b) { x64_buf_push(b, 0xF4); }
void enc_iretq(X64Buf *b) { x64_buf_push(b, 0x48); x64_buf_push(b, 0xCF); }
void enc_sysretq(X64Buf *b) { x64_buf_push(b, 0x48); x64_buf_push(b, 0x0F); x64_buf_push(b, 0x07); }
void enc_in_al_dx(X64Buf *b) { x64_buf_push(b, 0xEC); }
void enc_out_dx_al(X64Buf *b) { x64_buf_push(b, 0xEE); }
/* 16-bit port I/O - same opcodes as the 8-bit forms, with the operand-size
   override prefix (0x66) to select AX over AL. */
void enc_in_ax_dx(X64Buf *b) { x64_buf_push(b, 0x66); x64_buf_push(b, 0xED); }
void enc_out_dx_ax(X64Buf *b) { x64_buf_push(b, 0x66); x64_buf_push(b, 0xEF); }

/* lgdt/lidt/invlpg take a MEMORY operand ([addr], mod=0) - the caller MUST
   pass a register that is safe to use as a bare [reg] base in the mod=0
   ModRM encoding, i.e. not RSP/R12 (needs a SIB byte) and not RBP/R13
   (mod=0 rm=101 means RIP-relative, not [rbp]/[r13]). Every call site in
   lower_x64.c loads the operand into RAX specifically for this reason. */
void enc_lgdt_mem(X64Buf *b, X64Reg addr) {
    if (addr >= 8) x64_buf_push(b, 0x41);
    x64_buf_push(b, 0x0F);
    x64_buf_push(b, 0x01);
    x64_buf_push(b, x64_modrm(0, 2, addr));
}
void enc_lidt_mem(X64Buf *b, X64Reg addr) {
    if (addr >= 8) x64_buf_push(b, 0x41);
    x64_buf_push(b, 0x0F);
    x64_buf_push(b, 0x01);
    x64_buf_push(b, x64_modrm(0, 3, addr));
}
void enc_invlpg_mem(X64Buf *b, X64Reg addr) {
    if (addr >= 8) x64_buf_push(b, 0x41);
    x64_buf_push(b, 0x0F);
    x64_buf_push(b, 0x01);
    x64_buf_push(b, x64_modrm(0, 7, addr));
}
/* ltr r/m16 - register form. A segment selector is always 16 bits; no 66
   operand-size prefix is needed for this particular opcode/group. */
void enc_ltr_r16(X64Buf *b, X64Reg reg) {
    if (reg >= 8) x64_buf_push(b, 0x41);
    x64_buf_push(b, 0x0F);
    x64_buf_push(b, 0x00);
    x64_buf_push(b, x64_modrm(3, 3, reg));
}
void enc_wrmsr(X64Buf *b) { x64_buf_push(b, 0x0F); x64_buf_push(b, 0x30); }
void enc_rdmsr(X64Buf *b) { x64_buf_push(b, 0x0F); x64_buf_push(b, 0x32); }

void enc_mov_r_from_crN(X64Buf *b, X64Reg dst, int crN) {
    x64_buf_push(b, x64_rex(1, crN, dst));
    x64_buf_push(b, 0x0F);
    x64_buf_push(b, 0x20);
    x64_buf_push(b, x64_modrm(3, crN, dst));
}

void enc_mov_crN_from_r(X64Buf *b, int crN, X64Reg src) {
    x64_buf_push(b, x64_rex(1, crN, src));
    x64_buf_push(b, 0x0F);
    x64_buf_push(b, 0x22);
    x64_buf_push(b, x64_modrm(3, crN, src));
}

void enc_rep_stosb(X64Buf *b) { x64_buf_push(b, 0xF3); x64_buf_push(b, 0xAA); }
void enc_rep_movsb(X64Buf *b) { x64_buf_push(b, 0xF3); x64_buf_push(b, 0xA4); }

/* ---- SSE2 scalar double (F2 0F prefix) ---- */

void enc_movsd_xx(X64Buf *b, int dst_xmm, int src_xmm) {
    x64_buf_push(b, 0xF2);
    x64_buf_push(b, x64_rex(0, dst_xmm, src_xmm));
    x64_buf_push(b, 0x0F); x64_buf_push(b, 0x10);
    x64_buf_push(b, x64_modrm(3, dst_xmm, src_xmm));
}
void enc_movsd_load(X64Buf *b, int dst_xmm, X64Reg base, int32_t disp) {
    x64_buf_push(b, 0xF2);
    x64_buf_push(b, x64_rex(0, dst_xmm, base));
    x64_buf_push(b, 0x0F); x64_buf_push(b, 0x10);
    x64_buf_push(b, x64_modrm(2, dst_xmm, base));
    x64_buf_write32(b, (uint32_t)disp);
}
void enc_movsd_store(X64Buf *b, X64Reg base, int32_t disp, int src_xmm) {
    x64_buf_push(b, 0xF2);
    x64_buf_push(b, x64_rex(0, src_xmm, base));
    x64_buf_push(b, 0x0F); x64_buf_push(b, 0x11);
    x64_buf_push(b, x64_modrm(2, src_xmm, base));
    x64_buf_write32(b, (uint32_t)disp);
}
static void enc_sse_binop(X64Buf *b, uint8_t prefix, uint8_t opcode, int dst, int src) {
    x64_buf_push(b, prefix);
    x64_buf_push(b, x64_rex(0, dst, src));
    x64_buf_push(b, 0x0F); x64_buf_push(b, opcode);
    x64_buf_push(b, x64_modrm(3, dst, src));
}
void enc_addsd(X64Buf *b, int dst_xmm, int src_xmm) { enc_sse_binop(b, 0xF2, 0x58, dst_xmm, src_xmm); }
void enc_subsd(X64Buf *b, int dst_xmm, int src_xmm) { enc_sse_binop(b, 0xF2, 0x5C, dst_xmm, src_xmm); }
void enc_mulsd(X64Buf *b, int dst_xmm, int src_xmm) { enc_sse_binop(b, 0xF2, 0x59, dst_xmm, src_xmm); }
void enc_divsd(X64Buf *b, int dst_xmm, int src_xmm) { enc_sse_binop(b, 0xF2, 0x5E, dst_xmm, src_xmm); }

void enc_ucomisd(X64Buf *b, int a_xmm, int b_xmm) {
    x64_buf_push(b, 0x66);
    x64_buf_push(b, x64_rex(0, a_xmm, b_xmm));
    x64_buf_push(b, 0x0F); x64_buf_push(b, 0x2E);
    x64_buf_push(b, x64_modrm(3, a_xmm, b_xmm));
}

void enc_cvtsi2sd(X64Buf *b, int dst_xmm, X64Reg src_gpr) {
    x64_buf_push(b, 0xF2);
    x64_buf_push(b, x64_rex(1, dst_xmm, src_gpr));
    x64_buf_push(b, 0x0F); x64_buf_push(b, 0x2A);
    x64_buf_push(b, x64_modrm(3, dst_xmm, src_gpr));
}
void enc_cvttsd2si(X64Buf *b, X64Reg dst_gpr, int src_xmm) {
    x64_buf_push(b, 0xF2);
    x64_buf_push(b, x64_rex(1, dst_gpr, src_xmm));
    x64_buf_push(b, 0x0F); x64_buf_push(b, 0x2C);
    x64_buf_push(b, x64_modrm(3, dst_gpr, src_xmm));
}

/* ---- SSE scalar single (F3 0F prefix) - same shapes as the sd forms ---- */

void enc_movss_xx(X64Buf *b, int dst_xmm, int src_xmm) {
    x64_buf_push(b, 0xF3);
    x64_buf_push(b, x64_rex(0, dst_xmm, src_xmm));
    x64_buf_push(b, 0x0F); x64_buf_push(b, 0x10);
    x64_buf_push(b, x64_modrm(3, dst_xmm, src_xmm));
}
void enc_movss_load(X64Buf *b, int dst_xmm, X64Reg base, int32_t disp) {
    x64_buf_push(b, 0xF3);
    x64_buf_push(b, x64_rex(0, dst_xmm, base));
    x64_buf_push(b, 0x0F); x64_buf_push(b, 0x10);
    x64_buf_push(b, x64_modrm(2, dst_xmm, base));
    x64_buf_write32(b, (uint32_t)disp);
}
void enc_movss_store(X64Buf *b, X64Reg base, int32_t disp, int src_xmm) {
    x64_buf_push(b, 0xF3);
    x64_buf_push(b, x64_rex(0, src_xmm, base));
    x64_buf_push(b, 0x0F); x64_buf_push(b, 0x11);
    x64_buf_push(b, x64_modrm(2, src_xmm, base));
    x64_buf_write32(b, (uint32_t)disp);
}
void enc_addss(X64Buf *b, int dst_xmm, int src_xmm) { enc_sse_binop(b, 0xF3, 0x58, dst_xmm, src_xmm); }
void enc_subss(X64Buf *b, int dst_xmm, int src_xmm) { enc_sse_binop(b, 0xF3, 0x5C, dst_xmm, src_xmm); }
void enc_mulss(X64Buf *b, int dst_xmm, int src_xmm) { enc_sse_binop(b, 0xF3, 0x59, dst_xmm, src_xmm); }
void enc_divss(X64Buf *b, int dst_xmm, int src_xmm) { enc_sse_binop(b, 0xF3, 0x5E, dst_xmm, src_xmm); }

void enc_ucomiss(X64Buf *b, int a_xmm, int b_xmm) {
    x64_buf_push(b, x64_rex(0, a_xmm, b_xmm));
    x64_buf_push(b, 0x0F); x64_buf_push(b, 0x2E);
    x64_buf_push(b, x64_modrm(3, a_xmm, b_xmm));
}

void enc_cvtsi2ss(X64Buf *b, int dst_xmm, X64Reg src_gpr) {
    x64_buf_push(b, 0xF3);
    x64_buf_push(b, x64_rex(1, dst_xmm, src_gpr));
    x64_buf_push(b, 0x0F); x64_buf_push(b, 0x2A);
    x64_buf_push(b, x64_modrm(3, dst_xmm, src_gpr));
}
void enc_cvttss2si(X64Buf *b, X64Reg dst_gpr, int src_xmm) {
    x64_buf_push(b, 0xF3);
    x64_buf_push(b, x64_rex(1, dst_gpr, src_xmm));
    x64_buf_push(b, 0x0F); x64_buf_push(b, 0x2C);
    x64_buf_push(b, x64_modrm(3, dst_gpr, src_xmm));
}

void enc_cvtsd2ss(X64Buf *b, int dst_xmm, int src_xmm) { enc_sse_binop(b, 0xF2, 0x5A, dst_xmm, src_xmm); }
void enc_cvtss2sd(X64Buf *b, int dst_xmm, int src_xmm) { enc_sse_binop(b, 0xF3, 0x5A, dst_xmm, src_xmm); }

void enc_movq_gpr_to_xmm(X64Buf *b, int dst_xmm, X64Reg src_gpr) {
    x64_buf_push(b, 0x66);
    x64_buf_push(b, x64_rex(1, dst_xmm, src_gpr));
    x64_buf_push(b, 0x0F); x64_buf_push(b, 0x6E);
    x64_buf_push(b, x64_modrm(3, dst_xmm, src_gpr));
}
void enc_movq_xmm_to_gpr(X64Buf *b, X64Reg dst_gpr, int src_xmm) {
    x64_buf_push(b, 0x66);
    x64_buf_push(b, x64_rex(1, src_xmm, dst_gpr));
    x64_buf_push(b, 0x0F); x64_buf_push(b, 0x7E);
    x64_buf_push(b, x64_modrm(3, src_xmm, dst_gpr));
}
