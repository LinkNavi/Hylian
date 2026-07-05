#define _POSIX_C_SOURCE 200809L
#include "hymir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

MIRModule *mir_module_new(void) {
    MIRModule *m = calloc(1, sizeof(MIRModule));
    m->func_cap = 8;
    m->funcs = calloc(m->func_cap, sizeof(MIRFunc *));
    m->str_cap = 8;
    m->strs = calloc(m->str_cap, sizeof(MIRStringConst));
    m->global_cap = 8;
    m->globals = calloc(m->global_cap, sizeof(MIRGlobalVar));
    return m;
}

void mir_module_free(MIRModule *mod) {
    if (!mod) return;
    for (int i = 0; i < mod->func_count; i++) {
        MIRFunc *f = mod->funcs[i];
        for (int j = 0; j < f->count; j++) {
            if (f->instrs[j].args) free(f->instrs[j].args);
        }
        free(f->instrs);
        free(f->name);
        free(f);
    }
    free(mod->funcs);
    for (int i = 0; i < mod->str_count; i++) { free(mod->strs[i].label); free(mod->strs[i].data); }
    free(mod->strs);
    for (int i = 0; i < mod->global_count; i++) free(mod->globals[i].name);
    free(mod->globals);
    free(mod);
}

MIRFunc *mir_func_new(MIRModule *mod, const char *name) {
    if (mod->func_count == mod->func_cap) {
        mod->func_cap *= 2;
        mod->funcs = realloc(mod->funcs, mod->func_cap * sizeof(MIRFunc *));
    }
    MIRFunc *f = calloc(1, sizeof(MIRFunc));
    f->name = strdup(name);
    f->cap = 32;
    f->instrs = calloc(f->cap, sizeof(MIRInstr));
    mod->funcs[mod->func_count++] = f;
    return f;
}

int mir_new_vreg(MIRFunc *fn) {
    return fn->vreg_count++;
}

MIRInstr *mir_emit(MIRFunc *fn, MIROp op) {
    if (fn->count == fn->cap) {
        fn->cap *= 2;
        fn->instrs = realloc(fn->instrs, fn->cap * sizeof(MIRInstr));
    }
    MIRInstr *ins = &fn->instrs[fn->count++];
    memset(ins, 0, sizeof(MIRInstr));
    ins->op = op;
    ins->dest = mir_none();
    ins->src1 = mir_none();
    ins->src2 = mir_none();
    return ins;
}

MIRValue mir_vreg(int id, MIRType type) {
    MIRValue v = {0};
    v.kind = MIRV_VREG;
    v.type = type;
    v.vreg = id;
    return v;
}

MIRValue mir_imm_int(int64_t val, MIRType type) {
    MIRValue v = {0};
    v.kind = MIRV_IMM_INT;
    v.type = type;
    v.imm_int = val;
    return v;
}

MIRValue mir_imm_float(double val, MIRType type) {
    MIRValue v = {0};
    v.kind = MIRV_IMM_FLOAT;
    v.type = type;
    v.imm_float = val;
    return v;
}

MIRValue mir_label(int id) {
    MIRValue v = {0};
    v.kind = MIRV_LABEL;
    v.label_id = id;
    return v;
}

MIRValue mir_global(const char *name, MIRType type) {
    MIRValue v = {0};
    v.kind = MIRV_GLOBAL;
    v.type = type;
    v.global_name = name;
    return v;
}

MIRValue mir_none(void) {
    MIRValue v = {0};
    v.kind = MIRV_IMM_INT;
    v.type = MIR_I64;
    v.imm_int = 0;
    return v;
}

const char *mir_module_intern_string(MIRModule *mod, const char *data, int len) {
    /* dedupe identical literals so repeated string constants share storage */
    for (int i = 0; i < mod->str_count; i++) {
        if (mod->strs[i].len == len && memcmp(mod->strs[i].data, data, len) == 0)
            return mod->strs[i].label;
    }
    if (mod->str_count == mod->str_cap) {
        mod->str_cap *= 2;
        mod->strs = realloc(mod->strs, mod->str_cap * sizeof(MIRStringConst));
    }
    MIRStringConst *s = &mod->strs[mod->str_count];
    char label[32];
    snprintf(label, sizeof(label), "_Lstr%d", mod->str_count);
    s->label = strdup(label);
    s->data = malloc(len);
    memcpy(s->data, data, len);
    s->len = len;
    mod->str_count++;
    return s->label;
}

