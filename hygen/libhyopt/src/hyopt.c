#include "hyopt.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================================================================
 *  Shared helpers
 * ====================================================================== */

static int is_f(MIRType t) { return mir_type_is_float(t); }
static int max_label_id(const MIRFunc *fn);
static int fits_i32(int64_t v);

typedef struct {
    int has_def;      /* dest is written */
    int dest_is_use;  /* dest is read (STORE's address) */
    int use_src1, use_src2;
} Shape;

/* Which operand slots of an instruction are real reads/writes. Mirrors the
   register allocator's view (libhyregalloc) - a disagreement between the two
   would let a value be freed while still live. */
static Shape shape_of(const MIRInstr *ins) {
    Shape s = {1, 0, 1, 1};
    switch (ins->op) {
    case MIR_STORE:
        s.has_def = 0; s.dest_is_use = 1; s.use_src1 = 1; s.use_src2 = ins->mem_scale != 0;
        break;
    case MIR_LOAD:
        s.use_src1 = 1; s.use_src2 = ins->mem_scale != 0;
        break;
    case MIR_ARR_LEN: case MIR_ARR_DATA:
        s.use_src2 = 0;
        break;
    case MIR_LABEL: case MIR_JMP:
        s.has_def = 0; s.use_src1 = 0; s.use_src2 = 0;
        break;
    case MIR_JMP_IF: case MIR_JMP_UNLESS:
        s.has_def = 0; s.use_src1 = 1; s.use_src2 = 0;
        break;
    case MIR_CALL:
        s.use_src1 = ins->callee == NULL; s.use_src2 = 0;
        break;
    case MIR_ASM_TEXT:
        s.has_def = 0; s.use_src1 = 0; s.use_src2 = 0;
        break;
    case MIR_RET:
        s.has_def = 0; s.use_src1 = 1; s.use_src2 = 0;
        break;
    case MIR_OUTB: case MIR_OUTW: case MIR_WRMSR: case MIR_LGDT: case MIR_LIDT:
        s.has_def = 0; s.use_src1 = 1; s.use_src2 = 1;
        break;
    case MIR_INB: case MIR_INW: case MIR_RDMSR: case MIR_READ_CR:
        s.use_src2 = 0;
        break;
    case MIR_WRITE_CR: case MIR_LTR: case MIR_INVLPG:
        s.has_def = 0; s.use_src1 = 1; s.use_src2 = 0;
        break;
    case MIR_MEMSET: case MIR_MEMCPY:
        s.has_def = 0; s.dest_is_use = 1; s.use_src1 = 1; s.use_src2 = 1;
        break;
    case MIR_CLI: case MIR_STI: case MIR_IRET: case MIR_SYSRET: case MIR_HLT:
    case MIR_SAVE_REGS: case MIR_RESTORE_REGS: case MIR_NOP: case MIR_ASM_RAW:
        s.has_def = 0; s.use_src1 = 0; s.use_src2 = 0;
        break;
    case MIR_LEA_LOCAL: case MIR_LEA_GLOBAL: case MIR_ALLOCA_LOCAL: case MIR_GETPARAM:
        s.use_src1 = 0; s.use_src2 = 0;
        break;
    case MIR_MOV: case MIR_NEG: case MIR_NOT: case MIR_FNEG:
    case MIR_SEXT: case MIR_ZEXT: case MIR_TRUNC: case MIR_I2F: case MIR_F2I:
    case MIR_F2F: case MIR_BITCAST:
        s.use_src2 = 0;
        break;
    default:
        break;
    }
    return s;
}

/* Pointer to the vreg this instruction defines, or NULL. */
static MIRValue *def_of(MIRInstr *ins) {
    Shape s = shape_of(ins);
    if (s.has_def && ins->dest.kind == MIRV_VREG) return &ins->dest;
    return NULL;
}

typedef void (*UseFn)(MIRValue *v, void *ud);

/* Visit every vreg operand the instruction reads. */
static void for_each_use(MIRInstr *ins, UseFn f, void *ud) {
    Shape s = shape_of(ins);
    if (s.use_src1 && ins->src1.kind == MIRV_VREG) f(&ins->src1, ud);
    if (s.use_src2 && ins->src2.kind == MIRV_VREG) f(&ins->src2, ud);
    if (s.dest_is_use && ins->dest.kind == MIRV_VREG) f(&ins->dest, ud);
    for (int a = 0; a < ins->arg_count; a++)
        if (ins->args[a].kind == MIRV_VREG) f(&ins->args[a], ud);
}

typedef struct { int *cnt; } CntCtx;
static void cnt_use(MIRValue *v, void *ud) { ((CntCtx *)ud)->cnt[v->vreg]++; }

static int max_vreg(const MIRFunc *fn) {
    int m = fn->vreg_count - 1;
    for (int i = 0; i < fn->count; i++) {
        const MIRInstr *ins = &fn->instrs[i];
        const MIRValue *vs[3] = { &ins->dest, &ins->src1, &ins->src2 };
        for (int k = 0; k < 3; k++)
            if (vs[k]->kind == MIRV_VREG && vs[k]->vreg > m) m = vs[k]->vreg;
        for (int a = 0; a < ins->arg_count; a++)
            if (ins->args[a].kind == MIRV_VREG && ins->args[a].vreg > m) m = ins->args[a].vreg;
    }
    return m + 1;
}

static int is_terminator(MIROp op) {
    return op == MIR_RET || op == MIR_JMP || op == MIR_JMP_IF || op == MIR_JMP_UNLESS
        || op == MIR_IRET || op == MIR_SYSRET;
}

/* Removable when its result is unused. Anything that touches memory, control
   flow, the machine or another function stays. */
static int is_pure(const MIRInstr *ins) {
    switch (ins->op) {
    case MIR_MOV: case MIR_ADD: case MIR_SUB: case MIR_MUL:
    case MIR_SDIV: case MIR_UDIV: case MIR_SMOD: case MIR_UMOD:
    case MIR_NEG: case MIR_NOT: case MIR_AND: case MIR_OR: case MIR_XOR:
    case MIR_SHL: case MIR_SHR_A: case MIR_SHR_L:
    case MIR_FADD: case MIR_FSUB: case MIR_FMUL: case MIR_FDIV: case MIR_FNEG:
    case MIR_CMP_EQ: case MIR_CMP_NE: case MIR_CMP_LT: case MIR_CMP_LE:
    case MIR_CMP_GT: case MIR_CMP_GE:
    case MIR_UCMP_LT: case MIR_UCMP_LE: case MIR_UCMP_GT: case MIR_UCMP_GE:
    case MIR_FCMP_LT: case MIR_FCMP_LE: case MIR_FCMP_GT: case MIR_FCMP_GE:
    case MIR_FCMP_EQ: case MIR_FCMP_NE:
    case MIR_SEXT: case MIR_ZEXT: case MIR_TRUNC: case MIR_I2F: case MIR_F2I:
    case MIR_F2F: case MIR_BITCAST:
    case MIR_LEA_LOCAL: case MIR_LEA_GLOBAL: case MIR_ALLOCA_LOCAL:
    case MIR_GETPARAM: case MIR_NOP: case MIR_ARR_LEN: case MIR_ARR_DATA:
    case MIR_SMULH: case MIR_UMULH: case MIR_SELECT:
        return 1;
    case MIR_LOAD:
        return ins->extra_int == 0; /* volatile loads are observable */
    default:
        return 0;
    }
}

static void compact(MIRFunc *fn) {
    int w = 0;
    for (int i = 0; i < fn->count; i++) {
        if (fn->instrs[i].op == MIR_NOP) {
            /* a NOP that still owns an args array (from a killed call) */
            if (fn->instrs[i].args) { free(fn->instrs[i].args); fn->instrs[i].args = NULL; }
            continue;
        }
        if (w != i) fn->instrs[w] = fn->instrs[i];
        w++;
    }
    fn->count = w;
}

static void make_nop(MIRInstr *ins) {
    if (ins->args) { free(ins->args); ins->args = NULL; }
    ins->arg_count = 0;
    ins->op = MIR_NOP;
    ins->dest = mir_none(); ins->src1 = mir_none(); ins->src2 = mir_none();
}

/* Rewrite `ins` in place into  dest = src  (a MOV), dropping call state. */
static void make_mov(MIRInstr *ins, MIRValue src) {
    MIRValue d = ins->dest;
    if (ins->args) { free(ins->args); ins->args = NULL; }
    ins->arg_count = 0;
    ins->op = MIR_MOV;
    ins->dest = d;
    ins->src1 = src;
    ins->src2 = mir_none();
    ins->mem_offset = 0; ins->mem_scale = 0; ins->extra_int = 0;
}

static int has_asm(const MIRFunc *fn) {
    for (int i = 0; i < fn->count; i++) {
        MIROp op = fn->instrs[i].op;
        if (op == MIR_ASM_TEXT || op == MIR_ASM_RAW || op == MIR_SAVE_REGS ||
            op == MIR_RESTORE_REGS || op == MIR_IRET || op == MIR_SYSRET)
            return 1;
    }
    return 0;
}

/* ----------------------------------------------------------------------
 *  options
 * ---------------------------------------------------------------------- */

static int pass_off(const char *name) {
    const char *e = getenv("HYLIAN_OPT_OFF");
    if (!e) return 0;
    size_t n = strlen(name);
    for (const char *p = e; *p; ) {
        const char *q = strchr(p, ',');
        size_t len = q ? (size_t)(q - p) : strlen(p);
        if (len == n && strncmp(p, name, n) == 0) return 1;
        if (!q) break;
        p = q + 1;
    }
    return 0;
}

/* Debug aid (HYLIAN_OPT_VERIFY=1): after every pass, check that no vreg is read
   before some instruction defines it, in linear order (what mir_verify checks),
   and name the first pass that breaks it. */
static void verify_after(MIRFunc *fn, const char *pass) {
    if (!getenv("HYLIAN_OPT_VERIFY")) return;
    int nv = max_vreg(fn);
    unsigned char *def = calloc(nv + 1, 1);
    for (int i = 0; i < fn->count; i++) {
        MIRInstr *ins = &fn->instrs[i];
        if (ins->op == MIR_NOP) continue;
        Shape sh = shape_of(ins);
        const MIRValue *us[3] = { sh.use_src1 ? &ins->src1 : NULL, sh.use_src2 ? &ins->src2 : NULL,
                                  sh.dest_is_use ? &ins->dest : NULL };
        for (int k = 0; k < 3; k++)
            if (us[k] && us[k]->kind == MIRV_VREG && !def[us[k]->vreg]) {
                fprintf(stderr, "[hyopt] pass '%s' left %s reading v%d (instr %d, %s) before any def\n",
                        pass, fn->name, us[k]->vreg, i, mir_op_name(ins->op));
                free(def);
                return;
            }
        for (int a = 0; a < ins->arg_count; a++)
            if (ins->args[a].kind == MIRV_VREG && !def[ins->args[a].vreg]) {
                fprintf(stderr, "[hyopt] pass '%s' left %s reading v%d (arg of instr %d, %s) before any def\n",
                        pass, fn->name, ins->args[a].vreg, i, mir_op_name(ins->op));
                free(def);
                return;
            }
        MIRValue *d = def_of(ins);
        if (d) def[d->vreg] = 1;
    }
    free(def);
}

/* ======================================================================
 *  CFG + liveness
 * ====================================================================== */

typedef struct { int start, end; int succ[2]; int nsucc; } Blk;
typedef struct { Blk *b; int n; int *ib; } Cfg;

static Cfg build_cfg(const MIRFunc *fn) {
    Cfg cfg = {0};
    if (fn->count == 0) return cfg;
    int *leader = calloc(fn->count, sizeof(int));
    leader[0] = 1;
    for (int i = 0; i < fn->count; i++) {
        if (fn->instrs[i].op == MIR_LABEL) leader[i] = 1;
        if (is_terminator(fn->instrs[i].op) && i + 1 < fn->count) leader[i + 1] = 1;
    }
    int nb = 0;
    for (int i = 0; i < fn->count; i++) if (leader[i]) nb++;
    cfg.b = calloc(nb, sizeof(Blk));
    cfg.n = nb;
    cfg.ib = malloc(fn->count * sizeof(int));
    int bi = -1;
    for (int i = 0; i < fn->count; i++) {
        if (leader[i]) {
            bi++;
            cfg.b[bi].start = i;
            cfg.b[bi].end = fn->count;
            if (bi > 0) cfg.b[bi - 1].end = i;
        }
        cfg.ib[i] = bi;
    }
    free(leader);

    int maxl = -1;
    for (int i = 0; i < fn->count; i++)
        if (fn->instrs[i].op == MIR_LABEL && fn->instrs[i].dest.label_id > maxl)
            maxl = fn->instrs[i].dest.label_id;
    int *l2b = NULL;
    if (maxl >= 0) {
        l2b = malloc((maxl + 1) * sizeof(int));
        for (int i = 0; i <= maxl; i++) l2b[i] = -1;
        for (int i = 0; i < fn->count; i++)
            if (fn->instrs[i].op == MIR_LABEL) l2b[fn->instrs[i].dest.label_id] = cfg.ib[i];
    }
    for (int k = 0; k < nb; k++) {
        Blk *bk = &cfg.b[k];
        const MIRInstr *last = &fn->instrs[bk->end - 1];
        if (last->op == MIR_JMP) {
            int l = last->dest.label_id;
            if (l2b && l <= maxl && l2b[l] >= 0) bk->succ[bk->nsucc++] = l2b[l];
        } else if (last->op == MIR_JMP_IF || last->op == MIR_JMP_UNLESS) {
            int l = last->dest.label_id;
            if (l2b && l <= maxl && l2b[l] >= 0) bk->succ[bk->nsucc++] = l2b[l];
            if (k + 1 < nb) bk->succ[bk->nsucc++] = k + 1;
        } else if (last->op == MIR_RET || last->op == MIR_IRET || last->op == MIR_SYSRET) {
        } else if (k + 1 < nb) {
            bk->succ[bk->nsucc++] = k + 1;
        }
    }
    free(l2b);
    return cfg;
}

static void cfg_free(Cfg *c) { free(c->b); free(c->ib); }

typedef uint64_t Word;
#define BITSET_BUDGET ((size_t)6000000)   /* words per dataflow table, ~48 MB */
#define BS_WORDS(n) (((n) + 63) / 64)
static inline int  bs_get(const Word *b, int i) { return (int)((b[i >> 6] >> (i & 63)) & 1); }
static inline void bs_set(Word *b, int i) { b[i >> 6] |= (Word)1 << (i & 63); }
static inline void bs_clr(Word *b, int i) { b[i >> 6] &= ~((Word)1 << (i & 63)); }

typedef struct { int nv, words; Word *live_in, *live_out; } Live;

typedef struct { Word *use, *def; int nv; int blk; } UseCtx;
static void uc_use(MIRValue *v, void *ud) {
    UseCtx *u = ud;
    if (!bs_get(u->def, v->vreg)) bs_set(u->use, v->vreg);
}

static Live compute_live(MIRFunc *fn, const Cfg *cfg, int nv) {
    Live lv;
    lv.nv = nv;
    lv.words = BS_WORDS(nv);
    size_t sz = (size_t)cfg->n * lv.words;
    Word *use = calloc(sz, sizeof(Word)), *def = calloc(sz, sizeof(Word));
    lv.live_in = calloc(sz, sizeof(Word));
    lv.live_out = calloc(sz, sizeof(Word));
    for (int b = 0; b < cfg->n; b++) {
        UseCtx u = { use + (size_t)b * lv.words, def + (size_t)b * lv.words, nv, b };
        for (int i = cfg->b[b].start; i < cfg->b[b].end; i++) {
            MIRInstr *ins = &fn->instrs[i];
            for_each_use(ins, uc_use, &u);
            MIRValue *d = def_of(ins);
            if (d) bs_set(u.def, d->vreg);
        }
    }
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int b = cfg->n - 1; b >= 0; b--) {
            Word *out = lv.live_out + (size_t)b * lv.words;
            Word *in = lv.live_in + (size_t)b * lv.words;
            Word *ub = use + (size_t)b * lv.words, *db = def + (size_t)b * lv.words;
            for (int s = 0; s < cfg->b[b].nsucc; s++) {
                Word *sin = lv.live_in + (size_t)cfg->b[b].succ[s] * lv.words;
                for (int w = 0; w < lv.words; w++) out[w] |= sin[w];
            }
            for (int w = 0; w < lv.words; w++) {
                Word ni = ub[w] | (out[w] & ~db[w]);
                if (ni != in[w]) { in[w] = ni; changed = 1; }
            }
        }
    }
    free(use); free(def);
    return lv;
}

static void live_free(Live *lv) { free(lv->live_in); free(lv->live_out); }

/* ======================================================================
 *  Constant folding
 * ====================================================================== */

static int64_t ext_low(int64_t v, int size, int is_signed) {
    switch (size) {
    case 1: return is_signed ? (int64_t)(int8_t)v  : (int64_t)(uint8_t)v;
    case 2: return is_signed ? (int64_t)(int16_t)v : (int64_t)(uint16_t)v;
    case 4: return is_signed ? (int64_t)(int32_t)v : (int64_t)(uint32_t)v;
    default: return v;
    }
}

static int is_imm(const MIRValue *v) { return v->kind == MIRV_IMM_INT; }
static int is_immf(const MIRValue *v) { return v->kind == MIRV_IMM_FLOAT; }
static int is_vr(const MIRValue *v) { return v->kind == MIRV_VREG; }
static int same_vreg(const MIRValue *a, const MIRValue *b) {
    return a->kind == MIRV_VREG && b->kind == MIRV_VREG && a->vreg == b->vreg;
}

/* Evaluate an integer op on two constants. Returns 1 and sets *out if the
   result is defined. */
static int fold_int(MIROp op, int64_t a, int64_t b, const MIRInstr *ins, int64_t *out) {
    uint64_t ua = (uint64_t)a, ub = (uint64_t)b;
    switch (op) {
    case MIR_ADD: *out = (int64_t)(ua + ub); return 1;
    case MIR_SUB: *out = (int64_t)(ua - ub); return 1;
    case MIR_MUL: *out = (int64_t)(ua * ub); return 1;
    case MIR_AND: *out = a & b; return 1;
    case MIR_OR:  *out = a | b; return 1;
    case MIR_XOR: *out = a ^ b; return 1;
    case MIR_SHL:   *out = (int64_t)(ua << (ub & 63)); return 1;
    case MIR_SHR_L: *out = (int64_t)(ua >> (ub & 63)); return 1;
    case MIR_SHR_A: *out = a >> (ub & 63); return 1;
    case MIR_SDIV: if (b == 0 || (a == INT64_MIN && b == -1)) return 0; *out = a / b; return 1;
    case MIR_SMOD: if (b == 0 || (a == INT64_MIN && b == -1)) return 0; *out = a % b; return 1;
    case MIR_UDIV: if (b == 0) return 0; *out = (int64_t)(ua / ub); return 1;
    case MIR_UMOD: if (b == 0) return 0; *out = (int64_t)(ua % ub); return 1;
    case MIR_CMP_EQ: *out = a == b; return 1;
    case MIR_CMP_NE: *out = a != b; return 1;
    case MIR_CMP_LT: *out = a < b; return 1;
    case MIR_CMP_LE: *out = a <= b; return 1;
    case MIR_CMP_GT: *out = a > b; return 1;
    case MIR_CMP_GE: *out = a >= b; return 1;
    case MIR_UCMP_LT: *out = ua < ub; return 1;
    case MIR_UCMP_LE: *out = ua <= ub; return 1;
    case MIR_UCMP_GT: *out = ua > ub; return 1;
    case MIR_UCMP_GE: *out = ua >= ub; return 1;
    default: (void)ins; return 0;
    }
}

static int is_commutative(MIROp op) {
    return op == MIR_ADD || op == MIR_MUL || op == MIR_AND || op == MIR_OR || op == MIR_XOR
        || op == MIR_CMP_EQ || op == MIR_CMP_NE || op == MIR_FADD || op == MIR_FMUL
        || op == MIR_FCMP_EQ || op == MIR_FCMP_NE;
}

/* op with its operands swapped (a op b == b op' a), or MIR_NOP if none */
static MIROp swapped_cmp(MIROp op) {
    switch (op) {
    case MIR_CMP_EQ: return MIR_CMP_EQ;   case MIR_CMP_NE: return MIR_CMP_NE;
    case MIR_CMP_LT: return MIR_CMP_GT;   case MIR_CMP_GT: return MIR_CMP_LT;
    case MIR_CMP_LE: return MIR_CMP_GE;   case MIR_CMP_GE: return MIR_CMP_LE;
    case MIR_UCMP_LT: return MIR_UCMP_GT; case MIR_UCMP_GT: return MIR_UCMP_LT;
    case MIR_UCMP_LE: return MIR_UCMP_GE; case MIR_UCMP_GE: return MIR_UCMP_LE;
    default: return MIR_NOP;
    }
}

