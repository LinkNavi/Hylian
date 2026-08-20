#include "opt.h"
#include "ir.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- shared helpers ---- */

static int has_side_effects(const IRInstr *ins) {
    switch (ins->op) {
    case IR_STORE_VAR:
    case IR_SET_FIELD:
    case IR_ARRAY_STORE:
    case IR_ARRAY_PUSH:
    case IR_ARRAY_POP:
    case IR_CALL:
    case IR_NEW:
    case IR_RETURN:
    case IR_JUMP:
    case IR_JUMP_IF:
    case IR_JUMP_UNLESS:
    case IR_LABEL:
    case IR_ASM_BLOCK:
    case IR_PRINT:
    case IR_PRINTLN:
    case IR_ERR:
    case IR_PANIC:
    case IR_FUNC_BEGIN:
    case IR_FUNC_END:
    case IR_NOP:
    case IR_STORE_PTR:
    case IR_LOAD_PTR:
    case IR_LOAD_VOLATILE:
    case IR_STORE_VOLATILE:
    case IR_STATIC_VAR:
    case IR_CLI:
    case IR_STI:
    case IR_HLT:
    case IR_LGDT:
    case IR_LIDT:
    case IR_LTR:
    case IR_INVLPG:
    case IR_WRMSR:
    case IR_RDMSR:
    case IR_READ_CR:
    case IR_WRITE_CR:
    case IR_SAVE_REGS:
    case IR_RESTORE_REGS:
    case IR_IRET:
    case IR_OUTB:
    case IR_INB:
    case IR_OUTW:
    case IR_INW:
    case IR_MEMSET:
    case IR_MEMCPY:
    case IR_CAST:
        return 1;
    default:
        return 0;
    }
}

static int op_is_int_const(const IROperand *o, long *out) {
    if (o->kind == IROP_CONST_INT)  { *out = o->int_val;  return 1; }
    if (o->kind == IROP_CONST_BOOL) { *out = o->bool_val; return 1; }
    if (o->kind == IROP_NONE)       { *out = 0;           return 1; }
    return 0;
}

static int op_is_float_const(const IROperand *o, double *out) {
    if (o->kind == IROP_CONST_FLOAT) { *out = o->float_val;        return 1; }
    if (o->kind == IROP_CONST_INT)   { *out = (double)o->int_val;  return 1; }
    return 0;
}

static int is_pow2(long v, int *shift) {
    if (v <= 0) return 0;
    if (v & (v - 1)) return 0;
    int s = 0; long t = v;
    while (t > 1) { t >>= 1; s++; }
    *shift = s;
    return 1;
}

static int compute_max_temp(const IRModule *mod) {
    int max_temp = 0;
    for (int i = 0; i < mod->instr_count; i++) {
        const IRInstr *ins = &mod->instrs[i];
        if (ins->dest.kind      == IROP_TEMP && ins->dest.temp_id      > max_temp) max_temp = ins->dest.temp_id;
        if (ins->src1.kind      == IROP_TEMP && ins->src1.temp_id      > max_temp) max_temp = ins->src1.temp_id;
        if (ins->src2.kind      == IROP_TEMP && ins->src2.temp_id      > max_temp) max_temp = ins->src2.temp_id;
        if (ins->extra_src.kind == IROP_TEMP && ins->extra_src.temp_id > max_temp) max_temp = ins->extra_src.temp_id;
        for (int j = 0; j < ins->arg_count; j++)
            if (ins->args[j].kind == IROP_TEMP && ins->args[j].temp_id > max_temp) max_temp = ins->args[j].temp_id;
    }
    return max_temp + 1;
}

/* ---- constant folding + strength reduction ---- */

