#include "hyregalloc.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- vreg id bookkeeping: don't trust fn->vreg_count, scan for real max
   (frontends may allocate temp ids without always routing through
   mir_new_vreg, so this library defends itself rather than assuming) ---- */

static void mark_vreg(int *max_id, const MIRValue *v) {
    if (v->kind == MIRV_VREG && v->vreg > *max_id) *max_id = v->vreg;
}

static int scan_max_vreg(const MIRFunc *fn) {
    int max_id = -1;
    for (int i = 0; i < fn->count; i++) {
        const MIRInstr *ins = &fn->instrs[i];
        mark_vreg(&max_id, &ins->dest);
        mark_vreg(&max_id, &ins->src1);
        mark_vreg(&max_id, &ins->src2);
        for (int a = 0; a < ins->arg_count; a++) mark_vreg(&max_id, &ins->args[a]);
    }
    return max_id + 1;
}

/* ---- per-opcode def/use classification ----
   dest isn't always a "write": MIR_STORE reuses dest to hold the address
   operand (a read), so this can't be a blind "dest=def, srcs=use" rule. */

typedef struct {
    int has_def;      /* dest is a write */
    int dest_is_use;  /* dest is actually a read (e.g. STORE's address) */
    int use_src1, use_src2;
} OpShape;

static OpShape op_shape(const MIRInstr *ins) {
    OpShape s = {1, 0, 1, 1}; /* default: dest=def, src1/src2=use */
    switch (ins->op) {
    case MIR_STORE:
        /* src2 is the index of a scaled address, a read only when mem_scale is set */
        s.has_def = 0; s.dest_is_use = 1; s.use_src1 = 1; s.use_src2 = ins->mem_scale != 0;
        break;
    case MIR_LABEL: case MIR_JMP:
        s.has_def = 0; s.use_src1 = 0; s.use_src2 = 0;
        break;
    case MIR_JMP_IF: case MIR_JMP_UNLESS:
        s.has_def = 0; s.use_src1 = 1; s.use_src2 = 0;
        break;
    case MIR_CALL:
        s.use_src1 = 1; s.use_src2 = 0; /* args[] handled separately; src1 is the target of an indirect call (NONE otherwise) */
        break;
    case MIR_ASM_TEXT:
        s.has_def = 0; s.use_src1 = 0; s.use_src2 = 0; /* args[] handled separately, no dest in v1 */
        break;
    case MIR_RET:
        s.has_def = 0; s.use_src1 = 1; s.use_src2 = 0;
        break;
    case MIR_OUTB: case MIR_WRMSR: case MIR_LGDT: case MIR_LIDT:
        s.has_def = 0; s.use_src1 = 1; s.use_src2 = 1;
        break;
    case MIR_INB: case MIR_RDMSR: case MIR_READ_CR:
        s.use_src1 = 1; s.use_src2 = 0;
        break;
    case MIR_WRITE_CR: case MIR_LTR: case MIR_INVLPG:
        s.has_def = 0; s.use_src1 = 1; s.use_src2 = 0;
        break;
    case MIR_MEMSET: case MIR_MEMCPY:
        /* dest/src1/src2 all reads here (ptr, val-or-src, count) - see ir_to_mir.c */
        s.has_def = 0; s.dest_is_use = 1; s.use_src1 = 1; s.use_src2 = 1;
        break;
    case MIR_CLI: case MIR_STI: case MIR_IRET: case MIR_SYSRET:
    case MIR_SAVE_REGS: case MIR_RESTORE_REGS:
    case MIR_NOP: case MIR_ASM_RAW:
        s.has_def = 0; s.use_src1 = 0; s.use_src2 = 0;
        break;
    case MIR_LEA_LOCAL: case MIR_LEA_GLOBAL: case MIR_ALLOCA_LOCAL: case MIR_GETPARAM:
        s.use_src1 = 0; s.use_src2 = 0; /* src1 is a global/label name, not a vreg use */
        break;
    default:
        break; /* common arithmetic/load/cast case: default shape is correct */
    }
    return s;
}

/* ---- basic blocks ---- */