void mir_module_add_global(MIRModule *mod, const char *name, int has_init, int64_t init_val, int size) {
    if (mod->global_count == mod->global_cap) {
        mod->global_cap *= 2;
        mod->globals = realloc(mod->globals, mod->global_cap * sizeof(MIRGlobalVar));
    }
    MIRGlobalVar *g = &mod->globals[mod->global_count++];
    g->name = strdup(name);
    g->has_init = has_init;
    g->init_val = init_val;
    g->size = size;
}

const char *mir_op_name(MIROp op) {
    switch (op) {
    case MIR_MOV: return "mov";
    case MIR_ADD: return "add"; case MIR_SUB: return "sub"; case MIR_MUL: return "mul";
    case MIR_SDIV: return "sdiv"; case MIR_UDIV: return "udiv";
    case MIR_SMOD: return "smod"; case MIR_UMOD: return "umod";
    case MIR_NEG: return "neg"; case MIR_NOT: return "not";
    case MIR_AND: return "and"; case MIR_OR: return "or"; case MIR_XOR: return "xor";
    case MIR_SHL: return "shl"; case MIR_SHR_A: return "shr_a"; case MIR_SHR_L: return "shr_l";
    case MIR_FADD: return "fadd"; case MIR_FSUB: return "fsub";
    case MIR_FMUL: return "fmul"; case MIR_FDIV: return "fdiv"; case MIR_FNEG: return "fneg";
    case MIR_CMP_EQ: return "cmp_eq"; case MIR_CMP_NE: return "cmp_ne";
    case MIR_CMP_LT: return "cmp_lt"; case MIR_CMP_LE: return "cmp_le";
    case MIR_CMP_GT: return "cmp_gt"; case MIR_CMP_GE: return "cmp_ge";
    case MIR_UCMP_LT: return "ucmp_lt"; case MIR_UCMP_LE: return "ucmp_le";
    case MIR_UCMP_GT: return "ucmp_gt"; case MIR_UCMP_GE: return "ucmp_ge";
    case MIR_FCMP_LT: return "fcmp_lt"; case MIR_FCMP_LE: return "fcmp_le";
    case MIR_FCMP_GT: return "fcmp_gt"; case MIR_FCMP_GE: return "fcmp_ge";
    case MIR_FCMP_EQ: return "fcmp_eq"; case MIR_FCMP_NE: return "fcmp_ne";
    case MIR_SEXT: return "sext"; case MIR_ZEXT: return "zext"; case MIR_TRUNC: return "trunc";
    case MIR_I2F: return "i2f"; case MIR_F2I: return "f2i"; case MIR_F2F: return "f2f";
    case MIR_BITCAST: return "bitcast";
    case MIR_LOAD: return "load"; case MIR_STORE: return "store";
    case MIR_LEA_LOCAL: return "lea_local"; case MIR_LEA_GLOBAL: return "lea_global";
    case MIR_ALLOCA_LOCAL: return "alloca_local";
    case MIR_LABEL: return "label"; case MIR_JMP: return "jmp";
    case MIR_JMP_IF: return "jmp_if"; case MIR_JMP_UNLESS: return "jmp_unless";
    case MIR_CALL: return "call"; case MIR_RET: return "ret";
    case MIR_ASM_RAW: return "asm_raw";
    case MIR_CLI: return "cli"; case MIR_STI: return "sti"; case MIR_IRET: return "iret";
    case MIR_LGDT: return "lgdt"; case MIR_LIDT: return "lidt";
    case MIR_LTR: return "ltr"; case MIR_INVLPG: return "invlpg";
    case MIR_WRMSR: return "wrmsr"; case MIR_RDMSR: return "rdmsr";
    case MIR_READ_CR: return "read_cr"; case MIR_WRITE_CR: return "write_cr";
    case MIR_OUTB: return "outb"; case MIR_INB: return "inb";
    case MIR_SAVE_REGS: return "save_regs"; case MIR_RESTORE_REGS: return "restore_regs";
    case MIR_MEMSET: return "memset"; case MIR_MEMCPY: return "memcpy";
    case MIR_NOP: return "nop";
    }
    return "?";
}

static void dump_val(const MIRValue *v, FILE *out) {
    switch (v->kind) {
    case MIRV_VREG: fprintf(out, "v%d", v->vreg); break;
    case MIRV_IMM_INT: fprintf(out, "%lld", (long long)v->imm_int); break;
    case MIRV_IMM_FLOAT: fprintf(out, "%f", v->imm_float); break;
    case MIRV_LABEL: fprintf(out, ".L%d", v->label_id); break;
    case MIRV_GLOBAL: fprintf(out, "@%s", v->global_name); break;
    }
}