static int is_int_cmp(MIROp op) {
    return (op >= MIR_CMP_EQ && op <= MIR_UCMP_GE);
}

/* logical negation of an integer compare */
static MIROp negated_cmp(MIROp op) {
    switch (op) {
    case MIR_CMP_EQ: return MIR_CMP_NE;   case MIR_CMP_NE: return MIR_CMP_EQ;
    case MIR_CMP_LT: return MIR_CMP_GE;   case MIR_CMP_GE: return MIR_CMP_LT;
    case MIR_CMP_LE: return MIR_CMP_GT;   case MIR_CMP_GT: return MIR_CMP_LE;
    case MIR_UCMP_LT: return MIR_UCMP_GE; case MIR_UCMP_GE: return MIR_UCMP_LT;
    case MIR_UCMP_LE: return MIR_UCMP_GT; case MIR_UCMP_GT: return MIR_UCMP_LE;
    default: return MIR_NOP;
    }
}

static int log2_exact(int64_t v) {
    if (v <= 0 || (v & (v - 1))) return -1;
    int k = 0;
    while (((int64_t)1 << k) != v) k++;
    return k;
}

/* Try to simplify `ins` in place. Returns 1 if it changed. Operands have
   already had copies/constants propagated into them. */
static int simplify(MIRFunc *fn, MIRInstr *ins) {
    MIROp op = ins->op;
    MIRValue *a = &ins->src1, *b = &ins->src2;
    int dest_float = is_f(ins->dest.type);
    (void)fn;

    /* ---- pure constant folding ---- */
    if (ins->dest.kind == MIRV_VREG && !dest_float) {
        int64_t r;
        if ((op >= MIR_ADD && op <= MIR_UMOD) || (op >= MIR_AND && op <= MIR_SHR_L) ||
            (op >= MIR_CMP_EQ && op <= MIR_UCMP_GE)) {
            if (is_imm(a) && is_imm(b) && fold_int(op, a->imm_int, b->imm_int, ins, &r)) {
                make_mov(ins, mir_imm_int(r, ins->dest.type));
                return 1;
            }
        }
        if (op == MIR_NEG && is_imm(a)) { make_mov(ins, mir_imm_int((int64_t)(0 - (uint64_t)a->imm_int), ins->dest.type)); return 1; }
        if (op == MIR_NOT && is_imm(a)) { make_mov(ins, mir_imm_int(~a->imm_int, ins->dest.type)); return 1; }
        if (op == MIR_TRUNC && is_imm(a)) {
            make_mov(ins, mir_imm_int(ext_low(a->imm_int, mir_type_size(ins->dest.type),
                                              !mir_type_is_unsigned(ins->dest.type)), ins->dest.type));
            return 1;
        }
        if ((op == MIR_SEXT || op == MIR_ZEXT) && is_imm(a)) {
            make_mov(ins, mir_imm_int(ext_low(a->imm_int, mir_type_size(a->type), op == MIR_SEXT),
                                      ins->dest.type));
            return 1;
        }
        if (op == MIR_F2I && is_immf(a)) {
            double d = a->imm_float;
            if (d > -9.2e18 && d < 9.2e18) { make_mov(ins, mir_imm_int((int64_t)d, ins->dest.type)); return 1; }
        }
        if (op >= MIR_FCMP_LT && op <= MIR_FCMP_NE && is_immf(a) && is_immf(b)) {
            double x = a->imm_float, y = b->imm_float;
            int r2 = 0;
            switch (op) {
            case MIR_FCMP_LT: r2 = x < y; break;  case MIR_FCMP_LE: r2 = x <= y; break;
            case MIR_FCMP_GT: r2 = x > y; break;  case MIR_FCMP_GE: r2 = x >= y; break;
            case MIR_FCMP_EQ: r2 = x == y; break; default: r2 = x != y; break;
            }
            make_mov(ins, mir_imm_int(r2, ins->dest.type));
            return 1;
        }
    }
    if (ins->dest.kind == MIRV_VREG && dest_float) {
        if ((op == MIR_FADD || op == MIR_FSUB || op == MIR_FMUL || op == MIR_FDIV) &&
            is_immf(a) && is_immf(b) && a->type == b->type && a->type == ins->dest.type) {
            double x = a->imm_float, y = b->imm_float, r;
            switch (op) {
            case MIR_FADD: r = x + y; break; case MIR_FSUB: r = x - y; break;
            case MIR_FMUL: r = x * y; break; default: r = x / y; break;
            }
            if (ins->dest.type == MIR_F32) r = (double)(float)r;
            make_mov(ins, mir_imm_float(r, ins->dest.type));
            return 1;
        }
        if (op == MIR_FNEG && is_immf(a) && a->type == ins->dest.type) {
            make_mov(ins, mir_imm_float(-a->imm_float, ins->dest.type));
            return 1;
        }
        if (op == MIR_I2F && is_imm(a)) {
            double d = (double)a->imm_int;
            if (ins->dest.type == MIR_F32) d = (double)(float)a->imm_int;
            make_mov(ins, mir_imm_float(d, ins->dest.type));
            return 1;
        }
    }

    if (dest_float || ins->dest.kind != MIRV_VREG) return 0;

    /* an int<->pointer reinterpretation moves no bits */
    if (op == MIR_BITCAST && !is_f(a->type) && (is_vr(a) || is_imm(a))) { make_mov(ins, *a); return 1; }
    /* 0 - x */
    if (op == MIR_SUB && is_imm(a) && a->imm_int == 0 && is_vr(b)) {
        ins->op = MIR_NEG; ins->src1 = *b; ins->src2 = mir_none();
        return 1;
    }

    /* ---- canonicalise: constant on the right ---- */
    if (is_imm(a) && is_vr(b)) {
        MIROp nop2 = swapped_cmp(op);
        if (nop2 == MIR_NOP && is_commutative(op)) nop2 = op;
        if (nop2 != MIR_NOP) {
            MIRValue t = *a; *a = *b; *b = t;
            ins->op = nop2;
            simplify(fn, ins);
            return 1;
        }
    }

    /* ---- algebraic identities (right operand constant) ---- */
    if (is_imm(b) && is_vr(a)) {
        int64_t k = b->imm_int;
        switch (op) {
        case MIR_ADD: case MIR_SUB: case MIR_OR: case MIR_XOR:
        case MIR_SHL: case MIR_SHR_A: case MIR_SHR_L:
            if (k == 0) { make_mov(ins, *a); return 1; }
            break;
        case MIR_MUL:
            if (k == 1) { make_mov(ins, *a); return 1; }
            if (k == 0) { make_mov(ins, mir_imm_int(0, ins->dest.type)); return 1; }
            if (k == -1) { ins->op = MIR_NEG; ins->src2 = mir_none(); return 1; }
            { int s = log2_exact(k);
              if (s > 0) { ins->op = MIR_SHL; ins->src2 = mir_imm_int(s, MIR_I64); return 1; } }
            break;
        case MIR_AND:
            if (k == 0) { make_mov(ins, mir_imm_int(0, ins->dest.type)); return 1; }
            if (k == -1) { make_mov(ins, *a); return 1; }
            break;
        case MIR_SDIV:
            if (k == 1) { make_mov(ins, *a); return 1; }
            break;
        case MIR_UDIV:
            if (k == 1) { make_mov(ins, *a); return 1; }
            { int s = log2_exact(k);
              if (s > 0) { ins->op = MIR_SHR_L; ins->src2 = mir_imm_int(s, MIR_I64); return 1; } }
            break;
        case MIR_UMOD:
            { int s = log2_exact(k);
              if (s >= 0) { ins->op = MIR_AND; ins->src2 = mir_imm_int(k - 1, MIR_I64); return 1; } }
            break;
        default: break;
        }
        if (op == MIR_OR && k == -1) { make_mov(ins, mir_imm_int(-1, ins->dest.type)); return 1; }
    }

    /* ---- same-operand identities ---- */
    if (same_vreg(a, b)) {
        switch (op) {
        case MIR_SUB: case MIR_XOR: make_mov(ins, mir_imm_int(0, ins->dest.type)); return 1;
        case MIR_AND: case MIR_OR:  make_mov(ins, *a); return 1;
        case MIR_CMP_EQ: case MIR_CMP_LE: case MIR_CMP_GE:
        case MIR_UCMP_LE: case MIR_UCMP_GE:
            make_mov(ins, mir_imm_int(1, ins->dest.type)); return 1;
        case MIR_CMP_NE: case MIR_CMP_LT: case MIR_CMP_GT:
        case MIR_UCMP_LT: case MIR_UCMP_GT:
            make_mov(ins, mir_imm_int(0, ins->dest.type)); return 1;
        default: break;
        }
    }
    return 0;
}

/* ======================================================================
 *  mem2reg
 * ====================================================================== */

typedef struct {
    int loads, stores, escaped;
    int cls;          /* 0 none, 1 int, 2 float */
    MIRType type;
    int typed;        /* every access agreed on `type` */
    int vreg;
} SlotInfo;

static void slot_note(SlotInfo *s, MIRType t) {
    int c = is_f(t) ? 2 : 1;
    if (s->cls == 0) { s->cls = c; s->type = t; s->typed = 1; }
    else if (s->cls != c) s->escaped = 1;
    else if (s->type != t) { s->typed = 0; }
}

static int mem2reg(MIRFunc *fn) {
    int nl = fn->local_count;
    if (nl == 0) return 0;
    SlotInfo *sl = calloc(nl, sizeof(SlotInfo));

    for (int i = 0; i < fn->count; i++) {
        MIRInstr *ins = &fn->instrs[i];
        if (ins->op == MIR_LOAD && ins->src1.kind == MIRV_LOCAL) {
            int id = ins->src1.local_id;
            if (id < 0 || id >= nl) continue;
            sl[id].loads++;
            slot_note(&sl[id], ins->dest.type);
        } else if (ins->op == MIR_STORE && ins->dest.kind == MIRV_LOCAL) {
            int id = ins->dest.local_id;
            if (id < 0 || id >= nl) continue;
            sl[id].stores++;
            slot_note(&sl[id], ins->src1.type);
            /* a store of a float immediate to an int-typed slot etc. is a
               class mismatch and is caught by slot_note */
        } else {
            /* any other appearance of a local slot takes its address */
            MIRValue *vs[3] = { &ins->dest, &ins->src1, &ins->src2 };
            for (int k = 0; k < 3; k++)
                if (vs[k]->kind == MIRV_LOCAL && vs[k]->local_id >= 0 && vs[k]->local_id < nl)
                    sl[vs[k]->local_id].escaped = 1;
            for (int a = 0; a < ins->arg_count; a++)
                if (ins->args[a].kind == MIRV_LOCAL && ins->args[a].local_id >= 0 &&
                    ins->args[a].local_id < nl)
                    sl[ins->args[a].local_id].escaped = 1;
        }
        /* loads from a local through the LOAD path can't be indexed */
    }
    for (int i = 0; i < nl; i++)
        if (i < fn->local_pinned_cap && fn->local_pinned[i]) sl[i].escaped = 1;

    /* parameters: the incoming class has to agree with how the body uses it */
    for (int p = 0; p < fn->param_count && p < nl; p++) {
        int pf = fn->param_types && is_f(fn->param_types[p]);
        if (sl[p].cls && (sl[p].cls == 2) != pf) sl[p].escaped = 1;
        /* an unused param needs no slot handling */
    }

    int nv = max_vreg(fn);
    int promoted = 0;
    for (int i = 0; i < nl; i++) {
        if (sl[i].escaped || sl[i].cls == 0) continue;
        /* a float slot is only one vreg if every access agrees on its width */
        if (sl[i].cls == 2 && !sl[i].typed) continue;
        if (i < fn->param_count && sl[i].cls == 2 &&
            (!fn->param_types || fn->param_types[i] != sl[i].type)) continue;
        sl[i].vreg = nv++;
        promoted++;
    }
    if (!promoted) { free(sl); return 0; }

    /* entry sequence: fetch parameters, zero-init everything else so that no
       path reads an undefined vreg (dead inits are removed by DCE) */
    int extra = 0;
    for (int i = 0; i < nl; i++) if (sl[i].vreg) extra++;
    MIRInstr *entry = calloc(extra, sizeof(MIRInstr));
    int ne = 0;
    for (int i = 0; i < nl; i++) {
        if (!sl[i].vreg) continue;
        MIRType vt = sl[i].typed ? sl[i].type : (sl[i].cls == 2 ? MIR_F64 : MIR_I64);
        MIRInstr *e = &entry[ne++];
        e->dest = mir_vreg(sl[i].vreg, vt);
        e->src1 = mir_none(); e->src2 = mir_none();
        if (i < fn->param_count) {
            e->op = MIR_GETPARAM;
            e->extra_int = i;
            if (fn->param_types) e->dest.type = sl[i].cls == 2 ? fn->param_types[i] : vt;
        } else {
            e->op = MIR_MOV;
            e->src1 = sl[i].cls == 2 ? mir_imm_float(0.0, vt) : mir_imm_int(0, vt);
        }
        /* remember the slot's vreg type for the rewrite below */
        sl[i].type = e->dest.type;
    }

    for (int i = 0; i < fn->count; i++) {
        MIRInstr *ins = &fn->instrs[i];
        if (ins->op == MIR_LOAD && ins->src1.kind == MIRV_LOCAL && ins->src1.local_id < nl &&
            sl[ins->src1.local_id].vreg) {
            SlotInfo *s = &sl[ins->src1.local_id];
            MIRValue d = ins->dest;
            ins->op = MIR_MOV;
            ins->src1 = mir_vreg(s->vreg, s->type);
            ins->src2 = mir_none();
            ins->dest = d;
            ins->mem_offset = 0; ins->mem_scale = 0; ins->extra_int = 0;
        } else if (ins->op == MIR_STORE && ins->dest.kind == MIRV_LOCAL && ins->dest.local_id < nl &&
                   sl[ins->dest.local_id].vreg) {
            SlotInfo *s = &sl[ins->dest.local_id];
            MIRValue v = ins->src1;
            ins->op = MIR_MOV;
            ins->dest = mir_vreg(s->vreg, s->type);
            ins->src1 = v;
            ins->src2 = mir_none();
            ins->mem_offset = 0; ins->mem_scale = 0; ins->extra_int = 0;
        }
    }

    /* splice the entry sequence in front */
    fn->instrs = realloc(fn->instrs, (fn->count + ne + 1) * sizeof(MIRInstr));
    if (fn->cap < fn->count + ne) fn->cap = fn->count + ne;
    memmove(fn->instrs + ne, fn->instrs, fn->count * sizeof(MIRInstr));
    memcpy(fn->instrs, entry, ne * sizeof(MIRInstr));
    fn->count += ne;
    if (fn->cap < fn->count) fn->cap = fn->count;
    if (fn->vreg_count < nv) fn->vreg_count = nv;
    free(entry);
    free(sl);
    return 1;
}

/* ======================================================================
 *  Propagation
 * ====================================================================== */

/* result of asking "what is this vreg known to be?" */
enum { K_NONE = 0, K_CONST = 1, K_COPY = 2 };

typedef struct {
    int nv;
    unsigned char *kind;
    MIRValue *val;
    int *stamp; int gen;
} Known;

static void known_init(Known *k, int nv) {
    k->nv = nv;
    k->kind = calloc(nv, 1);
    k->val = calloc(nv, sizeof(MIRValue));
    k->stamp = calloc(nv, sizeof(int));
    k->gen = 1;
}
static void known_free(Known *k) { free(k->kind); free(k->val); free(k->stamp); }

typedef struct {
    Known *k;
    int changed;
    int block_local;   /* copies need invalidation tracking */
} PropCtx;

static void prop_use(MIRValue *u, void *ud) {
    PropCtx *pc = ud;
    Known *k = pc->k;
    for (int depth = 0; depth < 8; depth++) {
        int v = u->vreg;
        if (v >= k->nv) return;
        if (k->stamp[v] != k->gen || k->kind[v] == K_NONE) return;
        MIRValue m = k->val[v];
        if (k->kind[v] == K_CONST) {
            if (m.kind == MIRV_IMM_INT && !is_f(u->type)) {
                MIRType t = u->type; *u = m; u->type = t; pc->changed = 1; return;
            }
            if (m.kind == MIRV_IMM_FLOAT && is_f(u->type) && m.type == u->type) {
                *u = m; pc->changed = 1; return;
            }
            return;
        }
        /* copy */
        if (is_f(m.type) != is_f(u->type)) return;
        MIRType t = u->type;
        *u = m; u->type = t;
        pc->changed = 1;
    }
}

/* Global propagation for vregs assigned exactly once: no dataflow needed. */
static int global_prop(MIRFunc *fn) {
    int nv = max_vreg(fn);
    int *defs = calloc(nv, sizeof(int));
    int *defat = calloc(nv, sizeof(int));
    MIRInstr **defi = calloc(nv, sizeof(MIRInstr *));
    Cfg cfg = build_cfg(fn);
    for (int i = 0; i < fn->count; i++) {
        MIRValue *d = def_of(&fn->instrs[i]);
        if (d) { defs[d->vreg]++; defi[d->vreg] = &fn->instrs[i]; defat[d->vreg] = i; }
    }
    Known k; known_init(&k, nv);
    int any = 0;
    for (int v = 0; v < nv; v++) {
        if (defs[v] != 1) continue;
        MIRInstr *d = defi[v];
        if (d->op != MIR_MOV || d->extra_int == 1) continue;
        MIRValue s = d->src1;
        if (s.kind == MIRV_IMM_INT && !is_f(d->dest.type)) {
            k.stamp[v] = k.gen; k.kind[v] = K_CONST; k.val[v] = s; any = 1;
        } else if (s.kind == MIRV_IMM_FLOAT && is_f(d->dest.type) && s.type == d->dest.type) {
            k.stamp[v] = k.gen; k.kind[v] = K_CONST; k.val[v] = s; any = 1;
        } else if (s.kind == MIRV_VREG && s.vreg != v && defs[s.vreg] == 1 &&
                   is_f(s.type) == is_f(d->dest.type) &&
                   /* the source must not be able to change while the copy is
                      live: either it is fixed for the whole call (a parameter)
                      or both sit in one block, source first, so redefining
                      the source always redefines the copy right after */
                   (defi[s.vreg]->op == MIR_GETPARAM ||
                    (cfg.ib[defat[s.vreg]] == cfg.ib[defat[v]] && defat[s.vreg] < defat[v]))) {
            k.stamp[v] = k.gen; k.kind[v] = K_COPY; k.val[v] = s; any = 1;
        }
    }
    int changed = 0;
    if (any) {
        PropCtx pc = { &k, 0, 0 };
        for (int i = 0; i < fn->count; i++) {
            MIRInstr *ins = &fn->instrs[i];
            /* never rewrite the defining MOV itself into a self-copy loop */
            for_each_use(ins, prop_use, &pc);
        }
        changed = pc.changed;
    }
    known_free(&k);
    cfg_free(&cfg);
    free(defs); free(defi); free(defat);
    return changed;
}


/* ======================================================================
 *  Copy / constant propagation across blocks (available copies dataflow)
 * ====================================================================== */

typedef struct { int dst; MIRValue src; } CopyRec;


/* replace a use by what an available copy says it equals */
static int subst_avail(MIRValue *u, const int *cur, const CopyRec *recs) {
    int did = 0;
    for (int depth = 0; depth < 6; depth++) {
        int c = cur[u->vreg];
        if (c < 0) break;
        MIRValue m = recs[c].src;
        MIRType t = u->type;
        if (m.kind == MIRV_VREG) {
            if (is_f(m.type) != is_f(t)) break;
            *u = m; u->type = t; did = 1;
            continue;
        }
        if (m.kind == MIRV_IMM_INT && !is_f(t)) { *u = m; u->type = t; did = 1; }
        else if (m.kind == MIRV_IMM_FLOAT && is_f(t) && m.type == t) { *u = m; did = 1; }
        break;
    }
    return did;
}

/* a copy is usable at a point if, on every path there, `dst = src` executed
   and neither side was reassigned since */