typedef struct {
    int start, end;         /* [start, end) instruction index range */
    int succ[2];
    int succ_count;
} Block;

static int is_terminator_op(MIROp op) {
    return op == MIR_RET || op == MIR_JMP || op == MIR_JMP_IF || op == MIR_JMP_UNLESS
        || op == MIR_IRET || op == MIR_SYSRET;
}

typedef struct {
    Block *blocks;
    int    block_count;
    int   *instr_block; /* instr index -> owning block index */
} CFG;

static CFG build_cfg(const MIRFunc *fn) {
    CFG cfg = {0};
    if (fn->count == 0) return cfg;

    int *is_leader = calloc(fn->count, sizeof(int));
    is_leader[0] = 1;
    for (int i = 0; i < fn->count; i++) {
        if (fn->instrs[i].op == MIR_LABEL) is_leader[i] = 1;
        if (is_terminator_op(fn->instrs[i].op) && i + 1 < fn->count) is_leader[i + 1] = 1;
    }

    int nblocks = 0;
    for (int i = 0; i < fn->count; i++) if (is_leader[i]) nblocks++;
    cfg.blocks = calloc(nblocks, sizeof(Block));
    cfg.block_count = nblocks;
    cfg.instr_block = malloc(fn->count * sizeof(int));

    int b = -1;
    for (int i = 0; i < fn->count; i++) {
        if (is_leader[i]) {
            b++;
            cfg.blocks[b].start = i;
            cfg.blocks[b].end = fn->count;
            if (b > 0) cfg.blocks[b - 1].end = i;
        }
        cfg.instr_block[i] = b;
    }
    free(is_leader);

    /* label_id -> block index that starts with that label */
    int max_label = -1;
    for (int i = 0; i < fn->count; i++)
        if (fn->instrs[i].op == MIR_LABEL && fn->instrs[i].dest.label_id > max_label)
            max_label = fn->instrs[i].dest.label_id;
    int *label_to_block = NULL;
    if (max_label >= 0) {
        label_to_block = malloc((max_label + 1) * sizeof(int));
        for (int i = 0; i <= max_label; i++) label_to_block[i] = -1;
        for (int i = 0; i < fn->count; i++)
            if (fn->instrs[i].op == MIR_LABEL)
                label_to_block[fn->instrs[i].dest.label_id] = cfg.instr_block[i];
    }

    for (int bi = 0; bi < nblocks; bi++) {
        Block *blk = &cfg.blocks[bi];
        int last = blk->end - 1;
        MIROp lastop = fn->instrs[last].op;
        if (lastop == MIR_JMP) {
            int lbl = fn->instrs[last].dest.label_id;
            if (label_to_block && lbl <= max_label && label_to_block[lbl] >= 0) {
                blk->succ[blk->succ_count++] = label_to_block[lbl];
            }
        } else if (lastop == MIR_JMP_IF || lastop == MIR_JMP_UNLESS) {
            int lbl = fn->instrs[last].dest.label_id;
            if (label_to_block && lbl <= max_label && label_to_block[lbl] >= 0) {
                blk->succ[blk->succ_count++] = label_to_block[lbl];
            }
            if (bi + 1 < nblocks) blk->succ[blk->succ_count++] = bi + 1;
        } else if (lastop == MIR_RET || lastop == MIR_IRET || lastop == MIR_SYSRET) {
            /* no successors */
        } else {
            if (bi + 1 < nblocks) blk->succ[blk->succ_count++] = bi + 1;
        }
    }

    free(label_to_block);
    return cfg;
}

static void cfg_free(CFG *cfg) {
    free(cfg->blocks);
    free(cfg->instr_block);
}

/* ---- liveness: classic backward dataflow to a fixpoint (bit-packed) ---- */

typedef struct {
    int      words;      /* 64-bit words per block row */
    uint64_t *use;       /* use[B]: read before written in B */
    uint64_t *def;
    uint64_t *live_in;
    uint64_t *live_out;
} Liveness;

static inline int  lbit(const uint64_t *r, int v) { return (int)((r[v >> 6] >> (v & 63)) & 1); }
static inline void lset(uint64_t *r, int v) { r[v >> 6] |= (uint64_t)1 << (v & 63); }