int opt_constant_fold(IRModule *mod) {
    int changes = 0;

    for (int i = 0; i < mod->instr_count; i++) {
        IRInstr *ins = &mod->instrs[i];
        long lv, rv;

        /* unary int/float negation, logical not, bitwise not */
        if (ins->op == IR_NEG || ins->op == IR_NOT || ins->op == IR_BITNOT) {
            double fv;
            if (ins->op == IR_NEG && op_is_float_const(&ins->src1, &fv) && ins->src1.kind == IROP_CONST_FLOAT) {
                ins->op = IR_CONST_FLOAT; ins->src1 = irop_const_float(-fv); ins->src2 = irop_none();
                changes++; continue;
            }
            if (op_is_int_const(&ins->src1, &lv)) {
                long result = 0;
                if (ins->op == IR_NEG)    result = -lv;
                if (ins->op == IR_NOT)    result = !lv ? 1 : 0;
                if (ins->op == IR_BITNOT) result = ~lv;
                ins->op   = IR_CONST_INT;
                ins->src1 = irop_const_int(result);
                ins->src2 = irop_none();
                changes++;
            }
            continue;
        }

        /* strength reduction: mul by power-of-two -> shift (safe for both signs) */
        if (ins->op == IR_MUL) {
            int shift;
            long other;
            if (op_is_int_const(&ins->src2, &rv) && ins->src1.kind != IROP_CONST_INT &&
                is_pow2(rv, &shift)) {
                ins->op = IR_SHL; ins->src2 = irop_const_int(shift);
                changes++; continue;
            }
            if (op_is_int_const(&ins->src1, &lv) && ins->src2.kind != IROP_CONST_INT &&
                is_pow2(lv, &shift)) {
                other = 0; (void)other;
                IROperand base = ins->src2;
                ins->op = IR_SHL; ins->src1 = base; ins->src2 = irop_const_int(shift);
                changes++; continue;
            }
        }

        /* binary float constants */
        double lf, rf;
        if (op_is_float_const(&ins->src1, &lf) && op_is_float_const(&ins->src2, &rf) &&
            (ins->src1.kind == IROP_CONST_FLOAT || ins->src2.kind == IROP_CONST_FLOAT)) {
            int ffold = 1;
            double fresult = 0;
            switch (ins->op) {
            case IR_ADD: fresult = lf + rf; break;
            case IR_SUB: fresult = lf - rf; break;
            case IR_MUL: fresult = lf * rf; break;
            case IR_DIV: fresult = rf != 0.0 ? lf / rf : 0.0; break;
            default: ffold = 0; break;
            }
            if (ffold) {
                ins->op = IR_CONST_FLOAT; ins->src1 = irop_const_float(fresult); ins->src2 = irop_none();
                changes++; continue;
            }
            int fcmp = 1; long cresult = 0;
            switch (ins->op) {
            case IR_EQ:  cresult = lf == rf; break;
            case IR_NEQ: cresult = lf != rf; break;
            case IR_LT:  cresult = lf <  rf; break;
            case IR_LE:  cresult = lf <= rf; break;
            case IR_GT:  cresult = lf >  rf; break;
            case IR_GE:  cresult = lf >= rf; break;
            default: fcmp = 0; break;
            }
            if (fcmp) {
                ins->op = IR_CONST_BOOL; ins->src1 = irop_const_bool((int)cresult); ins->src2 = irop_none();
                changes++; continue;
            }
        }

        /* binary int constants: arithmetic, compare, bitwise */
        if (!op_is_int_const(&ins->src1, &lv)) continue;
        if (!op_is_int_const(&ins->src2, &rv)) continue;

        long result = 0;
        int  fold   = 1;
        switch (ins->op) {
        case IR_ADD:    result = lv + rv; break;
        case IR_SUB:    result = lv - rv; break;
        case IR_MUL:    result = lv * rv; break;
        case IR_DIV:    result = rv != 0 ? lv / rv : 0; break;
        case IR_MOD:    result = rv != 0 ? lv % rv : 0; break;
        case IR_EQ:     result = lv == rv; break;
        case IR_NEQ:    result = lv != rv; break;
        case IR_LT:     result = lv <  rv; break;
        case IR_LE:     result = lv <= rv; break;
        case IR_GT:     result = lv >  rv; break;
        case IR_GE:     result = lv >= rv; break;
        case IR_BITAND: result = lv & rv; break;
        case IR_BITOR:  result = lv | rv; break;
        case IR_BITXOR: result = lv ^ rv; break;
        case IR_SHL:    result = lv << rv; break;
        case IR_SHR:    result = lv >> rv; break;
        default:        fold   = 0;       break;
        }
        if (!fold) continue;

        ins->op   = IR_CONST_INT;
        ins->src1 = irop_const_int(result);
        ins->src2 = irop_none();
        changes++;
    }

    return changes;
}

/* ---- combined copy/const propagation + algebraic identities +
        store->load / load->load forwarding for non-aliased vars ----

   Temps are allocated monotonically per-function (SSA-like: each temp
   is defined exactly once and only used after its definition), so a
   single forward scan can resolve and substitute immediately. */

typedef enum { TS_UNKNOWN = 0, TS_CONST, TS_COPY } TSKind;
typedef struct { TSKind kind; IROperand val; } TempState;

