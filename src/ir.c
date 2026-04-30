#include "ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Module ─────────────────────────────────────────────────────────────────── */

IRModule *ir_module_new(void) {
    IRModule *m  = calloc(1, sizeof(IRModule));
    m->instr_cap = 512;
    m->instrs    = calloc(m->instr_cap, sizeof(IRInstr));
    return m;
}

void ir_module_free(IRModule *mod) {
    if (!mod) return;
    free(mod->instrs);
    free(mod->classes);  /* pointer array; Class nodes owned by AST */
    free(mod->enums);
    /* includes[] point into the AST — do not free */
    free(mod);
}

/* ─── Emit ───────────────────────────────────────────────────────────────────── */

IRInstr *ir_emit(IRModule *mod, IROpcode op) {
    if (mod->instr_count >= mod->instr_cap) {
        mod->instr_cap *= 2;
        mod->instrs = realloc(mod->instrs, mod->instr_cap * sizeof(IRInstr));
    }
    IRInstr *ins = &mod->instrs[mod->instr_count++];
    memset(ins, 0, sizeof(*ins));
    ins->op = op;
    return ins;
}

/* ─── Operand constructors ───────────────────────────────────────────────────── */

IROperand irop_none(void) {
    IROperand o; memset(&o, 0, sizeof(o)); o.kind = IROP_NONE; return o;
}
IROperand irop_temp(int id) {
    IROperand o; memset(&o, 0, sizeof(o)); o.kind = IROP_TEMP; o.temp_id = id; return o;
}
IROperand irop_const_int(long val) {
    IROperand o; memset(&o, 0, sizeof(o)); o.kind = IROP_CONST_INT; o.int_val = val; return o;
}
IROperand irop_const_str(const char *val) {
    IROperand o; memset(&o, 0, sizeof(o)); o.kind = IROP_CONST_STR; o.str_val = (char *)val; return o;
}
IROperand irop_const_bool(int val) {
    IROperand o; memset(&o, 0, sizeof(o)); o.kind = IROP_CONST_BOOL; o.bool_val = val; return o;
}
IROperand irop_label(int id) {
    IROperand o; memset(&o, 0, sizeof(o)); o.kind = IROP_LABEL_ID; o.label_id = id; return o;
}

/* ─── Opcode name ────────────────────────────────────────────────────────────── */

const char *ir_opcode_name(IROpcode op) {
    switch (op) {
    case IR_CONST_INT:   return "CONST_INT";
    case IR_CONST_STR:   return "CONST_STR";
    case IR_CONST_BOOL:  return "CONST_BOOL";
    case IR_CONST_NIL:   return "CONST_NIL";
    case IR_LOAD_VAR:    return "LOAD_VAR";
    case IR_STORE_VAR:   return "STORE_VAR";
    case IR_ADD:         return "ADD";
    case IR_SUB:         return "SUB";
    case IR_MUL:         return "MUL";
    case IR_DIV:         return "DIV";
    case IR_MOD:         return "MOD";
    case IR_NEG:         return "NEG";
    case IR_NOT:         return "NOT";
    case IR_EQ:          return "EQ";
    case IR_NEQ:         return "NEQ";
    case IR_LT:          return "LT";
    case IR_LE:          return "LE";
    case IR_GT:          return "GT";
    case IR_GE:          return "GE";
    case IR_LABEL:       return "LABEL";
    case IR_JUMP:        return "JUMP";
    case IR_JUMP_IF:     return "JUMP_IF";
    case IR_JUMP_UNLESS: return "JUMP_UNLESS";
    case IR_CALL:        return "CALL";
    case IR_RETURN:      return "RETURN";
    case IR_NEW:         return "NEW";
    case IR_GET_FIELD:   return "GET_FIELD";
    case IR_SET_FIELD:   return "SET_FIELD";
    case IR_ARRAY_ALLOC: return "ARRAY_ALLOC";
    case IR_ARRAY_INIT:  return "ARRAY_INIT";
    case IR_ARRAY_PUSH:  return "ARRAY_PUSH";
    case IR_ARRAY_POP:   return "ARRAY_POP";
    case IR_ARRAY_LEN:   return "ARRAY_LEN";
    case IR_ARRAY_CAP:   return "ARRAY_CAP";
    case IR_ARRAY_LOAD:  return "ARRAY_LOAD";
    case IR_ARRAY_STORE: return "ARRAY_STORE";
    case IR_MULTI_ALLOC: return "MULTI_ALLOC";
    case IR_ENUM_VAL:    return "ENUM_VAL";
    case IR_INTERP_STR:  return "INTERP_STR";
    case IR_ASM_BLOCK:   return "ASM_BLOCK";
    case IR_PRINT:       return "PRINT";
    case IR_PRINTLN:     return "PRINTLN";
    case IR_ERR:         return "ERR";
    case IR_PANIC:       return "PANIC";
    case IR_FUNC_BEGIN:  return "FUNC_BEGIN";
    case IR_FUNC_END:    return "FUNC_END";
    case IR_ALLOCA:      return "ALLOCA";
    case IR_NOP:         return "NOP";
    default:             return "???";
    }
}