static Liveness compute_liveness(const MIRFunc *fn, const CFG *cfg, int nvregs) {
    Liveness lv;
    lv.words = (nvregs + 63) / 64;
    size_t sz = (size_t)cfg->block_count * lv.words;
    lv.use = calloc(sz, sizeof(uint64_t));
    lv.def = calloc(sz, sizeof(uint64_t));
    lv.live_in = calloc(sz, sizeof(uint64_t));
    lv.live_out = calloc(sz, sizeof(uint64_t));

    for (int bi = 0; bi < cfg->block_count; bi++) {
        uint64_t *use_b = lv.use + (size_t)bi * lv.words;
        uint64_t *def_b = lv.def + (size_t)bi * lv.words;
        Block *blk = &cfg->blocks[bi];
        for (int i = blk->start; i < blk->end; i++) {
            const MIRInstr *ins = &fn->instrs[i];
            OpShape sh = op_shape(ins);
            /* uses first (before this instr's own def clobbers it) */
            if (sh.use_src1 && ins->src1.kind == MIRV_VREG && !lbit(def_b, ins->src1.vreg))
                lset(use_b, ins->src1.vreg);
            if (sh.use_src2 && ins->src2.kind == MIRV_VREG && !lbit(def_b, ins->src2.vreg))
                lset(use_b, ins->src2.vreg);
            if (sh.dest_is_use && ins->dest.kind == MIRV_VREG && !lbit(def_b, ins->dest.vreg))
                lset(use_b, ins->dest.vreg);
            for (int a = 0; a < ins->arg_count; a++)
                if (ins->args[a].kind == MIRV_VREG && !lbit(def_b, ins->args[a].vreg))
                    lset(use_b, ins->args[a].vreg);
            if (sh.has_def && ins->dest.kind == MIRV_VREG)
                lset(def_b, ins->dest.vreg);
        }
    }

    int changed = 1;
    while (changed) {
        changed = 0;
        for (int bi = cfg->block_count - 1; bi >= 0; bi--) {
            uint64_t *out_b = lv.live_out + (size_t)bi * lv.words;
            uint64_t *in_b  = lv.live_in  + (size_t)bi * lv.words;
            uint64_t *use_b = lv.use      + (size_t)bi * lv.words;
            uint64_t *def_b = lv.def      + (size_t)bi * lv.words;
            Block *blk = &cfg->blocks[bi];
            for (int s = 0; s < blk->succ_count; s++) {
                uint64_t *succ_in = lv.live_in + (size_t)blk->succ[s] * lv.words;
                for (int w = 0; w < lv.words; w++) out_b[w] |= succ_in[w];
            }
            for (int w = 0; w < lv.words; w++) {
                uint64_t ni = use_b[w] | (out_b[w] & ~def_b[w]);
                if (ni != in_b[w]) { in_b[w] = ni; changed = 1; }
            }
        }
    }

    return lv;
}

static void liveness_free(Liveness *lv) {
    free(lv->use); free(lv->def); free(lv->live_in); free(lv->live_out);
}

/* ---- live intervals from liveness + local def/use positions ---- */

typedef struct {
    int vreg;
    int start, end;
    int is_float;
    int crosses_call;   /* live range spans a call -> caller-saved regs are unsafe */
    double weight;      /* uses+defs, each scaled by 8^(loop depth): what spilling would cost */
    unsigned forbid;    /* integer registers (pool ids) some instruction inside the range clobbers */
    int pref;           /* preferred register id (coalescing / ABI hint), -1 = none */
} Interval;

/* Any instruction that executes a `call`-like transfer and therefore destroys
   the caller-saved registers. MIR_SYSCALL counts too: the `syscall`
   instruction itself clobbers rcx and r11, and the kernel is free to touch
   the caller-saved set. */
static int op_clobbers_caller_saved(MIROp op) {
    /* rep stos/movs consume rdi/rsi/rcx, which are (or overlap) allocatable */
    return op == MIR_CALL || op == MIR_SYSCALL || op == MIR_MEMSET || op == MIR_MEMCPY;
}