typedef struct { const char *name; IROperand val; } VarCacheEntry;

static int resolve_operand(IROperand *op, TempState *ts, int max_temp) {
    if (op->kind != IROP_TEMP) return 0;
    int id = op->temp_id;
    if (id < 0 || id >= max_temp) return 0;
    if (ts[id].kind == TS_CONST) { *op = ts[id].val; return 1; }
    if (ts[id].kind == TS_COPY)  { op->temp_id = ts[id].val.temp_id; return 1; }
    return 0;
}

static void define_const(TempState *ts, int id, IROperand val) { ts[id].kind = TS_CONST; ts[id].val = val; }
static void define_copy(TempState *ts, int id, int target)     { ts[id].kind = TS_COPY;  ts[id].val = irop_temp(target); }

static int var_is_addr_taken(char **names, int n, const char *name) {
    if (!name) return 0;
    for (int i = 0; i < n; i++) if (strcmp(names[i], name) == 0) return 1;
    return 0;
}

static int vc_find(VarCacheEntry *vc, int n, const char *name) {
    for (int i = 0; i < n; i++) if (strcmp(vc[i].name, name) == 0) return i;
    return -1;
}

static void vc_set(VarCacheEntry **vc, int *n, int *cap, const char *name, IROperand val) {
    int idx = vc_find(*vc, *n, name);
    if (idx >= 0) { (*vc)[idx].val = val; return; }
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 16;
        *vc = realloc(*vc, (*cap) * sizeof(VarCacheEntry));
    }
    (*vc)[*n].name = name;
    (*vc)[*n].val  = val;
    (*n)++;
}