static int copy_prop_df(MIRFunc *fn) {
    int nv = max_vreg(fn);
    int n = fn->count;
    int *cid = malloc((n + 1) * sizeof(int));
    int nc = 0;
    CopyRec *recs = malloc((n + 1) * sizeof(CopyRec));
    for (int i = 0; i < n; i++) {
        MIRInstr *ins = &fn->instrs[i];
        cid[i] = -1;
        if (ins->op != MIR_MOV || ins->dest.kind != MIRV_VREG || ins->extra_int == 1) continue;
        MIRValue sv = ins->src1;
        int ok = 0;
        if (sv.kind == MIRV_IMM_INT && !is_f(ins->dest.type)) ok = 1;
        else if (sv.kind == MIRV_IMM_FLOAT && is_f(ins->dest.type) && sv.type == ins->dest.type) ok = 1;
        else if (sv.kind == MIRV_VREG && sv.vreg != ins->dest.vreg &&
                 is_f(sv.type) == is_f(ins->dest.type)) ok = 1;
        if (!ok) continue;
        cid[i] = nc;
        recs[nc].dst = ins->dest.vreg;
        recs[nc].src = sv;
        nc++;
    }
    if (nc == 0) { free(cid); free(recs); return 0; }

    /* per-vreg list of copies that mention it (as dst or src) */
    int *cnt = calloc(nv + 1, sizeof(int));
    for (int c = 0; c < nc; c++) {
        cnt[recs[c].dst]++;
        if (recs[c].src.kind == MIRV_VREG) cnt[recs[c].src.vreg]++;
    }
    int *start = malloc((nv + 2) * sizeof(int));
    start[0] = 0;
    for (int v = 0; v < nv; v++) start[v + 1] = start[v] + cnt[v];
    int *list = malloc((start[nv] + 1) * sizeof(int));
    int *fill = calloc(nv + 1, sizeof(int));
    for (int c = 0; c < nc; c++) {
        list[start[recs[c].dst] + fill[recs[c].dst]++] = c;
        if (recs[c].src.kind == MIRV_VREG) list[start[recs[c].src.vreg] + fill[recs[c].src.vreg]++] = c;
    }
    free(cnt); free(fill);

    Cfg cfg = build_cfg(fn);
    int W = BS_WORDS(nc);
    size_t sz = (size_t)cfg.n * W;
    if (sz > BITSET_BUDGET) {          /* a huge function: the dataflow tables would be enormous */
        cfg_free(&cfg); free(cid); free(recs); free(start); free(list);
        return 0;
    }
    Word *gen = calloc(sz, sizeof(Word)), *kill = calloc(sz, sizeof(Word));
    Word *in = malloc(sz * sizeof(Word)), *out = malloc(sz * sizeof(Word));

    for (int b = 0; b < cfg.n; b++) {
        Word *g = gen + (size_t)b * W, *k = kill + (size_t)b * W;
        for (int i = cfg.b[b].start; i < cfg.b[b].end; i++) {
            MIRValue *d = def_of(&fn->instrs[i]);
            if (d) {
                for (int q = start[d->vreg]; q < start[d->vreg + 1]; q++) {
                    int c = list[q];
                    bs_clr(g, c); bs_set(k, c);
                }
            }
            if (cid[i] >= 0) bs_set(g, cid[i]);
        }
    }
    /* predecessors */
    int *pstart = calloc(cfg.n + 2, sizeof(int));
    for (int b = 0; b < cfg.n; b++) for (int q = 0; q < cfg.b[b].nsucc; q++) pstart[cfg.b[b].succ[q] + 1]++;
    for (int b = 0; b < cfg.n; b++) pstart[b + 1] += pstart[b];
    int *plist = malloc((pstart[cfg.n] + 1) * sizeof(int));
    int *pfill = calloc(cfg.n + 1, sizeof(int));
    for (int b = 0; b < cfg.n; b++)
        for (int q = 0; q < cfg.b[b].nsucc; q++) {
            int t = cfg.b[b].succ[q];
            plist[pstart[t] + pfill[t]++] = b;
        }
    free(pfill);
    /* optimistic start: everything available, except at the entry */
    memset(in, 0xff, sz * sizeof(Word));
    memset(out, 0xff, sz * sizeof(Word));
    int changed = 1, iters = 0;
    while (changed && iters++ < 100000) {
        changed = 0;
        for (int b = 0; b < cfg.n; b++) {
            Word *ib = in + (size_t)b * W;
            if (b == 0 || pstart[b + 1] == pstart[b]) {
                memset(ib, 0, W * sizeof(Word));
            } else {
                for (int w = 0; w < W; w++) ib[w] = ~(Word)0;
                for (int e = pstart[b]; e < pstart[b + 1]; e++) {
                    Word *op = out + (size_t)plist[e] * W;
                    for (int w = 0; w < W; w++) ib[w] &= op[w];
                }
            }
            Word *ob = out + (size_t)b * W;
            Word *g = gen + (size_t)b * W, *k = kill + (size_t)b * W;
            for (int w = 0; w < W; w++) {
                Word nw = g[w] | (ib[w] & ~k[w]);
                if (nw != ob[w]) { ob[w] = nw; changed = 1; }
            }
        }
    }
    free(pstart); free(plist);
    if (changed) {
        /* never converged: the optimistic state is unsound, so do nothing */
        cfg_free(&cfg);
        free(gen); free(kill); free(in); free(out);
        free(cid); free(recs); free(start); free(list);
        return 0;
    }

    /* rewrite */
    int *cur = malloc((nv + 1) * sizeof(int));
    int did = 0;
    for (int b = 0; b < cfg.n; b++) {
        for (int v = 0; v < nv; v++) cur[v] = -1;
        Word *ib = in + (size_t)b * W;
        for (int c = 0; c < nc; c++) if (bs_get(ib, c)) cur[recs[c].dst] = c;
        for (int i = cfg.b[b].start; i < cfg.b[b].end; i++) {
            MIRInstr *ins = &fn->instrs[i];
            /* propagate into operands */
            Shape sh = shape_of(ins);
            MIRValue *ops[8 + 64]; int nops = 0;
            if (sh.use_src1 && ins->src1.kind == MIRV_VREG) ops[nops++] = &ins->src1;
            if (sh.use_src2 && ins->src2.kind == MIRV_VREG) ops[nops++] = &ins->src2;
            if (sh.dest_is_use && ins->dest.kind == MIRV_VREG) ops[nops++] = &ins->dest;
            for (int k2 = 0; k2 < nops; k2++) did |= subst_avail(ops[k2], cur, recs);
            for (int a = 0; a < ins->arg_count; a++)
                if (ins->args[a].kind == MIRV_VREG) did |= subst_avail(&ins->args[a], cur, recs);
            /* transfer */
            MIRValue *d = def_of(ins);
            if (d) {
                int dv = d->vreg;
                for (int q = start[dv]; q < start[dv + 1]; q++) {
                    int c = list[q];
                    if (recs[c].dst == dv) { if (cur[dv] == c) cur[dv] = -1; }
                    else if (cur[recs[c].dst] == c) cur[recs[c].dst] = -1;
                }
                cur[dv] = -1;
            }
            if (cid[i] >= 0) cur[recs[cid[i]].dst] = cid[i];
        }
    }
    free(cur);
    cfg_free(&cfg);
    free(gen); free(kill); free(in); free(out);
    free(cid); free(recs); free(start); free(list);
    return did;
}

/* ======================================================================
 *  Local (per basic block) optimisation: propagation, folding, CSE
 * ====================================================================== */

static int bitlen64(uint64_t v) { int n = 0; while (v) { n++; v >>= 1; } return n; }

typedef struct {
    MIROp op;
    MIRValue a, b;
    MIRType dtype;
    int off, scale;
    int dest;       /* vreg holding the value */
    int is_load;
    int cls;        /* alias class of a memory read */
} Avail;

#define AVAIL_MAX 96

static int val_eq(const MIRValue *x, const MIRValue *y) {
    if (x->kind != y->kind) return 0;
    switch (x->kind) {
    case MIRV_VREG: return x->vreg == y->vreg && x->type == y->type;
    case MIRV_IMM_INT: return x->imm_int == y->imm_int && x->type == y->type;
    case MIRV_IMM_FLOAT: return memcmp(&x->imm_float, &y->imm_float, sizeof(double)) == 0 && x->type == y->type;
    case MIRV_GLOBAL: return strcmp(x->global_name, y->global_name) == 0;
    case MIRV_LOCAL: return x->local_id == y->local_id;
    default: return 0;
    }
}

static int cse_eligible(const MIRInstr *ins) {
    switch (ins->op) {
    case MIR_ADD: case MIR_SUB: case MIR_MUL: case MIR_SDIV: case MIR_UDIV:
    case MIR_SMOD: case MIR_UMOD: case MIR_NEG: case MIR_NOT: case MIR_AND:
    case MIR_OR: case MIR_XOR: case MIR_SHL: case MIR_SHR_A: case MIR_SHR_L:
    case MIR_FADD: case MIR_FSUB: case MIR_FMUL: case MIR_FDIV: case MIR_FNEG:
    case MIR_CMP_EQ: case MIR_CMP_NE: case MIR_CMP_LT: case MIR_CMP_LE:
    case MIR_CMP_GT: case MIR_CMP_GE: case MIR_UCMP_LT: case MIR_UCMP_LE:
    case MIR_UCMP_GT: case MIR_UCMP_GE:
    case MIR_FCMP_LT: case MIR_FCMP_LE: case MIR_FCMP_GT: case MIR_FCMP_GE:
    case MIR_FCMP_EQ: case MIR_FCMP_NE:
    case MIR_SEXT: case MIR_ZEXT: case MIR_TRUNC: case MIR_I2F: case MIR_F2I:
    case MIR_F2F: case MIR_BITCAST: case MIR_LEA_GLOBAL: case MIR_LEA_LOCAL:
    case MIR_ARR_LEN: case MIR_ARR_DATA: case MIR_SMULH: case MIR_UMULH:
        return 1;
    case MIR_LOAD:
        return ins->extra_int == 0;
    default:
        return 0;
    }
}

static int writes_memory(const MIRInstr *ins) {
    switch (ins->op) {
    case MIR_STORE: case MIR_CALL: case MIR_SYSCALL: case MIR_MEMSET: case MIR_MEMCPY:
    case MIR_ASM_TEXT: case MIR_ASM_RAW: case MIR_OUTB: case MIR_OUTW:
    case MIR_WRMSR: case MIR_WRITE_CR: case MIR_LGDT: case MIR_LIDT:
    /* an interrupt can change memory across these: they are barriers, like asm("":::"memory") */
    case MIR_HLT: case MIR_STI: case MIR_CLI: case MIR_INB: case MIR_INW: case MIR_INVLPG:
    case MIR_LTR: case MIR_RESTORE_REGS: case MIR_SAVE_REGS:
        return 1;
    default:
        return 0;
    }
}

static int local_pass(MIRFunc *fn, int do_cse) {
    int nv = max_vreg(fn);
    Known k; known_init(&k, nv);
    /* per-block facts about a vreg's upper bits: it is known to be the zero-
       (z) or sign-extension (sg) of its low N bits, or 0 if unknown */
    unsigned char *zb = calloc(nv + 1, 1), *sb = calloc(nv + 1, 1);
    int *fstamp = calloc(nv + 1, sizeof(int));
    Avail av[AVAIL_MAX]; int nav = 0;
    int *copies = malloc(nv * sizeof(int)); int ncop = 0;
    int changed = 0;

    Cfg cfg = build_cfg(fn);
    for (int b = 0; b < cfg.n; b++) {
        k.gen++;
        nav = 0; ncop = 0;
        for (int i = cfg.b[b].start; i < cfg.b[b].end; i++) {
            MIRInstr *ins = &fn->instrs[i];
            if (ins->op == MIR_NOP || ins->op == MIR_LABEL) continue;

            /* 1. propagate into operands */
            PropCtx pc = { &k, 0, 1 };
            for_each_use(ins, prop_use, &pc);
            if (pc.changed) changed = 1;

            /* 1b. an extension of something already extended that far is a copy */
            if ((ins->op == MIR_ZEXT || ins->op == MIR_SEXT || ins->op == MIR_TRUNC) &&
                ins->src1.kind == MIRV_VREG && ins->dest.kind == MIRV_VREG &&
                !is_f(ins->src1.type) && !is_f(ins->dest.type)) {
                int v = ins->src1.vreg;
                int z = fstamp[v] == k.gen ? zb[v] : 0, sg = fstamp[v] == k.gen ? sb[v] : 0;
                int bits, sgn;
                if (ins->op == MIR_ZEXT)      { bits = 8 * mir_type_size(ins->src1.type); sgn = 0; }
                else if (ins->op == MIR_SEXT) { bits = 8 * mir_type_size(ins->src1.type); sgn = 1; }
                else { bits = 8 * mir_type_size(ins->dest.type); sgn = !mir_type_is_unsigned(ins->dest.type); }
                if (bits < 64) {
                    int redundant = sgn ? ((sg && sg <= bits) || (z && z < bits))
                                        : (z && z <= bits);
                    if (redundant) { make_mov(ins, ins->src1); changed = 1; }
                }
            }

            /* 2. fold / simplify (repeat: one rewrite can enable another) */
            for (int r = 0; r < 4; r++) {
                if (!simplify(fn, ins)) break;
                changed = 1;
                PropCtx pc2 = { &k, 0, 1 };
                for_each_use(ins, prop_use, &pc2);
            }

            /* branch on a constant */
            if ((ins->op == MIR_JMP_IF || ins->op == MIR_JMP_UNLESS) && is_imm(&ins->src1)) {
                int taken = (ins->op == MIR_JMP_IF) ? ins->src1.imm_int != 0 : ins->src1.imm_int == 0;
                if (taken) { ins->op = MIR_JMP; ins->src1 = mir_none(); }
                else make_nop(ins);
                changed = 1;
                continue;
            }
            /* branch on a comparison result that was inverted: !(a<b) style */
            if (ins->op == MIR_JMP_IF || ins->op == MIR_JMP_UNLESS) {
                /* handled by the backend's compare/branch fusion */
            }

            /* 3. CSE */
            if (do_cse && cse_eligible(ins) && ins->dest.kind == MIRV_VREG) {
                MIRInstr tmp = *ins;
                if (is_commutative(tmp.op) && tmp.src1.kind == MIRV_VREG && tmp.src2.kind == MIRV_VREG &&
                    tmp.src2.vreg < tmp.src1.vreg) {
                    MIRValue t = tmp.src1; tmp.src1 = tmp.src2; tmp.src2 = t;
                }
                int is_load = ins->op == MIR_LOAD || ins->op == MIR_ARR_LEN || ins->op == MIR_ARR_DATA;
                int found = -1;
                for (int j = 0; j < nav; j++) {
                    if (av[j].op != tmp.op || av[j].is_load != is_load) continue;
                    if (av[j].dtype != tmp.dest.type) continue;
                    if (av[j].off != tmp.mem_offset || av[j].scale != tmp.mem_scale) continue;
                    if (!val_eq(&av[j].a, &tmp.src1) || !val_eq(&av[j].b, &tmp.src2)) continue;
                    found = j; break;
                }
                if (found >= 0 && is_f(av[found].dtype) == is_f(ins->dest.type)) {
                    MIRType dt = ins->dest.type;
                    MIRValue h = mir_vreg(av[found].dest, dt);
                    make_mov(ins, h);
                    changed = 1;
                    PropCtx pc3 = { &k, 0, 1 };
                    (void)pc3;
                }
            }

            /* 4. record the definition / invalidate */
            if (writes_memory(ins)) {
                int w = 0;
                for (int j = 0; j < nav; j++) {
                    int kill = av[j].is_load;
                    /* a plain store only clobbers reads of a conflicting class */
                    if (kill && ins->op == MIR_STORE && ins->mem_cls != MIR_MEM_ANY &&
                        av[j].cls != MIR_MEM_ANY && ins->mem_cls != av[j].cls)
                        kill = 0;
                    if (!kill) av[w++] = av[j];
                }
                nav = w;
            }
            MIRValue *d = def_of(ins);
            if (d) {
                int dv = d->vreg;
                {   /* extension facts about the new value */
                    int z = 0, sg = 0;
                    switch (ins->op) {
                    case MIR_LOAD:
                        if (!is_f(ins->dest.type) && mir_type_size(ins->dest.type) < 8) {
                            if (mir_type_is_unsigned(ins->dest.type)) z = 8 * mir_type_size(ins->dest.type);
                            else sg = 8 * mir_type_size(ins->dest.type);
                        }
                        break;
                    case MIR_ZEXT: z = 8 * mir_type_size(ins->src1.type); break;
                    case MIR_SEXT: sg = 8 * mir_type_size(ins->src1.type); break;
                    case MIR_TRUNC:
                        if (mir_type_size(ins->dest.type) < 8) {
                            if (mir_type_is_unsigned(ins->dest.type)) z = 8 * mir_type_size(ins->dest.type);
                            else sg = 8 * mir_type_size(ins->dest.type);
                        }
                        break;
                    case MIR_AND:
                        if (ins->src2.kind == MIRV_IMM_INT && ins->src2.imm_int >= 0) z = bitlen64((uint64_t)ins->src2.imm_int);
                        break;
                    case MIR_CMP_EQ: case MIR_CMP_NE: case MIR_CMP_LT: case MIR_CMP_LE: case MIR_CMP_GT: case MIR_CMP_GE:
                    case MIR_UCMP_LT: case MIR_UCMP_LE: case MIR_UCMP_GT: case MIR_UCMP_GE:
                    case MIR_FCMP_LT: case MIR_FCMP_LE: case MIR_FCMP_GT: case MIR_FCMP_GE: case MIR_FCMP_EQ: case MIR_FCMP_NE:
                        z = 1; break;
                    case MIR_MOV:
                        if (ins->src1.kind == MIRV_VREG && fstamp[ins->src1.vreg] == k.gen) {
                            z = zb[ins->src1.vreg]; sg = sb[ins->src1.vreg];
                        } else if (ins->src1.kind == MIRV_IMM_INT && ins->src1.imm_int >= 0) {
                            z = bitlen64((uint64_t)ins->src1.imm_int); if (z == 0) z = 1;
                        }
                        break;
                    default: break;
                    }
                    if (z >= 64) z = 0;
                    if (sg >= 64) sg = 0;
                    fstamp[dv] = k.gen; zb[dv] = (unsigned char)z; sb[dv] = (unsigned char)sg;
                }
                /* kill copies of dv */
                int w = 0;
                for (int j = 0; j < ncop; j++) {
                    int c = copies[j];
                    if (k.stamp[c] == k.gen && k.kind[c] == K_COPY && k.val[c].vreg == dv) {
                        k.kind[c] = K_NONE;
                        continue;
                    }
                    copies[w++] = c;
                }
                ncop = w;
                k.kind[dv] = K_NONE; k.stamp[dv] = k.gen;
                /* kill available expressions using or holding dv */
                w = 0;
                for (int j = 0; j < nav; j++) {
                    if (av[j].dest == dv ||
                        (av[j].a.kind == MIRV_VREG && av[j].a.vreg == dv) ||
                        (av[j].b.kind == MIRV_VREG && av[j].b.vreg == dv)) continue;
                    av[w++] = av[j];
                }
                nav = w;

                if (ins->op == MIR_MOV && ins->extra_int != 1) {
                    MIRValue s = ins->src1;
                    if (s.kind == MIRV_IMM_INT && !is_f(ins->dest.type)) {
                        k.kind[dv] = K_CONST; k.val[dv] = s;
                    } else if (s.kind == MIRV_IMM_FLOAT && is_f(ins->dest.type) && s.type == ins->dest.type) {
                        k.kind[dv] = K_CONST; k.val[dv] = s;
                    } else if (s.kind == MIRV_VREG && s.vreg != dv && is_f(s.type) == is_f(ins->dest.type)) {
                        k.kind[dv] = K_COPY; k.val[dv] = s;
                        copies[ncop++] = dv;
                    }
                } else if (do_cse && cse_eligible(ins) && nav < AVAIL_MAX) {
                    MIRInstr *t = ins;
                    int self = (t->src1.kind == MIRV_VREG && t->src1.vreg == dv) ||
                               (t->src2.kind == MIRV_VREG && t->src2.vreg == dv);
                    if (!self) {
                        Avail e;
                        e.op = t->op; e.a = t->src1; e.b = t->src2; e.dtype = t->dest.type;
                        e.off = t->mem_offset; e.scale = t->mem_scale; e.dest = dv;
                        e.is_load = t->op == MIR_LOAD || t->op == MIR_ARR_LEN || t->op == MIR_ARR_DATA;
                        e.cls = (t->op == MIR_ARR_LEN || t->op == MIR_ARR_DATA) ? MIR_MEM_ARRAY_HDR : t->mem_cls;
                        if (is_commutative(e.op) && e.a.kind == MIRV_VREG && e.b.kind == MIRV_VREG &&
                            e.b.vreg < e.a.vreg) { MIRValue x = e.a; e.a = e.b; e.b = x; }
                        av[nav++] = e;
                    }
                }
            }
        }
    }
    cfg_free(&cfg);
    free(copies);
    free(zb); free(sb); free(fstamp);
    known_free(&k);
    return changed;
}


/* ======================================================================
 *  Addressing-mode folding:  add/shift feeding a load or store
 * ====================================================================== */

/* [t] with t = a + b        ->  [a + b]
   [t] with t = a + imm      ->  [a + imm]
   [b + i*s] with i = j << k ->  [b + j*(s<<k)]
   [b + i*s] with i = j + c  ->  [b + j*s + c*s]
   only when the feeding instruction is in the same block, nothing it reads has
   changed since, and its result has no other use (so it dies). */