static void extend(int *start, int *end, int pos) {
    if (*start < 0 || pos < *start) *start = pos;
    if (pos > *end) *end = pos;
}

/* Loop nesting depth of every instruction, from backward jumps (structured
   code: a jump back to a label at or before it encloses everything between). */
static int *compute_depth(const MIRFunc *fn) {
    int n = fn->count;
    int *depth = calloc(n + 1, sizeof(int));
    int maxl = -1;
    for (int i = 0; i < n; i++)
        if (fn->instrs[i].op == MIR_LABEL && fn->instrs[i].dest.label_id > maxl) maxl = fn->instrs[i].dest.label_id;
    if (maxl < 0) return depth;
    int *lpos = malloc((maxl + 1) * sizeof(int));
    for (int i = 0; i <= maxl; i++) lpos[i] = -1;
    for (int i = 0; i < n; i++) if (fn->instrs[i].op == MIR_LABEL) lpos[fn->instrs[i].dest.label_id] = i;
    for (int j = 0; j < n; j++) {
        MIROp op = fn->instrs[j].op;
        if (op != MIR_JMP && op != MIR_JMP_IF && op != MIR_JMP_UNLESS) continue;
        int l = fn->instrs[j].dest.label_id;
        if (l < 0 || l > maxl || lpos[l] < 0 || lpos[l] > j) continue;
        for (int k = lpos[l]; k <= j; k++) depth[k]++;
    }
    free(lpos);
    return depth;
}

static double depth_weight(int d) {
    double w = 1;
    for (int i = 0; i < d && i < 6; i++) w *= 8;
    return w;
}