int opt_constant_prop(IRModule *mod) {
    int max_temp = compute_max_temp(mod);
    TempState *ts = calloc(max_temp, sizeof(TempState));
    int changes = 0;

    /* vars whose address is ever taken can't be safely forwarded */
    char **addr_taken = NULL;
    int addr_n = 0, addr_cap = 0;
    for (int i = 0; i < mod->instr_count; i++) {
        IRInstr *ins = &mod->instrs[i];
        if (ins->op == IR_ADDROF && ins->str_extra) {
            if (addr_n == addr_cap) { addr_cap = addr_cap ? addr_cap * 2 : 8; addr_taken = realloc(addr_taken, addr_cap * sizeof(char *)); }
            addr_taken[addr_n++] = ins->str_extra;
        }
    }

    VarCacheEntry *vc = NULL;
    int vc_n = 0, vc_cap = 0;

    for (int i = 0; i < mod->instr_count; i++) {
        IRInstr *ins = &mod->instrs[i];

        if (ins->op == IR_FUNC_BEGIN) {
            memset(ts, 0, max_temp * sizeof(TempState));
            vc_n = 0;
            continue;
        }
        if (ins->op == IR_FUNC_END) continue;
        if (ins->op == IR_LABEL)    { vc_n = 0; continue; } /* conservative: control-flow join */

        changes += resolve_operand(&ins->src1,      ts, max_temp);
        changes += resolve_operand(&ins->src2,      ts, max_temp);
        changes += resolve_operand(&ins->extra_src, ts, max_temp);
        for (int j = 0; j < ins->arg_count; j++)
            changes += resolve_operand(&ins->args[j], ts, max_temp);

        /* store/load forwarding for variables never address-taken */
        if (ins->op == IR_STORE_VAR && ins->str_extra) {
            if (!var_is_addr_taken(addr_taken, addr_n, ins->str_extra))
                vc_set(&vc, &vc_n, &vc_cap, ins->str_extra, ins->src1);
        } else if (ins->op == IR_LOAD_VAR && ins->str_extra) {
            if (!var_is_addr_taken(addr_taken, addr_n, ins->str_extra)) {
                int idx = vc_find(vc, vc_n, ins->str_extra);
                if (idx >= 0) {
                    if (ins->dest.kind == IROP_TEMP) {
                        int did = ins->dest.temp_id;
                        if (did >= 0 && did < max_temp) {
                            IROperand cached = vc[idx].val;
                            if (cached.kind == IROP_TEMP) define_copy(ts, did, cached.temp_id);
                            else                           define_const(ts, did, cached);
                        }
                    }
                    ins->op = IR_NOP;
                    changes++;
                    continue;
                } else if (ins->dest.kind == IROP_TEMP) {
                    vc_set(&vc, &vc_n, &vc_cap, ins->str_extra, ins->dest);
                }
            }
        } else if (ins->op == IR_CALL || ins->op == IR_ASM_BLOCK || ins->op == IR_STORE_PTR ||
                   ins->op == IR_STORE_VOLATILE || ins->op == IR_MEMSET || ins->op == IR_MEMCPY ||
                   ins->op == IR_NEW) {
            vc_n = 0; /* may write through an aliased pointer */
        }

        /* const/copy definition tracking, including algebraic identities */
        if (ins->dest.kind == IROP_TEMP) {
            int did = ins->dest.temp_id;
            if (did >= 0 && did < max_temp) {
                int did_set = 0;
                long lv, rv;
                int have_l = op_is_int_const(&ins->src1, &lv);
                int have_r = op_is_int_const(&ins->src2, &rv);

                switch (ins->op) {
                case IR_CONST_INT: case IR_CONST_BOOL: case IR_CONST_NIL:
                case IR_CONST_FLOAT: case IR_CONST_STR:
                    define_const(ts, did, ins->src1); did_set = 1; break;
                case IR_ADD:
                    if (have_r && rv == 0 && ins->src1.kind == IROP_TEMP) { define_copy(ts, did, ins->src1.temp_id); did_set = 1; }
                    else if (have_l && lv == 0 && ins->src2.kind == IROP_TEMP) { define_copy(ts, did, ins->src2.temp_id); did_set = 1; }
                    break;
                case IR_SUB:
                    if (have_r && rv == 0 && ins->src1.kind == IROP_TEMP) { define_copy(ts, did, ins->src1.temp_id); did_set = 1; }
                    break;
                case IR_MUL:
                    if ((have_r && rv == 0) || (have_l && lv == 0)) { define_const(ts, did, irop_const_int(0)); did_set = 1; }
                    else if (have_r && rv == 1 && ins->src1.kind == IROP_TEMP) { define_copy(ts, did, ins->src1.temp_id); did_set = 1; }
                    else if (have_l && lv == 1 && ins->src2.kind == IROP_TEMP) { define_copy(ts, did, ins->src2.temp_id); did_set = 1; }
                    break;
                case IR_DIV:
                    if (have_r && rv == 1 && ins->src1.kind == IROP_TEMP) { define_copy(ts, did, ins->src1.temp_id); did_set = 1; }
                    break;
                case IR_BITOR: case IR_BITXOR:
                    if (have_r && rv == 0 && ins->src1.kind == IROP_TEMP) { define_copy(ts, did, ins->src1.temp_id); did_set = 1; }
                    else if (have_l && lv == 0 && ins->src2.kind == IROP_TEMP) { define_copy(ts, did, ins->src2.temp_id); did_set = 1; }
                    break;
                case IR_BITAND:
                    if ((have_r && rv == 0) || (have_l && lv == 0)) { define_const(ts, did, irop_const_int(0)); did_set = 1; }
                    break;
                case IR_SHL: case IR_SHR:
                    if (have_r && rv == 0 && ins->src1.kind == IROP_TEMP) { define_copy(ts, did, ins->src1.temp_id); did_set = 1; }
                    break;
                default: break;
                }

                if (did_set) changes++;
                else ts[did].kind = TS_UNKNOWN;
            }
        }
    }

    free(vc);
    free(addr_taken);
    free(ts);
    return changes;
}

/* ---- dead code elimination ---- */

static void mark_used(const IROperand *op, int *used, int max_temp) {
    if (op->kind == IROP_TEMP && op->temp_id >= 0 && op->temp_id < max_temp)
        used[op->temp_id] = 1;
}

int opt_dce(IRModule *mod) {
    int max_temp = compute_max_temp(mod);
    int *used = calloc(max_temp, sizeof(int));
    int  changes = 0;

    for (int i = 0; i < mod->instr_count; i++) {
        const IRInstr *ins = &mod->instrs[i];
        mark_used(&ins->src1,      used, max_temp);
        mark_used(&ins->src2,      used, max_temp);
        mark_used(&ins->extra_src, used, max_temp);
        for (int j = 0; j < ins->arg_count; j++)
            mark_used(&ins->args[j], used, max_temp);
    }

    for (int i = 0; i < mod->instr_count; i++) {
        IRInstr *ins = &mod->instrs[i];
        if (has_side_effects(ins))        continue;
        if (ins->dest.kind != IROP_TEMP)  continue;
        int tid = ins->dest.temp_id;
        if (tid >= 0 && tid < max_temp && !used[tid]) {
            ins->op = IR_NOP;
            changes++;
        }
    }

    free(used);
    return changes;
}