static int fold_addr(MIRFunc *fn) {
    int nv = max_vreg(fn);
    int *uses = calloc(nv + 1, sizeof(int));
    CntCtx cc = { uses };
    for (int i = 0; i < fn->count; i++) for_each_use(&fn->instrs[i], cnt_use, &cc);
    int *lastdef = malloc((nv + 1) * sizeof(int));
    int *stamp = calloc(nv + 1, sizeof(int));
    int gen = 0, changed = 0;
    Cfg cfg = build_cfg(fn);
    for (int b = 0; b < cfg.n; b++) {
        gen++;
        for (int i = cfg.b[b].start; i < cfg.b[b].end; i++) {
            MIRInstr *ins = &fn->instrs[i];
            if (ins->op == MIR_NOP) continue;
            MIRValue *base = NULL;
            if (ins->op == MIR_LOAD && ins->src1.kind == MIRV_VREG && ins->extra_int == 0) base = &ins->src1;
            else if (ins->op == MIR_STORE && ins->dest.kind == MIRV_VREG && ins->extra_int == 0) base = &ins->dest;
            for (int round = 0; base && round < 3; round++) {
                int did = 0;
                int t = base->vreg;
                /* a definition of vreg v earlier in this block that is still valid at i */
#define DEF_OF(v) ((stamp[v] == gen) ? lastdef[v] : -1)
#define UNCHANGED(v, since) (!(stamp[v] == gen && lastdef[v] > (since)))
                int d = DEF_OF(t);
                if (d >= 0 && uses[t] == 1 && fn->instrs[d].op == MIR_ADD && fn->instrs[d].dest.kind == MIRV_VREG &&
                    fn->instrs[d].dest.vreg == t && !is_f(fn->instrs[d].dest.type)) {
                    MIRValue x = fn->instrs[d].src1, y = fn->instrs[d].src2;
                    if (x.kind == MIRV_VREG && UNCHANGED(x.vreg, d)) {
                        if (y.kind == MIRV_IMM_INT && fits_i32(ins->mem_offset + y.imm_int) &&
                            ins->mem_offset + y.imm_int > -(1 << 30) && ins->mem_offset + y.imm_int < (1 << 30)) {
                            ins->mem_offset += (int)y.imm_int;
                            MIRType ty = base->type; *base = x; base->type = ty;
                            uses[t]--; uses[x.vreg]++;
                            did = 1;
                        } else if (y.kind == MIRV_VREG && ins->mem_scale == 0 && UNCHANGED(y.vreg, d)) {
                            MIRType ty = base->type; *base = x; base->type = ty;
                            ins->src2 = y; ins->src2.type = MIR_I64;
                            ins->mem_scale = 1;
                            uses[t]--; uses[x.vreg]++; uses[y.vreg]++;
                            did = 1;
                        }
                    }
                }
                if (!did && ins->mem_scale != 0 && ins->src2.kind == MIRV_VREG) {
                    int u = ins->src2.vreg;
                    int e = DEF_OF(u);
                    if (e >= 0 && uses[u] == 1) {
                        MIRInstr *ei = &fn->instrs[e];
                        if (ei->dest.kind == MIRV_VREG && ei->dest.vreg == u && ei->src1.kind == MIRV_VREG &&
                            UNCHANGED(ei->src1.vreg, e) && !is_f(ei->dest.type)) {
                            if (ei->op == MIR_SHL && ei->src2.kind == MIRV_IMM_INT && ei->src2.imm_int >= 1 &&
                                ei->src2.imm_int <= 3 && (ins->mem_scale << ei->src2.imm_int) <= 8) {
                                ins->mem_scale <<= (int)ei->src2.imm_int;
                                uses[u]--; uses[ei->src1.vreg]++;
                                ins->src2 = ei->src1; ins->src2.type = MIR_I64;
                                did = 1;
                            } else if (ei->op == MIR_ADD && ei->src2.kind == MIRV_IMM_INT) {
                                int64_t no = ins->mem_offset + ei->src2.imm_int * ins->mem_scale;
                                if (no > -(1 << 30) && no < (1 << 30)) {
                                    ins->mem_offset = (int)no;
                                    uses[u]--; uses[ei->src1.vreg]++;
                                    ins->src2 = ei->src1; ins->src2.type = MIR_I64;
                                    did = 1;
                                }
                            }
                        }
                    }
                }
#undef DEF_OF
#undef UNCHANGED
                if (!did) break;
                changed = 1;
            }
            MIRValue *dv = def_of(ins);
            if (dv) { lastdef[dv->vreg] = i; stamp[dv->vreg] = gen; }
        }
    }
    cfg_free(&cfg);
    free(uses); free(lastdef); free(stamp);
    return changed;
}


/* ======================================================================
 *  Global common-subexpression elimination with branch facts
 * ====================================================================== */

/* Available-expressions dataflow. An "element" is a pure computation (or a
   memory read) at some instruction; it is available at a point if every path
   there executed it and neither its operands nor its result register changed
   since (and, for reads, no conflicting store/call). A second kind of element -
   a "fact" - records that a branch condition vreg is known nonzero / zero on the
   edge that was taken. Together they let a second identical compare (an array
   bounds check repeated for the same index) fold to a constant and its branch
   disappear. */

typedef struct { int e_count, f_count; } AvailDims;

static int expr_same(const MIRInstr *x, const MIRInstr *y) {
    if (x->op != y->op || x->dest.type != y->dest.type) return 0;
    if (x->mem_offset != y->mem_offset || x->mem_scale != y->mem_scale) return 0;
    if ((x->op == MIR_LOAD) && x->mem_cls != y->mem_cls) return 0;
    if (!val_eq(&x->src1, &y->src1) || !val_eq(&x->src2, &y->src2)) return 0;
    /* operand types matter for extensions */
    if (x->op == MIR_ZEXT || x->op == MIR_SEXT || x->op == MIR_TRUNC || x->op == MIR_BITCAST)
        if (x->src1.type != y->src1.type) return 0;
    return 1;
}

static unsigned expr_hash(const MIRInstr *x) {
    unsigned h = (unsigned)x->op * 2654435761u;
    const MIRValue *vs[2] = { &x->src1, &x->src2 };
    for (int k = 0; k < 2; k++) {
        h = h * 31 + (unsigned)vs[k]->kind;
        if (vs[k]->kind == MIRV_VREG) h = h * 31 + (unsigned)vs[k]->vreg;
        else if (vs[k]->kind == MIRV_IMM_INT) h = h * 31 + (unsigned)vs[k]->imm_int;
    }
    return h;
}

/* put commutative operands in a canonical order so a+b and b+a match */
static void canon_expr(MIRInstr *x) {
    if (is_commutative(x->op) && x->src1.kind == MIRV_VREG && x->src2.kind == MIRV_VREG &&
        x->src2.vreg < x->src1.vreg) {
        MIRValue t = x->src1; x->src1 = x->src2; x->src2 = t;
    }
}

static int gcse(MIRFunc *fn) {
    int n = fn->count, nv = max_vreg(fn);
    /* non-negative induction variables: every definition is a constant >= 0 or
       `v = v + c` with a small positive c (so within a loop that tests v < N with
       N <= the array length, v cannot wrap) */
    unsigned char *nn = malloc(nv + 1), *anydef = calloc(nv + 1, 1);
    memset(nn, 1, nv + 1);
    for (int i = 0; i < n; i++) {
        MIRInstr *x = &fn->instrs[i];
        MIRValue *d = def_of(x);
        if (!d) continue;
        int v = d->vreg; anydef[v] = 1;
        int ok = 0;
        if (x->op == MIR_MOV && x->src1.kind == MIRV_IMM_INT && x->src1.imm_int >= 0) ok = 1;
        else if (x->op == MIR_ADD && x->src1.kind == MIRV_VREG && x->src1.vreg == v &&
                 x->src2.kind == MIRV_IMM_INT && x->src2.imm_int >= 1 && x->src2.imm_int <= (1 << 20)) ok = 1;
        if (!ok) nn[v] = 0;
    }
    for (int v = 0; v <= nv; v++) if (!anydef[v]) nn[v] = 0;
    free(anydef);
    /* elements */
    int *elem = malloc((n + 1) * sizeof(int));
    int ne = 0;
    /* an element is an expression together with the register that holds it: two
       instructions computing the same expression into the same vreg (the loop test
       of a rotated loop, on the entry and the back-edge path) are one element */
    int HS0 = 1; while (HS0 < n * 2 + 2) HS0 <<= 1;
    int *h0 = malloc(HS0 * sizeof(int)), *nx0 = malloc((n + 1) * sizeof(int));
    for (int h = 0; h < HS0; h++) h0[h] = -1;
    int *einstr = malloc((n + 1) * sizeof(int));
    for (int i = 0; i < n; i++) {
        MIRInstr *ins = &fn->instrs[i];
        elem[i] = -1;
        if (!cse_eligible(ins) || ins->dest.kind != MIRV_VREG || is_f(ins->dest.type)) continue;
        if ((ins->src1.kind == MIRV_VREG && ins->src1.vreg == ins->dest.vreg) ||
            (ins->src2.kind == MIRV_VREG && ins->src2.vreg == ins->dest.vreg)) continue;
        if (ins->op == MIR_LEA_LOCAL) continue;
        MIRInstr key = *ins; canon_expr(&key);
        unsigned h = expr_hash(&key) & (HS0 - 1);
        int found = -1;
        for (int e = h0[h]; e >= 0; e = nx0[e]) {
            MIRInstr k2 = fn->instrs[einstr[e]]; canon_expr(&k2);
            if (k2.dest.vreg == key.dest.vreg && expr_same(&k2, &key)) { found = e; break; }
        }
        if (found < 0) { found = ne++; einstr[found] = i; nx0[found] = h0[h]; h0[h] = found; }
        elem[i] = found;
    }
    free(h0); free(nx0);
    if (ne == 0) { free(elem); free(einstr); free(nn); return 0; }

    /* facts: one pair per vreg used as a branch condition */
    int *fnz = malloc((nv + 1) * sizeof(int)), *fz = malloc((nv + 1) * sizeof(int));
    for (int v = 0; v <= nv; v++) { fnz[v] = -1; fz[v] = -1; }
    int nf = 0;
    for (int i = 0; i < n; i++) {
        MIRInstr *ins = &fn->instrs[i];
        if ((ins->op == MIR_JMP_IF || ins->op == MIR_JMP_UNLESS) && ins->src1.kind == MIRV_VREG &&
            fnz[ins->src1.vreg] < 0) {
            fnz[ins->src1.vreg] = ne + nf++;
            fz[ins->src1.vreg] = ne + nf++;
        }
    }
    int U = ne + nf;
    int W = BS_WORDS(U);

    /* per-vreg kill lists: elements that read or write the vreg, plus its facts */
    int *kc = calloc(nv + 2, sizeof(int));
    for (int e = 0; e < ne; e++) {
        MIRInstr *x = &fn->instrs[einstr[e]];
        kc[x->dest.vreg]++;
        if (x->src1.kind == MIRV_VREG) kc[x->src1.vreg]++;
        if (x->src2.kind == MIRV_VREG && !(x->src1.kind == MIRV_VREG && x->src1.vreg == x->src2.vreg)) kc[x->src2.vreg]++;
    }
    int *ks = malloc((nv + 2) * sizeof(int));
    ks[0] = 0;
    for (int v = 0; v <= nv; v++) ks[v + 1] = ks[v] + kc[v];
    int *kl = malloc((ks[nv + 1] + 1) * sizeof(int));
    memset(kc, 0, (nv + 2) * sizeof(int));
    for (int e = 0; e < ne; e++) {
        MIRInstr *x = &fn->instrs[einstr[e]];
        kl[ks[x->dest.vreg] + kc[x->dest.vreg]++] = e;
        if (x->src1.kind == MIRV_VREG) kl[ks[x->src1.vreg] + kc[x->src1.vreg]++] = e;
        if (x->src2.kind == MIRV_VREG && !(x->src1.kind == MIRV_VREG && x->src1.vreg == x->src2.vreg))
            kl[ks[x->src2.vreg] + kc[x->src2.vreg]++] = e;
    }
    free(kc);

    /* memory reads by alias class, for store/call kills */
    int nloads = 0;
    for (int e = 0; e < ne; e++) {
        MIROp o = fn->instrs[einstr[e]].op;
        if (o == MIR_LOAD || o == MIR_ARR_LEN || o == MIR_ARR_DATA) nloads++;
    }
    int *loads = malloc((nloads + 1) * sizeof(int));
    int *lcls = malloc((nloads + 1) * sizeof(int));
    nloads = 0;
    for (int e = 0; e < ne; e++) {
        MIRInstr *x = &fn->instrs[einstr[e]];
        if (x->op == MIR_LOAD) { loads[nloads] = e; lcls[nloads++] = x->mem_cls; }
        else if (x->op == MIR_ARR_LEN || x->op == MIR_ARR_DATA) { loads[nloads] = e; lcls[nloads++] = MIR_MEM_ARRAY_HDR; }
    }

    Cfg cfg = build_cfg(fn);
    size_t sz = (size_t)cfg.n * W;
    if (sz > BITSET_BUDGET) {
        cfg_free(&cfg); free(elem); free(einstr); free(fnz); free(fz); free(ks); free(kl); free(loads); free(lcls); free(nn);
        return 0;
    }
    Word *gen = calloc(sz, sizeof(Word)), *kill = calloc(sz, sizeof(Word));
    Word *in = malloc(sz * sizeof(Word)), *out = malloc(sz * sizeof(Word));

#define KILL_ELEM(ge, ki, e) do { bs_clr(ge, e); bs_set(ki, e); } while (0)
    for (int b = 0; b < cfg.n; b++) {
        Word *g = gen + (size_t)b * W, *k = kill + (size_t)b * W;
        for (int i = cfg.b[b].start; i < cfg.b[b].end; i++) {
            MIRInstr *ins = &fn->instrs[i];
            if (writes_memory(ins)) {
                for (int q = 0; q < nloads; q++) {
                    int kl_ = 1;
                    if (ins->op == MIR_STORE && ins->mem_cls != MIR_MEM_ANY && lcls[q] != MIR_MEM_ANY &&
                        ins->mem_cls != lcls[q]) kl_ = 0;
                    if (kl_) KILL_ELEM(g, k, loads[q]);
                }
            }
            MIRValue *d = def_of(ins);
            if (d) {
                int v = d->vreg;
                for (int q = ks[v]; q < ks[v + 1]; q++) KILL_ELEM(g, k, kl[q]);
                if (fnz[v] >= 0) { KILL_ELEM(g, k, fnz[v]); KILL_ELEM(g, k, fz[v]); }
            }
            if (elem[i] >= 0) { bs_set(g, elem[i]); bs_clr(k, elem[i]); }
        }
    }
    /* predecessor edges: entry = pred block * 2 + which successor slot of it */
    int *pstart = calloc(cfg.n + 2, sizeof(int));
    for (int b = 0; b < cfg.n; b++) for (int q = 0; q < cfg.b[b].nsucc; q++) pstart[cfg.b[b].succ[q] + 1]++;
    for (int b = 0; b < cfg.n; b++) pstart[b + 1] += pstart[b];
    int *plist = malloc((pstart[cfg.n] + 1) * sizeof(int));
    int *pfill = calloc(cfg.n + 1, sizeof(int));
    for (int b = 0; b < cfg.n; b++)
        for (int q = 0; q < cfg.b[b].nsucc; q++) {
            int t = cfg.b[b].succ[q];
            plist[pstart[t] + pfill[t]++] = b * 2 + q;
        }
    free(pfill);
    memset(in, 0xff, sz * sizeof(Word));
    memset(out, 0xff, sz * sizeof(Word));
    Word *tmp = malloc(W * sizeof(Word));
    int changed = 1, iters = 0;
    while (changed && iters++ < 100000) {
        changed = 0;
        for (int b = 0; b < cfg.n; b++) {
            Word *ib = in + (size_t)b * W;
            if (b == 0 || pstart[b + 1] == pstart[b]) {
                memset(ib, 0, W * sizeof(Word));
            } else {
                for (int w = 0; w < W; w++) ib[w] = ~(Word)0;
                for (int e = pstart[b]; e < pstart[b + 1]; e++) {
                    int p = plist[e] >> 1, q = plist[e] & 1;
                    const MIRInstr *last = &fn->instrs[cfg.b[p].end - 1];
                    memcpy(tmp, out + (size_t)p * W, W * sizeof(Word));
                    if ((last->op == MIR_JMP_IF || last->op == MIR_JMP_UNLESS) && last->src1.kind == MIRV_VREG &&
                        cfg.b[p].nsucc == 2) {
                        int v = last->src1.vreg;
                        /* succ[0] is the jump target, succ[1] the fall-through (see build_cfg) */
                        int taken = (q == 0);
                        int nonzero = (last->op == MIR_JMP_IF) ? taken : !taken;
                        if (fnz[v] >= 0) bs_set(tmp, nonzero ? fnz[v] : fz[v]);
                    }
                    for (int w = 0; w < W; w++) ib[w] &= tmp[w];
                }
            }
            Word *ob = out + (size_t)b * W;
            Word *g = gen + (size_t)b * W, *k = kill + (size_t)b * W;
            for (int w = 0; w < W; w++) {
                Word nw = g[w] | (ib[w] & ~k[w]);
                if (nw != ob[w]) { ob[w] = nw; changed = 1; }
            }
        }
    }
    free(tmp); free(pstart); free(plist);
    int did = 0;
    if (!changed) {
        /* hash the elements by expression for lookup */
        int HS = 1; while (HS < ne * 2 + 1) HS <<= 1;
        int *head = malloc(HS * sizeof(int)), *next = malloc(ne * sizeof(int));
        for (int h = 0; h < HS; h++) head[h] = -1;
        MIRInstr *keyed = malloc(ne * sizeof(MIRInstr));
        for (int e = 0; e < ne; e++) {
            keyed[e] = fn->instrs[einstr[e]];
            canon_expr(&keyed[e]);
            unsigned h = expr_hash(&keyed[e]) & (HS - 1);
            next[e] = head[h]; head[h] = e;
        }
        Word *cur = malloc(W * sizeof(Word));
        for (int b = 0; b < cfg.n; b++) {
            memcpy(cur, in + (size_t)b * W, W * sizeof(Word));
            for (int i = cfg.b[b].start; i < cfg.b[b].end; i++) {
                MIRInstr *ins = &fn->instrs[i];
                int replaced = 0;
                /* a branch whose condition is already known */
                if ((ins->op == MIR_JMP_IF || ins->op == MIR_JMP_UNLESS) && ins->src1.kind == MIRV_VREG &&
                    fnz[ins->src1.vreg] >= 0) {
                    int v = ins->src1.vreg;
                    int known_nz = bs_get(cur, fnz[v]), known_z = bs_get(cur, fz[v]);
                    if (known_nz || known_z) {
                        int nonzero = known_nz;
                        int taken = (ins->op == MIR_JMP_IF) ? nonzero : !nonzero;
                        if (taken) { ins->op = MIR_JMP; ins->src1 = mir_none(); }
                        else make_nop(ins);
                        did = 1;
                        continue;
                    }
                }
                /* the value is already in this very register */
                if (elem[i] >= 0 && bs_get(cur, elem[i])) { make_nop(ins); did = 1; continue; }
                /* bounds check `i <u L` where the loop test already established `i <s L` and i >= 0 */
                if (ins->op == MIR_UCMP_LT && ins->src1.kind == MIRV_VREG && ins->src2.kind == MIRV_VREG &&
                    ins->dest.kind == MIRV_VREG && nn[ins->src1.vreg]) {
                    MIRInstr key = *ins; key.op = MIR_CMP_LT;
                    unsigned h = expr_hash(&key) & (HS - 1);
                    for (int e = head[h]; e >= 0; e = next[e]) {
                        if (!bs_get(cur, e) || !expr_same(&keyed[e], &key)) continue;
                        int hv = fn->instrs[einstr[e]].dest.vreg;
                        if (fnz[hv] >= 0 && bs_get(cur, fnz[hv])) {
                            make_mov(ins, mir_imm_int(1, ins->dest.type));
                            replaced = 1; did = 1;
                            break;
                        }
                    }
                }
                if (!replaced && elem[i] >= 0) {
                    MIRInstr key = *ins; canon_expr(&key);
                    unsigned h = expr_hash(&key) & (HS - 1);
                    for (int e = head[h]; e >= 0; e = next[e]) {
                        if (e == elem[i] || !bs_get(cur, e)) continue;
                        if (!expr_same(&keyed[e], &key)) continue;
                        MIRInstr *hi = &fn->instrs[einstr[e]];
                        if (is_f(hi->dest.type) != is_f(ins->dest.type)) continue;
                        /* the holder's register must still contain the value: it does, because
                           its element is available (holder redefinitions kill it) */
                        int hv = hi->dest.vreg;
                        if (fnz[hv] >= 0 && (bs_get(cur, fnz[hv]) || bs_get(cur, fz[hv])) &&
                            (ins->op >= MIR_CMP_EQ && ins->op <= MIR_UCMP_GE)) {
                            make_mov(ins, mir_imm_int(bs_get(cur, fnz[hv]) ? 1 : 0, ins->dest.type));
                        } else {
                            make_mov(ins, mir_vreg(hv, ins->dest.type));
                        }
                        replaced = 1; did = 1;
                        break;
                    }
                }
                /* transfer */
                if (writes_memory(ins)) {
                    for (int q = 0; q < nloads; q++) {
                        int kl_ = 1;
                        if (ins->op == MIR_STORE && ins->mem_cls != MIR_MEM_ANY && lcls[q] != MIR_MEM_ANY &&
                            ins->mem_cls != lcls[q]) kl_ = 0;
                        if (kl_) bs_clr(cur, loads[q]);
                    }
                }
                MIRValue *d = def_of(ins);
                if (d) {
                    int v = d->vreg;
                    for (int q = ks[v]; q < ks[v + 1]; q++) bs_clr(cur, kl[q]);
                    if (fnz[v] >= 0) { bs_clr(cur, fnz[v]); bs_clr(cur, fz[v]); }
                }
                if (!replaced && elem[i] >= 0) bs_set(cur, elem[i]);
            }
        }
        free(cur); free(head); free(next); free(keyed);
    }
#undef KILL_ELEM
    cfg_free(&cfg);
    free(gen); free(kill); free(in); free(out);
    free(elem); free(einstr); free(fnz); free(fz); free(ks); free(kl); free(loads); free(lcls); free(nn);
    return did;
}