static Interval *compute_intervals(const MIRFunc *fn, const CFG *cfg, const Liveness *lv,
                                    int nvregs, int *out_count, const RegAllocTarget *target) {
    int *depth = compute_depth(fn);
    double *weight = calloc(nvregs, sizeof(double));
    int *pref = malloc(nvregs * sizeof(int));
    for (int v = 0; v < nvregs; v++) pref[v] = -1;
    int *start = malloc(nvregs * sizeof(int));
    int *end   = malloc(nvregs * sizeof(int));
    int *is_float = calloc(nvregs, sizeof(int));
    for (int v = 0; v < nvregs; v++) { start[v] = -1; end[v] = -1; }

    for (int bi = 0; bi < cfg->block_count; bi++) {
        Block *blk = &cfg->blocks[bi];
        const uint64_t *in_b  = lv->live_in  + (size_t)bi * lv->words;
        const uint64_t *out_b = lv->live_out + (size_t)bi * lv->words;
        for (int w = 0; w < lv->words; w++) {
            for (uint64_t m = in_b[w]; m; m &= m - 1) {
                int v = w * 64 + __builtin_ctzll(m);
                if (v < nvregs) extend(&start[v], &end[v], blk->start);
            }
            for (uint64_t m = out_b[w]; m; m &= m - 1) {
                int v = w * 64 + __builtin_ctzll(m);
                if (v < nvregs) extend(&start[v], &end[v], blk->end - 1);
            }
        }
        for (int i = blk->start; i < blk->end; i++) {
            const MIRInstr *ins = &fn->instrs[i];
            OpShape sh = op_shape(ins);
            {
                double w = depth_weight(depth[i]);
                if (sh.use_src1 && ins->src1.kind == MIRV_VREG) weight[ins->src1.vreg] += w;
                if (sh.use_src2 && ins->src2.kind == MIRV_VREG) weight[ins->src2.vreg] += w;
                if ((sh.dest_is_use || sh.has_def) && ins->dest.kind == MIRV_VREG) weight[ins->dest.vreg] += w;
                for (int a = 0; a < ins->arg_count; a++)
                    if (ins->args[a].kind == MIRV_VREG) weight[ins->args[a].vreg] += w;
                /* register hints */
                if (ins->op == MIR_MOV && ins->dest.kind == MIRV_VREG && ins->src1.kind == MIRV_VREG) {
                    /* dest and source like to share a register (the move vanishes) */
                    if (pref[ins->dest.vreg] < 0) pref[ins->dest.vreg] = -2 - ins->src1.vreg;   /* encoded: follow src */
                    if (pref[ins->src1.vreg] < 0) pref[ins->src1.vreg] = -2 - ins->dest.vreg;
                }
                if (ins->op == MIR_CALL && !ins->arg_aggr) {
                    for (int a = 0; a < ins->arg_count && a < 6; a++)
                        if (ins->args[a].kind == MIRV_VREG && target->arg_regs[a] >= 0 &&
                            !mir_type_is_float(ins->args[a].type) && pref[ins->args[a].vreg] < 0)
                            pref[ins->args[a].vreg] = target->arg_regs[a];
                }
                if (ins->op == MIR_GETPARAM && ins->dest.kind == MIRV_VREG && ins->extra_int < 6 &&
                    !mir_type_is_float(ins->dest.type) &&
                    target->arg_regs[ins->extra_int] >= 0 && pref[ins->dest.vreg] < 0)
                    pref[ins->dest.vreg] = target->arg_regs[ins->extra_int];
            }
            if (sh.use_src1 && ins->src1.kind == MIRV_VREG) {
                extend(&start[ins->src1.vreg], &end[ins->src1.vreg], i);
                if (mir_type_is_float(ins->src1.type)) is_float[ins->src1.vreg] = 1;
            }
            if (sh.use_src2 && ins->src2.kind == MIRV_VREG) {
                extend(&start[ins->src2.vreg], &end[ins->src2.vreg], i);
                if (mir_type_is_float(ins->src2.type)) is_float[ins->src2.vreg] = 1;
            }
            if (sh.dest_is_use && ins->dest.kind == MIRV_VREG) {
                extend(&start[ins->dest.vreg], &end[ins->dest.vreg], i);
                if (mir_type_is_float(ins->dest.type)) is_float[ins->dest.vreg] = 1;
            }
            for (int a = 0; a < ins->arg_count; a++) {
                if (ins->args[a].kind == MIRV_VREG) {
                    extend(&start[ins->args[a].vreg], &end[ins->args[a].vreg], i);
                    if (mir_type_is_float(ins->args[a].type)) is_float[ins->args[a].vreg] = 1;
                }
            }
            if (sh.has_def && ins->dest.kind == MIRV_VREG) {
                extend(&start[ins->dest.vreg], &end[ins->dest.vreg], i);
                if (mir_type_is_float(ins->dest.type)) is_float[ins->dest.vreg] = 1;
            }
        }
    }

    /* Prefix count of call-like instructions, so "is there a call strictly
       between start and end" is an O(1) subtraction per interval instead of a
       rescan. A call AT `start` (the interval is that call's result) or AT
       `end` (the interval is one of that call's arguments, read before the
       call executes) does NOT count - only a call the value has to survive. */
    int *calls_before = calloc(fn->count + 1, sizeof(int));
    for (int i = 0; i < fn->count; i++)
        calls_before[i + 1] = calls_before[i] + (op_clobbers_caller_saved(fn->instrs[i].op) ? 1 : 0);

    /* prefix counts of instructions clobbering each integer register */
    int nregs = target->num_int_regs > 32 ? 32 : target->num_int_regs;
    int *clob_prefix = NULL;
    if (target->clobber_mask && nregs > 0) {
        clob_prefix = calloc((size_t)(fn->count + 1) * nregs, sizeof(int));
        for (int i = 0; i < fn->count; i++) {
            unsigned m = target->clobber_mask(&fn->instrs[i]);
            for (int r = 0; r < nregs; r++)
                clob_prefix[(size_t)(i + 1) * nregs + r] = clob_prefix[(size_t)i * nregs + r] + ((m >> r) & 1);
        }
    }

    Interval *intervals = malloc(nvregs * sizeof(Interval));
    int count = 0;
    for (int v = 0; v < nvregs; v++) {
        if (start[v] < 0) continue; /* never referenced (dead/unused vreg id) */
        intervals[count].vreg = v;
        intervals[count].start = start[v];
        intervals[count].end = end[v];
        intervals[count].is_float = is_float[v];
        /* calls in the open interval (start, end) */
        intervals[count].crosses_call =
            (end[v] > start[v]) && (calls_before[end[v]] - calls_before[start[v] + 1] > 0);
        intervals[count].weight = weight[v];
        intervals[count].pref = pref[v];
        intervals[count].forbid = 0;
        if (clob_prefix && end[v] > start[v] + 1)
            for (int r = 0; r < nregs; r++)
                if (clob_prefix[(size_t)end[v] * nregs + r] - clob_prefix[(size_t)(start[v] + 1) * nregs + r] > 0)
                    intervals[count].forbid |= 1u << r;
        count++;
    }
    free(calls_before);
    free(start); free(end); free(is_float);
    free(depth); free(weight); free(pref); free(clob_prefix);
    *out_count = count;
    return intervals;
}

