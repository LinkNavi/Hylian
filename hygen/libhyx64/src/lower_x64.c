#include "encode_x64.h"
#include "miniasm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* label fixups: intra-function jumps, resolved once the whole function has
   been emitted (labels can be forward *or* backward references) */
typedef struct { size_t code_offset; int label_id; } LabelFixup;

typedef struct {
    X64Buf       *code;
    X64RelocList *relocs;
    const RegAllocResult *ra;
    const MIRFunc *fn; /* needed for local_count, to keep local slots and spill slots from overlapping */

    int          *label_offset;   /* label_id -> code offset, -1 until seen */
    int           max_label;

    LabelFixup   *fixups;
    int           fixup_count, fixup_cap;

    int          *use_count;      /* per vreg: how many operand reads (for compare/branch fusion) */
    int          *pair_count;     /* per vreg: uses that are a branch directly after the defining compare */
    int           flags_cc;       /* condition code left in EFLAGS by a fused compare, or -1 */
    int           flags_for;      /* ...and the (never materialised) vreg that compare defines */
    int           nv;
} LowerCtx;

static void add_fixup(LowerCtx *ctx, size_t code_offset, int label_id) {
    if (ctx->fixup_count == ctx->fixup_cap) {
        ctx->fixup_cap *= 2;
        ctx->fixups = realloc(ctx->fixups, ctx->fixup_cap * sizeof(LabelFixup));
    }
    ctx->fixups[ctx->fixup_count].code_offset = code_offset;
    ctx->fixups[ctx->fixup_count].label_id = label_id;
    ctx->fixup_count++;
}

static int32_t local_offset(int local_id) {
    /* locals live closest to rbp, one 8-byte slot each */
    return -8 * (local_id + 1);
}

static int32_t int_spill_offset(const MIRFunc *fn, int spill_slot) {
    /* int spill slots sit below all local slots */
    return -8 * (fn->local_count + spill_slot + 1);
}

static int32_t float_spill_offset(const MIRFunc *fn, const RegAllocResult *ra, int spill_slot) {
    /* float spill slots sit below locals AND int spills */
    return -8 * (fn->local_count + ra->num_int_spill_slots + spill_slot + 1);
}

/* One 8-byte home per callee-saved register this function actually used, below
   every spill slot. Saving to a fixed frame slot rather than push/pop keeps rsp
   constant through the body, which matters because MIR_CALL/MIR_SYSCALL do
   their own temporary pushes and the 16-byte call alignment has to hold. */
