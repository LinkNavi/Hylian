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
static int32_t callee_save_offset(const MIRFunc *fn, const RegAllocResult *ra, int idx) {
    return -8 * (fn->local_count + ra->num_int_spill_slots
                 + ra->num_float_spill_slots + idx + 1);
}

/* Scratch space handed out by MIR_ALLOCA_LOCAL, below everything else. */
static int32_t alloca_region_base(const MIRFunc *fn, const RegAllocResult *ra) {
    return 8 * (fn->local_count + ra->num_int_spill_slots
                + ra->num_float_spill_slots + ra->callee_saved_int_used);
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
                   + ra->callee_saved_int_used)
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
        /* no "mov xmm, imm" exists - stage the bit pattern through a
           reserved GPR scratch and movq it across */
        if (is32) {
            float f = (float)v.imm_float;
            uint32_t bits; memcpy(&bits, &f, 4);
            enc_mov_ri64(ctx->code, X64_RAX, (int64_t)(uint64_t)bits);
        } else {
            uint64_t bits; memcpy(&bits, &v.imm_float, 8);
            enc_mov_ri64(ctx->code, X64_RAX, (int64_t)bits);
        }
        enc_movq_gpr_to_xmm(ctx->code, scratch_xmm, X64_RAX);
        return scratch_xmm;
    }
    case MIRV_VREG: {
        MCLoc loc = ctx->ra->vreg_locs[v.vreg];
        if (loc.kind == MCLOC_REG) {
            int r = x64_float_reg_pool[loc.reg_id];
            if (is32) enc_movss_xx(ctx->code, scratch_xmm, r);
            else enc_movsd_xx(ctx->code, scratch_xmm, r);
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

static void store_result_f(LowerCtx *ctx, MIRValue dest, int from_xmm) {
    if (dest.kind != MIRV_VREG) return;
    MCLoc loc = ctx->ra->vreg_locs[dest.vreg];
    int is32 = (dest.type == MIR_F32);
    if (loc.kind == MCLOC_REG) {
        int d = x64_float_reg_pool[loc.reg_id];
        if (d != from_xmm) {
            if (is32) enc_movss_xx(ctx->code, d, from_xmm);
            else enc_movsd_xx(ctx->code, d, from_xmm);
        }
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

void x64_lower_func(const MIRFunc *fn, const RegAllocResult *ra,
                    X64Buf *out_code, X64RelocList *out_relocs) {
    LowerCtx ctx = {0};
    ctx.code = out_code;
    ctx.relocs = out_relocs;
    ctx.ra = ra;
    ctx.fn = fn;
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

    if (!fn->is_naked) {
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
        int nparams = fn->param_count < X64_SYSV_ARG_REG_COUNT ? fn->param_count : X64_SYSV_ARG_REG_COUNT;
        for (int i = 0; i < nparams; i++)
            enc_mov_store(out_code, X64_RBP, local_offset(i), x64_sysv_arg_regs[i]);
        if (fn->param_count > X64_SYSV_ARG_REG_COUNT)
            fprintf(stderr, "[lower_x64] function '%s' has >6 params, stack-passed params "
                    "not supported yet\n", fn->name);

        /* Save the callee-saved registers regalloc handed out. Without this,
           a callee scribbling on rbx/r12-r15 would silently destroy a value
           its *caller* was keeping there across the call - and since regalloc
           now deliberately parks call-crossing values in exactly these
           registers, that would break every one of them. Params are copied to
           their slots first (above) so this can't clobber an incoming arg. */
        for (int i = 0; i < ra->callee_saved_int_used; i++)
            enc_mov_store(out_code, X64_RBP, callee_save_offset(fn, ra, i),
                          x64_int_reg_pool[i]);
    }

    int alloca_cursor = 0; /* bytes handed out by MIR_ALLOCA_LOCAL so far */

    for (int i = 0; i < fn->count; i++) {
        const MIRInstr *ins = &fn->instrs[i];

        switch (ins->op) {

        case MIR_LABEL:
            ctx.label_offset[ins->dest.label_id] = (int)out_code->len;
            break;

        case MIR_MOV: {
            if (mir_type_is_float(ins->dest.type)) {
                int a = load_operand_f(&ctx, ins->src1, X64_XMM_SCRATCH_A);
                store_result_f(&ctx, ins->dest, a);
            } else {
                X64Reg a = load_operand(&ctx, ins->src1, X64_RAX);
                store_result(&ctx, ins->dest, a);
            }
            break;
        }

        case MIR_ADD: case MIR_SUB: case MIR_AND: case MIR_OR: case MIR_XOR: {
            X64Reg a = load_operand(&ctx, ins->src1, X64_RAX);
            X64Reg b = load_operand(&ctx, ins->src2, X64_R11);
            AluOp op = ins->op == MIR_ADD ? ALU_ADD : ins->op == MIR_SUB ? ALU_SUB
                     : ins->op == MIR_AND ? ALU_AND : ins->op == MIR_OR ? ALU_OR : ALU_XOR;
            enc_alu_rr(out_code, op, a, b);
            store_result(&ctx, ins->dest, a);
            break;
        }

        case MIR_MUL: {
            X64Reg a = load_operand(&ctx, ins->src1, X64_RAX);
            X64Reg b = load_operand(&ctx, ins->src2, X64_R11);
            enc_imul_rr(out_code, a, b);
            store_result(&ctx, ins->dest, a);
            break;
        }

        case MIR_SDIV: case MIR_UDIV: case MIR_SMOD: case MIR_UMOD: {
            load_operand(&ctx, ins->src1, X64_RAX); /* dividend low half must be in RAX */
            X64Reg b = load_operand(&ctx, ins->src2, X64_R11);
            if (ins->op == MIR_SDIV || ins->op == MIR_SMOD) {
                enc_cqo(out_code);
                enc_idiv_r(out_code, b);
            } else {
                /* unsigned: high half must be zeroed, not sign-extended */
                enc_alu_rr(out_code, ALU_XOR, X64_RDX, X64_RDX);
                enc_div_r(out_code, b);
            }
            X64Reg result = (ins->op == MIR_SDIV || ins->op == MIR_UDIV) ? X64_RAX : X64_RDX;
            store_result(&ctx, ins->dest, result);
            break;
        }

        case MIR_NEG: case MIR_NOT: {
            X64Reg a = load_operand(&ctx, ins->src1, X64_RAX);
            if (ins->op == MIR_NEG) enc_neg_r(out_code, a); else enc_not_r(out_code, a);
            store_result(&ctx, ins->dest, a);
            break;
        }

        case MIR_SHL: case MIR_SHR_A: case MIR_SHR_L: {
            X64Reg a = load_operand(&ctx, ins->src1, X64_RAX);
            load_operand(&ctx, ins->src2, X64_RCX); /* shift count must be in CL */
            ShiftKind k = ins->op == MIR_SHL ? SHIFT_SHL
                        : ins->op == MIR_SHR_A ? SHIFT_SAR : SHIFT_SHR;
            enc_shift_cl(out_code, k, a);
            store_result(&ctx, ins->dest, a);
            break;
        }

        case MIR_CMP_EQ: case MIR_CMP_NE:
        case MIR_CMP_LT: case MIR_CMP_LE: case MIR_CMP_GT: case MIR_CMP_GE:
        case MIR_UCMP_LT: case MIR_UCMP_LE: case MIR_UCMP_GT: case MIR_UCMP_GE: {
            X64Reg a = load_operand(&ctx, ins->src1, X64_RAX);
            X64Reg b = load_operand(&ctx, ins->src2, X64_R11);
            enc_alu_rr(out_code, ALU_CMP, a, b);
            CondCode cc;
            switch (ins->op) {
                case MIR_CMP_EQ: cc = CC_E; break;
                case MIR_CMP_NE: cc = CC_NE; break;
                case MIR_CMP_LT: cc = CC_L; break;
                case MIR_CMP_LE: cc = CC_LE; break;
                case MIR_CMP_GT: cc = CC_G; break;
                case MIR_CMP_GE: cc = CC_GE; break;
                case MIR_UCMP_LT: cc = CC_B; break;
                case MIR_UCMP_LE: cc = CC_BE; break;
                case MIR_UCMP_GT: cc = CC_A; break;
                default: cc = CC_AE; break;
            }
            enc_setcc(out_code, cc, a);
            enc_movzx_r64_r8(out_code, a, a);
            store_result(&ctx, ins->dest, a);
            break;
        }

        case MIR_FADD: case MIR_FSUB: case MIR_FMUL: case MIR_FDIV: {
            int a = load_operand_f(&ctx, ins->src1, X64_XMM_SCRATCH_A);
            int b = load_operand_f(&ctx, ins->src2, X64_XMM_SCRATCH_B);
            int is32 = (ins->src1.type == MIR_F32);
            switch (ins->op) {
                case MIR_FADD: is32 ? enc_addss(out_code, a, b) : enc_addsd(out_code, a, b); break;
                case MIR_FSUB: is32 ? enc_subss(out_code, a, b) : enc_subsd(out_code, a, b); break;
                case MIR_FMUL: is32 ? enc_mulss(out_code, a, b) : enc_mulsd(out_code, a, b); break;
                default:       is32 ? enc_divss(out_code, a, b) : enc_divsd(out_code, a, b); break;
            }
            store_result_f(&ctx, ins->dest, a);
            break;
        }

        case MIR_FNEG: {
            /* no dedicated negate instruction - flip the sign bit via an
               integer XOR round-trip through a GPR (no free rodata
               constant plumbed into this layer to do it via xorps) */
            int a = load_operand_f(&ctx, ins->src1, X64_XMM_SCRATCH_A);
            int is32 = (ins->src1.type == MIR_F32);
            enc_movq_xmm_to_gpr(out_code, X64_RAX, a);
            enc_mov_ri64(out_code, X64_R11, is32 ? (int64_t)0x80000000LL : (int64_t)((uint64_t)1 << 63));
            enc_alu_rr(out_code, ALU_XOR, X64_RAX, X64_R11);
            enc_movq_gpr_to_xmm(out_code, a, X64_RAX);
            store_result_f(&ctx, ins->dest, a);
            break;
        }

        case MIR_FCMP_LT: case MIR_FCMP_LE: case MIR_FCMP_GT: case MIR_FCMP_GE:
        case MIR_FCMP_EQ: case MIR_FCMP_NE: {
            /* NOTE: uses the flags straight off ucomisd/ucomiss without the
               extra PF (unordered/NaN) check real IEEE-correct comparisons
               need - a NaN operand will give a plausible-looking but not
               spec-correct result here. Flagged, not silently "correct". */
            int a = load_operand_f(&ctx, ins->src1, X64_XMM_SCRATCH_A);
            int b = load_operand_f(&ctx, ins->src2, X64_XMM_SCRATCH_B);
            int is32 = (ins->src1.type == MIR_F32);
            if (is32) enc_ucomiss(out_code, a, b); else enc_ucomisd(out_code, a, b);
            CondCode cc;
            switch (ins->op) {
                case MIR_FCMP_LT: cc = CC_B; break;
                case MIR_FCMP_LE: cc = CC_BE; break;
                case MIR_FCMP_GT: cc = CC_A; break;
                case MIR_FCMP_GE: cc = CC_AE; break;
                case MIR_FCMP_EQ: cc = CC_E; break;
                default: cc = CC_NE; break;
            }
            enc_setcc(out_code, cc, X64_RAX);
            enc_movzx_r64_r8(out_code, X64_RAX, X64_RAX);
            store_result(&ctx, ins->dest, X64_RAX); /* comparison result is always int/bool */
            break;
        }

        case MIR_I2F: {
            X64Reg a = load_operand(&ctx, ins->src1, X64_RAX);
            int is32dst = (ins->dest.type == MIR_F32);
            if (is32dst) enc_cvtsi2ss(out_code, X64_XMM_SCRATCH_A, a);
            else enc_cvtsi2sd(out_code, X64_XMM_SCRATCH_A, a);
            store_result_f(&ctx, ins->dest, X64_XMM_SCRATCH_A);
            break;
        }

        case MIR_F2I: {
            int a = load_operand_f(&ctx, ins->src1, X64_XMM_SCRATCH_A);
            int is32src = (ins->src1.type == MIR_F32);
            if (is32src) enc_cvttss2si(out_code, X64_RAX, a);
            else enc_cvttsd2si(out_code, X64_RAX, a);
            store_result(&ctx, ins->dest, X64_RAX);
            break;
        }

        case MIR_F2F: {
            int a = load_operand_f(&ctx, ins->src1, X64_XMM_SCRATCH_A);
            int src32 = (ins->src1.type == MIR_F32);
            int dst32 = (ins->dest.type == MIR_F32);
            if (src32 && !dst32) enc_cvtss2sd(out_code, X64_XMM_SCRATCH_A, a);
            else if (!src32 && dst32) enc_cvtsd2ss(out_code, X64_XMM_SCRATCH_A, a);
            /* else same width - a already holds the right bits */
            store_result_f(&ctx, ins->dest, X64_XMM_SCRATCH_A);
            break;
        }

        case MIR_SEXT: case MIR_ZEXT: case MIR_TRUNC: {
            /* All three are "reinterpret the low N bytes into a full 64-bit
               register", differing only in N and in which extension to use.
               Widening (SEXT/ZEXT) takes N from the source; narrowing (TRUNC)
               takes it from the destination. Until this existed, all three fell
               through to warn_unhandled and emitted NOTHING - the destination
               kept whatever stale value happened to be in its register, which
               is why `cast<uint8>(48 + digit)` produced the digit rather than
               its ASCII character. */
            X64Reg a = load_operand(&ctx, ins->src1, X64_RAX);
            int size = (ins->op == MIR_TRUNC) ? mir_type_size(ins->dest.type)
                                              : mir_type_size(ins->src1.type);
            int is_signed = (ins->op == MIR_SEXT) ? 1
                          : (ins->op == MIR_ZEXT) ? 0
                          : !mir_type_is_unsigned(ins->dest.type);
            enc_ext_rr(out_code, a, a, size, is_signed);
            store_result(&ctx, ins->dest, a);
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
            size_t off = enc_lea_rip(out_code, X64_RAX);
            x64_reloc_add(out_relocs, off, ins->src1.global_name, X64_RELOC_RIP32);
            store_result(&ctx, ins->dest, X64_RAX);
            break;
        }

        case MIR_LEA_LOCAL: {
            enc_lea_mem(out_code, X64_RAX, X64_RBP, local_offset(ins->src1.local_id));
            store_result(&ctx, ins->dest, X64_RAX);
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
            if (ins->src1.kind == MIRV_GLOBAL) {
                size_t off = enc_mov_load_rip(out_code, X64_RAX);
                x64_reloc_add(out_relocs, off, ins->src1.global_name, X64_RELOC_RIP32);
            } else if (ins->src1.kind == MIRV_LOCAL) {
                enc_mov_load(out_code, X64_RAX, X64_RBP, local_offset(ins->src1.local_id));
            } else {
                /* Real pointer dereference: width comes from the loaded type,
                   not the register width. An 8-byte load of a `*uint8` reads 7
                   bytes past the target and returns them as part of the value. */
                X64Reg base = load_operand(&ctx, ins->src1, X64_RAX);
                enc_mov_load_sz(out_code, X64_RAX, base,
                                ins->mem_offset /* struct field / array header */,
                                mir_type_size(ins->dest.type),
                                !mir_type_is_unsigned(ins->dest.type));
            }
            store_result(&ctx, ins->dest, X64_RAX);
            break;
        }

        case MIR_STORE: {
            X64Reg val = load_operand(&ctx, ins->src1, X64_RAX);
            if (ins->dest.kind == MIRV_GLOBAL) {
                size_t off = enc_mov_store_rip(out_code, val);
                x64_reloc_add(out_relocs, off, ins->dest.global_name, X64_RELOC_RIP32);
            } else if (ins->dest.kind == MIRV_LOCAL) {
                enc_mov_store(out_code, X64_RBP, local_offset(ins->dest.local_id), val);
            } else {
                /* Real pointer store: width comes from the stored value's type.
                   Writing 8 bytes for a `*uint8` store clobbers the following 7
                   bytes - in a loop filling a byte buffer, every iteration then
                   erases the bytes the previous ones wrote. */
                X64Reg addr = load_operand(&ctx, ins->dest, X64_R11);
                enc_mov_store_sz(out_code, addr, ins->mem_offset, val,
                                 mir_type_size(ins->src1.type));
            }
            break;
        }

        case MIR_JMP: {
            size_t off = enc_jmp_rel32(out_code);
            add_fixup(&ctx, off, ins->dest.label_id);
            break;
        }

        case MIR_JMP_IF: case MIR_JMP_UNLESS: {
            X64Reg cond = load_operand(&ctx, ins->src1, X64_RAX);
            enc_mov_ri64(out_code, X64_R11, 0);
            enc_alu_rr(out_code, ALU_CMP, cond, X64_R11);
            size_t off = enc_jcc_rel32(out_code, ins->op == MIR_JMP_IF ? CC_NE : CC_E);
            add_fixup(&ctx, off, ins->dest.label_id);
            break;
        }

        case MIR_CALL: {
            /* Same parallel-move hazard and fix as MIR_SYSCALL above: an
               argument's source vreg can already be sitting in a later
               arg's destination register, so materialize every arg into a
               scratch register and push it before any destination register
               is touched, then pop into place in reverse. All the pushes
               are popped again before `call` executes, so rsp is back to
               its pre-call value and the call's own return-address push
               isn't affected. */
            int n = ins->arg_count < X64_SYSV_ARG_REG_COUNT ? ins->arg_count : X64_SYSV_ARG_REG_COUNT;
            for (int a = 0; a < n; a++) {
                X64Reg r = load_operand(&ctx, ins->args[a], X64_RAX);
                enc_push(out_code, r);
            }
            for (int a = n - 1; a >= 0; a--)
                enc_pop(out_code, x64_sysv_arg_regs[a]);
            if (ins->arg_count > X64_SYSV_ARG_REG_COUNT)
                fprintf(stderr, "[lower_x64] call with >6 args not yet supported (stack args)\n");
            if (ins->callee) {
                size_t off = enc_call_rel32(out_code);
                x64_reloc_add(out_relocs, off, ins->callee, X64_RELOC_REL32);
            } else {
                fprintf(stderr, "[lower_x64] indirect calls not yet supported\n");
            }
            store_result(&ctx, ins->dest, X64_RAX);
            break;
        }

        case MIR_RET: {
            /* Return value into rax BEFORE restoring, since a restored
               register could otherwise overwrite it... and restore before
               `leave`, since leave drops the frame the saves live in. */
            load_operand(&ctx, ins->src1, X64_RAX);
            if (!fn->is_naked) {
                for (int cs = 0; cs < ra->callee_saved_int_used; cs++)
                    enc_mov_load(out_code, x64_int_reg_pool[cs], X64_RBP,
                                 callee_save_offset(fn, ra, cs));
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

        case MIR_MEMSET: {
            load_operand(&ctx, ins->dest, X64_RDI);
            load_operand(&ctx, ins->src1, X64_RAX);
            load_operand(&ctx, ins->src2, X64_RCX);
            enc_rep_stosb(out_code);
            break;
        }
        case MIR_MEMCPY: {
            load_operand(&ctx, ins->dest, X64_RDI);
            load_operand(&ctx, ins->src1, X64_RSI);
            load_operand(&ctx, ins->src2, X64_RCX);
            enc_rep_movsb(out_code);
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
}