/* ======================================================================
 *  Dead code elimination (liveness based)
 * ====================================================================== */

typedef struct { Word *live; } DceCtx;
static void dce_use(MIRValue *v, void *ud) { bs_set(((DceCtx *)ud)->live, v->vreg); }

static int dce(MIRFunc *fn) {
    int nv = max_vreg(fn);
    Cfg cfg = build_cfg(fn);
    if ((size_t)cfg.n * BS_WORDS(nv) > BITSET_BUDGET) { cfg_free(&cfg); return 0; }
    Live lv = compute_live(fn, &cfg, nv);
    Word *live = malloc(lv.words * sizeof(Word));
    int changed = 0;
    for (int b = 0; b < cfg.n; b++) {
        memcpy(live, lv.live_out + (size_t)b * lv.words, lv.words * sizeof(Word));
        DceCtx dc = { live };
        for (int i = cfg.b[b].end - 1; i >= cfg.b[b].start; i--) {
            MIRInstr *ins = &fn->instrs[i];
            if (ins->op == MIR_NOP) continue;
            MIRValue *d = def_of(ins);
            if (d && !bs_get(live, d->vreg)) {
                if (is_pure(ins)) {
                    make_nop(ins);
                    changed = 1;
                    continue;
                }
                if (ins->op == MIR_CALL || ins->op == MIR_SYSCALL) {
                    /* keep the call, drop the unused result */
                    if (!(ins->op == MIR_CALL && ins->ret_aggr)) {
                        ins->dest = mir_none();
                    }
                    for_each_use(ins, dce_use, &dc);
                    continue;
                }
            }
            if (d) bs_clr(live, d->vreg);
            for_each_use(ins, dce_use, &dc);
        }
    }
    free(live);
    live_free(&lv);
    cfg_free(&cfg);
    if (changed) compact(fn);
    return changed;
}

/* ======================================================================
 *  Fold `t = op ...; x = mov t`  into  `x = op ...`
 * ====================================================================== */


typedef struct { int vreg; int hit; } HitCtx;
static void hit_use(MIRValue *v, void *ud) {
    HitCtx *h = ud;
    if (v->vreg == h->vreg) h->hit = 1;
}

static int fold_movs(MIRFunc *fn) {
    int nv = max_vreg(fn);
    int *uses = calloc(nv, sizeof(int)), *defs = calloc(nv, sizeof(int));
    CntCtx cc = { uses };
    for (int i = 0; i < fn->count; i++) {
        MIRInstr *ins = &fn->instrs[i];
        for_each_use(ins, cnt_use, &cc);
        MIRValue *d = def_of(ins);
        if (d) defs[d->vreg]++;
    }
    int changed = 0;
    Cfg cfg = build_cfg(fn);
    for (int b = 0; b < cfg.n; b++) {
        for (int i = cfg.b[b].start; i < cfg.b[b].end; i++) {
            MIRInstr *m = &fn->instrs[i];
            if (m->op != MIR_MOV || m->dest.kind != MIRV_VREG || m->src1.kind != MIRV_VREG) continue;
            int t = m->src1.vreg, x = m->dest.vreg;
            if (t == x || uses[t] != 1 || defs[t] != 1) continue;
            if (is_f(m->src1.type) != is_f(m->dest.type)) continue;
            /* find t's def, scanning backwards in this block */
            int di = -1, ok = 1;
            for (int j = i - 1; j >= cfg.b[b].start; j--) {
                MIRInstr *p = &fn->instrs[j];
                if (p->op == MIR_NOP) continue;
                MIRValue *pd = def_of(p);
                if (pd && pd->vreg == t) { di = j; break; }
                /* x must be untouched between the def and the mov */
                HitCtx h = { x, 0 };
                for_each_use(p, hit_use, &h);
                if (h.hit || (pd && pd->vreg == x)) { ok = 0; break; }
                /* a call/asm reading nothing of x is fine, but an instruction
                   with hidden effects on x doesn't exist in MIR */
            }
            if (!ok || di < 0) continue;
            MIRInstr *dd = &fn->instrs[di];
            if (dd->op == MIR_MOV && dd->src1.kind == MIRV_VREG && dd->src1.vreg == x) continue;
            if (!def_of(dd)) continue;
            if (dd->op == MIR_ALLOCA_LOCAL) continue;
            if (is_f(dd->dest.type) != is_f(m->dest.type)) continue;
            /* the def must not be a call that returns an aggregate etc. */
            if (dd->op == MIR_CALL && dd->ret_aggr) continue;
            /* x must not be read by the def in a way that matters after
               renaming: `t = x + 1; x = t` -> `x = x + 1` is fine */
            dd->dest.vreg = x;
            make_nop(m);
            uses[t] = 0; defs[t] = 0; defs[x]++;
            changed = 1;
        }
    }
    cfg_free(&cfg);
    free(uses); free(defs);
    if (changed) compact(fn);
    return changed;
}

/* ======================================================================
 *  CFG cleanup
 * ====================================================================== */

static int cleanup_cfg(MIRFunc *fn) {
    int changed = 0;
    for (int round = 0; round < 8; round++) {
        int again = 0;
        int n = fn->count;
        if (n == 0) break;

        int maxl = -1;
        for (int i = 0; i < n; i++)
            if (fn->instrs[i].op == MIR_LABEL && fn->instrs[i].dest.label_id > maxl)
                maxl = fn->instrs[i].dest.label_id;
        int *lpos = NULL;
        if (maxl >= 0) {
            lpos = malloc((maxl + 1) * sizeof(int));
            for (int i = 0; i <= maxl; i++) lpos[i] = -1;
            for (int i = 0; i < n; i++)
                if (fn->instrs[i].op == MIR_LABEL) lpos[fn->instrs[i].dest.label_id] = i;
        }

        /* 1. jump threading: a jump to a label whose first real instruction
           is an unconditional jump goes straight to the final target */
        for (int i = 0; i < n; i++) {
            MIRInstr *ins = &fn->instrs[i];
            if (ins->op != MIR_JMP && ins->op != MIR_JMP_IF && ins->op != MIR_JMP_UNLESS) continue;
            for (int hop = 0; hop < 8; hop++) {
                int l = ins->dest.label_id;
                if (l < 0 || l > maxl || lpos[l] < 0) break;
                int p = lpos[l];
                while (p < n && (fn->instrs[p].op == MIR_LABEL || fn->instrs[p].op == MIR_NOP)) p++;
                if (p >= n || fn->instrs[p].op != MIR_JMP) break;
                int nl = fn->instrs[p].dest.label_id;
                if (nl == l) break;
                ins->dest = mir_label(nl);
                again = 1;
            }
        }

        /* 2. reachability from the entry */
        unsigned char *reach = calloc(n, 1);
        int *stack = malloc((n + 8) * sizeof(int)); int sp = 0;
        stack[sp++] = 0;
        while (sp) {
            int p = stack[--sp];
            while (p < n && !reach[p]) {
                reach[p] = 1;
                MIRInstr *ins = &fn->instrs[p];
                if (ins->op == MIR_JMP || ins->op == MIR_JMP_IF || ins->op == MIR_JMP_UNLESS) {
                    int l = ins->dest.label_id;
                    if (l >= 0 && l <= maxl && lpos[l] >= 0 && !reach[lpos[l]] && sp < n + 4)
                        stack[sp++] = lpos[l];
                    if (ins->op == MIR_JMP) break;
                } else if (ins->op == MIR_RET || ins->op == MIR_IRET || ins->op == MIR_SYSRET) {
                    break;
                }
                p++;
            }
        }
        for (int i = 0; i < n; i++)
            if (!reach[i] && fn->instrs[i].op != MIR_NOP) { make_nop(&fn->instrs[i]); again = 1; }
        free(reach); free(stack);

        /* 3. branch to the next instruction, and label reference counts */
        int *refs = calloc(maxl + 2, sizeof(int));
        for (int i = 0; i < n; i++) {
            MIRInstr *ins = &fn->instrs[i];
            if (ins->op == MIR_NOP) continue;
            if (ins->op == MIR_JMP || ins->op == MIR_JMP_IF || ins->op == MIR_JMP_UNLESS) {
                int l = ins->dest.label_id;
                /* is the target right after us (only labels/nops between)? */
                int p = i + 1;
                int fall = 0;
                while (p < n && (fn->instrs[p].op == MIR_LABEL || fn->instrs[p].op == MIR_NOP)) {
                    if (fn->instrs[p].op == MIR_LABEL && fn->instrs[p].dest.label_id == l) { fall = 1; break; }
                    p++;
                }
                if (fall) { make_nop(ins); again = 1; continue; }
                if (l >= 0 && l <= maxl) refs[l]++;
            }
        }

        /* 3b. `jcc L1; jmp L2; L1:` -> inverted `jcc' L2` */
        for (int i = 0; i + 1 < n; i++) {
            MIRInstr *c = &fn->instrs[i];
            if (c->op != MIR_JMP_IF && c->op != MIR_JMP_UNLESS) continue;
            int j = i + 1;
            while (j < n && fn->instrs[j].op == MIR_NOP) j++;
            if (j >= n || fn->instrs[j].op != MIR_JMP) continue;
            int p = j + 1, hit = 0;
            while (p < n && (fn->instrs[p].op == MIR_LABEL || fn->instrs[p].op == MIR_NOP)) {
                if (fn->instrs[p].op == MIR_LABEL && fn->instrs[p].dest.label_id == c->dest.label_id) { hit = 1; break; }
                p++;
            }
            if (!hit) continue;
            c->op = c->op == MIR_JMP_IF ? MIR_JMP_UNLESS : MIR_JMP_IF;
            c->dest = fn->instrs[j].dest;
            make_nop(&fn->instrs[j]);
            again = 1;
        }

        /* 4. drop unreferenced labels (merges blocks for the local passes) */
        for (int i = 0; i < n; i++) {
            MIRInstr *ins = &fn->instrs[i];
            if (ins->op != MIR_LABEL) continue;
            int l = ins->dest.label_id;
            if (l >= 0 && l <= maxl && refs[l] == 0) { make_nop(ins); again = 1; }
        }
        free(refs); free(lpos);
        compact(fn);
        if (!again) break;
        changed = 1;
    }
    return changed;
}



typedef struct { MIRInstr *v; int n, cap; } InstrBuf;
static void ib_push(InstrBuf *b, MIRInstr ins) {
    if (b->n == b->cap) { b->cap = b->cap ? b->cap * 2 : 64; b->v = realloc(b->v, b->cap * sizeof(MIRInstr)); }
    b->v[b->n++] = ins;
}
static MIRInstr copy_instr(const MIRInstr *ins) {
    MIRInstr c = *ins;
    if (ins->args && ins->arg_count > 0) {
        c.args = malloc(ins->arg_count * sizeof(MIRValue));
        memcpy(c.args, ins->args, ins->arg_count * sizeof(MIRValue));
    } else c.args = NULL;
    if (ins->arg_aggr && ins->arg_count > 0) {
        c.arg_aggr = malloc(ins->arg_count * sizeof(int));
        memcpy(c.arg_aggr, ins->arg_aggr, ins->arg_count * sizeof(int));
    }
    return c;
}


/* ======================================================================
 *  Division / remainder by a constant
 * ====================================================================== */

/* Hacker's Delight, 64-bit: multiply by a reciprocal instead of dividing. */
static void magic_s(int64_t d, int64_t *M, int *sh) {
    const uint64_t two63 = 1ULL << 63;
    uint64_t ad = d < 0 ? -(uint64_t)d : (uint64_t)d;
    uint64_t t = two63 + ((uint64_t)d >> 63);
    uint64_t anc = t - 1 - t % ad;
    int p = 63;
    uint64_t q1 = two63 / anc, r1 = two63 - q1 * anc;
    uint64_t q2 = two63 / ad,  r2 = two63 - q2 * ad;
    uint64_t delta;
    do {
        p++;
        q1 = 2 * q1; r1 = 2 * r1;
        if (r1 >= anc) { q1++; r1 -= anc; }
        q2 = 2 * q2; r2 = 2 * r2;
        if (r2 >= ad) { q2++; r2 -= ad; }
        delta = ad - r2;
    } while (q1 < delta || (q1 == delta && r1 == 0));
    int64_t m = (int64_t)(q2 + 1);
    if (d < 0) m = -m;
    *M = m; *sh = p - 64;
}

static void magic_u(uint64_t d, uint64_t *M, int *add, int *sh) {
    int p; uint64_t nc, delta, q1, r1, q2, r2;
    *add = 0;
    nc = (uint64_t)-1 - (0 - d) % d;
    p = 63;
    q1 = 0x8000000000000000ULL / nc; r1 = 0x8000000000000000ULL - q1 * nc;
    q2 = 0x7FFFFFFFFFFFFFFFULL / d;  r2 = 0x7FFFFFFFFFFFFFFFULL - q2 * d;
    do {
        p++;
        if (r1 >= nc - r1) { q1 = 2 * q1 + 1; r1 = 2 * r1 - nc; }
        else { q1 = 2 * q1; r1 = 2 * r1; }
        if (r2 + 1 >= d - r2) {
            if (q2 >= 0x7FFFFFFFFFFFFFFFULL) *add = 1;
            q2 = 2 * q2 + 1; r2 = 2 * r2 + 1 - d;
        } else {
            if (q2 >= 0x8000000000000000ULL) *add = 1;
            q2 = 2 * q2; r2 = 2 * r2 + 1;
        }
        delta = d - 1 - r2;
    } while (p < 128 && (q1 < delta || (q1 == delta && r1 == 0)));
    *M = q2 + 1; *sh = p - 64;
}

static MIRInstr mk(MIROp op, MIRValue dest, MIRValue a, MIRValue b) {
    MIRInstr m; memset(&m, 0, sizeof m);
    m.op = op; m.dest = dest; m.src1 = a; m.src2 = b;
    return m;
}

static int expand_div(MIRFunc *fn) {
    int nv = max_vreg(fn);
    int n = fn->count;
    int *uses = calloc(nv + 1, sizeof(int));
    CntCtx cc = { uses };
    for (int i = 0; i < n; i++) for_each_use(&fn->instrs[i], cnt_use, &cc);

    InstrBuf out = {0};
    int changed = 0;
#define TMP() mir_vreg(nv++, MIR_I64)
    for (int i = 0; i < n; i++) {
        MIRInstr *ins = &fn->instrs[i];
        int isdiv = ins->op == MIR_SDIV || ins->op == MIR_UDIV;
        int ismod = ins->op == MIR_SMOD || ins->op == MIR_UMOD;
        if (!(isdiv || ismod) || ins->src2.kind != MIRV_IMM_INT || ins->src1.kind != MIRV_VREG ||
            ins->dest.kind != MIRV_VREG || is_f(ins->dest.type)) {
            ib_push(&out, *ins);
            continue;
        }
        int64_t d = ins->src2.imm_int;
        MIRValue N = ins->src1, D = ins->dest;
        MIRType dt = D.type;
        int sgn = ins->op == MIR_SDIV || ins->op == MIR_SMOD;

        if (sgn) {
            if (d == 0 || d == INT64_MIN) { ib_push(&out, *ins); continue; }
            if (d == 1 || d == -1) {
                if (isdiv) ib_push(&out, mk(d == 1 ? MIR_MOV : MIR_NEG, D, N, mir_none()));
                else       ib_push(&out, mk(MIR_MOV, D, mir_imm_int(0, dt), mir_none()));
                changed = 1; continue;
            }
            int64_t ad = d < 0 ? -d : d;
            int k = log2_exact(ad);
            if (k > 0) {
                /* remainder tested against zero: only the low bits matter */
                int j = i + 1;
                while (j < n && fn->instrs[j].op == MIR_NOP) j++;
                if (ismod && j < n && (fn->instrs[j].op == MIR_CMP_EQ || fn->instrs[j].op == MIR_CMP_NE) &&
                    fn->instrs[j].src1.kind == MIRV_VREG && fn->instrs[j].src1.vreg == D.vreg &&
                    fn->instrs[j].src2.kind == MIRV_IMM_INT && fn->instrs[j].src2.imm_int == 0 &&
                    uses[D.vreg] == 1) {
                    ib_push(&out, mk(MIR_AND, D, N, mir_imm_int(ad - 1, MIR_I64)));
                    changed = 1; continue;
                }
                MIRValue t1 = TMP(), t2 = TMP(), t3 = TMP();
                ib_push(&out, mk(MIR_SHR_A, t1, N, mir_imm_int(63, MIR_I64)));
                ib_push(&out, mk(MIR_SHR_L, t2, t1, mir_imm_int(64 - k, MIR_I64)));
                ib_push(&out, mk(MIR_ADD, t3, N, t2));
                if (isdiv) {
                    if (d > 0) ib_push(&out, mk(MIR_SHR_A, D, t3, mir_imm_int(k, MIR_I64)));
                    else {
                        MIRValue q = TMP();
                        ib_push(&out, mk(MIR_SHR_A, q, t3, mir_imm_int(k, MIR_I64)));
                        ib_push(&out, mk(MIR_NEG, D, q, mir_none()));
                    }
                } else {
                    MIRValue m = TMP();
                    ib_push(&out, mk(MIR_AND, m, t3, mir_imm_int(-ad, MIR_I64)));
                    ib_push(&out, mk(MIR_SUB, D, N, m));
                }
                changed = 1; continue;
            }
            int64_t M; int sh;
            magic_s(d, &M, &sh);
            MIRValue h = TMP();
            ib_push(&out, mk(MIR_SMULH, h, N, mir_imm_int(M, MIR_I64)));
            if (d > 0 && M < 0) { MIRValue h2 = TMP(); ib_push(&out, mk(MIR_ADD, h2, h, N)); h = h2; }
            if (d < 0 && M > 0) { MIRValue h2 = TMP(); ib_push(&out, mk(MIR_SUB, h2, h, N)); h = h2; }
            if (sh > 0) { MIRValue h2 = TMP(); ib_push(&out, mk(MIR_SHR_A, h2, h, mir_imm_int(sh, MIR_I64))); h = h2; }
            MIRValue sb = TMP();
            /* d > 0: the quotient is one too small exactly when n < 0, and n's sign bit
               is available in parallel with the multiply; d < 0 uses the quotient's own */
            ib_push(&out, mk(MIR_SHR_L, sb, d > 0 ? N : h, mir_imm_int(63, MIR_I64)));
            if (isdiv) {
                ib_push(&out, mk(MIR_ADD, D, h, sb));
            } else {
                MIRValue q = TMP(), p = TMP();
                ib_push(&out, mk(MIR_ADD, q, h, sb));
                ib_push(&out, mk(MIR_MUL, p, q, mir_imm_int(d, MIR_I64)));
                ib_push(&out, mk(MIR_SUB, D, N, p));
            }
            changed = 1; continue;
        }

        /* unsigned */
        uint64_t ud = (uint64_t)d;
        if (ud == 0) { ib_push(&out, *ins); continue; }
        if (ud == 1) {
            if (isdiv) ib_push(&out, mk(MIR_MOV, D, N, mir_none()));
            else       ib_push(&out, mk(MIR_MOV, D, mir_imm_int(0, dt), mir_none()));
            changed = 1; continue;
        }
        if (log2_exact((int64_t)ud) > 0 && (int64_t)ud > 0) { ib_push(&out, *ins); continue; }  /* simplify() handles powers of two */
        if (ud >> 63) {                    /* quotient is 0 or 1 */
            MIRValue q = isdiv ? D : TMP();
            ib_push(&out, mk(MIR_UCMP_GE, q, N, mir_imm_int(d, MIR_I64)));
            if (ismod) {
                MIRValue p = TMP();
                ib_push(&out, mk(MIR_MUL, p, q, mir_imm_int(d, MIR_I64)));
                ib_push(&out, mk(MIR_SUB, D, N, p));
            }
            changed = 1; continue;
        }
        uint64_t M; int add, sh;
        magic_u(ud, &M, &add, &sh);
        MIRValue t = TMP();
        ib_push(&out, mk(MIR_UMULH, t, N, mir_imm_int((int64_t)M, MIR_I64)));
        MIRValue q = isdiv ? D : TMP();
        if (!add) {
            if (sh) ib_push(&out, mk(MIR_SHR_L, q, t, mir_imm_int(sh, MIR_I64)));
            else    ib_push(&out, mk(MIR_MOV, q, t, mir_none()));
        } else {
            MIRValue u = TMP(), u2 = TMP(), u3 = TMP();
            ib_push(&out, mk(MIR_SUB, u, N, t));
            ib_push(&out, mk(MIR_SHR_L, u2, u, mir_imm_int(1, MIR_I64)));
            ib_push(&out, mk(MIR_ADD, u3, u2, t));
            if (sh - 1 > 0) ib_push(&out, mk(MIR_SHR_L, q, u3, mir_imm_int(sh - 1, MIR_I64)));
            else            ib_push(&out, mk(MIR_MOV, q, u3, mir_none()));
        }
        if (ismod) {
            MIRValue p = TMP();
            ib_push(&out, mk(MIR_MUL, p, q, mir_imm_int(d, MIR_I64)));
            ib_push(&out, mk(MIR_SUB, D, N, p));
        }
        changed = 1;
    }
#undef TMP
    free(uses);
    if (!changed) {
        for (int i = 0; i < out.n; i++) (void)i;
        free(out.v);
        return 0;
    }
    /* the original instructions were copied by value: ownership of their
       argument arrays moved to `out`, so just swap the buffers */
    free(fn->instrs);
    fn->instrs = out.v; fn->count = out.n; fn->cap = out.cap;
    fn->vreg_count = nv;
    return 1;
}