static int cmp_by_start(const void *a, const void *b) {
    return ((const Interval *)a)->start - ((const Interval *)b)->start;
}

/* ---- linear scan (Poletto-Sarkar), run once per register class ----
   Differences from the textbook version:
   - When registers run out the victim is the interval that is cheapest to
     keep in memory - fewest (loop-depth weighted) uses per instruction of
     life - not merely the one that ends last, so loop variables win over
     values that are idle for most of the function.
   - Intervals carry a register hint (a move partner, or the ABI register they
     are passed in) which is honoured when that register happens to be free. */

typedef struct {
    Interval  iv;
    int       reg;      /* assigned register id */
} Active;

static double density(const Interval *iv) {
    return iv->weight / (double)(iv->end - iv->start + 1);
}

static unsigned g_cs_mask;   /* callee-saved registers used by the class being allocated */

static void set_reg(MCLoc *locs, unsigned char *assigned, const Interval *iv, int reg, int is_float_class,
                    int num_callee_saved, int *callee_saved_used) {
    locs[iv->vreg].kind = MCLOC_REG;
    locs[iv->vreg].is_float = is_float_class;
    locs[iv->vreg].reg_id = reg;
    assigned[iv->vreg] = 1;
    if (reg < num_callee_saved) g_cs_mask |= 1u << reg;
    if (reg < num_callee_saved && reg + 1 > *callee_saved_used) *callee_saved_used = reg + 1;
}

