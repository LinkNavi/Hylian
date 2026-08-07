#include "hyregalloc.h"
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

static OpShape op_shape(MIROp op) {
    OpShape s = {1, 0, 1, 1}; /* default: dest=def, src1/src2=use */
    switch (op) {
    case MIR_STORE:
        s.has_def = 0; s.dest_is_use = 1; s.use_src1 = 1; s.use_src2 = 0;
        break;
    case MIR_LABEL: case MIR_JMP:
        s.has_def = 0; s.use_src1 = 0; s.use_src2 = 0;
        break;
    case MIR_JMP_IF: case MIR_JMP_UNLESS:
        s.has_def = 0; s.use_src1 = 1; s.use_src2 = 0;
        break;
    case MIR_CALL:
        s.use_src1 = 0; s.use_src2 = 0; /* args[] handled separately */
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
    case MIR_CLI: case MIR_STI: case MIR_IRET:
    case MIR_SAVE_REGS: case MIR_RESTORE_REGS:
    case MIR_NOP: case MIR_ASM_RAW:
        s.has_def = 0; s.use_src1 = 0; s.use_src2 = 0;
        break;
    case MIR_LEA_LOCAL: case MIR_LEA_GLOBAL: case MIR_ALLOCA_LOCAL:
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
        || op == MIR_IRET;
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
        } else if (lastop == MIR_RET || lastop == MIR_IRET) {
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

/* ---- liveness: classic backward dataflow to a fixpoint ---- */

typedef struct {
    unsigned char *use; /* use[B][v] */
    unsigned char *def;
    unsigned char *live_in;
    unsigned char *live_out;
} Liveness;

static Liveness compute_liveness(const MIRFunc *fn, const CFG *cfg, int nvregs) {
    Liveness lv;
    size_t sz = (size_t)cfg->block_count * nvregs;
    lv.use = calloc(sz, 1);
    lv.def = calloc(sz, 1);
    lv.live_in = calloc(sz, 1);
    lv.live_out = calloc(sz, 1);

    for (int bi = 0; bi < cfg->block_count; bi++) {
        unsigned char *use_b = lv.use + (size_t)bi * nvregs;
        unsigned char *def_b = lv.def + (size_t)bi * nvregs;
        Block *blk = &cfg->blocks[bi];
        for (int i = blk->start; i < blk->end; i++) {
            const MIRInstr *ins = &fn->instrs[i];
            OpShape sh = op_shape(ins->op);
            /* uses first (before this instr's own def clobbers it) */
            if (sh.use_src1 && ins->src1.kind == MIRV_VREG && !def_b[ins->src1.vreg])
                use_b[ins->src1.vreg] = 1;
            if (sh.use_src2 && ins->src2.kind == MIRV_VREG && !def_b[ins->src2.vreg])
                use_b[ins->src2.vreg] = 1;
            if (sh.dest_is_use && ins->dest.kind == MIRV_VREG && !def_b[ins->dest.vreg])
                use_b[ins->dest.vreg] = 1;
            for (int a = 0; a < ins->arg_count; a++)
                if (ins->args[a].kind == MIRV_VREG && !def_b[ins->args[a].vreg])
                    use_b[ins->args[a].vreg] = 1;
            if (sh.has_def && ins->dest.kind == MIRV_VREG)
                def_b[ins->dest.vreg] = 1;
        }
    }

    int changed = 1;
    while (changed) {
        changed = 0;
        for (int bi = cfg->block_count - 1; bi >= 0; bi--) {
            unsigned char *out_b = lv.live_out + (size_t)bi * nvregs;
            unsigned char *in_b  = lv.live_in  + (size_t)bi * nvregs;
            unsigned char *use_b = lv.use      + (size_t)bi * nvregs;
            unsigned char *def_b = lv.def      + (size_t)bi * nvregs;
            Block *blk = &cfg->blocks[bi];

            unsigned char *new_out = calloc(nvregs, 1);
            for (int s = 0; s < blk->succ_count; s++) {
                unsigned char *succ_in = lv.live_in + (size_t)blk->succ[s] * nvregs;
                for (int v = 0; v < nvregs; v++) if (succ_in[v]) new_out[v] = 1;
            }
            for (int v = 0; v < nvregs; v++) {
                if (new_out[v] != out_b[v]) { changed = 1; out_b[v] = new_out[v]; }
            }
            free(new_out);

            for (int v = 0; v < nvregs; v++) {
                unsigned char new_in = use_b[v] || (out_b[v] && !def_b[v]);
                if (new_in != in_b[v]) { changed = 1; in_b[v] = new_in; }
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
} Interval;

/* Any instruction that executes a `call`-like transfer and therefore destroys
   the caller-saved registers. MIR_SYSCALL counts too: the `syscall`
   instruction itself clobbers rcx and r11, and the kernel is free to touch
   the caller-saved set. */
static int op_clobbers_caller_saved(MIROp op) {
    return op == MIR_CALL || op == MIR_SYSCALL;
}

static void extend(int *start, int *end, int pos) {
    if (*start < 0 || pos < *start) *start = pos;
    if (pos > *end) *end = pos;
}

static Interval *compute_intervals(const MIRFunc *fn, const CFG *cfg, const Liveness *lv,
                                    int nvregs, int *out_count) {
    int *start = malloc(nvregs * sizeof(int));
    int *end   = malloc(nvregs * sizeof(int));
    int *is_float = calloc(nvregs, sizeof(int));
    for (int v = 0; v < nvregs; v++) { start[v] = -1; end[v] = -1; }

    for (int bi = 0; bi < cfg->block_count; bi++) {
        Block *blk = &cfg->blocks[bi];
        unsigned char *in_b  = lv->live_in  + (size_t)bi * nvregs;
        unsigned char *out_b = lv->live_out + (size_t)bi * nvregs;
        for (int v = 0; v < nvregs; v++) {
            if (in_b[v])  extend(&start[v], &end[v], blk->start);
            if (out_b[v]) extend(&start[v], &end[v], blk->end - 1);
        }
        for (int i = blk->start; i < blk->end; i++) {
            const MIRInstr *ins = &fn->instrs[i];
            OpShape sh = op_shape(ins->op);
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
        count++;
    }
    free(calls_before);
    free(start); free(end); free(is_float);
    *out_count = count;
    return intervals;
}

static int cmp_by_start(const void *a, const void *b) {
    return ((const Interval *)a)->start - ((const Interval *)b)->start;
}

/* ---- linear scan (Poletto-Sarkar), run once per register class ---- */

typedef struct {
    Interval  iv;
    int       reg;      /* assigned register id, -1 if spilled */
} Active;

static void linear_scan_class(Interval *intervals, int n, int num_regs,
                               MCLoc *out_locs, int is_float_class, int *spill_count,
                               int num_callee_saved, int *callee_saved_used) {
    if (n == 0) return;
    qsort(intervals, n, sizeof(Interval), cmp_by_start);

    Active *active = malloc(num_regs > 0 ? num_regs * sizeof(Active) : sizeof(Active));
    int active_count = 0;
    int *free_regs = malloc(num_regs * sizeof(int));
    int free_count = num_regs;
    /* Hand out the *caller-saved* ids first (they're the tail of the pool) so
       the scarce callee-saved ones stay available for the values that actually
       need them, and so functions that never carry anything across a call
       don't pay for saving/restoring registers they didn't need. free_regs is
       popped from the top, so push callee-saved at the bottom. */
    for (int i = 0; i < num_regs; i++) free_regs[i] = num_regs - 1 - i;
    int next_spill = 0;

    for (int i = 0; i < n; i++) {
        Interval cur = intervals[i];

        /* expire intervals that have ended before cur starts */
        for (int a = 0; a < active_count; ) {
            if (active[a].iv.end < cur.start) {
                if (active[a].reg >= 0) free_regs[free_count++] = active[a].reg;
                active[a] = active[--active_count];
            } else {
                a++;
            }
        }

        /* A value live across a call can only sit in a callee-saved register.
           Look for a free one specifically; if the class has none (SysV has no
           callee-saved xmm) or they're all taken, fall through to spilling -
           the stack is the only other place a value can survive a call. */
        if (cur.crosses_call) {
            int pick = -1;
            for (int f = free_count - 1; f >= 0; f--) {
                if (free_regs[f] < num_callee_saved) { pick = f; break; }
            }
            if (pick >= 0 && active_count < num_regs) {
                int reg = free_regs[pick];
                free_regs[pick] = free_regs[--free_count];
                out_locs[cur.vreg].kind = MCLOC_REG;
                out_locs[cur.vreg].is_float = is_float_class;
                out_locs[cur.vreg].reg_id = reg;
                if (reg + 1 > *callee_saved_used) *callee_saved_used = reg + 1;
                active[active_count].iv = cur;
                active[active_count].reg = reg;
                active_count++;
            } else {
                out_locs[cur.vreg].kind = MCLOC_SPILL;
                out_locs[cur.vreg].is_float = is_float_class;
                out_locs[cur.vreg].spill_slot = next_spill++;
            }
            continue;
        }

        if (active_count < num_regs && free_count > 0) {
            int reg = free_regs[--free_count];
            out_locs[cur.vreg].kind = MCLOC_REG;
            out_locs[cur.vreg].is_float = is_float_class;
            out_locs[cur.vreg].reg_id = reg;
            if (reg < num_callee_saved && reg + 1 > *callee_saved_used)
                *callee_saved_used = reg + 1;
            active[active_count].iv = cur;
            active[active_count].reg = reg;
            active_count++;
        } else {
            /* spill: pick the active interval with the furthest end; if it
               ends later than cur, steal its register for cur and spill it
               instead (classic Poletto-Sarkar heuristic) */
            int spill_idx = -1;
            for (int a = 0; a < active_count; a++) {
                if (active[a].reg < 0) continue;
                if (spill_idx < 0 || active[a].iv.end > active[spill_idx].iv.end) spill_idx = a;
            }
            if (spill_idx >= 0 && active[spill_idx].iv.end > cur.end) {
                int reg = active[spill_idx].reg;
                out_locs[active[spill_idx].iv.vreg].kind = MCLOC_SPILL;
                out_locs[active[spill_idx].iv.vreg].is_float = is_float_class;
                out_locs[active[spill_idx].iv.vreg].spill_slot = next_spill++;

                /* reuse this active slot for cur instead of appending -
                   active_count must never exceed num_regs */
                out_locs[cur.vreg].kind = MCLOC_REG;
                out_locs[cur.vreg].is_float = is_float_class;
                out_locs[cur.vreg].reg_id = reg;
                if (reg < num_callee_saved && reg + 1 > *callee_saved_used)
                    *callee_saved_used = reg + 1;
                active[spill_idx].iv = cur;
                active[spill_idx].reg = reg;
            } else {
                out_locs[cur.vreg].kind = MCLOC_SPILL;
                out_locs[cur.vreg].is_float = is_float_class;
                out_locs[cur.vreg].spill_slot = next_spill++;
            }
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
    Interval *all = compute_intervals(fn, &cfg, &lv, nvregs, &total_count);

    Interval *ints = malloc(total_count * sizeof(Interval));
    Interval *floats = malloc(total_count * sizeof(Interval));
    int int_n = 0, float_n = 0;
    for (int i = 0; i < total_count; i++) {
        if (all[i].is_float) floats[float_n++] = all[i];
        else ints[int_n++] = all[i];
    }

    linear_scan_class(ints, int_n, target->num_int_regs, res.vreg_locs, 0,
                      &res.num_int_spill_slots,
                      target->num_callee_saved_int_regs, &res.callee_saved_int_used);
    linear_scan_class(floats, float_n, target->num_float_regs, res.vreg_locs, 1,
                      &res.num_float_spill_slots,
                      target->num_callee_saved_float_regs, &res.callee_saved_float_used);

    free(ints); free(floats); free(all);
    liveness_free(&lv);
    cfg_free(&cfg);
    return res;
}

void regalloc_result_free(RegAllocResult *res) {
    free(res->vreg_locs);
    res->vreg_locs = NULL;
}