/* ======================================================================
 *  If-conversion:  branches around tiny integer computations -> SELECT
 * ====================================================================== */

/* cost of speculating an instruction in an arm, or -1 if it may not be
   executed unconditionally (memory, calls, traps, floats, control flow) */
static int arm_cost(const MIRInstr *ins) {
    if (ins->dest.kind != MIRV_VREG && ins->op != MIR_NOP) return -1;
    if (ins->dest.kind == MIRV_VREG && is_f(ins->dest.type)) return -1;
    switch (ins->op) {
    case MIR_NOP: return 0;
    case MIR_MOV: case MIR_ADD: case MIR_SUB: case MIR_AND: case MIR_OR: case MIR_XOR:
    case MIR_SHL: case MIR_SHR_A: case MIR_SHR_L: case MIR_NEG: case MIR_NOT:
    case MIR_SEXT: case MIR_ZEXT: case MIR_TRUNC:
    case MIR_CMP_EQ: case MIR_CMP_NE: case MIR_CMP_LT: case MIR_CMP_LE: case MIR_CMP_GT: case MIR_CMP_GE:
    case MIR_UCMP_LT: case MIR_UCMP_LE: case MIR_UCMP_GT: case MIR_UCMP_GE:
    case MIR_SELECT:
        return 1;
    case MIR_MUL: return 3;
    default: return -1;
    }
}

typedef struct { int *map; } RenCtx;
static void ren_use(MIRValue *v, void *ud) {
    RenCtx *r = ud;
    if (r->map[v->vreg] >= 0) v->vreg = r->map[v->vreg];
}

/* the arm [a, b) as renamed copies; every vreg in `V` that it defines is
   redirected to a fresh temp. Returns the number of instructions emitted and
   records, per V member, the temp holding the arm's final value. */
static void copy_arm(MIRFunc *fn, int a, int b, const unsigned char *inV, int *tmp_of, int *nv,
                     InstrBuf *out, int nvtotal) {
    (void)nvtotal;
    int *map = malloc((*nv + 1) * sizeof(int));
    for (int i = 0; i <= *nv; i++) map[i] = -1;
    RenCtx rc = { map };
    for (int i = a; i < b; i++) {
        MIRInstr *src = &fn->instrs[i];
        if (src->op == MIR_NOP) continue;
        MIRInstr c = copy_instr(src);
        for_each_use(&c, ren_use, &rc);
        MIRValue *d = def_of(&c);
        if (d && inV[d->vreg]) {
            int t = (*nv)++;
            map = realloc(map, (*nv + 1) * sizeof(int));
            map[t] = -1;
            map[d->vreg] = t;
            tmp_of[d->vreg] = t;
            d->vreg = t;
        } else if (d && map[d->vreg] >= 0) {
            /* redefinition of a name that was renamed earlier in the arm: it is no
               longer in V's map for later uses */
            map[d->vreg] = -1;
        }
        ib_push(out, c);
    }
    free(map);
}

static int if_convert_one(MIRFunc *fn) {
    int n = fn->count;
    Cfg cfg = build_cfg(fn);
    int nv = max_vreg(fn);
    if ((size_t)cfg.n * BS_WORDS(nv) > BITSET_BUDGET) { cfg_free(&cfg); return 0; }
    Live lv = compute_live(fn, &cfg, nv);
    int *uses = calloc(nv + 1, sizeof(int));
    CntCtx cc = { uses };
    for (int i = 0; i < n; i++) for_each_use(&fn->instrs[i], cnt_use, &cc);
    int done = 0;

    for (int i = 0; i < n && !done; i++) {
        MIRInstr *br = &fn->instrs[i];
        if (br->op != MIR_JMP_IF && br->op != MIR_JMP_UNLESS) continue;
        if (br->src1.kind != MIRV_VREG) continue;
        int cv = br->src1.vreg;
        int L1 = br->dest.label_id;
        /* arm 1 runs when the branch is NOT taken */
        int a1 = i + 1, p = a1, cost1 = 0, cnt1 = 0;
        int ok = 1;
        while (p < n && fn->instrs[p].op != MIR_LABEL && !is_terminator(fn->instrs[p].op)) {
            int c = arm_cost(&fn->instrs[p]);
            if (c < 0) { ok = 0; break; }
            if (fn->instrs[p].op != MIR_NOP) cnt1++;
            cost1 += c; p++;
        }
        if (!ok || p >= n || cnt1 == 0 || cnt1 > 6 || cost1 > 8) continue;
        int shape = 0, a2 = 0, b2 = 0, end_idx = 0;   /* 1 triangle, 2 diamond */
        int cost2 = 0, cnt2 = 0;
        if (fn->instrs[p].op == MIR_LABEL && fn->instrs[p].dest.label_id == L1) {
            shape = 1; end_idx = p;
        } else if (fn->instrs[p].op == MIR_JMP) {
            int Lend = fn->instrs[p].dest.label_id;
            int q = p + 1;
            while (q < n && fn->instrs[q].op == MIR_NOP) q++;
            if (q < n && fn->instrs[q].op == MIR_LABEL && fn->instrs[q].dest.label_id == L1) {
                a2 = q + 1; int r = a2;
                while (r < n && fn->instrs[r].op != MIR_LABEL && !is_terminator(fn->instrs[r].op)) {
                    int c = arm_cost(&fn->instrs[r]);
                    if (c < 0) { ok = 0; break; }
                    if (fn->instrs[r].op != MIR_NOP) cnt2++;
                    cost2 += c; r++;
                }
                if (ok && r < n && fn->instrs[r].op == MIR_LABEL && fn->instrs[r].dest.label_id == Lend &&
                    cnt2 <= 6 && cost1 + cost2 <= 9) {
                    shape = 2; b2 = r; end_idx = r;
                }
            }
        }
        if (!shape) continue;

        /* V: vregs the arms define that are live at the join */
        int endblk = cfg.ib[end_idx];
        const Word *live_end = lv.live_in + (size_t)endblk * lv.words;
        unsigned char *inV = calloc(nv + 1, 1);
        unsigned char *defd = calloc(nv + 1, 1);
        int nV = 0;
        int bad = 0;
        int arms_lo[2] = { a1, a2 }, arms_hi[2] = { p, b2 };
        for (int k = 0; k < (shape == 2 ? 2 : 1); k++)
            for (int x = arms_lo[k]; x < arms_hi[k]; x++) {
                MIRValue *d = def_of(&fn->instrs[x]);
                if (!d) continue;
                defd[d->vreg] = 1;
                if (d->vreg == cv) bad = 1;
                if (bs_get(live_end, d->vreg) && !inV[d->vreg]) { inV[d->vreg] = 1; nV++; }
            }
        if (bad || nV == 0 || nV > 3) { free(inV); free(defd); continue; }

        /* the compare feeding the branch moves to the end of the arms when that
           is safe, so backend fusion can turn compare+selects into cmp+cmov */
        int prev = i - 1;
        while (prev >= 0 && fn->instrs[prev].op == MIR_NOP) prev--;
        int move_cmp = 0;
        if (prev >= 0 && uses[cv] == 1 && fn->instrs[prev].dest.kind == MIRV_VREG &&
            fn->instrs[prev].dest.vreg == cv &&
            ((fn->instrs[prev].op >= MIR_CMP_EQ && fn->instrs[prev].op <= MIR_UCMP_GE))) {
            move_cmp = 1;
            MIRInstr *cm = &fn->instrs[prev];
            const MIRValue *ops[2] = { &cm->src1, &cm->src2 };
            for (int k = 0; k < 2; k++)
                if (ops[k]->kind == MIRV_VREG && defd[ops[k]->vreg] && !inV[ops[k]->vreg]) move_cmp = 0;
        }

        /* build the replacement for instrs [start, end_idx) */
        int start = move_cmp ? prev : i;
        InstrBuf rep = {0};
        int nvv = nv;
        int *tmpA = malloc((nv + 1) * sizeof(int)), *tmpB = malloc((nv + 1) * sizeof(int));
        for (int k = 0; k <= nv; k++) { tmpA[k] = -1; tmpB[k] = -1; }
        unsigned char *inV2 = calloc(nv + 64 + 1, 1);
        memcpy(inV2, inV, nv + 1);
        int *tmpA2 = realloc(tmpA, (nv + 64 + 1) * sizeof(int));
        int *tmpB2 = realloc(tmpB, (nv + 64 + 1) * sizeof(int));
        tmpA = tmpA2; tmpB = tmpB2;
        for (int k = nv + 1; k < nv + 64 + 1; k++) { tmpA[k] = -1; tmpB[k] = -1; }
        int arm1_lo = a1, arm1_hi = p;
        copy_arm(fn, arm1_lo, arm1_hi, inV2, tmpA, &nvv, &rep, nv);
        if (shape == 2) copy_arm(fn, a2, b2, inV2, tmpB, &nvv, &rep, nv);
        if (move_cmp) ib_push(&rep, copy_instr(&fn->instrs[prev]));
        /* selects.  "branch not taken" arm = arm1 runs when JMP_IF cond is false / JMP_UNLESS cond is true */
        int arm1_when_true = br->op == MIR_JMP_UNLESS;
        for (int v = 0; v < nv; v++) {
            if (!inV[v]) continue;
            MIRType vt = MIR_I64;
            for (int x = a1; x < p; x++) { MIRValue *d = def_of(&fn->instrs[x]); if (d && d->vreg == v) vt = d->type; }
            if (shape == 2)
                for (int x = a2; x < b2; x++) { MIRValue *d = def_of(&fn->instrs[x]); if (d && d->vreg == v) vt = d->type; }
            MIRValue val1 = mir_vreg(tmpA[v] >= 0 ? tmpA[v] : v, vt);       /* value when arm1 runs */
            MIRValue val2 = mir_vreg(shape == 2 && tmpB[v] >= 0 ? tmpB[v] : v, vt);   /* when it doesn't */
            MIRValue t_val = arm1_when_true ? val1 : val2;
            MIRValue f_val = arm1_when_true ? val2 : val1;
            MIRInstr sel; memset(&sel, 0, sizeof sel);
            sel.op = MIR_SELECT;
            sel.dest = mir_vreg(v, vt);
            sel.src1 = mir_vreg(cv, MIR_I64);
            sel.src2 = t_val;
            sel.args = malloc(sizeof(MIRValue));
            sel.args[0] = f_val;
            sel.arg_count = 1;
            ib_push(&rep, sel);
        }
        /* splice: drop [start, end_idx), insert rep */
        InstrBuf outb = {0};
        for (int x = 0; x < start; x++) ib_push(&outb, fn->instrs[x]);
        for (int x = 0; x < rep.n; x++) ib_push(&outb, rep.v[x]);
        for (int x = start; x < end_idx; x++) {
            if (fn->instrs[x].args) { free(fn->instrs[x].args); }
        }
        for (int x = end_idx; x < n; x++) ib_push(&outb, fn->instrs[x]);
        free(rep.v); free(fn->instrs);
        fn->instrs = outb.v; fn->count = outb.n; fn->cap = outb.cap;
        if (fn->vreg_count < nvv) fn->vreg_count = nvv;
        free(tmpA); free(tmpB); free(inV2);
        free(inV); free(defd);
        done = 1;
    }
    free(uses);
    live_free(&lv);
    cfg_free(&cfg);
    return done;
}

static int if_convert(MIRFunc *fn) {
    int changed = 0;
    for (int k = 0; k < 32; k++) {
        if (!if_convert_one(fn)) break;
        changed = 1;
    }
    return changed;
}


/* ======================================================================
 *  64-bit constants inside loops
 * ====================================================================== */

/* An immediate that doesn't fit in 32 bits has to be loaded into a register
   by the instruction that uses it - every iteration. Giving it its own MOV
   (flagged extra_int=1 so propagation leaves it alone) lets LICM lift the
   load out of the loop. */
static int fits_i32(int64_t v) { return v >= INT32_MIN && v <= INT32_MAX; }

static int materialize_consts(MIRFunc *fn) {
    int n = fn->count;
    int maxl = max_label_id(fn);
    if (maxl < 0) return 0;
    int *lpos = malloc((maxl + 1) * sizeof(int));
    for (int i = 0; i <= maxl; i++) lpos[i] = -1;
    for (int i = 0; i < n; i++) if (fn->instrs[i].op == MIR_LABEL) lpos[fn->instrs[i].dest.label_id] = i;
    unsigned char *in_loop = calloc(n + 1, 1);
    for (int j = 0; j < n; j++) {
        MIROp op = fn->instrs[j].op;
        if (op != MIR_JMP && op != MIR_JMP_IF && op != MIR_JMP_UNLESS) continue;
        int l = fn->instrs[j].dest.label_id;
        if (l < 0 || l > maxl || lpos[l] < 0 || lpos[l] > j) continue;
        for (int k = lpos[l]; k <= j; k++) in_loop[k] = 1;
    }
    free(lpos);
    int nv = max_vreg(fn);
    InstrBuf out = {0};
    int changed = 0;
    for (int i = 0; i < n; i++) {
        MIRInstr *ins = &fn->instrs[i];
        if (in_loop[i]) {
            int eligible = 0;
            switch (ins->op) {
            case MIR_ADD: case MIR_SUB: case MIR_MUL: case MIR_AND: case MIR_OR: case MIR_XOR:
            case MIR_CMP_EQ: case MIR_CMP_NE: case MIR_CMP_LT: case MIR_CMP_LE: case MIR_CMP_GT: case MIR_CMP_GE:
            case MIR_UCMP_LT: case MIR_UCMP_LE: case MIR_UCMP_GT: case MIR_UCMP_GE:
            case MIR_SMULH: case MIR_UMULH:
                eligible = 1; break;
            default: break;
            }
            if (eligible && ins->dest.kind == MIRV_VREG && !is_f(ins->dest.type)) {
                MIRValue *ops[2] = { &ins->src1, &ins->src2 };
                for (int k = 0; k < 2; k++) {
                    if (ops[k]->kind != MIRV_IMM_INT || fits_i32(ops[k]->imm_int)) continue;
                    MIRValue t = mir_vreg(nv++, MIR_I64);
                    MIRInstr m = mk(MIR_MOV, t, *ops[k], mir_none());
                    m.extra_int = 1;
                    ib_push(&out, m);
                    MIRType ty = ops[k]->type;
                    *ops[k] = t; ops[k]->type = ty;
                    changed = 1;
                }
            }
        }
        ib_push(&out, *ins);
    }
    free(in_loop);
    if (!changed) { free(out.v); return 0; }
    free(fn->instrs);
    fn->instrs = out.v; fn->count = out.n; fn->cap = out.cap;
    fn->vreg_count = nv;
    return 1;
}


/* ======================================================================
 *  SROA: scalar replacement of aggregate locals
 * ====================================================================== */

/* A by-value struct local lives in a run of pinned frame slots and is reached
   through `lea_local` pointers. If that pointer only ever feeds 8-byte loads,
   stores and constant-size memcpys (never escapes into a call, a compare, ...),
   every 8-byte field can live in its own vreg and the memory disappears -
   including the per-call memcpy of a struct passed by value. */

typedef struct {
    int base, lo, nslots;
    int escaped;
    int *fv;            /* field vreg per 8-byte offset, 0 = not created */
    unsigned char *fcls;/* 1 int, 2 float, 0 unknown */
    int used;
} SObj;

