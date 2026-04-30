#include "opt.h"
#include "ir.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ─── Helpers ────────────────────────────────────────────────────────────────── */

/* True if the instruction's opcode has observable side effects (calls, stores,
   jumps, returns, labels, asm) — such instructions must never be removed. */
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
        return 1;
    default:
        return 0;
    }
}

/* Returns 1 if the operand is a compile-time integer constant. */
static int op_is_int_const(const IROperand *o, long *out) {
    if (o->kind == IROP_CONST_INT)  { *out = o->int_val;  return 1; }
    if (o->kind == IROP_CONST_BOOL) { *out = o->bool_val; return 1; }
    if (o->kind == IROP_NONE)       { *out = 0;           return 1; }
    return 0;
}

/* Substitute one operand: if it references a temp whose constant value is
   known, replace it with a CONST_INT operand. Returns 1 if changed. */
static int subst_operand(IROperand *op, const long *const_map, const int *known,
                         int max_temp) {
    if (op->kind == IROP_TEMP && op->temp_id >= 0 && op->temp_id < max_temp) {
        if (known[op->temp_id]) {
            op->kind    = IROP_CONST_INT;
            op->int_val = const_map[op->temp_id];
            return 1;
        }
    }
    return 0;
}

/* ─── Pass 1: Constant Folding ───────────────────────────────────────────────── */

int opt_constant_fold(IRModule *mod) {
    int changes = 0;

    for (int i = 0; i < mod->instr_count; i++) {
        IRInstr *ins = &mod->instrs[i];
        long lv, rv;

        /* Unary negation / logical-not of a constant */
        if ((ins->op == IR_NEG || ins->op == IR_NOT) &&
             op_is_int_const(&ins->src1, &lv)) {
            long result = (ins->op == IR_NEG) ? -lv : (!lv ? 1 : 0);
            ins->op       = IR_CONST_INT;
            ins->src1     = irop_const_int(result);
            ins->src2     = irop_none();
            changes++;
            continue;
        }

        /* Binary arithmetic / comparison of two constants */
        if (!op_is_int_const(&ins->src1, &lv)) continue;
        if (!op_is_int_const(&ins->src2, &rv)) continue;

        long result = 0;
        int  fold   = 1;
        switch (ins->op) {
        case IR_ADD:  result = lv + rv; break;
        case IR_SUB:  result = lv - rv; break;
        case IR_MUL:  result = lv * rv; break;
        case IR_DIV:  result = rv != 0 ? lv / rv : 0; break;
        case IR_MOD:  result = rv != 0 ? lv % rv : 0; break;
        case IR_EQ:   result = lv == rv; break;
        case IR_NEQ:  result = lv != rv; break;
        case IR_LT:   result = lv <  rv; break;
        case IR_LE:   result = lv <= rv; break;
        case IR_GT:   result = lv >  rv; break;
        case IR_GE:   result = lv >= rv; break;
        default:      fold   = 0;       break;
        }
        if (!fold) continue;

        ins->op   = IR_CONST_INT;
        ins->src1 = irop_const_int(result);
        ins->src2 = irop_none();
        changes++;
    }

    return changes;
}

/* ─── Pass 2: Constant Propagation ──────────────────────────────────────────── */

int opt_constant_prop(IRModule *mod) {
    /* We run a per-function forward pass.  At each IR_FUNC_BEGIN we flush the
       constant map because temps are per-function. */

    /* Determine max temp ID across the whole module to size the map. */
    int max_temp = 0;
    for (int i = 0; i < mod->instr_count; i++) {
        const IRInstr *ins = &mod->instrs[i];
        if (ins->dest.kind  == IROP_TEMP && ins->dest.temp_id  > max_temp) max_temp = ins->dest.temp_id;
        if (ins->src1.kind  == IROP_TEMP && ins->src1.temp_id  > max_temp) max_temp = ins->src1.temp_id;
        if (ins->src2.kind  == IROP_TEMP && ins->src2.temp_id  > max_temp) max_temp = ins->src2.temp_id;
        for (int j = 0; j < ins->arg_count; j++)
            if (ins->args[j].kind == IROP_TEMP && ins->args[j].temp_id > max_temp)
                max_temp = ins->args[j].temp_id;
    }
    max_temp++; /* size */

    long *const_map = calloc(max_temp, sizeof(long));
    int  *known     = calloc(max_temp, sizeof(int));
    int   changes   = 0;

    for (int i = 0; i < mod->instr_count; i++) {
        IRInstr *ins = &mod->instrs[i];

        /* Reset map at function boundaries */
        if (ins->op == IR_FUNC_BEGIN) {
            memset(const_map, 0, max_temp * sizeof(long));
            memset(known,     0, max_temp * sizeof(int));
            continue;
        }
        if (ins->op == IR_FUNC_END) continue;

        /* Substitute known constants into source operands */
        changes += subst_operand(&ins->src1,      const_map, known, max_temp);
        changes += subst_operand(&ins->src2,      const_map, known, max_temp);
        changes += subst_operand(&ins->extra_src, const_map, known, max_temp);
        for (int j = 0; j < ins->arg_count; j++)
            changes += subst_operand(&ins->args[j], const_map, known, max_temp);

        /* If this instruction now defines a temp with a constant value, record it */
        if (ins->dest.kind != IROP_TEMP) continue;
        int tid = ins->dest.temp_id;
        if (tid < 0 || tid >= max_temp) continue;

        long val;
        if ((ins->op == IR_CONST_INT || ins->op == IR_CONST_BOOL || ins->op == IR_CONST_NIL) &&
             op_is_int_const(&ins->src1, &val)) {
            const_map[tid] = val;
            known[tid]     = 1;
        } else if (ins->op == IR_CONST_NIL) {
            const_map[tid] = 0; known[tid] = 1;
        } else {
            /* Non-constant definition — invalidate */
            known[tid] = 0;
        }
    }

    free(const_map);
    free(known);
    return changes;
}

/* ─── Pass 3: Dead-Code Elimination ─────────────────────────────────────────── */

/* Helper: mark a single operand as "used" in the used[] bitmap. */
static void mark_used(const IROperand *op, int *used, int max_temp) {
    if (op->kind == IROP_TEMP && op->temp_id >= 0 && op->temp_id < max_temp)
        used[op->temp_id] = 1;
}

int opt_dce(IRModule *mod) {
    /* Determine max temp ID across the whole module. */
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
    max_temp++; /* size of bitmap */

    int *used = calloc(max_temp, sizeof(int));
    int  changes = 0;

    /* Pass A: mark all temps used as sources across all instructions. */
    for (int i = 0; i < mod->instr_count; i++) {
        const IRInstr *ins = &mod->instrs[i];
        mark_used(&ins->src1,      used, max_temp);
        mark_used(&ins->src2,      used, max_temp);
        mark_used(&ins->extra_src, used, max_temp);
        for (int j = 0; j < ins->arg_count; j++)
            mark_used(&ins->args[j], used, max_temp);
    }

    /* Pass B: NOP out pure instructions whose dest temp is never consumed. */
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

/* ─── Run all passes ─────────────────────────────────────────────────────────── */

int opt_run_all(IRModule *mod) {
    int total = 0;
    int delta;
    /* Run until fixed point (up to 8 iterations to avoid infinite loops) */
    for (int iter = 0; iter < 8; iter++) {
        delta  = opt_constant_fold(mod);
        delta += opt_constant_prop(mod);
        delta += opt_dce(mod);
        total += delta;
        if (delta == 0) break;
    }
    return total;
}