/* ---- branch folding: constant conditions, jump threading, unreachable code ---- */

int opt_branch_fold(IRModule *mod) {
    int changes = 0;

    int max_label = -1;
    for (int i = 0; i < mod->instr_count; i++) {
        const IRInstr *ins = &mod->instrs[i];
        if (ins->op == IR_LABEL && ins->dest.kind == IROP_LABEL_ID && ins->dest.label_id > max_label)
            max_label = ins->dest.label_id;
    }

    /* 1. constant-condition branches -> unconditional jump or NOP */
    for (int i = 0; i < mod->instr_count; i++) {
        IRInstr *ins = &mod->instrs[i];
        if (ins->op != IR_JUMP_IF && ins->op != IR_JUMP_UNLESS) continue;
        long cv;
        if (!op_is_int_const(&ins->src1, &cv)) continue;
        int take = (ins->op == IR_JUMP_IF) ? (cv != 0) : (cv == 0);
        if (take) { ins->op = IR_JUMP; ins->src1 = ins->src2; ins->src2 = irop_none(); }
        else      { ins->op = IR_NOP; }
        changes++;
    }

    /* 2. jump threading: retarget jumps through label->jump chains */
    if (max_label >= 0) {
        int *label_pos = malloc((max_label + 1) * sizeof(int));
        for (int k = 0; k <= max_label; k++) label_pos[k] = -1;
        for (int i = 0; i < mod->instr_count; i++) {
            const IRInstr *ins = &mod->instrs[i];
            if (ins->op == IR_LABEL && ins->dest.label_id <= max_label)
                label_pos[ins->dest.label_id] = i;
        }

        for (int i = 0; i < mod->instr_count; i++) {
            IRInstr *ins = &mod->instrs[i];
            IROperand *target = NULL;
            if (ins->op == IR_JUMP) target = &ins->src1;
            else if (ins->op == IR_JUMP_IF || ins->op == IR_JUMP_UNLESS) target = &ins->src2;
            if (!target || target->kind != IROP_LABEL_ID) continue;

            int cur = target->label_id;
            int hops = 0;
            while (hops < 32 && cur >= 0 && cur <= max_label && label_pos[cur] >= 0) {
                int j = label_pos[cur] + 1;
                while (j < mod->instr_count && mod->instrs[j].op == IR_NOP) j++;
                if (j >= mod->instr_count) break;
                if (mod->instrs[j].op == IR_JUMP && mod->instrs[j].src1.kind == IROP_LABEL_ID) {
                    if (mod->instrs[j].src1.label_id == cur) break; /* self loop guard */
                    cur = mod->instrs[j].src1.label_id;
                    hops++;
                    continue;
                }
                break;
            }
            if (cur != target->label_id) { target->label_id = cur; changes++; }
        }
        free(label_pos);
    }

    /* 3. unreachable code between an unconditional jump/return and the next label */
    int dead = 0;
    for (int i = 0; i < mod->instr_count; i++) {
        IRInstr *ins = &mod->instrs[i];
        if (ins->op == IR_FUNC_BEGIN) { dead = 0; continue; }
        if (dead) {
            if (ins->op == IR_LABEL || ins->op == IR_FUNC_END) dead = 0;
            else if (ins->op != IR_NOP) { ins->op = IR_NOP; changes++; }
            continue;
        }
        if (ins->op == IR_JUMP || ins->op == IR_RETURN) dead = 1;
    }

    return changes;
}

/* ---- whole-function unused-code elimination ---- */

typedef struct {
    const char *name;   /* points into the IR_FUNC_BEGIN's str_extra */
    int  begin;         /* index of IR_FUNC_BEGIN                    */
    int  end;           /* index of IR_FUNC_END                      */
    int  weak;          /* came from an include -> droppable         */
    int  reachable;
} FuncSpan;

static int span_find(const FuncSpan *spans, int n, const char *name) {
    if (!name) return -1;
    for (int i = 0; i < n; i++)
        if (spans[i].name && strcmp(spans[i].name, name) == 0) return i;
    return -1;
}

static int func_is_weak(const IRModule *mod, const char *name) {
    if (!name) return 0;
    for (int i = 0; i < mod->weak_func_count; i++)
        if (strcmp(mod->weak_funcs[i], name) == 0) return 1;
    return 0;
}