static int sroa(MIRFunc *fn) {
    if (!fn->local_pinned) return 0;
    int nl = fn->local_count;
    int nv = max_vreg(fn);
    if (nl == 0) return 0;
    unsigned char *pinned = calloc(nl + 1, 1);
    for (int i = 0; i < nl && i < fn->local_pinned_cap; i++) pinned[i] = fn->local_pinned[i];
    unsigned char *isbase = calloc(nl + 1, 1);
    int *lea_of = malloc((nv + 1) * sizeof(int));
    int *defs = calloc(nv + 1, sizeof(int));
    for (int v = 0; v <= nv; v++) lea_of[v] = -1;
    int nbases = 0;
    for (int i = 0; i < fn->count; i++) {
        MIRInstr *x = &fn->instrs[i];
        MIRValue *d = def_of(x);
        if (d) defs[d->vreg]++;
        if (x->op == MIR_LEA_LOCAL && x->dest.kind == MIRV_VREG && x->src1.kind == MIRV_LOCAL) {
            int id = x->src1.local_id;
            if (id >= 0 && id < nl && pinned[id]) { lea_of[x->dest.vreg] = id; if (!isbase[id]) { isbase[id] = 1; nbases++; } }
        }
    }
    if (!nbases) { free(pinned); free(isbase); free(lea_of); free(defs); return 0; }
    for (int v = 0; v <= nv; v++) if (lea_of[v] >= 0 && defs[v] != 1) lea_of[v] = -2;   /* multiply defined: unusable */
    SObj *ob = calloc(nl + 1, sizeof(SObj));
    for (int b = 0; b < nl; b++) {
        if (!isbase[b]) continue;
        int lo = b;
        while (lo - 1 >= 0 && pinned[lo - 1] && !isbase[lo - 1]) lo--;
        ob[b].base = b; ob[b].lo = lo; ob[b].nslots = b - lo + 1; ob[b].used = 1;
        ob[b].fv = calloc(ob[b].nslots, sizeof(int));
        ob[b].fcls = calloc(ob[b].nslots, 1);
    }
    for (int v = 0; v <= nv; v++) if (lea_of[v] == -2) {
        /* find the object it named (scan is cheap): mark escaped */
        for (int i = 0; i < fn->count; i++)
            if (fn->instrs[i].op == MIR_LEA_LOCAL && fn->instrs[i].dest.kind == MIRV_VREG &&
                fn->instrs[i].dest.vreg == v && fn->instrs[i].src1.kind == MIRV_LOCAL)
                ob[fn->instrs[i].src1.local_id].escaped = 1;
    }

    /* owner of a slot id: the object whose span contains it */
    int *owner = malloc((nl + 1) * sizeof(int));
    for (int i = 0; i <= nl; i++) owner[i] = -1;
    for (int b = 0; b < nl; b++) if (ob[b].used) for (int i = ob[b].lo; i <= b; i++) owner[i] = b;

#define FIELD_OK(o, off) ((off) >= 0 && (off) % 8 == 0 && (off) / 8 < ob[o].nslots)
    /* pass 1: escape analysis and field classes */
    for (int i = 0; i < fn->count; i++) {
        MIRInstr *x = &fn->instrs[i];
        if (x->op == MIR_NOP || x->op == MIR_LEA_LOCAL) continue;
        /* uses of lea vregs */
        MIRValue *addrs[3]; int na = 0;
        if (x->op == MIR_LOAD && x->src1.kind == MIRV_VREG) addrs[na++] = &x->src1;
        else if (x->op == MIR_STORE && x->dest.kind == MIRV_VREG) addrs[na++] = &x->dest;
        else if (x->op == MIR_MEMCPY) {
            if (x->dest.kind == MIRV_VREG) addrs[na++] = &x->dest;
            if (x->src1.kind == MIRV_VREG) addrs[na++] = &x->src1;
        }
        /* everything else that reads an lea vreg is an escape */
        {
            Shape sh = shape_of(x);
            MIRValue *us[8 + 64]; int nu = 0;
            if (sh.use_src1 && x->src1.kind == MIRV_VREG) us[nu++] = &x->src1;
            if (sh.use_src2 && x->src2.kind == MIRV_VREG) us[nu++] = &x->src2;
            if (sh.dest_is_use && x->dest.kind == MIRV_VREG) us[nu++] = &x->dest;
            for (int a = 0; a < x->arg_count && nu < 70; a++) if (x->args[a].kind == MIRV_VREG) us[nu++] = &x->args[a];
            for (int k = 0; k < nu; k++) {
                int v = us[k]->vreg;
                if (lea_of[v] == -1) continue;
                int is_addr = 0;
                for (int q = 0; q < na; q++) if (addrs[q] == us[k]) is_addr = 1;
                int o = lea_of[v] >= 0 ? lea_of[v] : -1;
                if (o < 0) continue;
                if (!is_addr) { ob[o].escaped = 1; continue; }
                if (x->op == MIR_LOAD) {
                    if (x->mem_scale || x->extra_int || !FIELD_OK(o, x->mem_offset) || mir_type_size(x->dest.type) != 8) ob[o].escaped = 1;
                    else { int f = x->mem_offset / 8; int c = is_f(x->dest.type) ? 2 : 1; if (!ob[o].fcls[f]) ob[o].fcls[f] = (unsigned char)c; }
                } else if (x->op == MIR_STORE) {
                    if (x->mem_scale || x->extra_int || !FIELD_OK(o, x->mem_offset) || mir_type_size(x->src1.type) != 8 ||
                        (x->src1.kind == MIRV_VREG && lea_of[x->src1.vreg] != -1)) ob[o].escaped = 1;
                    else { int f = x->mem_offset / 8; int c = is_f(x->src1.type) ? 2 : 1; if (!ob[o].fcls[f]) ob[o].fcls[f] = (unsigned char)c; }
                } else { /* memcpy */
                    if (x->src2.kind != MIRV_IMM_INT || x->src2.imm_int <= 0 || x->src2.imm_int % 8 != 0 ||
                        x->src2.imm_int > 8 * ob[o].nslots) ob[o].escaped = 1;
                }
            }
        }
        /* direct references to frame slots */
        MIRValue *vs[3] = { &x->dest, &x->src1, &x->src2 };
        for (int k = 0; k < 3; k++) {
            if (vs[k]->kind != MIRV_LOCAL) continue;
            int id = vs[k]->local_id;
            if (id < 0 || id >= nl || owner[id] < 0) continue;
            int o = owner[id];
            int off = 8 * (o - id);
            if ((x->op == MIR_LOAD && k == 1) && mir_type_size(x->dest.type) == 8 && !is_f(x->dest.type)) {
                if (!ob[o].fcls[off / 8]) ob[o].fcls[off / 8] = 1;
            } else if ((x->op == MIR_STORE && k == 0) && mir_type_size(x->src1.type) == 8 && !is_f(x->src1.type)) {
                if (!ob[o].fcls[off / 8]) ob[o].fcls[off / 8] = 1;
            } else ob[o].escaped = 1;     /* LEA_LOCAL of a non-base slot, calls with a local return buffer, ... */
        }
        for (int a = 0; a < x->arg_count; a++)
            if (x->args[a].kind == MIRV_LOCAL && x->args[a].local_id >= 0 && x->args[a].local_id < nl &&
                owner[x->args[a].local_id] >= 0) ob[owner[x->args[a].local_id]].escaped = 1;
    }
    /* LEA_LOCAL of a slot in the span that is not the base */
    for (int i = 0; i < fn->count; i++) {
        MIRInstr *x = &fn->instrs[i];
        if (x->op == MIR_LEA_LOCAL && x->src1.kind == MIRV_LOCAL && x->src1.local_id >= 0 && x->src1.local_id < nl) {
            int id = x->src1.local_id;
            if (owner[id] >= 0 && owner[id] != id) ob[owner[id]].escaped = 1;
        }
    }

    int any = 0;
    for (int b = 0; b < nl; b++) if (ob[b].used && !ob[b].escaped) any = 1;
    if (!any) goto done;

    {
    /* pass 2: rewrite */
    int nv2 = nv + 1;
    for (int b = 0; b < nl; b++) {
        if (!ob[b].used || ob[b].escaped) continue;
        for (int f = 0; f < ob[b].nslots; f++) { ob[b].fv[f] = nv2++; if (!ob[b].fcls[f]) ob[b].fcls[f] = 1; }
    }
    InstrBuf out = {0};
    /* entry init of every field (dead ones vanish in DCE) */
    for (int b = 0; b < nl; b++) {
        if (!ob[b].used || ob[b].escaped) continue;
        for (int f = 0; f < ob[b].nslots; f++) {
            MIRType t = ob[b].fcls[f] == 2 ? MIR_F64 : MIR_I64;
            MIRInstr m = mk(MIR_MOV, mir_vreg(ob[b].fv[f], t),
                            ob[b].fcls[f] == 2 ? mir_imm_float(0.0, t) : mir_imm_int(0, t), mir_none());
            ib_push(&out, m);
        }
    }
#define FLD_TYPE(o, f) (ob[o].fcls[f] == 2 ? MIR_F64 : MIR_I64)
    for (int i = 0; i < fn->count; i++) {
        MIRInstr *x = &fn->instrs[i];
        int o = -1, off = 0, handled = 0;
        if (x->op == MIR_LOAD && x->src1.kind == MIRV_VREG && lea_of[x->src1.vreg] >= 0 &&
            ob[lea_of[x->src1.vreg]].used && !ob[lea_of[x->src1.vreg]].escaped) { o = lea_of[x->src1.vreg]; off = x->mem_offset; }
        else if (x->op == MIR_LOAD && x->src1.kind == MIRV_LOCAL && x->src1.local_id >= 0 && x->src1.local_id < nl &&
                 owner[x->src1.local_id] >= 0 && !ob[owner[x->src1.local_id]].escaped) { o = owner[x->src1.local_id]; off = 8 * (o - x->src1.local_id); }
        if (o >= 0 && x->op == MIR_LOAD) {
            int f = off / 8;
            MIRValue fv = mir_vreg(ob[o].fv[f], FLD_TYPE(o, f));
            int same = is_f(x->dest.type) == (ob[o].fcls[f] == 2);
            MIRInstr m = mk(same ? MIR_MOV : MIR_BITCAST, x->dest, fv, mir_none());
            ib_push(&out, m); handled = 1;
        }
        if (!handled) {
            o = -1;
            if (x->op == MIR_STORE && x->dest.kind == MIRV_VREG && lea_of[x->dest.vreg] >= 0 &&
                ob[lea_of[x->dest.vreg]].used && !ob[lea_of[x->dest.vreg]].escaped) { o = lea_of[x->dest.vreg]; off = x->mem_offset; }
            else if (x->op == MIR_STORE && x->dest.kind == MIRV_LOCAL && x->dest.local_id >= 0 && x->dest.local_id < nl &&
                     owner[x->dest.local_id] >= 0 && !ob[owner[x->dest.local_id]].escaped) { o = owner[x->dest.local_id]; off = 8 * (o - x->dest.local_id); }
            if (o >= 0) {
                int f = off / 8;
                MIRValue fv = mir_vreg(ob[o].fv[f], FLD_TYPE(o, f));
                MIRValue v = x->src1;
                if (v.kind == MIRV_IMM_INT && ob[o].fcls[f] == 2) {
                    double dd; int64_t bits = v.imm_int; memcpy(&dd, &bits, 8);
                    ib_push(&out, mk(MIR_MOV, fv, mir_imm_float(dd, MIR_F64), mir_none()));
                } else if (v.kind == MIRV_IMM_FLOAT && ob[o].fcls[f] == 1) {
                    int64_t bits; double dd = v.imm_float; memcpy(&bits, &dd, 8);
                    ib_push(&out, mk(MIR_MOV, fv, mir_imm_int(bits, MIR_I64), mir_none()));
                } else {
                    int same = is_f(v.type) == (ob[o].fcls[f] == 2);
                    ib_push(&out, mk(same ? MIR_MOV : MIR_BITCAST, fv, v, mir_none()));
                }
                handled = 1;
            }
        }
        if (!handled && x->op == MIR_MEMCPY && x->src2.kind == MIRV_IMM_INT) {
            int od = (x->dest.kind == MIRV_VREG && lea_of[x->dest.vreg] >= 0 && ob[lea_of[x->dest.vreg]].used &&
                      !ob[lea_of[x->dest.vreg]].escaped) ? lea_of[x->dest.vreg] : -1;
            int os = (x->src1.kind == MIRV_VREG && lea_of[x->src1.vreg] >= 0 && ob[lea_of[x->src1.vreg]].used &&
                      !ob[lea_of[x->src1.vreg]].escaped) ? lea_of[x->src1.vreg] : -1;
            if (od >= 0 || os >= 0) {
                int words = (int)(x->src2.imm_int / 8);
                for (int f = 0; f < words; f++) {
                    if (od >= 0 && os >= 0) {
                        int cd = ob[od].fcls[f] == 2, cs = ob[os].fcls[f] == 2;
                        ib_push(&out, mk(cd == cs ? MIR_MOV : MIR_BITCAST,
                                         mir_vreg(ob[od].fv[f], FLD_TYPE(od, f)), mir_vreg(ob[os].fv[f], FLD_TYPE(os, f)), mir_none()));
                    } else if (od >= 0) {          /* load from the other pointer into the field */
                        MIRInstr m = mk(MIR_LOAD, mir_vreg(ob[od].fv[f], FLD_TYPE(od, f)), x->src1, mir_none());
                        m.mem_offset = 8 * f;
                        ib_push(&out, m);
                    } else {                        /* store the field through the other pointer */
                        MIRInstr m = mk(MIR_STORE, x->dest, mir_vreg(ob[os].fv[f], FLD_TYPE(os, f)), mir_none());
                        m.mem_offset = 8 * f;
                        ib_push(&out, m);
                    }
                }
                handled = 1;
            }
        }
        if (!handled) ib_push(&out, copy_instr(x));
        else if (x->args) free(x->args);
    }
#undef FLD_TYPE
    free(fn->instrs);
    fn->instrs = out.v; fn->count = out.n; fn->cap = out.cap;
    fn->vreg_count = nv2;
    }
    for (int b = 0; b < nl; b++) { free(ob[b].fv); free(ob[b].fcls); }
    free(ob); free(owner); free(pinned); free(isbase); free(lea_of); free(defs);
    return 1;
done:
    for (int b = 0; b < nl; b++) { free(ob[b].fv); free(ob[b].fcls); }
    free(ob); free(owner); free(pinned); free(isbase); free(lea_of); free(defs);
    return 0;
#undef FIELD_OK
}

/* ======================================================================
 *  Loops: rotation and invariant code motion
 * ====================================================================== */

static int max_label_id(const MIRFunc *fn) {
    int m = -1;
    for (int i = 0; i < fn->count; i++)
        if ((fn->instrs[i].op == MIR_LABEL || fn->instrs[i].op == MIR_JMP ||
             fn->instrs[i].op == MIR_JMP_IF || fn->instrs[i].op == MIR_JMP_UNLESS) &&
            fn->instrs[i].dest.label_id > m)
            m = fn->instrs[i].dest.label_id;
    return m;
}

/* while-loop -> do-while:
       H:  test; jcc X          H:  test; jcc X
       F:  body                 F:  body
           jmp H         =>         test'; jcc' F; jmp X
   The test is duplicated at the bottom, so a running loop takes one branch
   per iteration instead of a conditional plus an unconditional jump. */
static int rotate_loops(MIRFunc *fn) {
    int n = fn->count;
    int maxl = max_label_id(fn);
    if (maxl < 0) return 0;
    int *lpos = malloc((maxl + 1) * sizeof(int));
    for (int i = 0; i <= maxl; i++) lpos[i] = -1;
    for (int i = 0; i < n; i++) if (fn->instrs[i].op == MIR_LABEL) lpos[fn->instrs[i].dest.label_id] = i;

    typedef struct { int j, h, bj, lf; } Site;
    Site *sites = malloc((n + 1) * sizeof(Site)); int ns = 0;
    int *hdr_lf = malloc((n + 1) * sizeof(int));     /* header index -> its fallthrough label, or -1 */
    int *hdr_bj = malloc((n + 1) * sizeof(int));
    for (int i = 0; i <= n; i++) { hdr_lf[i] = -1; hdr_bj[i] = -1; }
    int next_label = maxl + 1;

    for (int j = 0; j < n; j++) {
        MIRInstr *jm = &fn->instrs[j];
        if (jm->op != MIR_JMP) continue;
        int L = jm->dest.label_id;
        if (L < 0 || L > maxl || lpos[L] < 0 || lpos[L] >= j) continue;
        int h = lpos[L];
        /* header region: straight-line code ending in a conditional branch */
        int bj = -1, cost = 0;
        for (int k = h + 1; k < j && cost <= 10; k++) {
            MIROp op = fn->instrs[k].op;
            if (op == MIR_NOP) continue;
            if (op == MIR_JMP_IF || op == MIR_JMP_UNLESS) { bj = k; break; }
            if (op == MIR_LABEL || is_terminator(op) || op == MIR_CALL || op == MIR_SYSCALL) break;
            cost++;
        }
        if (bj < 0) continue;
        int X = fn->instrs[bj].dest.label_id;
        if (X < 0 || X > maxl || lpos[X] < 0 || lpos[X] <= j) continue;   /* must exit past the latch */
        if (hdr_lf[h] < 0) { hdr_lf[h] = next_label++; hdr_bj[h] = bj; }
        sites[ns].j = j; sites[ns].h = h; sites[ns].bj = bj; sites[ns].lf = hdr_lf[h];
        ns++;
    }
    if (!ns) { free(lpos); free(sites); free(hdr_lf); free(hdr_bj); return 0; }

    int *site_of = malloc((n + 1) * sizeof(int));
    for (int i = 0; i <= n; i++) site_of[i] = -1;
    for (int k = 0; k < ns; k++) site_of[sites[k].j] = k;
    int *lf_after = malloc((n + 2) * sizeof(int));    /* instr index -> fallthrough label to place before it */
    for (int i = 0; i <= n + 1; i++) lf_after[i] = -1;
    for (int h = 0; h < n; h++) if (hdr_lf[h] >= 0) lf_after[hdr_bj[h] + 1] = hdr_lf[h];

    InstrBuf out = {0};
    for (int i = 0; i < n; i++) {
        if (lf_after[i] >= 0) {
            MIRInstr lab; memset(&lab, 0, sizeof lab);
            lab.op = MIR_LABEL; lab.dest = mir_label(lf_after[i]);
            lab.src1 = mir_none(); lab.src2 = mir_none();
            ib_push(&out, lab);
        }
        if (site_of[i] >= 0) {
            Site *st = &sites[site_of[i]];
            for (int k = st->h + 1; k < st->bj; k++)
                if (fn->instrs[k].op != MIR_NOP) ib_push(&out, copy_instr(&fn->instrs[k]));
            MIRInstr br = fn->instrs[st->bj];
            br.op = br.op == MIR_JMP_IF ? MIR_JMP_UNLESS : MIR_JMP_IF;
            br.dest = mir_label(st->lf);
            br.args = NULL; br.arg_count = 0;
            ib_push(&out, br);
            MIRInstr ex; memset(&ex, 0, sizeof ex);
            ex.op = MIR_JMP; ex.dest = fn->instrs[st->bj].dest; ex.src1 = mir_none(); ex.src2 = mir_none();
            ib_push(&out, ex);
            /* the original `jmp H` is replaced */
            if (fn->instrs[i].args) { free(fn->instrs[i].args); fn->instrs[i].args = NULL; }
            continue;
        }
        ib_push(&out, fn->instrs[i]);
    }
    free(fn->instrs);
    fn->instrs = out.v; fn->count = out.n; fn->cap = out.cap;
    free(lpos); free(sites); free(hdr_lf); free(hdr_bj); free(site_of); free(lf_after);
    return 1;
}

/* ---- dominators / natural loops ---- */

typedef struct {
    Cfg cfg;
    Word *dom;          /* dom[b]: blocks dominating b */
    int valid;          /* the dominator iteration converged */
    int W;
    int *pred_start, *pred_list;
} Dom;

static Dom dom_build(MIRFunc *fn) {
    Dom d; memset(&d, 0, sizeof d);
    d.cfg = build_cfg(fn);
    int nb = d.cfg.n;
    d.W = BS_WORDS(nb > 0 ? nb : 1);
    if ((size_t)nb * d.W > BITSET_BUDGET) {          /* too big for the O(n^2) dominator sets */
        d.dom = NULL; d.pred_start = NULL; d.pred_list = NULL; d.valid = 0;
        return d;
    }
    d.dom = malloc((size_t)(nb > 0 ? nb : 1) * d.W * sizeof(Word));
    for (int b = 0; b < nb; b++) {
        Word *db = d.dom + (size_t)b * d.W;
        if (b == 0) { memset(db, 0, d.W * sizeof(Word)); bs_set(db, 0); }
        else memset(db, 0xff, d.W * sizeof(Word));
    }
    int *pc = calloc(nb + 1, sizeof(int));
    for (int b = 0; b < nb; b++) for (int q = 0; q < d.cfg.b[b].nsucc; q++) pc[d.cfg.b[b].succ[q]]++;
    d.pred_start = malloc((nb + 2) * sizeof(int));
    d.pred_start[0] = 0;
    for (int b = 0; b < nb; b++) d.pred_start[b + 1] = d.pred_start[b] + pc[b];
    d.pred_list = malloc((d.pred_start[nb] + 1) * sizeof(int));
    memset(pc, 0, (nb + 1) * sizeof(int));
    for (int b = 0; b < nb; b++)
        for (int q = 0; q < d.cfg.b[b].nsucc; q++) {
            int t = d.cfg.b[b].succ[q];
            d.pred_list[d.pred_start[t] + pc[t]++] = b;
        }
    free(pc);
    int changed = 1, it = 0;
    Word *tmp = malloc(d.W * sizeof(Word));
    while (changed && it++ < 100000) {
        changed = 0;
        for (int b = 1; b < nb; b++) {
            if (d.pred_start[b + 1] == d.pred_start[b]) continue;      /* unreachable */
            for (int w = 0; w < d.W; w++) tmp[w] = ~(Word)0;
            for (int q = d.pred_start[b]; q < d.pred_start[b + 1]; q++) {
                Word *dp = d.dom + (size_t)d.pred_list[q] * d.W;
                for (int w = 0; w < d.W; w++) tmp[w] &= dp[w];
            }
            bs_set(tmp, b);
            Word *db = d.dom + (size_t)b * d.W;
            for (int w = 0; w < d.W; w++) if (db[w] != tmp[w]) { db[w] = tmp[w]; changed = 1; }
        }
    }
    free(tmp);
    d.valid = !changed;
    return d;
}

static void dom_free(Dom *d) { cfg_free(&d->cfg); free(d->dom); free(d->pred_start); free(d->pred_list); }

typedef struct { int header; Word *body; int nbody; } Loop;

static int find_loops(Dom *d, Loop **out) {
    int nb = d->cfg.n, W = d->W;
    Loop *loops = calloc(nb + 1, sizeof(Loop)); int nl = 0;
    int *stack = malloc((nb + 1) * sizeof(int));
    for (int b = 0; b < nb; b++) {
        for (int q = 0; q < d->cfg.b[b].nsucc; q++) {
            int h = d->cfg.b[b].succ[q];
            if (!bs_get(d->dom + (size_t)b * W, h)) continue;      /* not a back edge */
            Loop *lp = NULL;
            for (int k = 0; k < nl; k++) if (loops[k].header == h) lp = &loops[k];
            if (!lp) { lp = &loops[nl++]; lp->header = h; lp->body = calloc(W, sizeof(Word)); bs_set(lp->body, h); }
            int sp = 0;
            if (!bs_get(lp->body, b)) { bs_set(lp->body, b); stack[sp++] = b; }
            while (sp) {
                int x = stack[--sp];
                for (int pq = d->pred_start[x]; pq < d->pred_start[x + 1]; pq++) {
                    int pr = d->pred_list[pq];
                    if (!bs_get(lp->body, pr)) { bs_set(lp->body, pr); stack[sp++] = pr; }
                }
            }
        }
    }
    free(stack);
    for (int k = 0; k < nl; k++) {
        int c = 0;
        for (int b = 0; b < nb; b++) if (bs_get(loops[k].body, b)) c++;
        loops[k].nbody = c;
    }
    *out = loops;
    return nl;
}