void mir_dump(const MIRModule *mod, FILE *out) {
    for (int i = 0; i < mod->func_count; i++) {
        MIRFunc *f = mod->funcs[i];
        fprintf(out, "func %s%s:\n", f->name, f->is_naked ? " (naked)" : "");
        for (int j = 0; j < f->count; j++) {
            MIRInstr *ins = &f->instrs[j];
            fprintf(out, "  %s", mir_op_name(ins->op));
            fprintf(out, " ");
            dump_val(&ins->dest, out);
            fprintf(out, ", ");
            dump_val(&ins->src1, out);
            fprintf(out, ", ");
            dump_val(&ins->src2, out);
            fprintf(out, "\n");
        }
    }
}

MIRInstr *mir_build_call(MIRFunc *fn, const char *callee, MIRValue *args, int arg_count, MIRValue dest) {
    MIRInstr *m = mir_emit(fn, MIR_CALL);
    m->callee = callee;
    m->args = args;
    m->arg_count = arg_count;
    m->dest = dest;
    return m;
}

static int is_terminator(MIROp op) {
    return op == MIR_RET || op == MIR_JMP || op == MIR_JMP_IF || op == MIR_JMP_UNLESS
        || op == MIR_IRET;
}

static int is_float_op(MIROp op) {
    switch (op) {
    case MIR_FADD: case MIR_FSUB: case MIR_FMUL: case MIR_FDIV: case MIR_FNEG:
    case MIR_FCMP_LT: case MIR_FCMP_LE: case MIR_FCMP_GT: case MIR_FCMP_GE:
    case MIR_FCMP_EQ: case MIR_FCMP_NE: case MIR_I2F:
        return 1;
    default:
        return 0;
    }
}

MIRVerifyResult mir_verify(const MIRModule *mod) {
    MIRVerifyResult r = {1, ""};

    for (int fi = 0; fi < mod->func_count; fi++) {
        MIRFunc *fn = mod->funcs[fi];
        if (fn->count == 0) continue;

        /* track vreg definitions seen so far (dense-ish ids, linear scan is fine
           for typical function sizes) */
        int *defined = calloc(fn->vreg_count + 1, sizeof(int));

        for (int i = 0; i < fn->count; i++) {
            MIRInstr *ins = &fn->instrs[i];

            /* use-before-def check on plain vreg sources */
            MIRValue *srcs[2] = { &ins->src1, &ins->src2 };
            for (int s = 0; s < 2; s++) {
                if (srcs[s]->kind == MIRV_VREG && !defined[srcs[s]->vreg]) {
                    snprintf(r.msg, sizeof(r.msg),
                        "func %s: instr %d (%s) uses v%d before it's defined",
                        fn->name, i, mir_op_name(ins->op), srcs[s]->vreg);
                    r.ok = 0;
                    free(defined);
                    return r;
                }
            }
            for (int a = 0; a < ins->arg_count; a++) {
                if (ins->args[a].kind == MIRV_VREG && !defined[ins->args[a].vreg]) {
                    snprintf(r.msg, sizeof(r.msg),
                        "func %s: instr %d (%s) call arg uses v%d before it's defined",
                        fn->name, i, mir_op_name(ins->op), ins->args[a].vreg);
                    r.ok = 0;
                    free(defined);
                    return r;
                }
            }

            /* float/int op vs operand type mismatch */
            if (is_float_op(ins->op) && ins->op != MIR_I2F) {
                if (ins->src1.kind != MIRV_IMM_INT && ins->src1.kind != MIRV_IMM_FLOAT
                    && !mir_type_is_float(ins->src1.type)) {
                    snprintf(r.msg, sizeof(r.msg),
                        "func %s: instr %d (%s) is a float op but v%d isn't typed float",
                        fn->name, i, mir_op_name(ins->op), ins->src1.vreg);
                    r.ok = 0;
                    free(defined);
                    return r;
                }
            }

            if (ins->dest.kind == MIRV_VREG) defined[ins->dest.vreg] = 1;
        }

        if (!is_terminator(fn->instrs[fn->count - 1].op)) {
            snprintf(r.msg, sizeof(r.msg),
                "func %s: doesn't end in a terminator (ret/jmp/jmp_if/jmp_unless/iret)",
                fn->name);
            r.ok = 0;
        }

        free(defined);
        if (!r.ok) return r;
    }

    return r;
}