/* ─── Dump ───────────────────────────────────────────────────────────────────── */

static void dump_operand(const IROperand *op, FILE *out) {
    switch (op->kind) {
    case IROP_NONE:       fprintf(out, "-");                          break;
    case IROP_TEMP:       fprintf(out, "t%d", op->temp_id);          break;
    case IROP_CONST_INT:  fprintf(out, "%ld", op->int_val);          break;
    case IROP_CONST_STR:  fprintf(out, "\"%s\"", op->str_val ? op->str_val : ""); break;
    case IROP_CONST_BOOL: fprintf(out, "%s", op->bool_val ? "true" : "false"); break;
    case IROP_LABEL_ID:   fprintf(out, ".L%d", op->label_id);        break;
    }
}

void ir_dump(const IRModule *mod, FILE *out) {
    fprintf(out, "; ══════════════════════════════════════ IR Dump ══\n");
    int in_func = 0;
    for (int i = 0; i < mod->instr_count; i++) {
        const IRInstr *ins = &mod->instrs[i];

        /* Add visual spacing between functions */
        if (ins->op == IR_FUNC_BEGIN) {
            if (in_func) fprintf(out, "\n");
            in_func = 1;
            fprintf(out, "; FUNC_BEGIN %s%s\n",
                    ins->str_extra ? ins->str_extra : "?",
                    ins->extra_int ? " [main]" : "");
            fprintf(out, "; ── fn %s%s ──\n",
                    ins->str_extra ? ins->str_extra : "?",
                    ins->extra_int ? " [main]" : "");
            continue;
        }
        if (ins->op == IR_FUNC_END) { fprintf(out, "\n"); continue; }

        /* Labels don't get indented */
        if (ins->op == IR_LABEL) {
            fprintf(out, ".L%d:\n", ins->dest.label_id); continue;
        }

        fprintf(out, "  %4d  %-14s  ", i, ir_opcode_name(ins->op));

        /* dest = ... */
        if (ins->dest.kind != IROP_NONE && ins->op != IR_LABEL) {
            dump_operand(&ins->dest, out);
            fprintf(out, " = ");
        }

        /* src1, src2 */
        if (ins->src1.kind != IROP_NONE) dump_operand(&ins->src1, out);
        if (ins->src2.kind != IROP_NONE) { fprintf(out, ", "); dump_operand(&ins->src2, out); }
        if (ins->extra_src.kind != IROP_NONE) { fprintf(out, ", "); dump_operand(&ins->extra_src, out); }

        /* annotations */
        if (ins->str_extra)  fprintf(out, "  @%s", ins->str_extra);
        if (ins->str_extra2) fprintf(out, ".%s", ins->str_extra2);
        if (ins->extra_int && ins->op != IR_FUNC_BEGIN && ins->op != IR_ALLOCA)
            fprintf(out, "  [%d]", ins->extra_int);

        /* args for calls */
        if (ins->arg_count > 0) {
            fprintf(out, "  (");
            for (int j = 0; j < ins->arg_count; j++) {
                if (j) fprintf(out, ", ");
                dump_operand(&ins->args[j], out);
            }
            fprintf(out, ")");
        }
        /* params for FUNC_BEGIN */
        if (ins->param_count > 0) {
            fprintf(out, "  params=(");
            for (int j = 0; j < ins->param_count; j++) {
                if (j) fprintf(out, ", ");
                fprintf(out, "%s:%s", ins->params[j].name ? ins->params[j].name : "?",
                        ins->params[j].type_name ? ins->params[j].type_name : "?");
            }
            fprintf(out, ")");
        }
        /* interp segs */
        if (ins->extra_seg_count > 0) {
            fprintf(out, "  segs=[");
            for (int j = 0; j < ins->extra_seg_count; j++) {
                if (j) fprintf(out, "|");
                if (ins->extra_segs[j].is_expr)
                    fprintf(out, "{{%s}}", ins->extra_segs[j].text);
                else
                    fprintf(out, "\"%s\"", ins->extra_segs[j].text);
            }
            fprintf(out, "]");
        }
        fprintf(out, "\n");
    }
    fprintf(out, "; ══════════════════════════════════════ End IR ═══\n");
}