/* multi-byte NOPs (Intel SDM recommended forms) */
static void emit_nops(X64Buf *b, int n) {
    static const uint8_t nop[10][10] = {
        {0x90},
        {0x66, 0x90},
        {0x0F, 0x1F, 0x00},
        {0x0F, 0x1F, 0x40, 0x00},
        {0x0F, 0x1F, 0x44, 0x00, 0x00},
        {0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00},
        {0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00},
        {0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x66, 0x2E, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
    };
    while (n > 0) {
        int k = n > 10 ? 10 : n;
        x64_buf_write(b, nop[k - 1], (size_t)k);
        n -= k;
    }
}

static int popcount32(unsigned m) { int n = 0; while (m) { n += m & 1; m >>= 1; } return n; }

static int32_t callee_save_offset(const MIRFunc *fn, const RegAllocResult *ra, int idx) {
    return -8 * (fn->local_count + ra->num_int_spill_slots
                 + ra->num_float_spill_slots + idx + 1);
}

/* Scratch space handed out by MIR_ALLOCA_LOCAL, below everything else. */
static int32_t alloca_region_base(const MIRFunc *fn, const RegAllocResult *ra) {
    return 8 * (fn->local_count + ra->num_int_spill_slots
                + ra->num_float_spill_slots + popcount32(ra->callee_saved_int_mask));
}

static int alloca_total_bytes(const MIRFunc *fn) {
    int total = 0;
    for (int i = 0; i < fn->count; i++) {
        if (fn->instrs[i].op != MIR_ALLOCA_LOCAL) continue;
        int n = fn->instrs[i].extra_int;
        if (n <= 0) n = 8;
        total += (n + 7) & ~7; /* keep every allocation 8-byte aligned */
    }
    return total;
}

static int32_t total_frame_bytes(const MIRFunc *fn, const RegAllocResult *ra) {
    int raw = 8 * (fn->local_count + ra->num_int_spill_slots + ra->num_float_spill_slots
                   + popcount32(ra->callee_saved_int_mask))
              + alloca_total_bytes(fn);
    return ((raw + 15) / 16) * 16;
}

/* Always materializes v into `scratch` via an explicit copy - never returns
   an aliased pointer to a live vreg's actual register. Slightly more moves
   than a fully optimal allocator-aware selector would emit, but guarantees
   we never accidentally clobber a vreg that's still live. Redundant-move
   elimination is a follow-up peephole pass, not solved here. */
static X64Reg load_operand(LowerCtx *ctx, MIRValue v, X64Reg scratch) {
    switch (v.kind) {
    case MIRV_IMM_INT:
        enc_mov_ri64(ctx->code, scratch, v.imm_int);
        return scratch;
    case MIRV_VREG: {
        MCLoc loc = ctx->ra->vreg_locs[v.vreg];
        if (loc.kind == MCLOC_REG) {
            enc_mov_rr(ctx->code, scratch, x64_int_reg_pool[loc.reg_id]);
        } else {
            int32_t off = loc.is_float ? float_spill_offset(ctx->fn, ctx->ra, loc.spill_slot)
                                        : int_spill_offset(ctx->fn, loc.spill_slot);
            enc_mov_load(ctx->code, scratch, X64_RBP, off);
        }
        return scratch;
    }
    case MIRV_GLOBAL: {
        size_t off = enc_mov_load_rip(ctx->code, scratch);
        x64_reloc_add(ctx->relocs, off, v.global_name, X64_RELOC_RIP32);
        return scratch;
    }
    case MIRV_LOCAL:
        enc_mov_load(ctx->code, scratch, X64_RBP, local_offset(v.local_id));
        return scratch;
    default:
        fprintf(stderr, "[lower_x64] unsupported operand kind, emitting 0\n");
        enc_mov_ri64(ctx->code, scratch, 0);
        return scratch;
    }
}

static void store_result(LowerCtx *ctx, MIRValue dest, X64Reg from) {
    if (dest.kind != MIRV_VREG) return; /* MIR_NONE dest (e.g. void call) */
    MCLoc loc = ctx->ra->vreg_locs[dest.vreg];
    if (loc.kind == MCLOC_REG) {
        X64Reg d = x64_int_reg_pool[loc.reg_id];
        if (d != from) enc_mov_rr(ctx->code, d, from);
    } else {
        int32_t off = loc.is_float ? float_spill_offset(ctx->fn, ctx->ra, loc.spill_slot)
                                    : int_spill_offset(ctx->fn, loc.spill_slot);
        enc_mov_store(ctx->code, X64_RBP, off, from);
    }
}

/* Float counterpart to load_operand/store_result - same "always materialize
   a fresh copy" philosophy, dispatches ss vs sd purely off MIRValue.type so
   both F32 and F64 are genuinely supported, not just F64. */
static int load_operand_f(LowerCtx *ctx, MIRValue v, int scratch_xmm) {
    int is32 = (v.type == MIR_F32);
    switch (v.kind) {
    case MIRV_IMM_FLOAT: {
        uint64_t bits;
        if (is32) { float f = (float)v.imm_float; uint32_t b32; memcpy(&b32, &f, 4); bits = b32; }
        else      { memcpy(&bits, &v.imm_float, 8); }
        if (bits == 0) {                                  /* +0.0: xorps */
            enc_xorps_xx(ctx->code, scratch_xmm, scratch_xmm);
        } else if (x64_get_const_pool()) {
            /* movss/movsd xmm, [rip + constant] */
            size_t off = enc_sse_rip(ctx->code, is32 ? 0xF3 : 0xF2, 0x10, scratch_xmm);
            x64_reloc_add(ctx->relocs, off, x64_const_label(bits, is32 ? 4 : 8), X64_RELOC_RIP32);
        } else {
            /* no "mov xmm, imm" exists - stage the bit pattern through a
               reserved GPR scratch and movq it across */
            enc_mov_ri64(ctx->code, X64_RAX, (int64_t)bits);
            enc_movq_gpr_to_xmm(ctx->code, scratch_xmm, X64_RAX);
        }
        return scratch_xmm;
    }
    case MIRV_VREG: {
        MCLoc loc = ctx->ra->vreg_locs[v.vreg];
        if (loc.kind == MCLOC_REG) {
            int r = x64_float_reg_pool[loc.reg_id];
            enc_movaps_xx(ctx->code, scratch_xmm, r);
        } else {
            int32_t off = float_spill_offset(ctx->fn, ctx->ra, loc.spill_slot);
            if (is32) enc_movss_load(ctx->code, scratch_xmm, X64_RBP, off);
            else enc_movsd_load(ctx->code, scratch_xmm, X64_RBP, off);
        }
        return scratch_xmm;
    }
    default:
        fprintf(stderr, "[lower_x64] unsupported float operand kind, emitting 0.0\n");
        enc_mov_ri64(ctx->code, X64_RAX, 0);
        enc_movq_gpr_to_xmm(ctx->code, scratch_xmm, X64_RAX);
        return scratch_xmm;
    }
}

/* Value -> GPR as raw bits. For a float value this moves the bit pattern
   across (movq) instead of converting, which is how floats travel through
   the integer paths: stored to memory slots, passed as call arguments and
   returned. Hylian-to-Hylian calls pass floats this way (in the integer
   argument registers) - NOT the SysV xmm convention - so a Hylian `float`
   function can't be called from C or vice versa. For non-floats it's exactly
   load_operand. */
static X64Reg load_operand_bits(LowerCtx *ctx, MIRValue v, X64Reg scratch) {
    if (!mir_type_is_float(v.type)) return load_operand(ctx, v, scratch);
    if (v.kind == MIRV_IMM_FLOAT) {
        if (v.type == MIR_F32) {
            float f = (float)v.imm_float;
            uint32_t bits; memcpy(&bits, &f, 4);
            enc_mov_ri64(ctx->code, scratch, (int64_t)(uint64_t)bits);
        } else {
            uint64_t bits; memcpy(&bits, &v.imm_float, 8);
            enc_mov_ri64(ctx->code, scratch, (int64_t)bits);
        }
        return scratch;
    }
    int x = load_operand_f(ctx, v, X64_XMM_SCRATCH_A);
    enc_movq_xmm_to_gpr(ctx->code, scratch, x);
    return scratch;
}

/* One 8-byte piece of a call's arguments (see MIR_CALL below). */
typedef enum { SLOT_INT, SLOT_SSE, SLOT_STACK, SLOT_SRET } CallSlotKind;
typedef struct {
    int arg;            /* index into ins->args, -1 for the hidden sret pointer */
    int off;            /* byte offset into a by-value struct, -1 for a scalar */
    CallSlotKind kind;
    int reg;            /* argument register / xmm number for INT / SSE / SRET */
} CallSlot;

/* Materialise a slot's 8 bytes in a register and return it. A struct piece is
   read from memory at the struct's address; a short tail (4/2/1 bytes) is
   loaded at exactly its width so we never read past the end of the struct. */
static X64Reg load_call_slot(LowerCtx *ctx, const MIRInstr *ins, const CallSlot *sl) {
    X64Buf *code = ctx->code;
    if (sl->kind == SLOT_SRET) {
        enc_lea_mem(code, X64_RAX, X64_RBP, local_offset(ins->src2.local_id));
        return X64_RAX;
    }
    if (sl->off < 0) return load_operand_bits(ctx, ins->args[sl->arg], X64_RAX);
    X64Reg base = load_operand(ctx, ins->args[sl->arg], X64_RAX);
    int rem = MIR_AGG_SIZE(ins->arg_aggr[sl->arg]) - sl->off;
    if (rem == 1 || rem == 2 || rem == 4) enc_mov_load_sz(code, X64_RAX, base, sl->off, rem, 0);
    else                                   enc_mov_load(code, X64_RAX, base, sl->off);
    return X64_RAX;
}

static void store_result_f(LowerCtx *ctx, MIRValue dest, int from_xmm) {
    if (dest.kind != MIRV_VREG) return;
    MCLoc loc = ctx->ra->vreg_locs[dest.vreg];
    int is32 = (dest.type == MIR_F32);
    if (loc.kind == MCLOC_REG) {
        int d = x64_float_reg_pool[loc.reg_id];
        if (d != from_xmm) enc_movaps_xx(ctx->code, d, from_xmm);
    } else {
        int32_t off = float_spill_offset(ctx->fn, ctx->ra, loc.spill_slot);
        if (is32) enc_movss_store(ctx->code, X64_RBP, off, from_xmm);
        else enc_movsd_store(ctx->code, X64_RBP, off, from_xmm);
    }
}

static void warn_unhandled(MIROp op) {
    fprintf(stderr, "[lower_x64] not lowered yet: %s (skipped)\n", mir_op_name(op));
}

static const char *reg_name(X64Reg r) {
    static const char *names[] = {
        "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
        "r8","r9","r10","r11","r12","r13","r14","r15",
    };
    return names[r];
}


/* ======================================================================
 *  Operand-aware instruction selection helpers
 * ====================================================================== */

typedef enum { OPK_REG, OPK_MEM, OPK_IMM } OpndKind;
typedef struct {
    OpndKind kind;
    X64Reg   reg;
    X64Mem   mem;
    int64_t  imm;
} Opnd;

static X64Mem spill_mem(LowerCtx *c, MCLoc loc) {
    int32_t off = loc.is_float ? float_spill_offset(c->fn, c->ra, loc.spill_slot)
                               : int_spill_offset(c->fn, loc.spill_slot);
    return x64_mem(X64_RBP, off);
}

static int fits32(int64_t v) { return v >= INT32_MIN && v <= INT32_MAX; }

/* Where an integer-class value lives, WITHOUT emitting any code. Anything that
   isn't a plain vreg or immediate is materialised into `scratch` first. */
static Opnd get_opnd(LowerCtx *c, MIRValue v, X64Reg scratch) {
    Opnd o;
    memset(&o, 0, sizeof o);
    if (v.kind == MIRV_VREG) {
        MCLoc loc = c->ra->vreg_locs[v.vreg];
        if (loc.kind == MCLOC_REG && !loc.is_float) {
            o.kind = OPK_REG; o.reg = x64_int_reg_pool[loc.reg_id];
        } else if (loc.kind == MCLOC_REG) {
            /* a float-class vreg read as an integer: move its bits across */
            o.kind = OPK_REG; o.reg = scratch;
            enc_movq_xmm_to_gpr(c->code, scratch, x64_float_reg_pool[loc.reg_id]);
        } else {
            o.kind = OPK_MEM; o.mem = spill_mem(c, loc);
        }
        return o;
    }
    if (v.kind == MIRV_IMM_INT) {
        o.kind = OPK_IMM; o.imm = v.imm_int;
        return o;
    }
    o.kind = OPK_REG;
    o.reg = load_operand(c, v, scratch);
    return o;
}

/* A register holding the value (the vreg's own register when it has one - the
   caller must not modify it). */
static X64Reg use_reg(LowerCtx *c, MIRValue v, X64Reg scratch) {
    Opnd o = get_opnd(c, v, scratch);
    switch (o.kind) {
    case OPK_REG: return o.reg;
    case OPK_MEM: enc_load_mem(c->code, scratch, o.mem, 8, 0); return scratch;
    default:      enc_mov_ri(c->code, scratch, o.imm); return scratch;
    }
}

/* The register to build a result in: the destination's own register when it
   has one, else `scratch` (to be stored by finish_dest). */
static X64Reg dest_reg(LowerCtx *c, MIRValue d, X64Reg scratch) {
    if (d.kind == MIRV_VREG) {
        MCLoc loc = c->ra->vreg_locs[d.vreg];
        if (loc.kind == MCLOC_REG && !loc.is_float) return x64_int_reg_pool[loc.reg_id];
    }
    return scratch;
}

/* `from` holds the result; put it where the destination lives. */
static void finish_dest(LowerCtx *c, MIRValue d, X64Reg from) {
    store_result(c, d, from);
}

static void mov_opnd(LowerCtx *c, X64Reg dst, Opnd o) {
    switch (o.kind) {
    case OPK_REG: if (o.reg != dst) enc_mov_rr(c->code, dst, o.reg); break;
    case OPK_MEM: enc_load_mem(c->code, dst, o.mem, 8, 0); break;
    default:      enc_mov_ri(c->code, dst, o.imm); break;
    }
}

static AluOp alu_for(MIROp op) {
    switch (op) {
    case MIR_ADD: return ALU_ADD; case MIR_SUB: return ALU_SUB;
    case MIR_AND: return ALU_AND; case MIR_OR: return ALU_OR; default: return ALU_XOR;
    }
}

static void apply_alu(LowerCtx *c, AluOp op, X64Reg d, Opnd b) {
    switch (b.kind) {
    case OPK_REG: enc_alu_rr(c->code, op, d, b.reg); break;
    case OPK_MEM: enc_alu_rm(c->code, op, d, b.mem); break;
    default:
        if (fits32(b.imm)) enc_alu_ri(c->code, op, d, (int32_t)b.imm);
        else { enc_mov_ri(c->code, X64_R11, b.imm); enc_alu_rr(c->code, op, d, X64_R11); }
        break;
    }
}

static void emit_alu(LowerCtx *c, MIROp mop, MIRValue dest, MIRValue av, MIRValue bv) {
    AluOp op = alu_for(mop);
    X64Reg D = dest_reg(c, dest, X64_RAX);
    Opnd A = get_opnd(c, av, X64_RAX);
    Opnd B = get_opnd(c, bv, X64_R11);
    int commut = (mop != MIR_SUB);

    /* three-address forms via lea */
    if ((mop == MIR_ADD || mop == MIR_SUB) && A.kind == OPK_REG && A.reg != D) {
        if (B.kind == OPK_IMM && fits32(mop == MIR_ADD ? B.imm : -B.imm) && B.imm != INT32_MIN) {
            enc_lea_m(c->code, D, x64_mem(A.reg, (int32_t)(mop == MIR_ADD ? B.imm : -B.imm)));
            finish_dest(c, dest, D);
            return;
        }
        if (mop == MIR_ADD && B.kind == OPK_REG && B.reg != D) {
            enc_lea_m(c->code, D, x64_mem_idx(A.reg, B.reg, 1, 0));
            finish_dest(c, dest, D);
            return;
        }
    }

    if (B.kind == OPK_REG && B.reg == D && !(A.kind == OPK_REG && A.reg == D)) {
        if (commut) { Opnd t = A; A = B; B = t; }
        else {
            /* D = A - D  ==  -D + A */
            enc_neg_r(c->code, D);
            apply_alu(c, ALU_ADD, D, A);
            finish_dest(c, dest, D);
            return;
        }
    }
    mov_opnd(c, D, A);
    apply_alu(c, op, D, B);
    finish_dest(c, dest, D);
}

static void emit_mul(LowerCtx *c, MIRValue dest, MIRValue av, MIRValue bv) {
    X64Reg D = dest_reg(c, dest, X64_RAX);
    Opnd A = get_opnd(c, av, X64_RAX);
    Opnd B = get_opnd(c, bv, X64_R11);
    if (A.kind == OPK_IMM && B.kind != OPK_IMM) { Opnd t = A; A = B; B = t; }
    if (B.kind == OPK_IMM && A.kind == OPK_REG) {
        int64_t k = B.imm;
        /* 3, 5, 9: one lea (1 cycle) instead of an imul (3) */
        if (k == 3 || k == 5 || k == 9) {
            enc_lea_m(c->code, D, x64_mem_idx(A.reg, A.reg, (int)(k - 1), 0));
            finish_dest(c, dest, D);
            return;
        }
        /* 2^n +/- 1: shift and add/sub */
        for (int sh = 2; sh <= 20; sh++) {
            int plus = (k == ((int64_t)1 << sh) + 1), minus = (k == ((int64_t)1 << sh) - 1);
            if (!plus && !minus) continue;
            if (A.reg == D) break;                       /* needs the original value afterwards */
            enc_mov_rr(c->code, D, A.reg);
            enc_shift_ri(c->code, SHIFT_SHL, D, sh);
            enc_alu_rr(c->code, plus ? ALU_ADD : ALU_SUB, D, A.reg);
            finish_dest(c, dest, D);
            return;
        }
    }
    if (B.kind == OPK_IMM && fits32(B.imm) && A.kind != OPK_IMM) {
        if (A.kind == OPK_REG) enc_imul_rri(c->code, D, A.reg, (int32_t)B.imm);
        else                   enc_imul_rmi(c->code, D, A.mem, (int32_t)B.imm);
        finish_dest(c, dest, D);
        return;
    }
    if (B.kind == OPK_REG && B.reg == D && !(A.kind == OPK_REG && A.reg == D)) { Opnd t = A; A = B; B = t; }
    mov_opnd(c, D, A);
    switch (B.kind) {
    case OPK_REG: enc_imul_rr(c->code, D, B.reg); break;
    case OPK_MEM: enc_imul_rm(c->code, D, B.mem); break;
    default:
        enc_mov_ri(c->code, X64_R11, B.imm);
        enc_imul_rr(c->code, D, X64_R11);
        break;
    }
    finish_dest(c, dest, D);
}

static void emit_shift(LowerCtx *c, MIROp mop, MIRValue dest, MIRValue av, MIRValue bv) {
    ShiftKind k = mop == MIR_SHL ? SHIFT_SHL : mop == MIR_SHR_A ? SHIFT_SAR : SHIFT_SHR;
    X64Reg D = dest_reg(c, dest, X64_RAX);
    Opnd B = get_opnd(c, bv, X64_R11);
    if (B.kind == OPK_IMM) {
        Opnd A = get_opnd(c, av, X64_RAX);
        int cnt = (int)(B.imm & 63);
        mov_opnd(c, D, A);
        if (cnt) enc_shift_ri(c->code, k, D, cnt);
        finish_dest(c, dest, D);
        return;
    }
    /* Variable count: it must be in CL. rcx is allocatable, so the shifted value
       or the destination may be living there; in that case build the result in
       rax. Otherwise read the count before writing D, which can be the very
       register that holds it. */
    Opnd A = get_opnd(c, av, X64_RAX);
    if (D == X64_RCX || (A.kind == OPK_REG && A.reg == X64_RCX)) {
        mov_opnd(c, X64_RAX, A);
        if (!(B.kind == OPK_REG && B.reg == X64_RCX)) mov_opnd(c, X64_RCX, B);
        enc_shift_cl(c->code, k, X64_RAX);
        finish_dest(c, dest, X64_RAX);
        return;
    }
    mov_opnd(c, X64_RCX, B);
    mov_opnd(c, D, A);
    enc_shift_cl(c->code, k, D);
    finish_dest(c, dest, D);
}

static CondCode cc_of(MIROp op) {
    switch (op) {
    case MIR_CMP_EQ: return CC_E;   case MIR_CMP_NE: return CC_NE;
    case MIR_CMP_LT: return CC_L;   case MIR_CMP_LE: return CC_LE;
    case MIR_CMP_GT: return CC_G;   case MIR_CMP_GE: return CC_GE;
    case MIR_UCMP_LT: return CC_B;  case MIR_UCMP_LE: return CC_BE;
    case MIR_UCMP_GT: return CC_A;  default: return CC_AE;
    }
}

static CondCode cc_invert(CondCode cc) { return (CondCode)(cc ^ 1); }

/* set flags for  a ? b  (integer) */
static void emit_cmp_flags(LowerCtx *c, MIRValue av, MIRValue bv) {
    Opnd A = get_opnd(c, av, X64_RAX);
    Opnd B = get_opnd(c, bv, X64_R11);
    if (A.kind == OPK_IMM || (A.kind == OPK_MEM && B.kind != OPK_IMM)) {
        X64Reg r = X64_RAX;
        mov_opnd(c, r, A);
        A.kind = OPK_REG; A.reg = r;
    }
    if (A.kind == OPK_MEM) {   /* B is an immediate */
        if (fits32(B.imm)) { enc_alu_mi(c->code, ALU_CMP, A.mem, (int32_t)B.imm); return; }
        enc_load_mem(c->code, X64_RAX, A.mem, 8, 0);
        A.kind = OPK_REG; A.reg = X64_RAX;
    }
    switch (B.kind) {
    case OPK_REG: enc_alu_rr(c->code, ALU_CMP, A.reg, B.reg); break;
    case OPK_MEM: enc_alu_rm(c->code, ALU_CMP, A.reg, B.mem); break;
    default:
        if (B.imm == 0) enc_test_rr(c->code, A.reg, A.reg);
        else if (fits32(B.imm)) enc_alu_ri(c->code, ALU_CMP, A.reg, (int32_t)B.imm);
        else { enc_mov_ri(c->code, X64_R11, B.imm); enc_alu_rr(c->code, ALU_CMP, A.reg, X64_R11); }
        break;
    }
}

static int is_int_cmp_op(MIROp op) { return op >= MIR_CMP_EQ && op <= MIR_UCMP_GE; }
static int is_float_cmp_op(MIROp op) { return op >= MIR_FCMP_LT && op <= MIR_FCMP_NE; }

/* Effective address for a pointer LOAD/STORE. Scratch use: R11 for a spilled or
   immediate base, RAX for a spilled index (combined with the base into R11 by a
   lea). The caller must therefore compute the address BEFORE it loads a value
   into RAX. */
static X64Mem pointer_mem(LowerCtx *c, MIRValue base, MIRValue idx, int scale, int32_t off) {
    Opnd B = get_opnd(c, base, X64_R11);
    X64Reg breg;
    if (B.kind == OPK_REG) breg = B.reg;
    else if (B.kind == OPK_MEM) { enc_load_mem(c->code, X64_R11, B.mem, 8, 0); breg = X64_R11; }
    else {
        /* absolute address */
        if (scale == 0 || idx.kind == MIRV_IMM_INT) {
            int64_t a = B.imm + off;
            if (scale && idx.kind == MIRV_IMM_INT) a += idx.imm_int * scale;
            enc_mov_ri(c->code, X64_R11, a);
            return x64_mem(X64_R11, 0);
        }
        enc_mov_ri(c->code, X64_R11, B.imm);
        breg = X64_R11;
    }
    if (!scale) return x64_mem(breg, off);
    Opnd I = get_opnd(c, idx, X64_RAX);
    if (I.kind == OPK_IMM) {
        int64_t extra = I.imm * scale;
        if (fits32(extra + off)) return x64_mem(breg, (int32_t)(extra + off));
        enc_mov_ri(c->code, X64_RAX, I.imm);
        enc_lea_m(c->code, X64_R11, x64_mem_idx(breg, X64_RAX, scale, off));
        return x64_mem(X64_R11, 0);
    }
    if (I.kind == OPK_REG) return x64_mem_idx(breg, I.reg, scale, off);
    enc_load_mem(c->code, X64_RAX, I.mem, 8, 0);
    enc_lea_m(c->code, X64_R11, x64_mem_idx(breg, X64_RAX, scale, off));
    return x64_mem(X64_R11, 0);
}

/* The xmm register holding a float value (its own register when it has one -
   caller must not modify it), else it is loaded into `scratch`. */
static int xmm_use(LowerCtx *c, MIRValue v, int scratch) {
    if (v.kind == MIRV_VREG) {
        MCLoc loc = c->ra->vreg_locs[v.vreg];
        if (loc.kind == MCLOC_REG) return x64_float_reg_pool[loc.reg_id];
    }
    return load_operand_f(c, v, scratch);
}

/* the xmm register to build a float result in */
static int xmm_dest(LowerCtx *c, MIRValue d, int scratch) {
    if (d.kind == MIRV_VREG) {
        MCLoc loc = c->ra->vreg_locs[d.vreg];
        if (loc.kind == MCLOC_REG) return x64_float_reg_pool[loc.reg_id];
    }
    return scratch;
}

static int next_real(const MIRFunc *fn, int i) {
    while (i < fn->count && fn->instrs[i].op == MIR_NOP) i++;
    return i;
}

/* If instruction i is a compare whose result feeds the conditional branch that
   follows it directly, return that branch's index, else -1. */
static int branch_after_cmp(const MIRFunc *fn, int i) {
    const MIRInstr *c = &fn->instrs[i];
    if (!((c->op >= MIR_CMP_EQ && c->op <= MIR_UCMP_GE) || (c->op >= MIR_FCMP_LT && c->op <= MIR_FCMP_NE))) return -1;
    if (c->dest.kind != MIRV_VREG) return -1;
    int j = next_real(fn, i + 1);
    if (j >= fn->count) return -1;
    const MIRInstr *b = &fn->instrs[j];
    if ((b->op != MIR_JMP_IF && b->op != MIR_JMP_UNLESS) ||
        b->src1.kind != MIRV_VREG || b->src1.vreg != c->dest.vreg) return -1;
    return j;
}

/* A compare followed by a run of selects on its result: how many selects (all
   condition-consumers) there are, and the index of the last. Only integer
   compares participate (cmov reads integer flags). */
static int selects_after_cmp(const MIRFunc *fn, int i, int *last) {
    const MIRInstr *c = &fn->instrs[i];
    if (!(c->op >= MIR_CMP_EQ && c->op <= MIR_UCMP_GE)) return 0;
    if (c->dest.kind != MIRV_VREG) return 0;
    int n = 0, j = next_real(fn, i + 1);
    while (j < fn->count && fn->instrs[j].op == MIR_SELECT &&
           fn->instrs[j].src1.kind == MIRV_VREG && fn->instrs[j].src1.vreg == c->dest.vreg) {
        n++; *last = j; j = next_real(fn, j + 1);
    }
    return n;
}

static void emit_fbin(LowerCtx *c, MIROp op, MIRValue dest, MIRValue av, MIRValue bv) {
    X64Buf *b = c->code;
    int is32 = (av.type == MIR_F32);
    int d = X64_XMM_SCRATCH_A;
    if (dest.kind == MIRV_VREG) {
        MCLoc loc = c->ra->vreg_locs[dest.vreg];
        if (loc.kind == MCLOC_REG) d = x64_float_reg_pool[loc.reg_id];
    }
    int a = xmm_use(c, av, X64_XMM_SCRATCH_A);
    /* a non-zero constant right operand is a direct memory operand */
    if (bv.kind == MIRV_IMM_FLOAT && x64_get_const_pool()) {
        uint64_t bits;
        if (is32) { float f = (float)bv.imm_float; uint32_t b32; memcpy(&b32, &f, 4); bits = b32; }
        else      { memcpy(&bits, &bv.imm_float, 8); }
        if (bits != 0) {
            int opc = op == MIR_FADD ? 0x58 : op == MIR_FSUB ? 0x5C : op == MIR_FMUL ? 0x59 : 0x5E;
            int t2 = d;
            if (t2 != a) enc_movaps_xx(b, t2, a);
            size_t off = enc_sse_rip(b, is32 ? 0xF3 : 0xF2, opc, t2);
            x64_reloc_add(c->relocs, off, x64_const_label(bits, is32 ? 4 : 8), X64_RELOC_RIP32);
            store_result_f(c, dest, t2);
            return;
        }
    }
    int bb = (bv.kind == MIRV_VREG && av.kind == MIRV_VREG && bv.vreg == av.vreg) ? a
                                                                                : xmm_use(c, bv, X64_XMM_SCRATCH_B);
    int commut = (op == MIR_FADD || op == MIR_FMUL);
    int t = d;
    if (d == bb && d != a) {
        if (commut) { int x = a; a = bb; bb = x; }
        else t = X64_XMM_SCRATCH_B;   /* d = a - d: build in scratch, then move */
    }
    if (t != a) enc_movaps_xx(b, t, a);
    switch (op) {
    case MIR_FADD: is32 ? enc_addss(b, t, bb) : enc_addsd(b, t, bb); break;
    case MIR_FSUB: is32 ? enc_subss(b, t, bb) : enc_subsd(b, t, bb); break;
    case MIR_FMUL: is32 ? enc_mulss(b, t, bb) : enc_mulsd(b, t, bb); break;
    default:       is32 ? enc_divss(b, t, bb) : enc_divsd(b, t, bb); break;
    }
    store_result_f(c, dest, t);
}

/* ucomis* for a float compare; returns nothing, flags are set as documented
   in the MIR_FCMP_* setcc code (LT/LE are evaluated with swapped operands so
   an unordered result is "false"). */
static void emit_fcmp_flags(LowerCtx *c, MIROp op, MIRValue av, MIRValue bv) {
    int is32 = (av.type == MIR_F32);
    int a = xmm_use(c, av, X64_XMM_SCRATCH_A);
    int b = xmm_use(c, bv, X64_XMM_SCRATCH_B);
    int swap = (op == MIR_FCMP_LT || op == MIR_FCMP_LE);
    int x = swap ? b : a, y = swap ? a : b;
    if (is32) enc_ucomiss(c->code, x, y); else enc_ucomisd(c->code, x, y);
}

/* branch to `label` when the compare (flags already set) is true (jt=1) or
   false (jt=0) */
static void emit_fcmp_branch(LowerCtx *c, MIROp op, int jt, int label) {
    X64Buf *b = c->code;
    switch (op) {
    case MIR_FCMP_LT: case MIR_FCMP_GT: case MIR_FCMP_LE: case MIR_FCMP_GE: {
        CondCode cc = (op == MIR_FCMP_LT || op == MIR_FCMP_GT) ? CC_A : CC_AE;
        if (!jt) cc = cc_invert(cc);
        add_fixup(c, enc_jcc_rel32(b, cc), label);
        break;
    }
    case MIR_FCMP_EQ: case MIR_FCMP_NE: {
        int eq_jump = (op == MIR_FCMP_EQ) == (jt != 0);   /* jump on "equal and ordered" */
        if (eq_jump) {
            size_t skip = enc_jcc_rel32(b, CC_P);
            add_fixup(c, enc_jcc_rel32(b, CC_E), label);
            int32_t rel = (int32_t)(b->len - (skip + 4));
            memcpy(b->data + skip, &rel, 4);
        } else {
            add_fixup(c, enc_jcc_rel32(b, CC_NE), label);
            add_fixup(c, enc_jcc_rel32(b, CC_P), label);
        }
        break;
    }
    default: break;
    }
}

/* ---- parallel moves (argument passing / parameter receipt) ---- */

typedef struct {
    int      dst_is_mem;
    X64Reg   dst;      X64Mem dst_mem;
    OpndKind src_kind; X64Reg src; X64Mem src_mem; int64_t imm;
    int      done;
} PMove;

static void emit_par_moves(LowerCtx *c, PMove *m, int n) {
    X64Buf *b = c->code;
    /* memory destinations first: they only read registers */
    for (int i = 0; i < n; i++) {
        if (!m[i].dst_is_mem) continue;
        if (m[i].src_kind == OPK_REG) enc_store_mem(b, m[i].dst_mem, m[i].src, 8);
        else if (m[i].src_kind == OPK_IMM && fits32(m[i].imm)) enc_store_imm_mem(b, m[i].dst_mem, (int32_t)m[i].imm, 8);
        else {
            if (m[i].src_kind == OPK_MEM) enc_load_mem(b, X64_RAX, m[i].src_mem, 8, 0);
            else enc_mov_ri(b, X64_RAX, m[i].imm);
            enc_store_mem(b, m[i].dst_mem, X64_RAX, 8);
        }
        m[i].done = 1;
    }
    for (int i = 0; i < n; i++)
        if (!m[i].done && m[i].src_kind == OPK_REG && m[i].src == m[i].dst) m[i].done = 1;
    int progress = 1;
    while (progress) {
        progress = 0;
        for (int i = 0; i < n; i++) {
            if (m[i].done || m[i].src_kind != OPK_REG) continue;
            int blocked = 0;
            for (int j = 0; j < n; j++)
                if (j != i && !m[j].done && m[j].src_kind == OPK_REG && m[j].src == m[i].dst) { blocked = 1; break; }
            if (blocked) continue;
            enc_mov_rr(b, m[i].dst, m[i].src);
            m[i].done = 1; progress = 1;
        }
        if (progress) continue;
        /* only cycles are left: rotate one with xchg */
        for (int i = 0; i < n; i++) {
            if (m[i].done || m[i].src_kind != OPK_REG) continue;
            X64Reg d = m[i].dst, sr = m[i].src;
            enc_xchg_rr(b, d, sr);
            m[i].done = 1; progress = 1;
            for (int j = 0; j < n; j++) {
                if (m[j].done || m[j].src_kind != OPK_REG) continue;
                if (m[j].src == d) m[j].src = sr;
                else if (m[j].src == sr) m[j].src = d;
                if (m[j].src == m[j].dst) m[j].done = 1;
            }
            break;
        }
    }
    for (int i = 0; i < n; i++) {
        if (m[i].done) continue;
        if (m[i].src_kind == OPK_MEM) enc_load_mem(b, m[i].dst, m[i].src_mem, 8, 0);
        else enc_mov_ri(b, m[i].dst, m[i].imm);
        m[i].done = 1;
    }
}

/* A call with only scalar arguments. Integer arguments are placed with a
   parallel move straight into the SysV argument registers; the (rare) stack
   and xmm arguments are staged with pushes first. */
static void emit_call_simple(LowerCtx *c, const MIRInstr *ins) {
    X64Buf *b = c->code;
    int nargs = ins->arg_count;
    int *kind = malloc((nargs + 1) * sizeof(int));   /* 0 int reg, 1 sse, 2 stack */
    int *rno  = malloc((nargs + 1) * sizeof(int));
    int ii = 0, fi = 0, nstack = 0;
    for (int a = 0; a < nargs; a++) {
        int isf = mir_type_is_float(ins->args[a].type);
        if (isf && fi < 8)                            { kind[a] = 1; rno[a] = fi++; }
        else if (!isf && ii < X64_SYSV_ARG_REG_COUNT) { kind[a] = 0; rno[a] = ii++; }
        else                                          { kind[a] = 2; nstack++; }
    }
    int pad = nstack & 1;
    if (pad) enc_push(b, X64_RAX);
    for (int a = nargs - 1; a >= 0; a--) {
        if (kind[a] != 2) continue;
        MIRValue v = ins->args[a];
        if (v.kind == MIRV_IMM_INT && fits32(v.imm_int)) enc_push_imm32(b, (int32_t)v.imm_int);
        else enc_push(b, load_operand_bits(c, v, X64_RAX));
    }
    int nsse = 0;
    for (int a = 0; a < nargs; a++) {
        if (kind[a] != 1) continue;
        enc_push(b, load_operand_bits(c, ins->args[a], X64_RAX));
        nsse++;
    }
    if (!ins->callee) {
        X64Reg r = use_reg(c, ins->src1, X64_R11);
        if (r != X64_R11) enc_mov_rr(b, X64_R11, r);
    }
    PMove *mv = calloc(ii + 1, sizeof(PMove));
    int nmv = 0;
    for (int a = 0; a < nargs; a++) {
        if (kind[a] != 0) continue;
        PMove m; memset(&m, 0, sizeof m);
        m.dst = x64_sysv_arg_regs[rno[a]];
        Opnd o = get_opnd(c, ins->args[a], X64_RAX);
        m.src_kind = o.kind; m.src = o.reg; m.src_mem = o.mem; m.imm = o.imm;
        mv[nmv++] = m;
    }
    emit_par_moves(c, mv, nmv);
    free(mv);
    for (int k = nsse - 1; k >= 0; k--) {
        enc_pop(b, X64_RAX);
        enc_movq_gpr_to_xmm(b, k, X64_RAX);
    }
    if (!(ins->extra_int & 2)) enc_mov_ri(b, X64_RAX, fi);   /* %al = number of vector registers (variadic C callees) */
    if (ins->callee) {
        size_t off = enc_call_rel32(b);
        x64_reloc_add(c->relocs, off, ins->callee, X64_RELOC_REL32);
    } else {
        enc_call_r(b, X64_R11);
    }
    if (nstack + pad > 0) enc_alu_ri(b, ALU_ADD, X64_RSP, 8 * (nstack + pad));
    free(kind); free(rno);
    if (ins->dest.kind == MIRV_VREG && mir_type_is_float(ins->dest.type)) {
        store_result_f(c, ins->dest, 0);   /* float results come back in xmm0 */
    } else if (ins->dest.kind == MIRV_VREG) {
        /* a narrow integer result (C `int` etc.) only defines the low bytes of rax */
        if (mir_type_size(ins->dest.type) < 8 && ins->dest.type != MIR_PTR)
            enc_ext_rr(b, X64_RAX, X64_RAX, mir_type_size(ins->dest.type),
                       !mir_type_is_unsigned(ins->dest.type));
        store_result(c, ins->dest, X64_RAX);
    }
}

void x64_lower_func(const MIRFunc *fn_in, const RegAllocResult *ra,
                    X64Buf *out_code, X64RelocList *out_relocs) {
    /* Which parameters the optimizer promoted into vregs (MIR_GETPARAM), and
       how many local slots the body really touches. Slots the optimizer made
       redundant are not given frame space. */
    int nparams = fn_in->param_count;
    unsigned char *has_gp = calloc(nparams > 0 ? nparams : 1, 1);
    int nlocals = 0;
    for (int i = 0; i < fn_in->count; i++) {
        const MIRInstr *ins = &fn_in->instrs[i];
        if (ins->op == MIR_GETPARAM && ins->extra_int >= 0 && ins->extra_int < nparams)
            has_gp[ins->extra_int] = 1;
        const MIRValue *vs[3] = { &ins->dest, &ins->src1, &ins->src2 };
        for (int k = 0; k < 3; k++)
            if (vs[k]->kind == MIRV_LOCAL && vs[k]->local_id + 1 > nlocals) nlocals = vs[k]->local_id + 1;
        for (int a = 0; a < ins->arg_count; a++)
            if (ins->args[a].kind == MIRV_LOCAL && ins->args[a].local_id + 1 > nlocals)
                nlocals = ins->args[a].local_id + 1;
    }
    for (int i = 0; i < nparams; i++) {
        int isf = fn_in->param_types && mir_type_is_float(fn_in->param_types[i]);
        if ((!has_gp[i] || isf) && i + 1 > nlocals) nlocals = i + 1;
    }
    /* where each incoming parameter arrives: SysV integer register, xmm
       register, or the caller's stack (kind 0 / 1 / 2, and its index) */
    int *pk_kind = calloc(nparams > 0 ? nparams : 1, sizeof(int));
    int *pk_idx  = calloc(nparams > 0 ? nparams : 1, sizeof(int));
    {
        int ii = 0, fi = 0, si = 0;
        for (int i = 0; i < nparams; i++) {
            int isf = fn_in->param_types && mir_type_is_float(fn_in->param_types[i]);
            if (isf && fi < 8)                             { pk_kind[i] = 1; pk_idx[i] = fi++; }
            else if (!isf && ii < X64_SYSV_ARG_REG_COUNT)  { pk_kind[i] = 0; pk_idx[i] = ii++; }
            else                                           { pk_kind[i] = 2; pk_idx[i] = si++; }
        }
    }
    MIRFunc fcopy = *fn_in;
    fcopy.local_count = nlocals;
    const MIRFunc *fn = &fcopy;

    LowerCtx ctx = {0};
    ctx.code = out_code;
    ctx.relocs = out_relocs;
    ctx.ra = ra;
    ctx.fn = fn;
    ctx.nv = ra->vreg_count;
    ctx.use_count = calloc(ctx.nv > 0 ? ctx.nv : 1, sizeof(int));
    for (int i = 0; i < fn->count; i++) {
        const MIRInstr *ins = &fn->instrs[i];
        const MIRValue *vs[3] = { &ins->src1, &ins->src2, &ins->dest };
        for (int k = 0; k < 3; k++) {
            if (vs[k]->kind != MIRV_VREG || vs[k]->vreg >= ctx.nv) continue;
            /* dest is a read only for STORE/MEMSET/MEMCPY */
            if (k == 2 && ins->op != MIR_STORE && ins->op != MIR_MEMSET && ins->op != MIR_MEMCPY) continue;
            ctx.use_count[vs[k]->vreg]++;
        }
        for (int a = 0; a < ins->arg_count; a++)
            if (ins->args[a].kind == MIRV_VREG && ins->args[a].vreg < ctx.nv) ctx.use_count[ins->args[a].vreg]++;
    }
    ctx.flags_cc = -1;
    ctx.pair_count = calloc(ctx.nv > 0 ? ctx.nv : 1, sizeof(int));
    for (int i = 0; i < fn->count; i++) {
        int j = branch_after_cmp(fn, i);
        if (j >= 0 && fn->instrs[i].dest.vreg < ctx.nv) ctx.pair_count[fn->instrs[i].dest.vreg]++;
        int last;
        int ns = selects_after_cmp(fn, i, &last);
        if (ns && fn->instrs[i].dest.vreg < ctx.nv) ctx.pair_count[fn->instrs[i].dest.vreg] += ns;
    }
    ctx.fixup_cap = 16;
    ctx.fixups = malloc(ctx.fixup_cap * sizeof(LabelFixup));

    int max_label = -1;
    for (int i = 0; i < fn->count; i++)
        if (fn->instrs[i].op == MIR_LABEL && fn->instrs[i].dest.label_id > max_label)
            max_label = fn->instrs[i].dest.label_id;
    ctx.max_label = max_label;
    if (max_label >= 0) {
        ctx.label_offset = malloc((max_label + 1) * sizeof(int));
        for (int i = 0; i <= max_label; i++) ctx.label_offset[i] = -1;
    }

    int aligned_frame = total_frame_bytes(fn, ra);

    /* loop headers: labels that some later jump goes back to */
    unsigned char *loop_head = NULL;
    if (max_label >= 0) {
        loop_head = calloc(max_label + 1, 1);
        int *lp = malloc((max_label + 1) * sizeof(int));
        for (int l = 0; l <= max_label; l++) lp[l] = -1;
        for (int i = 0; i < fn->count; i++)
            if (fn->instrs[i].op == MIR_LABEL) lp[fn->instrs[i].dest.label_id] = i;
        for (int i = 0; i < fn->count; i++) {
            MIROp op = fn->instrs[i].op;
            if (op != MIR_JMP && op != MIR_JMP_IF && op != MIR_JMP_UNLESS) continue;
            int l = fn->instrs[i].dest.label_id;
            if (l >= 0 && l <= max_label && lp[l] >= 0 && lp[l] <= i) loop_head[l] = 1;
        }
        free(lp);
    }

    /* A function that needs no stack slots at all (no locals, spills, scratch
       buffers or stack-passed parameters) doesn't need a frame pointer either:
       it saves the callee-saved registers it uses with push/pop and, if it
       makes calls, pads to keep the stack 16-byte aligned. */
    int frameless = 0, fl_pad = 0;
    if (!fn->is_naked && nlocals == 0 && ra->num_int_spill_slots == 0 && ra->num_float_spill_slots == 0 &&
        alloca_total_bytes(fn) == 0 && !getenv("HYLIAN_KEEP_FP")) {
        frameless = 1;
        for (int i = 0; i < nparams; i++) if (pk_kind[i] == 2) frameless = 0;
        for (int i = 0; i < fn->count && frameless; i++) {
            switch (fn->instrs[i].op) {
            case MIR_ASM_TEXT: case MIR_ASM_RAW: case MIR_SAVE_REGS: case MIR_RESTORE_REGS:
            case MIR_IRET: case MIR_SYSRET: case MIR_LGDT: case MIR_LIDT: case MIR_LTR:
            case MIR_ALLOCA_LOCAL: case MIR_LEA_LOCAL:
                frameless = 0; break;
            default: break;
            }
        }
        if (frameless) {
            int has_calls = 0;
            for (int i = 0; i < fn->count; i++) if (fn->instrs[i].op == MIR_CALL) has_calls = 1;
            fl_pad = (has_calls && popcount32(ra->callee_saved_int_mask) % 2 == 0) ? 8 : 0;
        }
    }

    if (frameless) {
        for (int i = 0; i < ra->callee_saved_int_used; i++)
            if (ra->callee_saved_int_mask & (1u << i)) enc_push(out_code, x64_int_reg_pool[i]);
        if (fl_pad) enc_alu_ri(out_code, ALU_SUB, X64_RSP, fl_pad);
    } else if (!fn->is_naked) {
        enc_push(out_code, X64_RBP);
        enc_mov_rr(out_code, X64_RBP, X64_RSP);
        if (aligned_frame > 0) {
            /* sub rsp, imm32 - reuse ALU encoding path via direct bytes since
               sub-immediate has its own opcode (0x81 /5) not covered by
               enc_alu_rr (register-only form) */
            x64_buf_push(out_code, x64_rex(1, 0, X64_RSP));
            x64_buf_push(out_code, 0x81);
            x64_buf_push(out_code, x64_modrm(3, 5, X64_RSP));
            x64_buf_write32(out_code, (uint32_t)aligned_frame);
        }
        /* receive incoming params: SysV puts them in registers, but this
           backend always treats named variables (including params) as real
           stack-resident locals, so copy each one from its ABI register into
           its local slot right away. Anything beyond 6 needs stack-passed
           args, which isn't supported yet - same limitation already flagged
           for calls, just visible from the other side of the call here. */
        /* SysV: integer-class parameters come in rdi, rsi, rdx, rcx, r8, r9
           and float-class ones in xmm0-xmm7, each class counted separately
           (so `f(int a, double b, int c)` is rdi, xmm0, rsi). Whatever
           doesn't fit spills to the caller's stack in argument order: after
           `push rbp; mov rbp, rsp` the first stack argument is at [rbp+16],
           the next at [rbp+24], ... Every parameter is copied into its own
           local slot straight away. */
        {
            int ii = 0, fi = 0, si = 0;
            for (int i = 0; i < fn->param_count; i++) {
                int isf = fn->param_types && mir_type_is_float(fn->param_types[i]);
                int is32 = isf && fn->param_types[i] == MIR_F32;
                if (isf && fi < 8) {
                    if (is32) enc_movss_store(out_code, X64_RBP, local_offset(i), fi);
                    else      enc_movsd_store(out_code, X64_RBP, local_offset(i), fi);
                    fi++;
                } else if (!isf && ii < X64_SYSV_ARG_REG_COUNT) {
                    /* promoted parameters are moved straight into their home
                       by MIR_GETPARAM; the rest live in their slot */
                    if (!has_gp[i]) enc_mov_store(out_code, X64_RBP, local_offset(i), x64_sysv_arg_regs[ii]);
                    ii++;
                } else {
                    if (!has_gp[i] || isf) {
                        enc_mov_load(out_code, X64_RAX, X64_RBP, 16 + 8 * si);
                        enc_mov_store(out_code, X64_RBP, local_offset(i), X64_RAX);
                    }
                    si++;
                }
            }
        }

        /* Save the callee-saved registers regalloc handed out. Without this,
           a callee scribbling on rbx/r12-r15 would silently destroy a value
           its *caller* was keeping there across the call - and since regalloc
           now deliberately parks call-crossing values in exactly these
           registers, that would break every one of them. Params are copied to
           their slots first (above) so this can't clobber an incoming arg. */
        for (int i = 0, k = 0; i < ra->callee_saved_int_used; i++) {
            if (!(ra->callee_saved_int_mask & (1u << i))) continue;
            enc_mov_store(out_code, X64_RBP, callee_save_offset(fn, ra, k++), x64_int_reg_pool[i]);
        }
    }

    int alloca_cursor = 0; /* bytes handed out by MIR_ALLOCA_LOCAL so far */

    for (int i = 0; i < fn->count; i++) {
        const MIRInstr *ins = &fn->instrs[i];
        if (ins->op != MIR_SELECT && ins->op != MIR_NOP) ctx.flags_cc = -1;

        switch (ins->op) {

        case MIR_LABEL:
            /* align the top of a loop (a label something jumps back to) so its
               first fetch block is full; the padding is skipped over unless it
               is short, and only executes when the loop is entered */
            if (ins->dest.label_id >= 0 && ins->dest.label_id <= ctx.max_label && loop_head &&
                loop_head[ins->dest.label_id]) {
                size_t pad = (16 - (out_code->len & 15)) & 15;
                if (pad && pad <= 10) emit_nops(out_code, (int)pad);
            }
            ctx.label_offset[ins->dest.label_id] = (int)out_code->len;
            break;

        case MIR_MOV: {
            if (mir_type_is_float(ins->dest.type)) {
                int a = xmm_use(&ctx, ins->src1, X64_XMM_SCRATCH_A);
                store_result_f(&ctx, ins->dest, a);
            } else {
                Opnd A = get_opnd(&ctx, ins->src1, X64_RAX);
                MCLoc dl = ins->dest.kind == MIRV_VREG ? ra->vreg_locs[ins->dest.vreg] : (MCLoc){0};
                if (ins->dest.kind != MIRV_VREG) break;
                if (dl.kind == MCLOC_REG) {
                    /* flags are never live across a plain MOV (compare fusion only fuses
                       adjacent instructions), so zeroing can use the shorter xor */
                    if (A.kind == OPK_IMM && A.imm == 0) {
                        X64Reg zr = x64_int_reg_pool[dl.reg_id];
                        enc_alu_rr(out_code, ALU_XOR, zr, zr);
                    } else
                    mov_opnd(&ctx, x64_int_reg_pool[dl.reg_id], A);
                } else {
                    X64Mem dm = spill_mem(&ctx, dl);
                    if (A.kind == OPK_REG) enc_store_mem(out_code, dm, A.reg, 8);
                    else if (A.kind == OPK_IMM && fits32(A.imm)) enc_store_imm_mem(out_code, dm, (int32_t)A.imm, 8);
                    else { mov_opnd(&ctx, X64_RAX, A); enc_store_mem(out_code, dm, X64_RAX, 8); }
                }
            }
            break;
        }

        case MIR_ADD: case MIR_SUB: case MIR_AND: case MIR_OR: case MIR_XOR: {
            /* (a & b) compared with zero and branched on: a single `test` */
            if (ins->op == MIR_AND && ins->dest.kind == MIRV_VREG && ctx.use_count[ins->dest.vreg] == 1) {
                int c = next_real(fn, i + 1);
                if (c < fn->count && (fn->instrs[c].op == MIR_CMP_EQ || fn->instrs[c].op == MIR_CMP_NE) &&
                    fn->instrs[c].src1.kind == MIRV_VREG && fn->instrs[c].src1.vreg == ins->dest.vreg &&
                    fn->instrs[c].src2.kind == MIRV_IMM_INT && fn->instrs[c].src2.imm_int == 0) {
                    int j = branch_after_cmp(fn, c);
                    if (j >= 0 && ctx.use_count[fn->instrs[c].dest.vreg] == ctx.pair_count[fn->instrs[c].dest.vreg]) {
                        Opnd A = get_opnd(&ctx, ins->src1, X64_RAX);
                        Opnd B = get_opnd(&ctx, ins->src2, X64_R11);
                        if (A.kind == OPK_IMM && B.kind != OPK_IMM) { Opnd t = A; A = B; B = t; }
                        if (A.kind != OPK_REG) { mov_opnd(&ctx, X64_RAX, A); A.kind = OPK_REG; A.reg = X64_RAX; }
                        if (B.kind == OPK_REG) enc_test_rr(out_code, A.reg, B.reg);
                        else if (B.kind == OPK_IMM && fits32(B.imm)) enc_test_ri(out_code, A.reg, (int32_t)B.imm);
                        else { mov_opnd(&ctx, X64_R11, B); enc_test_rr(out_code, A.reg, X64_R11); }
                        int eq = fn->instrs[c].op == MIR_CMP_EQ;     /* cmp_eq: branch cond = (a&b)==0 */
                        int jt = fn->instrs[j].op == MIR_JMP_IF;
                        CondCode cc = (eq == jt) ? CC_E : CC_NE;
                        size_t off = enc_jcc_rel32(out_code, cc);
                        add_fixup(&ctx, off, fn->instrs[j].dest.label_id);
                        i = j;
                        break;
                    }
                }
            }
            emit_alu(&ctx, ins->op, ins->dest, ins->src1, ins->src2);
            break;
        }

        case MIR_MUL:
            emit_mul(&ctx, ins->dest, ins->src1, ins->src2);
            break;

        case MIR_SDIV: case MIR_UDIV: case MIR_SMOD: case MIR_UMOD: {
            Opnd B = get_opnd(&ctx, ins->src2, X64_R11);
            X64Reg br = B.kind == OPK_REG ? B.reg : X64_R11;
            if (B.kind != OPK_REG) mov_opnd(&ctx, X64_R11, B);
            Opnd A = get_opnd(&ctx, ins->src1, X64_RAX);
            mov_opnd(&ctx, X64_RAX, A);
            if (br == X64_RDX) { enc_mov_rr(out_code, X64_R11, X64_RDX); br = X64_R11; }   /* cqo/xor clobber rdx */
            if (ins->op == MIR_SDIV || ins->op == MIR_SMOD) {
                enc_cqo(out_code);
                enc_idiv_r(out_code, br);
            } else {
                enc_alu_rr(out_code, ALU_XOR, X64_RDX, X64_RDX);
                enc_div_r(out_code, br);
            }
            finish_dest(&ctx, ins->dest, (ins->op == MIR_SDIV || ins->op == MIR_UDIV) ? X64_RAX : X64_RDX);
            break;
        }

        case MIR_SELECT: {
            /* dest = cond ? a : b  via cmov. The flags come from either the
               compare fused in front (see the compare cases) or a test of the
               condition. cmov and mov leave flags untouched, so a run of selects
               can share one compare. */
            X64Reg D0 = dest_reg(&ctx, ins->dest, X64_RAX);
            Opnd A = get_opnd(&ctx, ins->src2, X64_R11);
            Opnd B = get_opnd(&ctx, ins->args[0], X64_RAX);
            CondCode cc = CC_NE;
            if (ctx.flags_cc >= 0 && ctx.flags_for == ins->src1.vreg) {
                cc = (CondCode)ctx.flags_cc;
            } else {
                Opnd C = get_opnd(&ctx, ins->src1, X64_RAX);
                if (C.kind == OPK_REG) enc_test_rr(out_code, C.reg, C.reg);
                else if (C.kind == OPK_MEM) enc_alu_mi(out_code, ALU_CMP, C.mem, 0);
                else { enc_mov_ri(out_code, X64_RAX, C.imm); enc_test_rr(out_code, X64_RAX, X64_RAX); }
            }
            /* make A and B usable as cmov sources: a register or memory */
            if (A.kind == OPK_IMM) { enc_mov_ri(out_code, X64_R11, A.imm); A.kind = OPK_REG; A.reg = X64_R11; }
            X64Reg W = D0;
            if (A.kind == OPK_REG && A.reg == D0) {
                /* D already holds a: the result is  cond ? D : b  ==  (!cond) ? b : D */
                if (B.kind == OPK_IMM) { enc_mov_ri(out_code, X64_RAX, B.imm); B.kind = OPK_REG; B.reg = X64_RAX; }
                if (B.kind == OPK_REG) enc_cmovcc_rr(out_code, cc_invert(cc), D0, B.reg);
                else enc_cmovcc_rm(out_code, cc_invert(cc), D0, B.mem);
                finish_dest(&ctx, ins->dest, D0);
                break;
            }
            if (B.kind == OPK_REG && B.reg == D0) {
                /* D already holds b: cmov a on cond */
            } else {
                mov_opnd(&ctx, W, B);
            }
            if (A.kind == OPK_REG) enc_cmovcc_rr(out_code, cc, W, A.reg);
            else enc_cmovcc_rm(out_code, cc, W, A.mem);
            finish_dest(&ctx, ins->dest, W);
            break;
        }

        case MIR_SMULH: case MIR_UMULH: {
            /* rdx:rax = rax * operand; the high half is the result */
            Opnd B = get_opnd(&ctx, ins->src2, X64_R11);
            Opnd A = get_opnd(&ctx, ins->src1, X64_RAX);
            if (A.kind == OPK_IMM && B.kind != OPK_IMM) { Opnd t = A; A = B; B = t; }
            X64Reg br;
            if (B.kind == OPK_REG) br = B.reg;
            else { mov_opnd(&ctx, X64_R11, B); br = X64_R11; }
            mov_opnd(&ctx, X64_RAX, A);
            enc_mul1_r(out_code, br, ins->op == MIR_SMULH);
            finish_dest(&ctx, ins->dest, X64_RDX);
            break;
        }

        case MIR_NEG: case MIR_NOT: {
            X64Reg D = dest_reg(&ctx, ins->dest, X64_RAX);
            Opnd A = get_opnd(&ctx, ins->src1, X64_RAX);
            mov_opnd(&ctx, D, A);
            if (ins->op == MIR_NEG) enc_neg_r(out_code, D); else enc_not_r(out_code, D);
            finish_dest(&ctx, ins->dest, D);
            break;
        }

        case MIR_SHL: case MIR_SHR_A: case MIR_SHR_L:
            emit_shift(&ctx, ins->op, ins->dest, ins->src1, ins->src2);
            break;

        case MIR_CMP_EQ: case MIR_CMP_NE:
        case MIR_CMP_LT: case MIR_CMP_LE: case MIR_CMP_GT: case MIR_CMP_GE:
        case MIR_UCMP_LT: case MIR_UCMP_LE: case MIR_UCMP_GT: case MIR_UCMP_GE: {
            int lastsel = -1;
            int nsel = selects_after_cmp(fn, i, &lastsel);
            if (nsel && ctx.use_count[ins->dest.vreg] == ctx.pair_count[ins->dest.vreg]) {
                emit_cmp_flags(&ctx, ins->src1, ins->src2);
                ctx.flags_cc = cc_of(ins->op);
                ctx.flags_for = ins->dest.vreg;
                break;
            }
            int j = branch_after_cmp(fn, i);
            if (j >= 0 && ctx.use_count[ins->dest.vreg] == ctx.pair_count[ins->dest.vreg]) {
                emit_cmp_flags(&ctx, ins->src1, ins->src2);
                CondCode cc = cc_of(ins->op);
                if (fn->instrs[j].op == MIR_JMP_UNLESS) cc = cc_invert(cc);
                size_t off = enc_jcc_rel32(out_code, cc);
                add_fixup(&ctx, off, fn->instrs[j].dest.label_id);
                i = j;
                break;
            }
            emit_cmp_flags(&ctx, ins->src1, ins->src2);
            X64Reg D = dest_reg(&ctx, ins->dest, X64_RAX);
            enc_setcc(out_code, cc_of(ins->op), D);
            enc_movzx_r64_r8(out_code, D, D);
            finish_dest(&ctx, ins->dest, D);
            break;
        }

        case MIR_FADD: case MIR_FSUB: case MIR_FMUL: case MIR_FDIV:
            emit_fbin(&ctx, ins->op, ins->dest, ins->src1, ins->src2);
            break;

        case MIR_FNEG: {
            int is32 = (ins->src1.type == MIR_F32);
            if (x64_get_const_pool()) {
                /* flip the sign bit: xorps with a sign-mask constant */
                int a = xmm_use(&ctx, ins->src1, X64_XMM_SCRATCH_A);
                int D = xmm_dest(&ctx, ins->dest, X64_XMM_SCRATCH_A);
                if (D != a) enc_movaps_xx(out_code, D, a);
                uint64_t mask = is32 ? 0x80000000ULL : 0x8000000000000000ULL;
                size_t off = enc_sse_rip(out_code, is32 ? 0xF3 : 0xF2, 0x10, X64_XMM_SCRATCH_B);
                x64_reloc_add(out_relocs, off, x64_const_label(mask, is32 ? 4 : 8), X64_RELOC_RIP32);
                enc_xorps_xx(out_code, D, X64_XMM_SCRATCH_B);
                store_result_f(&ctx, ins->dest, D);
                break;
            }
            /* no constant pool: flip the sign bit via an integer XOR round-trip through a GPR */
            int a = load_operand_f(&ctx, ins->src1, X64_XMM_SCRATCH_A);
            enc_movq_xmm_to_gpr(out_code, X64_RAX, a);
            enc_mov_ri64(out_code, X64_R11, is32 ? (int64_t)0x80000000LL : (int64_t)((uint64_t)1 << 63));
            enc_alu_rr(out_code, ALU_XOR, X64_RAX, X64_R11);
            enc_movq_gpr_to_xmm(out_code, a, X64_RAX);
            store_result_f(&ctx, ins->dest, a);
            break;
        }

        case MIR_FCMP_LT: case MIR_FCMP_LE: case MIR_FCMP_GT: case MIR_FCMP_GE:
        case MIR_FCMP_EQ: case MIR_FCMP_NE: {
            /* ucomis* reports "unordered" (either operand NaN) as ZF=PF=CF=1.
               So ordering compares are done as "above"/"above-or-equal" with
               the operands arranged so that an unordered result is false
               (a < b  is  b > a), and ==/!= also consult the parity flag:
               NaN == anything is false, NaN != anything is true. */
            int j = branch_after_cmp(fn, i);
            emit_fcmp_flags(&ctx, ins->op, ins->src1, ins->src2);
            if (j >= 0 && ctx.use_count[ins->dest.vreg] == ctx.pair_count[ins->dest.vreg]) {
                emit_fcmp_branch(&ctx, ins->op, fn->instrs[j].op == MIR_JMP_IF, fn->instrs[j].dest.label_id);
                i = j;
                break;
            }
            switch (ins->op) {
                case MIR_FCMP_LT: case MIR_FCMP_GT:
                    enc_setcc(out_code, CC_A, X64_RAX);
                    enc_movzx_r64_r8(out_code, X64_RAX, X64_RAX);
                    break;
                case MIR_FCMP_LE: case MIR_FCMP_GE:
                    enc_setcc(out_code, CC_AE, X64_RAX);
                    enc_movzx_r64_r8(out_code, X64_RAX, X64_RAX);
                    break;
                case MIR_FCMP_EQ:
                    enc_setcc(out_code, CC_E, X64_RAX);
                    enc_setcc(out_code, CC_NP, X64_R11);
                    enc_movzx_r64_r8(out_code, X64_RAX, X64_RAX);
                    enc_movzx_r64_r8(out_code, X64_R11, X64_R11);
                    enc_alu_rr(out_code, ALU_AND, X64_RAX, X64_R11);
                    break;
                default: /* MIR_FCMP_NE */
                    enc_setcc(out_code, CC_NE, X64_RAX);
                    enc_setcc(out_code, CC_P, X64_R11);
                    enc_movzx_r64_r8(out_code, X64_RAX, X64_RAX);
                    enc_movzx_r64_r8(out_code, X64_R11, X64_R11);
                    enc_alu_rr(out_code, ALU_OR, X64_RAX, X64_R11);
                    break;
            }
            store_result(&ctx, ins->dest, X64_RAX); /* comparison result is always int/bool */
            break;
        }

        case MIR_I2F: {
            int is32dst = (ins->dest.type == MIR_F32);
            int D = xmm_dest(&ctx, ins->dest, X64_XMM_SCRATCH_A);
            Opnd A = get_opnd(&ctx, ins->src1, X64_RAX);
            X64Reg a;
            if (A.kind == OPK_REG) a = A.reg;
            else { mov_opnd(&ctx, X64_RAX, A); a = X64_RAX; }
            enc_xorps_xx(out_code, D, D);            /* break the false dependency on D */
            if (is32dst) enc_cvtsi2ss(out_code, D, a);
            else enc_cvtsi2sd(out_code, D, a);
            store_result_f(&ctx, ins->dest, D);
            break;
        }

        case MIR_F2I: {
            int a = xmm_use(&ctx, ins->src1, X64_XMM_SCRATCH_A);
            int is32src = (ins->src1.type == MIR_F32);
            X64Reg D = dest_reg(&ctx, ins->dest, X64_RAX);
            if (is32src) enc_cvttss2si(out_code, D, a);
            else enc_cvttsd2si(out_code, D, a);
            finish_dest(&ctx, ins->dest, D);
            break;
        }

        case MIR_F2F: {
            int src32 = (ins->src1.type == MIR_F32);
            int dst32 = (ins->dest.type == MIR_F32);
            int a = xmm_use(&ctx, ins->src1, X64_XMM_SCRATCH_A);
            int D = xmm_dest(&ctx, ins->dest, X64_XMM_SCRATCH_A);
            if (src32 && !dst32)      enc_cvtss2sd(out_code, D, a);
            else if (!src32 && dst32) enc_cvtsd2ss(out_code, D, a);
            else if (D != a)          enc_movaps_xx(out_code, D, a);
            store_result_f(&ctx, ins->dest, D);
            break;
        }

        case MIR_SEXT: case MIR_ZEXT: case MIR_TRUNC: {
            /* All three are "reinterpret the low N bytes into a full 64-bit
               register", differing only in N and in which extension to use.
               Widening (SEXT/ZEXT) takes N from the source; narrowing (TRUNC)
               takes it from the destination. */
            int size = (ins->op == MIR_TRUNC) ? mir_type_size(ins->dest.type)
                                              : mir_type_size(ins->src1.type);
            int is_signed = (ins->op == MIR_SEXT) ? 1
                          : (ins->op == MIR_ZEXT) ? 0
                          : !mir_type_is_unsigned(ins->dest.type);
            X64Reg D = dest_reg(&ctx, ins->dest, X64_RAX);
            Opnd A = get_opnd(&ctx, ins->src1, X64_RAX);
            if (A.kind == OPK_REG) {
                enc_ext_rr(out_code, D, A.reg, size, is_signed);
            } else if (A.kind == OPK_MEM && (size == 1 || size == 2 || size == 4)) {
                enc_load_mem(out_code, D, A.mem, size, is_signed);   /* little endian: low bytes first */
            } else {
                mov_opnd(&ctx, D, A);
                enc_ext_rr(out_code, D, D, size, is_signed);
            }
            finish_dest(&ctx, ins->dest, D);
            break;
        }

        case MIR_BITCAST: {
            int src_f = mir_type_is_float(ins->src1.type);
            int dst_f = mir_type_is_float(ins->dest.type);
            if (src_f && !dst_f) {
                int a = load_operand_f(&ctx, ins->src1, X64_XMM_SCRATCH_A);
                enc_movq_xmm_to_gpr(out_code, X64_RAX, a);
                store_result(&ctx, ins->dest, X64_RAX);
            } else if (!src_f && dst_f) {
                X64Reg a = load_operand(&ctx, ins->src1, X64_RAX);
                enc_movq_gpr_to_xmm(out_code, X64_XMM_SCRATCH_A, a);
                store_result_f(&ctx, ins->dest, X64_XMM_SCRATCH_A);
            } else if (!src_f && !dst_f) {
                X64Reg a = load_operand(&ctx, ins->src1, X64_RAX);
                store_result(&ctx, ins->dest, a);
            } else {
                int a = load_operand_f(&ctx, ins->src1, X64_XMM_SCRATCH_A);
                store_result_f(&ctx, ins->dest, a);
            }
            break;
        }

        case MIR_LEA_GLOBAL: {
            X64Reg D = dest_reg(&ctx, ins->dest, X64_RAX);
            size_t off = enc_lea_rip(out_code, D);
            x64_reloc_add(out_relocs, off, ins->src1.global_name, X64_RELOC_RIP32);
            finish_dest(&ctx, ins->dest, D);
            break;
        }

        case MIR_LEA_LOCAL: {
            X64Reg D = dest_reg(&ctx, ins->dest, X64_RAX);
            enc_lea_mem(out_code, D, X64_RBP, local_offset(ins->src1.local_id));
            finish_dest(&ctx, ins->dest, D);
            break;
        }

        case MIR_ALLOCA_LOCAL: {
            /* extra_int bytes of frame scratch, address into dest. The space
               is reserved statically by total_frame_bytes(), and each
               ALLOCA_LOCAL gets its own non-overlapping chunk assigned in
               instruction order via the running cursor - so this is really
               "carve out a fixed buffer", not a dynamic alloca. That's exactly
               what the only current user needs (print/println's int->string
               conversion buffer), and it means rsp never moves.
               Until this existed the opcode fell through to warn_unhandled and
               emitted nothing at all, leaving dest holding garbage - printing
               any integer wrote through a wild pointer. */
            int size = ins->extra_int > 0 ? ins->extra_int : 8;
            size = (size + 7) & ~7;
            alloca_cursor += size;
            int32_t off = -(alloca_region_base(fn, ra) + alloca_cursor);
            enc_lea_mem(out_code, X64_RAX, X64_RBP, off);
            store_result(&ctx, ins->dest, X64_RAX);
            break;
        }

        case MIR_LOAD: {
            int fdest = mir_type_is_float(ins->dest.type);
            if (fdest && ins->src1.kind != MIRV_GLOBAL && ins->src1.kind != MIRV_LOCAL) {
                X64Mem m = pointer_mem(&ctx, ins->src1, ins->src2, ins->mem_scale, ins->mem_offset);
                int xd = xmm_dest(&ctx, ins->dest, X64_XMM_SCRATCH_A);
                enc_sse_rm(out_code, ins->dest.type == MIR_F32 ? 0xF3 : 0xF2, 0x10, xd, m);
                store_result_f(&ctx, ins->dest, xd);
                break;
            }
            X64Reg D = fdest ? X64_RAX : dest_reg(&ctx, ins->dest, X64_RAX);
            if (ins->src1.kind == MIRV_GLOBAL) {
                size_t off = enc_mov_load_rip(out_code, D);
                x64_reloc_add(out_relocs, off, ins->src1.global_name, X64_RELOC_RIP32);
            } else if (ins->src1.kind == MIRV_LOCAL) {
                enc_mov_load(out_code, D, X64_RBP, local_offset(ins->src1.local_id));
            } else {
                /* Real pointer dereference: width comes from the loaded type,
                   not the register width. An 8-byte load of a `*uint8` reads 7
                   bytes past the target and returns them as part of the value. */
                X64Mem m = pointer_mem(&ctx, ins->src1, ins->src2, ins->mem_scale, ins->mem_offset);
                enc_load_mem(out_code, D, m, mir_type_size(ins->dest.type),
                             !mir_type_is_unsigned(ins->dest.type));
            }
            if (fdest) {
                /* memory holds the bit pattern; move it into an xmm register */
                enc_movq_gpr_to_xmm(out_code, X64_XMM_SCRATCH_A, D);
                store_result_f(&ctx, ins->dest, X64_XMM_SCRATCH_A);
            } else {
                finish_dest(&ctx, ins->dest, D);
            }
            break;
        }

        case MIR_ARR_LEN: case MIR_ARR_DATA: {
            /* dest = base ? *(base + off) : 0  - so a nil array reads as empty */
            int off = ins->op == MIR_ARR_LEN ? 0 : 16;
            X64Reg D = dest_reg(&ctx, ins->dest, X64_RAX);
            Opnd A = get_opnd(&ctx, ins->src1, X64_R11);
            if (A.kind == OPK_IMM) {
                if (A.imm == 0) { enc_mov_ri(out_code, D, 0); }
                else { enc_mov_ri(out_code, X64_R11, A.imm); enc_load_mem(out_code, D, x64_mem(X64_R11, off), 8, 0); }
                finish_dest(&ctx, ins->dest, D);
                break;
            }
            X64Reg base;
            if (A.kind == OPK_REG) base = A.reg;
            else { enc_load_mem(out_code, X64_R11, A.mem, 8, 0); base = X64_R11; }
            if (D != base) {
                enc_alu_rr(out_code, ALU_XOR, D, D);
                enc_test_rr(out_code, base, base);
                size_t skip = enc_jcc_rel32(out_code, CC_E);
                enc_load_mem(out_code, D, x64_mem(base, off), 8, 0);
                int32_t rel = (int32_t)(out_code->len - (skip + 4));
                memcpy(out_code->data + skip, &rel, 4);
            } else {
                enc_test_rr(out_code, base, base);
                size_t skip = enc_jcc_rel32(out_code, CC_E);
                enc_load_mem(out_code, D, x64_mem(base, off), 8, 0);
                int32_t rel = (int32_t)(out_code->len - (skip + 4));
                memcpy(out_code->data + skip, &rel, 4);
                /* base was null (still 0 in D): already the right answer */
            }
            finish_dest(&ctx, ins->dest, D);
            break;
        }

        case MIR_STORE: {
            int size = mir_type_size(ins->src1.type);
            if (ins->dest.kind == MIRV_GLOBAL || ins->dest.kind == MIRV_LOCAL) {
                X64Reg val = load_operand_bits(&ctx, ins->src1, X64_RAX);
                if (ins->dest.kind == MIRV_GLOBAL) {
                    size_t off = enc_mov_store_rip(out_code, val);
                    x64_reloc_add(out_relocs, off, ins->dest.global_name, X64_RELOC_RIP32);
                } else {
                    enc_mov_store(out_code, X64_RBP, local_offset(ins->dest.local_id), val);
                }
                break;
            }
            /* Real pointer store: width comes from the stored value's type.
               Writing 8 bytes for a `*uint8` store clobbers the following 7
               bytes - in a loop filling a byte buffer, every iteration then
               erases the bytes the previous ones wrote. */
            if (ins->src1.kind == MIRV_IMM_INT && (size < 8 || fits32(ins->src1.imm_int))) {
                X64Mem m = pointer_mem(&ctx, ins->dest, ins->src2, ins->mem_scale, ins->mem_offset);
                enc_store_imm_mem(out_code, m, (int32_t)ins->src1.imm_int, size);
                break;
            }
            if (mir_type_is_float(ins->src1.type) && ins->src1.kind == MIRV_VREG &&
                ctx.ra->vreg_locs[ins->src1.vreg].kind == MCLOC_REG) {
                int xs = x64_float_reg_pool[ctx.ra->vreg_locs[ins->src1.vreg].reg_id];
                X64Mem m = pointer_mem(&ctx, ins->dest, ins->src2, ins->mem_scale, ins->mem_offset);
                enc_sse_rm(out_code, ins->src1.type == MIR_F32 ? 0xF3 : 0xF2, 0x11, xs, m);
                break;
            }
            /* address first (it may use rax as an index scratch), then the value */
            X64Mem m = pointer_mem(&ctx, ins->dest, ins->src2, ins->mem_scale, ins->mem_offset);
            X64Reg val;
            if (mir_type_is_float(ins->src1.type) || ins->src1.kind != MIRV_VREG) {
                val = load_operand_bits(&ctx, ins->src1, X64_RAX);
            } else {
                val = use_reg(&ctx, ins->src1, X64_RAX);
            }
            enc_store_mem(out_code, m, val, size);
            break;
        }

        case MIR_JMP: {
            size_t off = enc_jmp_rel32(out_code);
            add_fixup(&ctx, off, ins->dest.label_id);
            break;
        }

        case MIR_JMP_IF: case MIR_JMP_UNLESS: {
            Opnd A = get_opnd(&ctx, ins->src1, X64_RAX);
            int jt = ins->op == MIR_JMP_IF;
            if (A.kind == OPK_IMM) {
                if ((A.imm != 0) == jt) {
                    size_t off = enc_jmp_rel32(out_code);
                    add_fixup(&ctx, off, ins->dest.label_id);
                }
                break;
            }
            if (A.kind == OPK_REG) enc_test_rr(out_code, A.reg, A.reg);
            else enc_alu_mi(out_code, ALU_CMP, A.mem, 0);
            size_t off = enc_jcc_rel32(out_code, jt ? CC_NE : CC_E);
            add_fixup(&ctx, off, ins->dest.label_id);
            break;
        }

        case MIR_CALL: {
            int has_agg = ins->ret_aggr != 0;
            if (ins->arg_aggr)
                for (int a = 0; a < ins->arg_count; a++) if (ins->arg_aggr[a]) has_agg = 1;
            for (int a = 0; a < ins->arg_count; a++)
                if (ins->args[a].kind != MIRV_VREG && ins->args[a].kind != MIRV_IMM_INT &&
                    ins->args[a].kind != MIRV_IMM_FLOAT) has_agg = 1;
            if (!has_agg) {
                emit_call_simple(&ctx, ins);
                break;
            }
            /* Same parallel-move hazard and fix as MIR_SYSCALL above: an
               argument's source vreg can already be sitting in a later
               arg's destination register, so materialize every arg into a
               scratch register and push it before any destination register
               is touched, then pop into place in reverse. All the pushes
               are popped again before `call` executes, so rsp is back to
               its pre-call value and the call's own return-address push
               isn't affected. */
            /* SysV classification: integer-class arguments take rdi, rsi, rdx,
               rcx, r8, r9 and float-class ones take xmm0-xmm7, counted
               separately; the overflow of either class goes on the stack, in
               argument order. Class comes from the argument's MIR type, so
               `sin(x)` puts x in xmm0 exactly as a C compiler would.
               Every argument is materialised and pushed BEFORE any argument
               register is written (a source vreg may live in a register we're
               about to overwrite): stack arguments first (last one first, so
               the first ends up lowest), then the register arguments, which
               are popped straight into place in reverse. rsp must be 16-byte
               aligned at the call, so an odd number of stack arguments gets a
               dummy slot pushed first. `al` carries the number of xmm
               registers used, which variadic C functions (printf) require. */
            /* Each argument becomes one or more 8-byte slots. A scalar is one
               slot; a struct passed by value (arg_aggr) is one per eightbyte,
               classified integer/sse, and goes to registers only if ALL its
               eightbytes fit (otherwise the whole struct goes on the stack,
               as does any MEMORY-class struct). A struct returned in memory
               takes a hidden first integer argument: the buffer's address. */
            int nargs = ins->arg_count;
            int ret_agg = ins->ret_aggr;
            int ret_mem = ret_agg && MIR_AGG_MEM(ret_agg);
            int cap = ret_mem ? 1 : 0;
            for (int a = 0; a < nargs; a++) {
                int d = ins->arg_aggr ? ins->arg_aggr[a] : 0;
                cap += d ? (int)((MIR_AGG_SIZE(d) + 7) / 8) : 1;
            }
            CallSlot *slots = malloc((cap ? cap : 1) * sizeof(CallSlot));
            int nslots = 0, ii = 0, fi = 0, nstack = 0;
            if (ret_mem) {
                slots[nslots++] = (CallSlot){ -1, 0, SLOT_SRET, ii++ };
            }
            for (int a = 0; a < nargs; a++) {
                int d = ins->arg_aggr ? ins->arg_aggr[a] : 0;
                if (!d) {
                    int isf = mir_type_is_float(ins->args[a].type);
                    CallSlot sl = { a, -1, SLOT_STACK, -1 };
                    if (isf && fi < 8)                             { sl.kind = SLOT_SSE; sl.reg = fi++; }
                    else if (!isf && ii < X64_SYSV_ARG_REG_COUNT)  { sl.kind = SLOT_INT; sl.reg = ii++; }
                    else nstack++;
                    slots[nslots++] = sl;
                    continue;
                }
                int ne = (MIR_AGG_SIZE(d) + 7) / 8, need_i = 0, need_f = 0;
                if (!MIR_AGG_MEM(d))
                    for (int k = 0; k < ne; k++) { if (MIR_AGG_SSE(d, k)) need_f++; else need_i++; }
                int in_regs = !MIR_AGG_MEM(d) && ii + need_i <= X64_SYSV_ARG_REG_COUNT && fi + need_f <= 8;
                for (int k = 0; k < ne; k++) {
                    CallSlot sl = { a, 8 * k, SLOT_STACK, -1 };
                    if (in_regs) {
                        if (MIR_AGG_SSE(d, k)) { sl.kind = SLOT_SSE; sl.reg = fi++; }
                        else                   { sl.kind = SLOT_INT; sl.reg = ii++; }
                    } else nstack++;
                    slots[nslots++] = sl;
                }
            }
            int pad = nstack & 1;
            if (pad) enc_push(out_code, X64_RAX);
            for (int k = nslots - 1; k >= 0; k--) {
                if (slots[k].kind != SLOT_STACK) continue;
                enc_push(out_code, load_call_slot(&ctx, ins, &slots[k]));
            }
            for (int k = 0; k < nslots; k++) {
                if (slots[k].kind == SLOT_STACK) continue;
                enc_push(out_code, load_call_slot(&ctx, ins, &slots[k]));
            }
            /* Indirect call: park the target in r11 (never an argument
               register, never allocated) now, while every source vreg is
               still intact - the pops below start writing argument regs. */
            if (!ins->callee) {
                X64Reg r = load_operand(&ctx, ins->src1, X64_RAX);
                enc_mov_rr(out_code, X64_R11, r);
            }
            for (int k = nslots - 1; k >= 0; k--) {
                if (slots[k].kind == SLOT_STACK) continue;
                if (slots[k].kind == SLOT_SSE) {
                    enc_pop(out_code, X64_RAX);
                    enc_movq_gpr_to_xmm(out_code, slots[k].reg, X64_RAX);
                } else {
                    enc_pop(out_code, x64_sysv_arg_regs[slots[k].reg]);
                }
            }
            free(slots);
            enc_mov_ri64(out_code, X64_RAX, fi);
            if (ins->callee) {
                size_t off = enc_call_rel32(out_code);
                x64_reloc_add(out_relocs, off, ins->callee, X64_RELOC_REL32);
            } else {
                enc_call_r(out_code, X64_R11);
            }
            if (nstack + pad > 0) {
                /* drop the stack arguments (add rsp, imm32) */
                x64_buf_push(out_code, x64_rex(1, 0, X64_RSP));
                x64_buf_push(out_code, 0x81);
                x64_buf_push(out_code, x64_modrm(3, 0, X64_RSP));
                x64_buf_write32(out_code, (uint32_t)(8 * (nstack + pad)));
            }
            if (ret_agg) {
                /* A small struct comes back split over rax/rdx and xmm0/xmm1
                   by eightbyte class; store it into its buffer. (MEMORY-class
                   ones were already written through the hidden pointer.) */
                if (!ret_mem) {
                    int size = MIR_AGG_SIZE(ret_agg), ne = (size + 7) / 8, ei = 0, xi = 0;
                    enc_lea_mem(out_code, X64_R11, X64_RBP, local_offset(ins->src2.local_id));
                    for (int k = 0; k < ne; k++) {
                        int rem = size - 8 * k;
                        int sz = rem >= 8 ? 8 : (rem == 1 || rem == 2 || rem == 4) ? rem : 8;
                        if (MIR_AGG_SSE(ret_agg, k)) {
                            int x = xi++;
                            if (sz == 4) enc_movss_store(out_code, X64_R11, 8 * k, x);
                            else         enc_movsd_store(out_code, X64_R11, 8 * k, x);
                        } else {
                            enc_mov_store_sz(out_code, X64_R11, 8 * k, ei++ == 0 ? X64_RAX : X64_RDX, sz);
                        }
                    }
                }
                break;
            }
            if (ins->dest.kind == MIRV_VREG && mir_type_is_float(ins->dest.type)) {
                store_result_f(&ctx, ins->dest, 0); /* float results come back in xmm0 */
            } else {
                /* a narrow integer result (C `int` etc.) only defines the low
                   bytes of rax - extend to the full register */
                if (ins->dest.kind == MIRV_VREG && mir_type_size(ins->dest.type) < 8 &&
                    ins->dest.type != MIR_PTR)
                    enc_ext_rr(out_code, X64_RAX, X64_RAX, mir_type_size(ins->dest.type),
                               !mir_type_is_unsigned(ins->dest.type));
                store_result(&ctx, ins->dest, X64_RAX);
            }
            break;
        }

        case MIR_RET: {
            /* Return value into rax BEFORE restoring, since a restored
               register could otherwise overwrite it... and restore before
               `leave`, since leave drops the frame the saves live in. */
            if (mir_type_is_float(ins->src1.type)) {
                /* float return value goes in xmm0 (SysV) */
                int rx = load_operand_f(&ctx, ins->src1, X64_XMM_SCRATCH_A);
                if (ins->src1.type == MIR_F32) enc_movss_xx(out_code, 0, rx);
                else                           enc_movsd_xx(out_code, 0, rx);
            } else {
                Opnd A = get_opnd(&ctx, ins->src1, X64_RAX);
                mov_opnd(&ctx, X64_RAX, A);
            }
            if (frameless) {
                if (fl_pad) enc_alu_ri(out_code, ALU_ADD, X64_RSP, fl_pad);
                for (int cs = ra->callee_saved_int_used - 1; cs >= 0; cs--)
                    if (ra->callee_saved_int_mask & (1u << cs)) enc_pop(out_code, x64_int_reg_pool[cs]);
            } else if (!fn->is_naked) {
                for (int cs = 0, k = 0; cs < ra->callee_saved_int_used; cs++) {
                    if (!(ra->callee_saved_int_mask & (1u << cs))) continue;
                    enc_mov_load(out_code, x64_int_reg_pool[cs], X64_RBP, callee_save_offset(fn, ra, k++));
                }
                enc_leave(out_code);
            }
            enc_ret(out_code);
            break;
        }

        case MIR_CLI: enc_cli(out_code); break;
        case MIR_STI: enc_sti(out_code); break;
        case MIR_IRET: enc_iretq(out_code); break;
        case MIR_SYSRET: enc_sysretq(out_code); break;
        case MIR_HLT: enc_hlt(out_code); break;

        /* save_regs()/restore_regs() - the asm-free way to write an ISR body.
           Pushes/pops every GPR but rsp, so the handler can freely clobber
           registers between the two calls. extra_int=1 means "this vector
           has no CPU-pushed error code" - save_regs(1) pushes a dummy 0
           first so the stack layout matches vectors that do get one, and
           restore_regs(1) drops that slot (or the real error code) before
           the caller's iret(). Order is a mirror image so save/restore always
           pair up regardless of which variant is used. */
        case MIR_SAVE_REGS: {
            if (ins->extra_int) enc_push_imm32(out_code, 0);
            enc_push(out_code, X64_RAX);
            enc_push(out_code, X64_RBX);
            enc_push(out_code, X64_RCX);
            enc_push(out_code, X64_RDX);
            enc_push(out_code, X64_RSI);
            enc_push(out_code, X64_RDI);
            enc_push(out_code, X64_RBP);
            enc_push(out_code, X64_R8);
            enc_push(out_code, X64_R9);
            enc_push(out_code, X64_R10);
            enc_push(out_code, X64_R11);
            enc_push(out_code, X64_R12);
            enc_push(out_code, X64_R13);
            enc_push(out_code, X64_R14);
            enc_push(out_code, X64_R15);
            break;
        }
        case MIR_RESTORE_REGS: {
            enc_pop(out_code, X64_R15);
            enc_pop(out_code, X64_R14);
            enc_pop(out_code, X64_R13);
            enc_pop(out_code, X64_R12);
            enc_pop(out_code, X64_R11);
            enc_pop(out_code, X64_R10);
            enc_pop(out_code, X64_R9);
            enc_pop(out_code, X64_R8);
            enc_pop(out_code, X64_RBP);
            enc_pop(out_code, X64_RDI);
            enc_pop(out_code, X64_RSI);
            enc_pop(out_code, X64_RDX);
            enc_pop(out_code, X64_RCX);
            enc_pop(out_code, X64_RBX);
            enc_pop(out_code, X64_RAX);
            if (ins->extra_int) enc_alu_ri32(out_code, ALU_ADD, X64_RSP, 8);
            break;
        }

        case MIR_OUTB: {
            load_operand(&ctx, ins->src1, X64_RDX); /* port in DX */
            load_operand(&ctx, ins->src2, X64_RAX); /* value in AL */
            enc_out_dx_al(out_code);
            break;
        }
        case MIR_INB: {
            load_operand(&ctx, ins->src1, X64_RDX);
            enc_in_al_dx(out_code);
            enc_movzx_r64_r8(out_code, X64_RAX, X64_RAX);
            store_result(&ctx, ins->dest, X64_RAX);
            break;
        }

        case MIR_OUTW: {
            load_operand(&ctx, ins->src1, X64_RDX); /* port in DX */
            load_operand(&ctx, ins->src2, X64_RAX); /* value in AX */
            enc_out_dx_ax(out_code);
            break;
        }
        case MIR_INW: {
            load_operand(&ctx, ins->src1, X64_RDX);
            enc_in_ax_dx(out_code);
            enc_ext_rr(out_code, X64_RAX, X64_RAX, 2, 0); /* zero-extend AX -> RAX */
            store_result(&ctx, ins->dest, X64_RAX);
            break;
        }

        case MIR_LGDT: {
            load_operand(&ctx, ins->src1, X64_RAX); /* descriptor address (ir_to_mir.c already built it) */
            enc_lgdt_mem(out_code, X64_RAX);
            break;
        }
        case MIR_LIDT: {
            load_operand(&ctx, ins->src1, X64_RAX);
            enc_lidt_mem(out_code, X64_RAX);
            break;
        }
        case MIR_LTR: {
            load_operand(&ctx, ins->src1, X64_RAX); /* selector, low 16 bits used */
            enc_ltr_r16(out_code, X64_RAX);
            break;
        }
        case MIR_INVLPG: {
            load_operand(&ctx, ins->src1, X64_RAX); /* linear address */
            enc_invlpg_mem(out_code, X64_RAX);
            break;
        }

        case MIR_WRMSR: {
            load_operand(&ctx, ins->src1, X64_RCX);
            /* value's low/high halves need to land in EAX/EDX - MVP: only
               handles values that fit in 32 bits (upper EDX left as
               whatever load_operand leaves, typically 0 from mov_ri64's
               upper bits) since a real 64->32:32 split isn't wired yet */
            load_operand(&ctx, ins->src2, X64_RAX);
            enc_alu_rr(out_code, ALU_XOR, X64_RDX, X64_RDX);
            enc_wrmsr(out_code);
            break;
        }
        case MIR_RDMSR: {
            load_operand(&ctx, ins->src1, X64_RCX);
            enc_rdmsr(out_code);
            store_result(&ctx, ins->dest, X64_RAX); /* low 32 bits only for now, see note above */
            break;
        }

        case MIR_READ_CR: {
            enc_mov_r_from_crN(out_code, X64_RAX, ins->extra_int);
            store_result(&ctx, ins->dest, X64_RAX);
            break;
        }
        case MIR_WRITE_CR: {
            X64Reg v = load_operand(&ctx, ins->src1, X64_RAX);
            enc_mov_crN_from_r(out_code, ins->extra_int, v);
            break;
        }

        case MIR_MEMSET: case MIR_MEMCPY: {
            /* rep stosb / rep movsb want rdi, rsi (memcpy) or rax (memset
               value) and rcx. rdi/rsi are allocatable, so the operands are
               placed with a parallel move rather than one load at a time. */
            PMove mv[3]; int nmv = 0;
            X64Reg dst2[3]; MIRValue srcs[3];
            int is_set = ins->op == MIR_MEMSET;
            dst2[0] = X64_RDI; srcs[0] = ins->dest;
            dst2[1] = is_set ? X64_RAX : X64_RSI; srcs[1] = ins->src1;
            dst2[2] = X64_RCX; srcs[2] = ins->src2;
            for (int k = 0; k < 3; k++) {
                PMove m; memset(&m, 0, sizeof m);
                m.dst = dst2[k];
                Opnd o = get_opnd(&ctx, srcs[k], X64_R11);
                m.src_kind = o.kind; m.src = o.reg; m.src_mem = o.mem; m.imm = o.imm;
                mv[nmv++] = m;
            }
            emit_par_moves(&ctx, mv, nmv);
            if (is_set) enc_rep_stosb(out_code); else enc_rep_movsb(out_code);
            break;
        }

        case MIR_SYSCALL: {
            /* args[0] = syscall number (runtime value), args[1..] = real args.
               Parallel-move fix: an arg's source vreg can already be sitting
               in a *later* arg's fixed destination register (rdi/rsi/rdx/
               r10/r8/r9 - r10 in particular is still in the regalloc pool,
               not scratch-reserved). Loading straight into destination
               registers one at a time would let an earlier load clobber a
               later arg's source. Instead, materialize every arg (and the
               syscall number) into a scratch register and push it to the
               stack *before* any destination register is touched, then pop
               them into place in reverse - stack slots can't alias GP
               registers, so this sidesteps the hazard entirely regardless
               of how many args or what cycles exist between them. */
            int real_argc = ins->arg_count - 1;
            if (real_argc < 0) {
                fprintf(stderr, "[lower_x64] MIR_SYSCALL with no syscall-number arg\n");
                break;
            }
            int n = real_argc < X64_SYSCALL_ARG_REG_COUNT ? real_argc : X64_SYSCALL_ARG_REG_COUNT;
            if (real_argc > X64_SYSCALL_ARG_REG_COUNT)
                fprintf(stderr, "[lower_x64] syscall with >6 args not supported\n");

            load_operand(&ctx, ins->args[0], X64_RAX); /* syscall number */
            enc_push(out_code, X64_RAX);
            for (int a = 0; a < n; a++) {
                X64Reg r = load_operand(&ctx, ins->args[a + 1], X64_RAX);
                enc_push(out_code, r);
            }
            for (int a = n - 1; a >= 0; a--)
                enc_pop(out_code, x64_syscall_arg_regs[a]);
            enc_pop(out_code, X64_RAX); /* syscall number, back from the bottom */

            enc_syscall(out_code);
            store_result(&ctx, ins->dest, X64_RAX);
            break;
        }

        case MIR_ASM_TEXT: {
            /* resolve each {N} to a real register name: args already in a
               register substitute directly, spilled ones get loaded into a
               scratch register first (reused if {N} appears more than once) -
               cycling through the same reserved scratch set used everywhere
               else in this file, since a genuinely free register isn't knowable
               here (the user's asm text can legally reference any of them too). */
            static const X64Reg scratch_pool[4] = { X64_RAX, X64_RDX, X64_RCX, X64_R11 };
            const char *resolved[16];
            int resolved_scratch_used = 0;
            char namebuf[16][8];

            for (int a = 0; a < ins->arg_count && a < 16; a++) {
                if (ins->args[a].kind == MIRV_VREG) {
                    MCLoc loc = ra->vreg_locs[ins->args[a].vreg];
                    if (loc.kind == MCLOC_REG) {
                        resolved[a] = reg_name(x64_int_reg_pool[loc.reg_id]);
                    } else {
                        if (resolved_scratch_used >= 4) {
                            fprintf(stderr, "[lower_x64] asm block references more than 4 "
                                    "simultaneously-spilled operands, not supported\n");
                            resolved[a] = "rax";
                        } else {
                            X64Reg s = scratch_pool[resolved_scratch_used++];
                            int32_t off = loc.is_float ? float_spill_offset(fn, ra, loc.spill_slot)
                                                        : int_spill_offset(fn, loc.spill_slot);
                            enc_mov_load(out_code, s, X64_RBP, off);
                            resolved[a] = reg_name(s);
                        }
                    }
                } else if (ins->args[a].kind == MIRV_IMM_INT) {
                    snprintf(namebuf[a], sizeof(namebuf[a]), "%lld", (long long)ins->args[a].imm_int);
                    resolved[a] = namebuf[a];
                } else {
                    resolved[a] = "0";
                }
            }

            /* substitute {N} -> resolved[N] into a fresh buffer */
            size_t cap = strlen(ins->asm_text) + 512;
            char *substituted = malloc(cap);
            size_t out_len = 0;
            const char *p = ins->asm_text;
            while (*p) {
                if (*p == '{' && isdigit((unsigned char)p[1])) {
                    int n = atoi(p + 1);
                    const char *q = p + 1;
                    while (isdigit((unsigned char)*q)) q++;
                    if (*q == '}' && n < ins->arg_count) {
                        const char *rn = resolved[n];
                        size_t rl = strlen(rn);
                        if (out_len + rl >= cap) { cap *= 2; substituted = realloc(substituted, cap); }
                        memcpy(substituted + out_len, rn, rl);
                        out_len += rl;
                        p = q + 1;
                        continue;
                    }
                }
                if (out_len + 1 >= cap) { cap *= 2; substituted = realloc(substituted, cap); }
                substituted[out_len++] = *p++;
            }
            substituted[out_len] = '\0';

            char errbuf[256];
            if (!miniasm_assemble(substituted, out_code, out_relocs, errbuf, sizeof(errbuf)))
                fprintf(stderr, "[lower_x64] asm block error: %s\n", errbuf);

            free(substituted);
            break;
        }

        case MIR_GETPARAM: {
            /* The run of GETPARAMs at the top of the function receives every
               promoted parameter at once: the ABI registers are moved into
               their homes as one parallel move (a home may be another
               parameter's ABI register). Floats were saved to their slot by
               the prologue and are loaded from there. */
            int j = i, nmv = 0;
            PMove *mv = calloc(fn->param_count + 1, sizeof(PMove));
            int *fl = malloc((fn->param_count + 1) * sizeof(int)); int nfl = 0;
            while (j < fn->count && (fn->instrs[j].op == MIR_GETPARAM || fn->instrs[j].op == MIR_NOP)) {
                const MIRInstr *g = &fn->instrs[j];
                j++;
                if (g->op != MIR_GETPARAM || g->dest.kind != MIRV_VREG) continue;
                int pidx = g->extra_int;
                int isf = fn->param_types && mir_type_is_float(fn->param_types[pidx]);
                if (isf) { fl[nfl++] = j - 1; continue; }
                PMove m; memset(&m, 0, sizeof m);
                MCLoc dl = ra->vreg_locs[g->dest.vreg];
                if (dl.kind == MCLOC_REG) m.dst = x64_int_reg_pool[dl.reg_id];
                else { m.dst_is_mem = 1; m.dst_mem = spill_mem(&ctx, dl); }
                if (pk_kind[pidx] == 0) { m.src_kind = OPK_REG; m.src = x64_sysv_arg_regs[pk_idx[pidx]]; }
                else { m.src_kind = OPK_MEM; m.src_mem = x64_mem(X64_RBP, 16 + 8 * pk_idx[pidx]); }
                mv[nmv++] = m;
            }
            emit_par_moves(&ctx, mv, nmv);
            for (int k = 0; k < nfl; k++) {
                const MIRInstr *g = &fn->instrs[fl[k]];
                enc_mov_load(out_code, X64_RAX, X64_RBP, local_offset(g->extra_int));
                enc_movq_gpr_to_xmm(out_code, X64_XMM_SCRATCH_A, X64_RAX);
                store_result_f(&ctx, g->dest, X64_XMM_SCRATCH_A);
            }
            free(mv); free(fl);
            i = j - 1;
            break;
        }

        case MIR_NOP:
            break;

        default:
            warn_unhandled(ins->op);
            break;
        }
    }

    for (int f = 0; f < ctx.fixup_count; f++) {
        int target = ctx.label_offset[ctx.fixups[f].label_id];
        int32_t rel = (int32_t)(target - (int)(ctx.fixups[f].code_offset + 4));
        memcpy(out_code->data + ctx.fixups[f].code_offset, &rel, 4);
    }

    free(ctx.fixups);
    free(ctx.label_offset);
    free(ctx.use_count);
    free(loop_head);
    free(ctx.pair_count);
    free(has_gp);
    free(pk_kind);
    free(pk_idx);
}