static int licm_op_ok(const MIRInstr *ins) {
    if (ins->op == MIR_MOV) return ins->extra_int == 1 && ins->src1.kind == MIRV_IMM_INT;
    switch (ins->op) {
    case MIR_ADD: case MIR_SUB: case MIR_MUL: case MIR_NEG: case MIR_NOT:
    case MIR_AND: case MIR_OR: case MIR_XOR: case MIR_SHL: case MIR_SHR_A: case MIR_SHR_L:
    case MIR_FADD: case MIR_FSUB: case MIR_FMUL: case MIR_FDIV: case MIR_FNEG:
    case MIR_CMP_EQ: case MIR_CMP_NE: case MIR_CMP_LT: case MIR_CMP_LE:
    case MIR_CMP_GT: case MIR_CMP_GE: case MIR_UCMP_LT: case MIR_UCMP_LE:
    case MIR_UCMP_GT: case MIR_UCMP_GE:
    case MIR_FCMP_LT: case MIR_FCMP_LE: case MIR_FCMP_GT: case MIR_FCMP_GE:
    case MIR_FCMP_EQ: case MIR_FCMP_NE:
    case MIR_SEXT: case MIR_ZEXT: case MIR_TRUNC: case MIR_I2F: case MIR_F2I:
    case MIR_F2F: case MIR_BITCAST: case MIR_LEA_GLOBAL: case MIR_LEA_LOCAL:
    case MIR_ARR_LEN: case MIR_ARR_DATA: case MIR_SMULH: case MIR_UMULH:
        return 1;
    case MIR_SDIV: case MIR_UDIV: case MIR_SMOD: case MIR_UMOD:
        /* only a constant, nonzero divisor can't trap */
        return ins->src2.kind == MIRV_IMM_INT && ins->src2.imm_int != 0 &&
               !(ins->src2.imm_int == -1 && (ins->op == MIR_SDIV || ins->op == MIR_SMOD));
    default:
        return 0;
    }
}

typedef struct { int *def_in_loop; int vreg_hit; } LicmCtx;

static int licm_loop(MIRFunc *fn, Dom *d, Loop *lp, const Live *lv) {
    int nv = lv->nv;
    int W = d->W;
    int h = lp->header;
    Cfg *cfg = &d->cfg;
    int hstart = cfg->b[h].start;
    if (fn->instrs[hstart].op != MIR_LABEL && hstart != 0) return 0;

    /* a fall-through into the header from inside the loop makes the layout
       unsuitable for a preheader */
    if (h > 0 && bs_get(lp->body, h - 1)) {
        const MIRInstr *last = &fn->instrs[cfg->b[h - 1].end - 1];
        if (!is_terminator(last->op) || last->op == MIR_JMP_IF || last->op == MIR_JMP_UNLESS) return 0;
    }

    int *defs = calloc(nv, sizeof(int));
    int has_call = 0, store_any = 0, store_hdr = 0, store_elem = 0;
    for (int b = 0; b < cfg->n; b++) {
        if (!bs_get(lp->body, b)) continue;
        for (int i = cfg->b[b].start; i < cfg->b[b].end; i++) {
            MIRInstr *ins = &fn->instrs[i];
            MIRValue *dv = def_of(ins);
            if (dv) defs[dv->vreg]++;
            switch (ins->op) {
            case MIR_CALL: case MIR_SYSCALL: case MIR_MEMSET: case MIR_MEMCPY: case MIR_ASM_TEXT:
            case MIR_ASM_RAW: case MIR_OUTB: case MIR_OUTW: case MIR_HLT: case MIR_STI: case MIR_CLI:
            case MIR_INB: case MIR_INW:
                has_call = 1; break;
            case MIR_STORE:
                if (ins->mem_cls == MIR_MEM_ARRAY_ELEM) store_elem = 1;
                else if (ins->mem_cls == MIR_MEM_ARRAY_HDR) store_hdr = 1;
                else store_any = 1;
                break;
            default: break;
            }
        }
    }
    (void)store_elem;
    int mem_ok_hdr = !has_call && !store_hdr && !store_any;

    unsigned char *hoist = calloc(fn->count + 1, 1);
    unsigned char *inv = calloc(nv, 1);
    const Word *live_h = lv->live_in + (size_t)h * lv->words;
    int nhoist = 0, progress = 1;
    while (progress) {
        progress = 0;
        for (int b = 0; b < cfg->n; b++) {
            if (!bs_get(lp->body, b)) continue;
            for (int i = cfg->b[b].start; i < cfg->b[b].end; i++) {
                if (hoist[i]) continue;
                MIRInstr *ins = &fn->instrs[i];
                if (!licm_op_ok(ins)) continue;
                MIRValue *dv = def_of(ins);
                if (!dv) continue;
                if (defs[dv->vreg] != 1 || bs_get(live_h, dv->vreg)) continue;
                if ((ins->op == MIR_ARR_LEN || ins->op == MIR_ARR_DATA) && !mem_ok_hdr) continue;
                /* every operand invariant */
                int ok = 1;
                Shape sh = shape_of(ins);
                const MIRValue *ops[3] = { sh.use_src1 ? &ins->src1 : NULL, sh.use_src2 ? &ins->src2 : NULL, NULL };
                for (int k = 0; k < 2 && ok; k++) {
                    if (!ops[k] || ops[k]->kind != MIRV_VREG) continue;
                    int v = ops[k]->vreg;
                    if (defs[v] != 0 && !inv[v]) ok = 0;
                }
                if (!ok) continue;
                hoist[i] = 1; inv[dv->vreg] = 1; nhoist++; progress = 1;
            }
        }
    }
    (void)W;
    if (!nhoist) { free(defs); free(hoist); free(inv); return 0; }

    /* build the new instruction stream: preheader code goes right before the
       header's label, outside jumps to the header are retargeted to it */
    int hl = fn->instrs[hstart].op == MIR_LABEL ? fn->instrs[hstart].dest.label_id : -1;
    int pre = max_label_id(fn) + 1;
    InstrBuf out = {0};
    for (int i = 0; i < fn->count; i++) {
        if (i == hstart) {
            MIRInstr lab; memset(&lab, 0, sizeof lab);
            lab.op = MIR_LABEL; lab.dest = mir_label(pre); lab.src1 = mir_none(); lab.src2 = mir_none();
            ib_push(&out, lab);
            for (int k = 0; k < fn->count; k++) if (hoist[k]) ib_push(&out, fn->instrs[k]);
        }
        MIRInstr ins = fn->instrs[i];
        if (hoist[i]) continue;                   /* moved */
        if (hl >= 0 && (ins.op == MIR_JMP || ins.op == MIR_JMP_IF || ins.op == MIR_JMP_UNLESS) &&
            ins.dest.label_id == hl && !bs_get(lp->body, d->cfg.ib[i]))
            ins.dest = mir_label(pre);
        ib_push(&out, ins);
    }
    free(fn->instrs);
    fn->instrs = out.v; fn->count = out.n; fn->cap = out.cap;
    free(defs); free(hoist); free(inv);
    return 1;
}

static int licm(MIRFunc *fn) {
    int changed = 0;
    for (int round = 0; round < 12; round++) {
        Dom d = dom_build(fn);
        Loop *loops = NULL;
        int nl = d.valid ? find_loops(&d, &loops) : 0;
        int did = 0;
        if (nl && (size_t)d.cfg.n * BS_WORDS(max_vreg(fn)) > BITSET_BUDGET) nl = 0;
        if (nl) {
            int nv = max_vreg(fn);
            Live lv = compute_live(fn, &d.cfg, nv);
            /* innermost first */
            int *order = malloc(nl * sizeof(int));
            for (int k = 0; k < nl; k++) order[k] = k;
            for (int a = 0; a < nl; a++)
                for (int b2 = a + 1; b2 < nl; b2++)
                    if (loops[order[b2]].nbody < loops[order[a]].nbody) { int t = order[a]; order[a] = order[b2]; order[b2] = t; }
            for (int k = 0; k < nl && !did; k++) did = licm_loop(fn, &d, &loops[order[k]], &lv);
            free(order);
            live_free(&lv);
        }
        for (int k = 0; k < nl; k++) free(loops[k].body);
        free(loops);
        dom_free(&d);
        if (!did) break;
        changed = 1;
    }
    return changed;
}

/* ======================================================================
 *  Driver
 * ====================================================================== */

static void scalar_fixpoint(MIRFunc *fn) {
    for (int iter = 0; iter < 8; iter++) {
        int ch = 0;
        if (!pass_off("prop")) { ch |= global_prop(fn); verify_after(fn, "global_prop"); }
        if (!pass_off("copyprop")) { ch |= copy_prop_df(fn); verify_after(fn, "copy_prop_df"); }
        if (!pass_off("prop")) { ch |= local_pass(fn, !pass_off("cse")); verify_after(fn, "local_pass"); }
        if (!pass_off("addr")) { ch |= fold_addr(fn); verify_after(fn, "fold_addr"); }
        if (!pass_off("gcse")) { ch |= gcse(fn); verify_after(fn, "gcse"); }
        if (!pass_off("cfg")) { ch |= cleanup_cfg(fn); verify_after(fn, "cleanup_cfg"); }
        if (!pass_off("prop")) { ch |= fold_movs(fn); verify_after(fn, "fold_movs"); }
        if (!pass_off("dce")) { ch |= dce(fn); verify_after(fn, "dce"); }
        if (!ch) break;
    }
}

void hyopt_func(MIRFunc *fn) {
    if (fn->is_naked || fn->count == 0) return;
    const char *lv = getenv("HYLIAN_OPT");
    if (lv && lv[0] == '0') return;
    if (has_asm(fn)) return;

    if (!pass_off("cfg")) { cleanup_cfg(fn); verify_after(fn, "cleanup_cfg(entry)"); }
    if (!pass_off("mem2reg")) { mem2reg(fn); verify_after(fn, "mem2reg"); }
    scalar_fixpoint(fn);

    if (!pass_off("sroa") && sroa(fn)) { verify_after(fn, "sroa"); scalar_fixpoint(fn); }
    if (!pass_off("div") && expand_div(fn)) { verify_after(fn, "expand_div"); scalar_fixpoint(fn); }
    if (!pass_off("ifcvt") && if_convert(fn)) { verify_after(fn, "if_convert"); scalar_fixpoint(fn); }
    if (!pass_off("loops") && rotate_loops(fn)) { verify_after(fn, "rotate_loops"); scalar_fixpoint(fn); }
    if (!pass_off("licm")) {
        materialize_consts(fn);
        verify_after(fn, "materialize_consts");
        if (licm(fn)) { verify_after(fn, "licm"); scalar_fixpoint(fn); }
    }

    if (fn->count == 0 || !is_terminator(fn->instrs[fn->count - 1].op)) {
        MIRInstr *r = mir_emit(fn, MIR_RET);
        (void)r;
    }
}

/* ======================================================================
 *  Inlining (within one module)
 * ====================================================================== */

typedef struct {
    int size;          /* non-NOP instructions */
    int can_inline;    /* eligible as a callee */
    int nv;            /* vreg id bound */
    int maxlabel;
    int recursive;
} FnInfo;

#define INLINE_MAX_SIZE 28
#define CALLER_GROWTH_CAP 4000

static int fn_operand_locals(const MIRFunc *fn) {
    for (int i = 0; i < fn->count; i++) {
        const MIRInstr *ins = &fn->instrs[i];
        if (ins->dest.kind == MIRV_LOCAL || ins->src1.kind == MIRV_LOCAL || ins->src2.kind == MIRV_LOCAL) return 1;
        for (int a = 0; a < ins->arg_count; a++) if (ins->args[a].kind == MIRV_LOCAL) return 1;
    }
    return 0;
}

static void fn_info(MIRFunc *fn, FnInfo *fi) {
    memset(fi, 0, sizeof *fi);
    fi->nv = max_vreg(fn);
    fi->maxlabel = max_label_id(fn);
    int size = 0, ok = !fn->is_naked && !has_asm(fn) && fn->count > 0;
    for (int i = 0; i < fn->count; i++) {
        const MIRInstr *ins = &fn->instrs[i];
        if (ins->op == MIR_NOP) continue;
        size++;
        if (ins->op == MIR_CALL && (ins->ret_aggr || ins->arg_aggr)) ok = 0;
        if (ins->op == MIR_ALLOCA_LOCAL) ok = 0;    /* keep frames of inlined copies from piling up */
        if (ins->op == MIR_GETPARAM && fn->param_types && ins->extra_int < fn->param_count &&
            is_f(fn->param_types[ins->extra_int])) ok = 0;    /* float params: keep them simple */
    }
    if (fn_operand_locals(fn)) ok = 0;
    fi->size = size;
    fi->can_inline = ok && size <= INLINE_MAX_SIZE;
}

static int find_func(const MIRModule *mod, const char *name) {
    if (!name) return -1;
    for (int i = 0; i < mod->func_count; i++)
        if (strcmp(mod->funcs[i]->name, name) == 0) return i;
    return -1;
}

static void renumber_operand(MIRValue *v, int base_v) {
    if (v->kind == MIRV_VREG) v->vreg += base_v;
}

/* expand the calls in `caller` whose target is an inlinable function */
static int inline_into(MIRModule *mod, MIRFunc *caller, FnInfo *info, MIRFunc *self_snap, FnInfo *self_info) {
    if (caller->is_naked || has_asm(caller)) return 0;
    int n = caller->count;
    int nv = max_vreg(caller);
    int nextl = max_label_id(caller) + 1;
    int budget = CALLER_GROWTH_CAP;
    InstrBuf out = {0};
    int changed = 0;

    for (int i = 0; i < n; i++) {
        MIRInstr *call = &caller->instrs[i];
        int fi = -1;
        if (call->op == MIR_CALL && call->callee && !call->ret_aggr && !call->arg_aggr) {
            fi = find_func(mod, call->callee);
            if (self_snap && fi >= 0 && mod->funcs[fi] == caller) fi = -2;   /* self call: use the snapshot */
            if (fi == -2 || fi >= 0) {
                MIRFunc *callee = fi == -2 ? self_snap : mod->funcs[fi];
                FnInfo *ci = fi == -2 ? self_info : &info[fi];
                if ((fi >= 0 && (callee == caller || info[fi].recursive)) || !ci->can_inline ||
                    call->arg_count != callee->param_count || ci->size > budget) fi = -1;
                else {
                    /* every argument/result must agree with the callee's classes */
                    for (int k = 0; k < callee->count && fi != -1; k++) {
                        MIRInstr *ci = &callee->instrs[k];
                        if (ci->op == MIR_GETPARAM) {
                            int pi = ci->extra_int;
                            if (pi >= call->arg_count || call->args[pi].kind == MIRV_LABEL ||
                                is_f(call->args[pi].type) != is_f(ci->dest.type)) fi = -1;
                        } else if (ci->op == MIR_RET && call->dest.kind == MIRV_VREG) {
                            if (is_f(ci->src1.type) != is_f(call->dest.type)) fi = -1;
                        }
                    }
                }
            }
        }
        if (fi == -1) { ib_push(&out, copy_instr(call)); continue; }

        MIRFunc *callee = fi == -2 ? self_snap : mod->funcs[fi];
        FnInfo *cinfo = fi == -2 ? self_info : &info[fi];
        int base_v = nv, base_l = nextl;
        nv += cinfo->nv + 1;
        nextl += cinfo->maxlabel + 3;
        int l_end = base_l + cinfo->maxlabel + 1;
        budget -= cinfo->size;
        /* the last real instruction of the callee, to elide a needless jump */
        int last = callee->count - 1;
        while (last >= 0 && callee->instrs[last].op == MIR_NOP) last--;
        for (int k = 0; k < callee->count; k++) {
            MIRInstr *ci = &callee->instrs[k];
            if (ci->op == MIR_NOP) continue;
            if (ci->op == MIR_GETPARAM) {
                MIRInstr m = mk(MIR_MOV, mir_vreg(ci->dest.vreg + base_v, ci->dest.type),
                                call->args[ci->extra_int], mir_none());
                ib_push(&out, m);
                continue;
            }
            if (ci->op == MIR_RET) {
                if (call->dest.kind == MIRV_VREG) {
                    MIRValue rv = ci->src1;
                    renumber_operand(&rv, base_v);
                    MIRInstr m = mk(MIR_MOV, call->dest, rv, mir_none());
                    ib_push(&out, m);
                }
                if (k != last) {
                    MIRInstr j = mk(MIR_JMP, mir_label(l_end), mir_none(), mir_none());
                    ib_push(&out, j);
                }
                continue;
            }
            MIRInstr c = copy_instr(ci);
            renumber_operand(&c.dest, base_v);
            renumber_operand(&c.src1, base_v);
            renumber_operand(&c.src2, base_v);
            for (int a = 0; a < c.arg_count; a++) renumber_operand(&c.args[a], base_v);
            if (c.op == MIR_LABEL || c.op == MIR_JMP || c.op == MIR_JMP_IF || c.op == MIR_JMP_UNLESS)
                c.dest.label_id += base_l;
            ib_push(&out, c);
        }
        MIRInstr lab = mk(MIR_LABEL, mir_label(l_end), mir_none(), mir_none());
        ib_push(&out, lab);
        changed = 1;
    }
    if (!changed) {
        for (int i = 0; i < out.n; i++) if (out.v[i].args) free(out.v[i].args);
        free(out.v);
        return 0;
    }
    for (int i = 0; i < caller->count; i++) if (caller->instrs[i].args) free(caller->instrs[i].args);
    free(caller->instrs);
    caller->instrs = out.v; caller->count = out.n; caller->cap = out.cap;
    caller->vreg_count = nv;
    return 1;
}

/* DFS over the module's call graph: callees before callers, cycle members flagged */
typedef struct { const MIRModule *mod; int *color; int *order; int norder; FnInfo *info; } CgCtx;
static void cg_visit(CgCtx *c, int f) {
    c->color[f] = 1;
    const MIRFunc *fn = c->mod->funcs[f];
    for (int i = 0; i < fn->count; i++) {
        const MIRInstr *ins = &fn->instrs[i];
        if (ins->op != MIR_CALL || !ins->callee) continue;
        int g = find_func(c->mod, ins->callee);
        if (g < 0) continue;
        if (c->color[g] == 1) {              /* back edge: g..f is a cycle */
            c->info[g].recursive = 1; c->info[f].recursive = 1;
        } else if (c->color[g] == 0) {
            cg_visit(c, g);
        }
        if (g == f) c->info[f].recursive = 1;
    }
    c->color[f] = 2;
    c->order[c->norder++] = f;
}

void hyopt_module(MIRModule *mod) {
    const char *lv = getenv("HYLIAN_OPT");
    if (lv && lv[0] == '0') return;
    int nf = mod->func_count;
    if (nf == 0) return;
    FnInfo *info = calloc(nf, sizeof(FnInfo));
    if (pass_off("inline")) {
        for (int i = 0; i < nf; i++) hyopt_func(mod->funcs[i]);
        free(info);
        return;
    }
    int *color = calloc(nf, sizeof(int)), *order = malloc(nf * sizeof(int));
    CgCtx cg = { mod, color, order, 0, info };
    for (int f = 0; f < nf; f++) if (!color[f]) cg_visit(&cg, f);
    for (int k = 0; k < cg.norder; k++) {
        int f = order[k];
        MIRFunc *fn = mod->funcs[f];
        int rec = info[f].recursive;
        inline_into(mod, fn, info, NULL, NULL);
        hyopt_func(fn);
        fn_info(fn, &info[f]);
        info[f].recursive = rec;
        /* a small self-recursive function is inlined into itself once (the way gcc
           turns naive fib into a two-levels-per-call loop): about half the calls */
        for (int round = 0; round < 2 && rec && !pass_off("selfinline") &&
                            (round == 0 ? info[f].can_inline : info[f].size <= 40); round++) {
            int selfcalls = 0;
            for (int i = 0; i < fn->count; i++)
                if (fn->instrs[i].op == MIR_CALL && fn->instrs[i].callee && strcmp(fn->instrs[i].callee, fn->name) == 0)
                    selfcalls++;
            if (selfcalls == 0) break;
            {
                MIRFunc snap = *fn;
                snap.instrs = malloc((fn->count + 1) * sizeof(MIRInstr));
                for (int i = 0; i < fn->count; i++) snap.instrs[i] = copy_instr(&fn->instrs[i]);
                snap.cap = fn->count;
                FnInfo sinfo = info[f];
                if (inline_into(mod, fn, info, &snap, &sinfo)) {
                    hyopt_func(fn);
                    fn_info(fn, &info[f]);
                    info[f].recursive = rec;
                }
                for (int i = 0; i < snap.count; i++) free(snap.instrs[i].args);
                free(snap.instrs);
            }
        }
    }
    /* calls to functions defined in this module are ordinary Hylian calls: the
       callee is never variadic, so the caller needn't set %al */
    for (int f = 0; f < nf; f++) {
        MIRFunc *fn = mod->funcs[f];
        for (int i = 0; i < fn->count; i++) {
            MIRInstr *ins = &fn->instrs[i];
            if (ins->op == MIR_CALL && ins->callee && find_func(mod, ins->callee) >= 0) ins->extra_int |= 2;
        }
    }
    free(color); free(order); free(info);
}