int opt_strip_unreachable(IRModule *mod) {
    if (mod->weak_func_count == 0) return 0;

    /* 1. index every function's instruction range */
    int span_cap = 64, span_n = 0;
    FuncSpan *spans = malloc(span_cap * sizeof(FuncSpan));
    for (int i = 0; i < mod->instr_count; i++) {
        if (mod->instrs[i].op != IR_FUNC_BEGIN) continue;
        if (span_n == span_cap) {
            span_cap *= 2;
            spans = realloc(spans, span_cap * sizeof(FuncSpan));
        }
        int end = i;
        while (end < mod->instr_count && mod->instrs[end].op != IR_FUNC_END) end++;
        spans[span_n].name      = mod->instrs[i].str_extra;
        spans[span_n].begin     = i;
        spans[span_n].end       = end < mod->instr_count ? end : mod->instr_count - 1;
        spans[span_n].weak      = func_is_weak(mod, mod->instrs[i].str_extra);
        spans[span_n].reachable = 0;
        span_n++;
        i = end;
    }
    if (span_n == 0) { free(spans); return 0; }

    /* 2. roots: everything the user wrote themselves. Anything not marked weak
          is a root, which conveniently also covers the case where the entry
          point isn't called `main` (freestanding targets, naked entry stubs)
          and the case where the whole translation unit IS a library. */
    int *work = malloc(span_n * sizeof(int));
    int work_n = 0;
    for (int i = 0; i < span_n; i++) {
        if (spans[i].weak) continue;
        spans[i].reachable = 1;
        work[work_n++] = i;
    }

    /* 3. transitive closure over calls and function-address references */
    while (work_n > 0) {
        int fi = work[--work_n];
        for (int i = spans[fi].begin; i <= spans[fi].end; i++) {
            const IRInstr *ins = &mod->instrs[i];

            const char *target = NULL;
            char ctor_name[256];

            if (ins->op == IR_CALL || ins->op == IR_ADDROF_FN) {
                target = ins->str_extra;
            } else if (ins->op == IR_ARENA_ALLOC || ins->op == IR_NEW) {
                /* `new Foo(...)` doesn't reference Foo__ctor anywhere in the
                   IR — ir_to_mir.c synthesises that call later, after this
                   pass has already run. Without treating the allocation itself
                   as a reference, the constructor of any class defined in an
                   included file gets stripped and the program fails to link
                   with "undefined reference to Foo__ctor". */
                if (ins->str_extra) {
                    snprintf(ctor_name, sizeof(ctor_name), "%s__ctor", ins->str_extra);
                    target = ctor_name;
                }
            } else {
                continue;
            }

            int callee = span_find(spans, span_n, target);
            if (callee < 0 || spans[callee].reachable) continue;
            spans[callee].reachable = 1;
            work[work_n++] = callee;
        }
    }
    free(work);

    /* 4. compact the instruction stream, dropping unreachable weak functions.
          Removing the range outright (rather than NOP-ing it) matters: the
          IR_FUNC_BEGIN/IR_FUNC_END pair is what ir_to_mir.c keys on to start
          and finish a MIR function, so a NOP'd-out body would still produce a
          real, empty, emitted function. */
    int removed = 0;
    int keep = 0;
    int drop_until = -1;
    for (int i = 0; i < mod->instr_count; i++) {
        if (drop_until < 0) {
            for (int f = 0; f < span_n; f++) {
                if (spans[f].begin == i && spans[f].weak && !spans[f].reachable) {
                    drop_until = spans[f].end;
                    removed++;
                    break;
                }
            }
        }
        if (drop_until >= 0) {
            if (i >= drop_until) drop_until = -1;  /* this was IR_FUNC_END */
            continue;
        }
        if (keep != i) mod->instrs[keep] = mod->instrs[i];
        keep++;
    }
    mod->instr_count = keep;

    free(spans);
    return removed;
}

/* ---- driver ---- */

int opt_run_all(IRModule *mod) {
    /* Prune whole unreachable functions FIRST, so the per-instruction passes
       below never spend time on code that's about to be deleted — and, more
       importantly, so a malformed unused stdlib function can't fail the build
       of a program that never calls it. */
    int total = opt_strip_unreachable(mod);
    int delta;
    for (int iter = 0; iter < 12; iter++) {
        delta  = opt_constant_fold(mod);
        delta += opt_constant_prop(mod);
        delta += opt_branch_fold(mod);
        delta += opt_dce(mod);
        total += delta;
        if (delta == 0) break;
    }
    return total;
}
