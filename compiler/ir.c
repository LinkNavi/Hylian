#include "ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


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
IROperand irop_const_float(double val) {
    IROperand o; memset(&o, 0, sizeof(o)); o.kind = IROP_CONST_FLOAT; o.float_val = val; return o;
}


const char *ir_opcode_name(IROpcode op) {
    switch (op) {
    case IR_CONST_INT:   return "CONST_INT";
    case IR_CONST_STR:   return "CONST_STR";
    case IR_CONST_BOOL:  return "CONST_BOOL";
    case IR_CONST_NIL:   return "CONST_NIL";
    case IR_CONST_FLOAT: return "CONST_FLOAT";
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
    case IR_NOP:            return "NOP";
    case IR_LOAD_PTR:       return "LOAD_PTR";
    case IR_STORE_PTR:      return "STORE_PTR";
    case IR_LOAD_VOLATILE:  return "LOAD_VOLATILE";
    case IR_STORE_VOLATILE: return "STORE_VOLATILE";
    case IR_ADDROF:         return "ADDROF";
    case IR_STATIC_VAR:     return "STATIC_VAR";
    case IR_BITAND:         return "BITAND";
    case IR_BITOR:          return "BITOR";
    case IR_BITXOR:         return "BITXOR";
    case IR_BITNOT:         return "BITNOT";
    case IR_SHL:            return "SHL";
    case IR_SHR:            return "SHR";
    case IR_MEMSET:         return "MEMSET";
    case IR_MEMCPY:         return "MEMCPY";
    case IR_CLI:            return "CLI";
    case IR_STI:            return "STI";
    case IR_CAST:           return "CAST";
    case IR_ADDROF_FN:      return "ADDROF_FN";
    case IR_READ_CR:        return "READ_CR";
    case IR_WRITE_CR:       return "WRITE_CR";
    case IR_SAVE_REGS:      return "SAVE_REGS";
    case IR_RESTORE_REGS:   return "RESTORE_REGS";
    case IR_IRET:           return "IRET";
    case IR_OUTB:           return "OUTB";
    case IR_INB:            return "INB";
    case IR_ARENA_ALLOC:    return "ARENA_ALLOC";
    default:             return "???";
    }
}


static void dump_operand(const IROperand *op, FILE *out) {
    switch (op->kind) {
    case IROP_NONE:        fprintf(out, "-");                          break;
    case IROP_TEMP:        fprintf(out, "t%d", op->temp_id);          break;
    case IROP_CONST_INT:   fprintf(out, "%ld", op->int_val);          break;
    case IROP_CONST_STR:   fprintf(out, "\"%s\"", op->str_val ? op->str_val : ""); break;
    case IROP_CONST_BOOL:  fprintf(out, "%s", op->bool_val ? "true" : "false"); break;
    case IROP_LABEL_ID:    fprintf(out, ".L%d", op->label_id);        break;
    case IROP_CONST_FLOAT: fprintf(out, "%g", op->float_val);         break;
    }
}

void ir_dump(const IRModule *mod, FILE *out) {
    fprintf(out, "; ══════════════════════════════════════ IR Dump ══\n");
    int in_func = 0;
    for (int i = 0; i < mod->instr_count; i++) {
        const IRInstr *ins = &mod->instrs[i];

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

        if (ins->op == IR_LABEL) {
            fprintf(out, ".L%d:\n", ins->dest.label_id); continue;
        }

        fprintf(out, "  %4d  %-14s  ", i, ir_opcode_name(ins->op));

        if (ins->dest.kind != IROP_NONE && ins->op != IR_LABEL) {
            dump_operand(&ins->dest, out);
            fprintf(out, " = ");
        }

        if (ins->src1.kind != IROP_NONE) dump_operand(&ins->src1, out);
        if (ins->src2.kind != IROP_NONE) { fprintf(out, ", "); dump_operand(&ins->src2, out); }
        if (ins->extra_src.kind != IROP_NONE) { fprintf(out, ", "); dump_operand(&ins->extra_src, out); }

        if (ins->str_extra)  fprintf(out, "  @%s", ins->str_extra);
        if (ins->str_extra2) fprintf(out, ".%s", ins->str_extra2);
        if (ins->extra_int && ins->op != IR_FUNC_BEGIN && ins->op != IR_ALLOCA)
            fprintf(out, "  [%d]", ins->extra_int);

        if (ins->arg_count > 0) {
            fprintf(out, "  (");
            for (int j = 0; j < ins->arg_count; j++) {
                if (j) fprintf(out, ", ");
                dump_operand(&ins->args[j], out);
            }
            fprintf(out, ")");
        }
        if (ins->param_count > 0) {
            fprintf(out, "  params=(");
            for (int j = 0; j < ins->param_count; j++) {
                if (j) fprintf(out, ", ");
                fprintf(out, "%s:%s", ins->params[j].name ? ins->params[j].name : "?",
                        ins->params[j].type_name ? ins->params[j].type_name : "?");
            }
            fprintf(out, ")");
        }
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