static void linear_scan_class(Interval *intervals, int n, int num_regs,
                               MCLoc *out_locs, unsigned char *assigned, int is_float_class,
                               int *spill_count, int num_callee_saved, int *callee_saved_used,
                               const int *order) {
    if (n == 0) return;
    qsort(intervals, n, sizeof(Interval), cmp_by_start);

    Active *active = malloc((num_regs > 0 ? num_regs : 1) * sizeof(Active));
    int active_count = 0;
    int *free_regs = malloc((num_regs > 0 ? num_regs : 1) * sizeof(int));
    int free_count = num_regs;
    /* free_regs is popped from the top: keep the caller-saved ids there so the
       scarce callee-saved ones stay available for values that cross calls */
    for (int i = 0; i < num_regs; i++) free_regs[i] = num_regs - 1 - i;
    int next_spill = 0;

    for (int i = 0; i < n; i++) {
        Interval cur = intervals[i];

        for (int a = 0; a < active_count; ) {
            if (active[a].iv.end < cur.start) {
                free_regs[free_count++] = active[a].reg;
                active[a] = active[--active_count];
            } else {
                a++;
            }
        }

        /* --- pick a free register, honouring the hint --- */
        int pick = -1;                       /* index into free_regs */
        int want = -1;
        if (cur.pref >= 0) want = cur.pref;
        else if (cur.pref <= -2) {
            int partner = -2 - cur.pref;
            if (assigned[partner] && out_locs[partner].kind == MCLOC_REG &&
                out_locs[partner].is_float == is_float_class)
                want = out_locs[partner].reg_id;
        }
        if (want >= 0 && want < num_regs && (!cur.crosses_call || want < num_callee_saved) &&
            !((cur.forbid >> want) & 1)) {
            for (int f = 0; f < free_count; f++) if (free_regs[f] == want) { pick = f; break; }
        }
        if (pick < 0) {
            /* walk the preference order: cheap caller-saved registers first unless the
               value has to survive a call */
            for (int oi = 0; oi < num_regs && pick < 0; oi++) {
                int rg = order ? order[oi] : num_regs - 1 - oi;
                if ((cur.forbid >> rg) & 1) continue;
                if (cur.crosses_call && rg >= num_callee_saved) continue;
                for (int f = 0; f < free_count; f++) if (free_regs[f] == rg) { pick = f; break; }
            }
        }
        if (pick >= 0) {
            int reg = free_regs[pick];
            free_regs[pick] = free_regs[--free_count];
            set_reg(out_locs, assigned, &cur, reg, is_float_class, num_callee_saved, callee_saved_used);
            active[active_count].iv = cur;
            active[active_count].reg = reg;
            active_count++;
            continue;
        }

        /* --- no register free: evict the cheapest usable victim, or spill cur --- */
        int victim = -1;
        for (int a = 0; a < active_count; a++) {
            if (cur.crosses_call && active[a].reg >= num_callee_saved) continue;
            if ((cur.forbid >> active[a].reg) & 1) continue;
            if (victim < 0 || density(&active[a].iv) < density(&active[victim].iv)) victim = a;
        }
        if (victim >= 0 && density(&active[victim].iv) < density(&cur)) {
            int reg = active[victim].reg;
            out_locs[active[victim].iv.vreg].kind = MCLOC_SPILL;
            out_locs[active[victim].iv.vreg].is_float = is_float_class;
            out_locs[active[victim].iv.vreg].spill_slot = next_spill++;
            assigned[active[victim].iv.vreg] = 1;
            set_reg(out_locs, assigned, &cur, reg, is_float_class, num_callee_saved, callee_saved_used);
            active[victim].iv = cur;
            active[victim].reg = reg;
        } else {
            out_locs[cur.vreg].kind = MCLOC_SPILL;
            out_locs[cur.vreg].is_float = is_float_class;
            out_locs[cur.vreg].spill_slot = next_spill++;
            assigned[cur.vreg] = 1;
        }
    }

    free(active);
    free(free_regs);
    *spill_count = next_spill;
}

RegAllocResult regalloc_run(const MIRFunc *fn, const RegAllocTarget *target) {
    RegAllocResult res = {0};
    int nvregs = fn->vreg_count;
    int scanned = scan_max_vreg(fn);
    if (scanned > nvregs) nvregs = scanned;
    if (nvregs == 0) { res.vreg_count = 0; return res; }

    res.vreg_locs = calloc(nvregs, sizeof(MCLoc));
    res.vreg_count = nvregs;

    if (fn->count == 0) return res;

    CFG cfg = build_cfg(fn);
    Liveness lv = compute_liveness(fn, &cfg, nvregs);
    int total_count = 0;
    Interval *all = compute_intervals(fn, &cfg, &lv, nvregs, &total_count, target);

    Interval *ints = malloc(total_count * sizeof(Interval));
    Interval *floats = malloc(total_count * sizeof(Interval));
    int int_n = 0, float_n = 0;
    for (int i = 0; i < total_count; i++) {
        if (all[i].is_float) floats[float_n++] = all[i];
        else ints[int_n++] = all[i];
    }

    unsigned char *assigned = calloc(nvregs, 1);
    g_cs_mask = 0;
    linear_scan_class(ints, int_n, target->num_int_regs, res.vreg_locs, assigned, 0,
                      &res.num_int_spill_slots,
                      target->num_callee_saved_int_regs, &res.callee_saved_int_used,
                      target->int_alloc_order);
    res.callee_saved_int_mask = g_cs_mask;
    linear_scan_class(floats, float_n, target->num_float_regs, res.vreg_locs, assigned, 1,
                      &res.num_float_spill_slots,
                      target->num_callee_saved_float_regs, &res.callee_saved_float_used, NULL);

    free(ints); free(floats); free(all); free(assigned);
    liveness_free(&lv);
    cfg_free(&cfg);
    return res;
}

void regalloc_result_free(RegAllocResult *res) {
    free(res->vreg_locs);
    res->vreg_locs = NULL;
}
