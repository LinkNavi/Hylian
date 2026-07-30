#include "ir_to_mir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- tiny temp_id/var_name -> MIRType maps ---- */

typedef struct { int temp_id; MIRType type; } TempTypeEntry;
typedef struct { char *name; MIRType type; int is_local; int local_id; } VarTypeEntry;

typedef struct {
    TempTypeEntry *temps; int temp_count, temp_cap;
    VarTypeEntry  *vars;  int var_count, var_cap;
} TypeEnv;

static void tenv_init(TypeEnv *e) {
    e->temp_cap = 64; e->temps = calloc(e->temp_cap, sizeof(TempTypeEntry));
    e->temp_count = 0;
    e->var_cap = 32; e->vars = calloc(e->var_cap, sizeof(VarTypeEntry));
    e->var_count = 0;
}

static void tenv_free(TypeEnv *e) {
    for (int i = 0; i < e->var_count; i++) free(e->vars[i].name);
    free(e->temps); free(e->vars);
}

/* Keep in sync with compiler/ir_to_mir.c: MIR local slot ids are
   per-function-frame, but `vars` is one flat table shared across the whole
   module's IR_FUNC_BEGIN...IR_FUNC_END stream. Clear local (is_local=1)
   entries between functions so two functions sharing a param/local name
   don't alias onto each other's slot ids; global/static entries
   (is_local=0) must survive since they're module-scoped. */
static void tenv_clear_locals(TypeEnv *e) {
    int kept = 0;
    for (int i = 0; i < e->var_count; i++) {
        if (e->vars[i].is_local) {
            free(e->vars[i].name);
            continue;
        }
        if (kept != i) e->vars[kept] = e->vars[i];
        kept++;
    }
    e->var_count = kept;
}

static void tenv_set_temp(TypeEnv *e, int temp_id, MIRType t) {
    for (int i = 0; i < e->temp_count; i++)
        if (e->temps[i].temp_id == temp_id) { e->temps[i].type = t; return; }
    if (e->temp_count == e->temp_cap) {
        e->temp_cap *= 2;
        e->temps = realloc(e->temps, e->temp_cap * sizeof(TempTypeEntry));
    }
    e->temps[e->temp_count].temp_id = temp_id;
    e->temps[e->temp_count].type = t;
    e->temp_count++;
}

/* unknown temps default to I64 - safe default since that's the only width
   the language currently exposes for `int` */
static MIRType tenv_get_temp(const TypeEnv *e, int temp_id) {
    for (int i = 0; i < e->temp_count; i++)
        if (e->temps[i].temp_id == temp_id) return e->temps[i].type;
    return MIR_I64;
}

static void tenv_set_var(TypeEnv *e, const char *name, MIRType t) {
    for (int i = 0; i < e->var_count; i++)
        if (strcmp(e->vars[i].name, name) == 0) { e->vars[i].type = t; e->vars[i].is_local = 0; return; }
    if (e->var_count == e->var_cap) {
        e->var_cap *= 2;
        e->vars = realloc(e->vars, e->var_cap * sizeof(VarTypeEntry));
    }
    e->vars[e->var_count].name = strdup(name);
    e->vars[e->var_count].type = t;
    e->vars[e->var_count].is_local = 0;
    e->vars[e->var_count].local_id = -1;
    e->var_count++;
}

/* params and ALLOCA'd locals go through here instead of tenv_set_var - they
   need real per-call-frame storage (a MIRV_LOCAL slot), not the global/static
   symbol mechanism, or two different functions with a same-named local (or
   two calls to the same recursive function) would alias onto one another. */
static void tenv_set_local(TypeEnv *e, const char *name, MIRType t, int local_id) {
    for (int i = 0; i < e->var_count; i++)
        if (strcmp(e->vars[i].name, name) == 0) {
            e->vars[i].type = t; e->vars[i].is_local = 1; e->vars[i].local_id = local_id;
            return;
        }
    if (e->var_count == e->var_cap) {
        e->var_cap *= 2;
        e->vars = realloc(e->vars, e->var_cap * sizeof(VarTypeEntry));
    }
    e->vars[e->var_count].name = strdup(name);
    e->vars[e->var_count].type = t;
    e->vars[e->var_count].is_local = 1;
    e->vars[e->var_count].local_id = local_id;
    e->var_count++;
}

static MIRType tenv_get_var(const TypeEnv *e, const char *name) {
    for (int i = 0; i < e->var_count; i++)
        if (strcmp(e->vars[i].name, name) == 0) return e->vars[i].type;
    return MIR_I64;
}

/* returns the local slot id, or -1 if `name` is a true global/static instead */
static int tenv_local_id(const TypeEnv *e, const char *name) {
    for (int i = 0; i < e->var_count; i++)
        if (strcmp(e->vars[i].name, name) == 0) return e->vars[i].is_local ? e->vars[i].local_id : -1;
    return -1;
}

/* builds the right MIRValue (local slot or global symbol) for referencing
   the current value location of a Hylian variable by name - the one place
   this decision gets made, so LOAD_VAR/STORE_VAR/ADDROF/asm-block variable
   substitution all stay consistent with each other. */
static MIRValue var_location(const TypeEnv *e, const char *name, MIRType t) {
    int lid = tenv_local_id(e, name);
    return lid >= 0 ? mir_local(lid, t) : mir_global(name, t);
}

/* used by STORE_VAR - the variable's local/global status and slot were
   already decided at declaration time (ALLOCA/param/STATIC_VAR); a store
   must not silently reset that, only refresh the tracked type */
static void tenv_update_type(TypeEnv *e, const char *name, MIRType t) {
    for (int i = 0; i < e->var_count; i++)
        if (strcmp(e->vars[i].name, name) == 0) { e->vars[i].type = t; return; }
    tenv_set_var(e, name, t); /* not previously declared - fall back to global, safe default */
}

/* Hylian's surface type names -> MIRType. Only int/float/bool exist as
   arithmetic types right now; no sized ints yet, so "int" is I64. */
static MIRType type_name_to_mir(const char *name) {
    if (!name) return MIR_I64;
    if (strcmp(name, "float") == 0) return MIR_F64;
    if (strcmp(name, "bool") == 0)  return MIR_I64; /* bools stored as 0/1 in I64 for now */
    if (strcmp(name, "rawptr") == 0 || strcmp(name, "ptr") == 0) return MIR_PTR;
    return MIR_I64; /* int, str (as ptr-ish), classes, enums default */
}

/* lower_operand can emit (e.g. a bare CONST_STR operand needs a LEA_GLOBAL
   to actually get its address), so it needs the module + current function. */
static MIRValue lower_operand(const IROperand *op, TypeEnv *env, MIRModule *mod, MIRFunc *fn) {
    switch (op->kind) {
    case IROP_NONE:
        return mir_none();
    case IROP_TEMP:
        return mir_vreg(op->temp_id, tenv_get_temp(env, op->temp_id));
    case IROP_CONST_INT:
        return mir_imm_int(op->int_val, MIR_I64);
    case IROP_CONST_BOOL:
        return mir_imm_int(op->bool_val ? 1 : 0, MIR_I64);
    case IROP_CONST_FLOAT:
        return mir_imm_float(op->float_val, MIR_F64);
    case IROP_LABEL_ID:
        return mir_label(op->label_id);
    case IROP_CONST_STR: {
        const char *sv = op->str_val ? op->str_val : "";
        const char *lbl = mir_module_intern_string(mod, sv, (int)strlen(sv));
        int v = mir_new_vreg(fn);
        MIRInstr *m = mir_emit(fn, MIR_LEA_GLOBAL);
        m->dest = mir_vreg(v, MIR_PTR);
        m->src1 = mir_global(lbl, MIR_PTR);
        return mir_vreg(v, MIR_PTR);
    }
    }
    return mir_none();
}

static MIRValue *lower_args(const IROperand *args, int count, TypeEnv *env, MIRModule *mod, MIRFunc *fn) {
    if (count == 0) return NULL;
    MIRValue *out = malloc(count * sizeof(MIRValue));
    for (int i = 0; i < count; i++) out[i] = lower_operand(&args[i], env, mod, fn);
    return out;
}

static char *fmt_sym(const char *a, const char *b, const char *sep) {
    size_t n = strlen(a ? a : "") + strlen(sep) + strlen(b ? b : "") + 1;
    char *s = malloc(n);
    snprintf(s, n, "%s%s%s", a ? a : "", sep, b ? b : "");
    return s;
}

static void warn_unhandled(const IRInstr *ins) {
    fprintf(stderr, "[ir_to_mir] not lowered yet: %s (left as nop, not silently correct)\n",
            ir_opcode_name(ins->op));
}

MIRModule *lower_ir_to_mir(const IRModule *ir) {
    MIRModule *mod = mir_module_new();
    MIRFunc *fn = NULL;
    TypeEnv env;
    tenv_init(&env);

    for (int i = 0; i < ir->instr_count; i++) {
        const IRInstr *ins = &ir->instrs[i];

        switch (ins->op) {

        case IR_FUNC_BEGIN: {
            tenv_clear_locals(&env);
            fn = mir_func_new(mod, ins->str_extra ? ins->str_extra : "?");
            fn->param_count = ins->param_count;
            for (int p = 0; p < ins->param_count; p++) {
                MIRType t = type_name_to_mir(ins->params[p].type_name);
                if (ins->params[p].name) {
                    int lid = mir_new_local(fn);
                    tenv_set_local(&env, ins->params[p].name, t, lid);
                }
            }
            break;
        }

        case IR_FUNC_END:
            fn = NULL;
            break;

        case IR_ALLOCA: {
            /* str_extra = var name, str_extra2 = type name.
               Keep in sync with compiler/ir_to_mir.c: parameters already
               got a local slot from IR_FUNC_BEGIN (which the prologue
               writes the incoming register arg into), so allocating a
               second slot here for the same name would silently repoint
               LOAD_VAR/STORE_VAR at an unrelated, never-written slot. */
            MIRType t = type_name_to_mir(ins->str_extra2);
            if (ins->str_extra) {
                int existing = tenv_local_id(&env, ins->str_extra);
                if (existing >= 0) {
                    tenv_update_type(&env, ins->str_extra, t);
                } else {
                    int lid = mir_new_local(fn);
                    tenv_set_local(&env, ins->str_extra, t, lid);
                }
            }
            break;
        }

        case IR_CONST_INT: {
            MIRInstr *m = mir_emit(fn, MIR_MOV);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_I64);
            m->src1 = mir_imm_int(ins->src1.int_val, MIR_I64);
            tenv_set_temp(&env, ins->dest.temp_id, MIR_I64);
            break;
        }

        case IR_CONST_FLOAT: {
            MIRInstr *m = mir_emit(fn, MIR_MOV);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_F64);
            m->src1 = mir_imm_float(ins->src1.float_val, MIR_F64);
            tenv_set_temp(&env, ins->dest.temp_id, MIR_F64);
            break;
        }

        case IR_CONST_BOOL: {
            MIRInstr *m = mir_emit(fn, MIR_MOV);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_I64);
            m->src1 = mir_imm_int(ins->src1.bool_val ? 1 : 0, MIR_I64);
            tenv_set_temp(&env, ins->dest.temp_id, MIR_I64);
            break;
        }

        case IR_CONST_NIL: {
            MIRInstr *m = mir_emit(fn, MIR_MOV);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_PTR);
            m->src1 = mir_imm_int(0, MIR_PTR);
            tenv_set_temp(&env, ins->dest.temp_id, MIR_PTR);
            break;
        }

        case IR_CONST_STR: {
            const char *sv = ins->src1.str_val ? ins->src1.str_val : "";
            const char *lbl = mir_module_intern_string(mod, sv, (int)strlen(sv));
            MIRInstr *m = mir_emit(fn, MIR_LEA_GLOBAL);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_PTR);
            m->src1 = mir_global(lbl, MIR_PTR);
            tenv_set_temp(&env, ins->dest.temp_id, MIR_PTR);
            break;
        }

        case IR_LOAD_VAR: {
            MIRType t = tenv_get_var(&env, ins->str_extra);
            MIRInstr *m = mir_emit(fn, MIR_LOAD);
            m->dest = mir_vreg(ins->dest.temp_id, t);
            m->src1 = var_location(&env, ins->str_extra, t);
            tenv_set_temp(&env, ins->dest.temp_id, t);
            break;
        }

        case IR_STORE_VAR: {
            MIRType t = tenv_get_temp(&env, ins->src1.temp_id);
            tenv_update_type(&env, ins->str_extra, t);
            MIRInstr *m = mir_emit(fn, MIR_STORE);
            m->dest = var_location(&env, ins->str_extra, t);
            m->src1 = lower_operand(&ins->src1, &env, mod, fn);
            break;
        }

        /* ---- arithmetic: dispatch int vs float based on src1's inferred type ---- */
        case IR_ADD: case IR_SUB: case IR_MUL: {
            MIRValue a = lower_operand(&ins->src1, &env, mod, fn);
            MIRValue b = lower_operand(&ins->src2, &env, mod, fn);
            int is_f = mir_type_is_float(a.type);
            MIROp op;
            if (is_f) {
                op = ins->op == IR_ADD ? MIR_FADD : ins->op == IR_SUB ? MIR_FSUB : MIR_FMUL;
            } else {
                op = ins->op == IR_ADD ? MIR_ADD : ins->op == IR_SUB ? MIR_SUB : MIR_MUL;
            }
            MIRInstr *m = mir_emit(fn, op);
            m->dest = mir_vreg(ins->dest.temp_id, a.type);
            m->src1 = a; m->src2 = b;
            tenv_set_temp(&env, ins->dest.temp_id, a.type);
            break;
        }

        case IR_DIV: case IR_MOD: {
            MIRValue a = lower_operand(&ins->src1, &env, mod, fn);
            MIRValue b = lower_operand(&ins->src2, &env, mod, fn);
            int is_f = mir_type_is_float(a.type);
            MIROp op;
            if (is_f) {
                op = MIR_FDIV; /* no float mod in this language */
            } else {
                /* no unsigned surface type yet -> always signed */
                op = ins->op == IR_DIV ? MIR_SDIV : MIR_SMOD;
            }
            MIRInstr *m = mir_emit(fn, op);
            m->dest = mir_vreg(ins->dest.temp_id, a.type);
            m->src1 = a; m->src2 = b;
            tenv_set_temp(&env, ins->dest.temp_id, a.type);
            break;
        }

        case IR_NEG: {
            MIRValue a = lower_operand(&ins->src1, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, mir_type_is_float(a.type) ? MIR_FNEG : MIR_NEG);
            m->dest = mir_vreg(ins->dest.temp_id, a.type);
            m->src1 = a;
            tenv_set_temp(&env, ins->dest.temp_id, a.type);
            break;
        }

        case IR_NOT: case IR_BITNOT: {
            MIRValue a = lower_operand(&ins->src1, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, ins->op == IR_NOT ? MIR_CMP_EQ : MIR_NOT);
            if (ins->op == IR_NOT) { m->src2 = mir_imm_int(0, MIR_I64); }
            m->dest = mir_vreg(ins->dest.temp_id, MIR_I64);
            m->src1 = a;
            tenv_set_temp(&env, ins->dest.temp_id, MIR_I64);
            break;
        }

        case IR_BITAND: case IR_BITOR: case IR_BITXOR:
        case IR_SHL: case IR_SHR: {
            MIRValue a = lower_operand(&ins->src1, &env, mod, fn);
            MIRValue b = lower_operand(&ins->src2, &env, mod, fn);
            MIROp op;
            switch (ins->op) {
                case IR_BITAND: op = MIR_AND; break;
                case IR_BITOR:  op = MIR_OR;  break;
                case IR_BITXOR: op = MIR_XOR; break;
                case IR_SHL:    op = MIR_SHL; break;
                default:        op = MIR_SHR_A; break; /* no unsigned type yet - see docs */
            }
            MIRInstr *m = mir_emit(fn, op);
            m->dest = mir_vreg(ins->dest.temp_id, a.type);
            m->src1 = a; m->src2 = b;
            tenv_set_temp(&env, ins->dest.temp_id, a.type);
            break;
        }

        case IR_EQ: case IR_NEQ: case IR_LT: case IR_LE: case IR_GT: case IR_GE: {
            MIRValue a = lower_operand(&ins->src1, &env, mod, fn);
            MIRValue b = lower_operand(&ins->src2, &env, mod, fn);
            int is_f = mir_type_is_float(a.type);
            MIROp op;
            if (is_f) {
                switch (ins->op) {
                    case IR_EQ: op = MIR_FCMP_EQ; break;
                    case IR_NEQ: op = MIR_FCMP_NE; break;
                    case IR_LT: op = MIR_FCMP_LT; break;
                    case IR_LE: op = MIR_FCMP_LE; break;
                    case IR_GT: op = MIR_FCMP_GT; break;
                    default: op = MIR_FCMP_GE; break;
                }
            } else {
                switch (ins->op) {
                    case IR_EQ: op = MIR_CMP_EQ; break;
                    case IR_NEQ: op = MIR_CMP_NE; break;
                    case IR_LT: op = MIR_CMP_LT; break;
                    case IR_LE: op = MIR_CMP_LE; break;
                    case IR_GT: op = MIR_CMP_GT; break;
                    default: op = MIR_CMP_GE; break;
                }
            }
            MIRInstr *m = mir_emit(fn, op);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_I64);
            m->src1 = a; m->src2 = b;
            tenv_set_temp(&env, ins->dest.temp_id, MIR_I64);
            break;
        }

        /* ---- the actual fix: real conversions instead of a bit-copy ---- */
        case IR_CAST: {
            MIRValue src = lower_operand(&ins->src1, &env, mod, fn);
            MIRType dst_t = type_name_to_mir(ins->str_extra);
            MIROp op;
            if (mir_type_is_float(src.type) && !mir_type_is_float(dst_t)) {
                op = MIR_F2I;
            } else if (!mir_type_is_float(src.type) && mir_type_is_float(dst_t)) {
                op = MIR_I2F;
            } else if (mir_type_is_float(src.type) && mir_type_is_float(dst_t)) {
                op = MIR_F2F;
            } else if (mir_type_size(dst_t) < mir_type_size(src.type)) {
                op = MIR_TRUNC;
            } else if (mir_type_size(dst_t) > mir_type_size(src.type)) {
                op = mir_type_is_unsigned(src.type) ? MIR_ZEXT : MIR_SEXT;
            } else {
                op = MIR_BITCAST;
            }
            MIRInstr *m = mir_emit(fn, op);
            m->dest = mir_vreg(ins->dest.temp_id, dst_t);
            m->src1 = src;
            tenv_set_temp(&env, ins->dest.temp_id, dst_t);
            break;
        }

        case IR_LABEL: {
            MIRInstr *m = mir_emit(fn, MIR_LABEL);
            m->dest = mir_label(ins->dest.label_id);
            break;
        }

        case IR_JUMP: {
            MIRInstr *m = mir_emit(fn, MIR_JMP);
            m->dest = mir_label(ins->src1.label_id);
            break;
        }

        case IR_JUMP_IF: case IR_JUMP_UNLESS: {
            MIRInstr *m = mir_emit(fn, ins->op == IR_JUMP_IF ? MIR_JMP_IF : MIR_JMP_UNLESS);
            m->src1 = lower_operand(&ins->src1, &env, mod, fn);
            m->dest = mir_label(ins->src2.label_id);
            break;
        }

        case IR_RETURN: {
            MIRInstr *m = mir_emit(fn, MIR_RET);
            if (ins->src1.kind != IROP_NONE)
                m->src1 = lower_operand(&ins->src1, &env, mod, fn);
            break;
        }

        /* ---- calls ---- */
        case IR_CALL: {
            /* `syscall(nr, a0, a1, ...)` is a compiler builtin, not a real
               function - it lowers straight to MIR_SYSCALL, which already
               does the Linux syscall ABI shuffling correctly (arg4 in r10,
               not rcx). This replaces sys.hy's whole family of naked asm
               wrappers (_sc3/_sc5/_sc_pread/_sc_pwrite/_sc_mmap) with a
               single call the stdlib can use directly at any arity -
               mmap's 6 real args, pread's 4, exit's 1, whatever the
               specific syscall needs, no fixed-shape wrapper required. */
            if (ins->str_extra && strcmp(ins->str_extra, "syscall") == 0) {
                if (ins->arg_count < 1) {
                    fprintf(stderr, "[ir_to_mir] syscall() needs at least a syscall number\n");
                    break;
                }
                if (ins->arg_count > 7)
                    fprintf(stderr, "[ir_to_mir] syscall() with >6 real args not supported\n");
                MIRValue *args = lower_args(ins->args, ins->arg_count, &env, mod, fn);
                MIRInstr *m = mir_emit(fn, MIR_SYSCALL);
                m->args = args; m->arg_count = ins->arg_count;
                m->dest = mir_vreg(ins->dest.temp_id, MIR_I64);
                tenv_set_temp(&env, ins->dest.temp_id, MIR_I64);
                break;
            }

            MIRValue *args = lower_args(ins->args, ins->arg_count, &env, mod, fn);
            mir_build_call(fn, ins->str_extra, args, ins->arg_count, mir_vreg(ins->dest.temp_id, MIR_I64));
            tenv_set_temp(&env, ins->dest.temp_id, MIR_I64);
            break;
        }

        /* ---- OOP: mirrors codegen_elf.c's <cls>_new / <cls>_get_<f> / <cls>_set_<f> convention ---- */
        case IR_NEW: {
            char *ctor = fmt_sym(ins->str_extra ? ins->str_extra : "_obj", "_new", "");
            MIRValue *args = lower_args(ins->args, ins->arg_count, &env, mod, fn);
            mir_build_call(fn, ctor, args, ins->arg_count, mir_vreg(ins->dest.temp_id, MIR_PTR));
            tenv_set_temp(&env, ins->dest.temp_id, MIR_PTR);
            break;
        }

        case IR_GET_FIELD: {
            char tmp[256];
            snprintf(tmp, sizeof(tmp), "%s_get_%s", ins->str_extra ? ins->str_extra : "",
                     ins->str_extra2 ? ins->str_extra2 : "");
            MIRValue *args = malloc(sizeof(MIRValue));
            args[0] = lower_operand(&ins->src1, &env, mod, fn);
            mir_build_call(fn, strdup(tmp), args, 1, mir_vreg(ins->dest.temp_id, MIR_I64));
            tenv_set_temp(&env, ins->dest.temp_id, MIR_I64);
            break;
        }

        case IR_SET_FIELD: {
            char tmp[256];
            snprintf(tmp, sizeof(tmp), "%s_set_%s", ins->str_extra ? ins->str_extra : "",
                     ins->str_extra2 ? ins->str_extra2 : "");
            MIRValue *args = malloc(2 * sizeof(MIRValue));
            args[0] = lower_operand(&ins->src1, &env, mod, fn);
            args[1] = lower_operand(&ins->src2, &env, mod, fn);
            mir_build_call(fn, strdup(tmp), args, 2, mir_none());
            break;
        }

        case IR_MULTI_ALLOC: {
            MIRValue *args = malloc(2 * sizeof(MIRValue));
            args[0] = lower_operand(&ins->src1, &env, mod, fn);
            args[1] = lower_operand(&ins->src2, &env, mod, fn);
            mir_build_call(fn, "hylian_multi_alloc", args, 2, mir_vreg(ins->dest.temp_id, MIR_PTR));
            tenv_set_temp(&env, ins->dest.temp_id, MIR_PTR);
            break;
        }

        case IR_ENUM_VAL: {
            char tmp[256];
            snprintf(tmp, sizeof(tmp), "%s_%s", ins->str_extra ? ins->str_extra : "",
                     ins->str_extra2 ? ins->str_extra2 : "");
            MIRInstr *m = mir_emit(fn, MIR_LEA_GLOBAL);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_PTR);
            m->src1 = mir_global(strdup(tmp), MIR_PTR);
            tenv_set_temp(&env, ins->dest.temp_id, MIR_PTR);
            break;
        }

        case IR_ARENA_ALLOC: {
            /* arena_alloc(Arena *a, size_t size) - keep in sync with
               compiler/ir_to_mir.c: pass both the arena pointer (src1) and
               the size (extra_int), matching runtime/std/mem.c. */
            MIRValue *args = malloc(2 * sizeof(MIRValue));
            args[0] = lower_operand(&ins->src1, &env, mod, fn);
            args[1] = mir_imm_int(ins->extra_int, MIR_I64);
            mir_build_call(fn, "arena_alloc", args, 2, mir_vreg(ins->dest.temp_id, MIR_PTR));
            tenv_set_temp(&env, ins->dest.temp_id, MIR_PTR);
            break;
        }

        /* ---- arrays: mirrors codegen_elf.c's hylian_array_* runtime convention ---- */
        case IR_ARRAY_ALLOC: {
            MIRValue *args = malloc(sizeof(MIRValue));
            args[0] = mir_imm_int(0, MIR_I64);
            mir_build_call(fn, "hylian_array_alloc", args, 1, mir_vreg(ins->dest.temp_id, MIR_PTR));
            tenv_set_temp(&env, ins->dest.temp_id, MIR_PTR);
            break;
        }

        case IR_ARRAY_INIT: {
            MIRValue *alloc_args = malloc(sizeof(MIRValue));
            alloc_args[0] = mir_imm_int(ins->arg_count, MIR_I64);
            mir_build_call(fn, "hylian_array_alloc", alloc_args, 1, mir_vreg(ins->dest.temp_id, MIR_PTR));
            tenv_set_temp(&env, ins->dest.temp_id, MIR_PTR);
            for (int a = 0; a < ins->arg_count; a++) {
                MIRValue *push_args = malloc(2 * sizeof(MIRValue));
                push_args[0] = mir_vreg(ins->dest.temp_id, MIR_PTR);
                push_args[1] = lower_operand(&ins->args[a], &env, mod, fn);
                mir_build_call(fn, "hylian_array_push", push_args, 2, mir_none());
            }
            break;
        }

        case IR_ARRAY_PUSH: {
            MIRValue *args = malloc(2 * sizeof(MIRValue));
            args[0] = lower_operand(&ins->src1, &env, mod, fn);
            args[1] = lower_operand(&ins->src2, &env, mod, fn);
            mir_build_call(fn, "hylian_array_push", args, 2, mir_none());
            break;
        }

        case IR_ARRAY_POP: {
            MIRValue *args = malloc(sizeof(MIRValue));
            args[0] = lower_operand(&ins->src1, &env, mod, fn);
            mir_build_call(fn, "hylian_array_pop", args, 1, mir_vreg(ins->dest.temp_id, MIR_I64));
            tenv_set_temp(&env, ins->dest.temp_id, MIR_I64);
            break;
        }

        case IR_ARRAY_LOAD: {
            MIRValue *args = malloc(2 * sizeof(MIRValue));
            args[0] = lower_operand(&ins->src1, &env, mod, fn);
            args[1] = lower_operand(&ins->src2, &env, mod, fn);
            mir_build_call(fn, "hylian_array_get", args, 2, mir_vreg(ins->dest.temp_id, MIR_I64));
            tenv_set_temp(&env, ins->dest.temp_id, MIR_I64);
            break;
        }

        case IR_ARRAY_STORE: {
            MIRValue *args = malloc(3 * sizeof(MIRValue));
            args[0] = lower_operand(&ins->src1, &env, mod, fn);
            args[1] = lower_operand(&ins->src2, &env, mod, fn);
            args[2] = lower_operand(&ins->extra_src, &env, mod, fn);
            mir_build_call(fn, "hylian_array_set", args, 3, mir_none());
            break;
        }

        case IR_ARRAY_LEN: {
            MIRInstr *m = mir_emit(fn, MIR_LOAD);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_I64);
            m->src1 = lower_operand(&ins->src1, &env, mod, fn);
            tenv_set_temp(&env, ins->dest.temp_id, MIR_I64);
            break;
        }

        case IR_ARRAY_CAP: {
            /* cap is the 8-byte field right after len; encoder can special-case
               a nonzero extra_int as a byte offset added to the load address */
            MIRInstr *m = mir_emit(fn, MIR_LOAD);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_I64);
            m->src1 = lower_operand(&ins->src1, &env, mod, fn);
            m->extra_int = 8;
            tenv_set_temp(&env, ins->dest.temp_id, MIR_I64);
            break;
        }

        /* ---- raw pointers / volatile ---- */
        case IR_LOAD_PTR: case IR_LOAD_VOLATILE: {
            MIRValue src = lower_operand(&ins->src1, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, MIR_LOAD);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_I64);
            m->src1 = src;
            m->extra_int = (ins->op == IR_LOAD_VOLATILE) ? 1 : 0;
            tenv_set_temp(&env, ins->dest.temp_id, MIR_I64);
            break;
        }

        case IR_STORE_PTR: case IR_STORE_VOLATILE: {
            MIRInstr *m = mir_emit(fn, MIR_STORE);
            m->dest = lower_operand(&ins->src1, &env, mod, fn);
            m->src1 = lower_operand(&ins->src2, &env, mod, fn);
            m->extra_int = (ins->op == IR_STORE_VOLATILE) ? 1 : 0;
            break;
        }

        case IR_ADDROF: {
            int lid = tenv_local_id(&env, ins->str_extra);
            MIRInstr *m = mir_emit(fn, lid >= 0 ? MIR_LEA_LOCAL : MIR_LEA_GLOBAL);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_PTR);
            m->src1 = lid >= 0 ? mir_local(lid, MIR_PTR) : mir_global(ins->str_extra, MIR_PTR);
            tenv_set_temp(&env, ins->dest.temp_id, MIR_PTR);
            break;
        }

        case IR_ADDROF_FN: {
            MIRInstr *m = mir_emit(fn, MIR_LEA_GLOBAL);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_PTR);
            m->src1 = mir_global(ins->str_extra ? ins->str_extra : "_fn", MIR_PTR);
            tenv_set_temp(&env, ins->dest.temp_id, MIR_PTR);
            break;
        }

        case IR_STATIC_VAR: {
            int has_init = (ins->src1.kind == IROP_CONST_INT || ins->src1.kind == IROP_CONST_BOOL);
            int64_t init_val = ins->src1.kind == IROP_CONST_INT ? ins->src1.int_val
                              : ins->src1.kind == IROP_CONST_BOOL ? ins->src1.bool_val : 0;
            mir_module_add_global(mod, ins->str_extra, has_init, init_val, 8);
            tenv_set_var(&env, ins->str_extra, type_name_to_mir(ins->str_extra2));
            break;
        }

        /* ---- memory ---- */
        case IR_MEMSET: {
            MIRInstr *m = mir_emit(fn, MIR_MEMSET);
            m->dest = lower_operand(&ins->src1, &env, mod, fn);
            m->src1 = lower_operand(&ins->src2, &env, mod, fn);
            m->src2 = lower_operand(&ins->extra_src, &env, mod, fn);
            break;
        }

        case IR_MEMCPY: {
            MIRInstr *m = mir_emit(fn, MIR_MEMCPY);
            m->dest = lower_operand(&ins->src1, &env, mod, fn);
            m->src1 = lower_operand(&ins->src2, &env, mod, fn);
            m->src2 = lower_operand(&ins->extra_src, &env, mod, fn);
            break;
        }

        /* ---- privileged / kernel intrinsics: direct 1:1 to dedicated MIR ops ---- */
        case IR_CLI: mir_emit(fn, MIR_CLI); break;
        case IR_STI: mir_emit(fn, MIR_STI); break;
        case IR_IRET: mir_emit(fn, MIR_IRET); break;

        case IR_LGDT: case IR_LIDT: {
            MIRInstr *m = mir_emit(fn, ins->op == IR_LGDT ? MIR_LGDT : MIR_LIDT);
            m->src1 = lower_operand(&ins->src1, &env, mod, fn);
            m->src2 = lower_operand(&ins->src2, &env, mod, fn);
            break;
        }

        case IR_LTR: case IR_INVLPG: {
            MIRInstr *m = mir_emit(fn, ins->op == IR_LTR ? MIR_LTR : MIR_INVLPG);
            m->src1 = lower_operand(&ins->src1, &env, mod, fn);
            break;
        }

        case IR_WRMSR: {
            MIRInstr *m = mir_emit(fn, MIR_WRMSR);
            m->src1 = lower_operand(&ins->src1, &env, mod, fn);
            m->src2 = lower_operand(&ins->src2, &env, mod, fn);
            break;
        }

        case IR_RDMSR: {
            MIRInstr *m = mir_emit(fn, MIR_RDMSR);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_U64);
            m->src1 = lower_operand(&ins->src1, &env, mod, fn);
            tenv_set_temp(&env, ins->dest.temp_id, MIR_U64);
            break;
        }

        case IR_READ_CR: {
            MIRInstr *m = mir_emit(fn, MIR_READ_CR);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_U64);
            m->extra_int = ins->extra_int;
            tenv_set_temp(&env, ins->dest.temp_id, MIR_U64);
            break;
        }

        case IR_WRITE_CR: {
            MIRInstr *m = mir_emit(fn, MIR_WRITE_CR);
            m->src1 = lower_operand(&ins->src1, &env, mod, fn);
            m->extra_int = ins->extra_int;
            break;
        }

        case IR_OUTB: {
            MIRInstr *m = mir_emit(fn, MIR_OUTB);
            m->src1 = lower_operand(&ins->src1, &env, mod, fn);
            m->src2 = lower_operand(&ins->src2, &env, mod, fn);
            break;
        }

        case IR_INB: {
            MIRInstr *m = mir_emit(fn, MIR_INB);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_U64);
            m->src1 = lower_operand(&ins->src1, &env, mod, fn);
            tenv_set_temp(&env, ins->dest.temp_id, MIR_U64);
            break;
        }

        case IR_SAVE_REGS: {
            MIRInstr *m = mir_emit(fn, MIR_SAVE_REGS);
            m->extra_int = ins->extra_int;
            break;
        }

        case IR_RESTORE_REGS: {
            MIRInstr *m = mir_emit(fn, MIR_RESTORE_REGS);
            m->extra_int = ins->extra_int;
            break;
        }

        /* ---- error/panic ---- */
        case IR_ERR: {
            MIRValue *args = malloc(sizeof(MIRValue));
            args[0] = lower_operand(&ins->src1, &env, mod, fn);
            mir_build_call(fn, "hylian_make_err", args, 1, mir_vreg(ins->dest.temp_id, MIR_PTR));
            tenv_set_temp(&env, ins->dest.temp_id, MIR_PTR);
            break;
        }

        case IR_PANIC: {
            MIRValue *args = malloc(sizeof(MIRValue));
            args[0] = lower_operand(&ins->src1, &env, mod, fn);
            mir_build_call(fn, "hylian_panic", args, 1, mir_none());
            break;
        }

        /* ---- print/println: mirrors codegen_elf.c's arg-type dispatch ---- */
        case IR_PRINT: case IR_PRINTLN: {
            const char *fn_name = ins->op == IR_PRINTLN ? "hylian_println" : "hylian_print";
            int arg_type = ins->extra_int;

            if (arg_type == PRINT_ARG_STR_LIT) {
                const char *sv = ins->src1.str_val ? ins->src1.str_val : "";
                const char *lbl = mir_module_intern_string(mod, sv, (int)strlen(sv));
                int ptrv = mir_new_vreg(fn);
                MIRInstr *lea = mir_emit(fn, MIR_LEA_GLOBAL);
                lea->dest = mir_vreg(ptrv, MIR_PTR);
                lea->src1 = mir_global(lbl, MIR_PTR);
                MIRValue *args = malloc(2 * sizeof(MIRValue));
                args[0] = mir_vreg(ptrv, MIR_PTR);
                args[1] = mir_imm_int((int64_t)strlen(sv), MIR_I64);
                mir_build_call(fn, fn_name, args, 2, mir_none());
            } else if (arg_type == PRINT_ARG_INT) {
                int buf = mir_new_vreg(fn);
                MIRInstr *alloc = mir_emit(fn, MIR_ALLOCA_LOCAL);
                alloc->dest = mir_vreg(buf, MIR_PTR);
                alloc->extra_int = 32;
                MIRValue *conv_args = malloc(3 * sizeof(MIRValue));
                conv_args[0] = lower_operand(&ins->src1, &env, mod, fn);
                conv_args[1] = mir_vreg(buf, MIR_PTR);
                conv_args[2] = mir_imm_int(32, MIR_I64);
                int lenv = mir_new_vreg(fn);
                mir_build_call(fn, "hylian_int_to_str", conv_args, 3, mir_vreg(lenv, MIR_I64));
                MIRValue *print_args = malloc(2 * sizeof(MIRValue));
                print_args[0] = mir_vreg(buf, MIR_PTR);
                print_args[1] = mir_vreg(lenv, MIR_I64);
                mir_build_call(fn, fn_name, print_args, 2, mir_none());
            } else if (arg_type == PRINT_ARG_STR_PTR) {
                MIRValue ptrv = lower_operand(&ins->src1, &env, mod, fn);
                MIRValue *len_args = malloc(sizeof(MIRValue));
                len_args[0] = ptrv;
                int lenv = mir_new_vreg(fn);
                mir_build_call(fn, "strlen", len_args, 1, mir_vreg(lenv, MIR_I64));
                MIRValue *print_args = malloc(2 * sizeof(MIRValue));
                print_args[0] = ptrv;
                print_args[1] = mir_vreg(lenv, MIR_I64);
                mir_build_call(fn, fn_name, print_args, 2, mir_none());
            } else {
                /* PRINT_ARG_FLOAT / PRINT_ARG_INTERP: the existing direct-ELF
                   backend doesn't support these either (no xmm/interp-segment
                   codegen there) - staying honest about the same gap here
                   instead of faking it. */
                warn_unhandled(ins);
            }
            break;
        }

        /* ---- known, currently-unsolved gaps (same as codegen_elf.c) ---- */
        case IR_INTERP_STR: {
            /* Literal-only interpolation is fully solvable here: since every
               literal segment's content is known at compile time, the whole
               result can just be precomputed and interned as one ordinary
               string constant - same as a plain string literal, no runtime
               buffer/copy needed at all.

               Expression segments are a different story: InterpSegment's
               is_expr=1 case carries raw, UNPARSED expression source text
               (see ast.h) - lower.c hands it straight through without ever
               evaluating it into IR. ir_to_mir.c has no parser/typechecker
               available to it (that machinery is lower.c's alone), so it
               structurally cannot evaluate that text itself. This is a real
               gap in lower.c, not something fixable from this pass - so
               instead of faking success, this drops each expr segment's
               contribution with a specific, loud diagnostic naming exactly
               which segment couldn't be evaluated and why, and still
               produces a real (if incomplete) result from whatever literal
               segments exist. */
            size_t cap = 64;
            char *buf = malloc(cap);
            size_t len = 0;
            int had_expr = 0;

            for (int s = 0; s < ins->extra_seg_count; s++) {
                InterpSegment *seg = &ins->extra_segs[s];
                if (seg->is_expr) {
                    had_expr = 1;
                    fprintf(stderr,
                        "[ir_to_mir] IR_INTERP_STR: expression segment '%s' was not "
                        "evaluated (lower.c hands interp-string expressions through as "
                        "raw unparsed source text - ir_to_mir.c has no expression "
                        "evaluator available to it, that needs a fix in lower.c itself, "
                        "not here). This segment is omitted from the result.\n",
                        seg->text ? seg->text : "?");
                    continue;
                }
                const char *t = seg->text ? seg->text : "";
                size_t tlen = strlen(t);
                if (len + tlen + 1 > cap) {
                    while (len + tlen + 1 > cap) cap *= 2;
                    buf = realloc(buf, cap);
                }
                memcpy(buf + len, t, tlen);
                len += tlen;
            }
            buf[len] = '\0';

            if (had_expr)
                fprintf(stderr,
                    "[ir_to_mir] IR_INTERP_STR: result is INCOMPLETE (literal segments "
                    "only) - one or more {expr} segments above couldn't be evaluated\n");

            const char *lbl = mir_module_intern_string(mod, buf, (int)len);
            MIRInstr *m = mir_emit(fn, MIR_LEA_GLOBAL);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_PTR);
            m->src1 = mir_global(lbl, MIR_PTR);
            tenv_set_temp(&env, ins->dest.temp_id, MIR_PTR);
            free(buf);
            break;
        }

        case IR_ASM_BLOCK: {
            /* rewrite {varname} -> {N} and build args[] so MIR stays
               frontend-blind (it never sees Hylian variable names) - the
               mini-assembler in libhyx64 only ever deals with positional
               placeholders, resolving them to real registers post-regalloc. */
            const char *src = ins->str_extra ? ins->str_extra : "";
            size_t cap = strlen(src) + 256;
            char *out = malloc(cap);
            size_t out_len = 0;

            char names[16][128];
            int name_count = 0;

            while (*src) {
                if (*src == '{') {
                    const char *end = src + 1;
                    while (*end && *end != '}') end++;
                    int len = (int)(end - (src + 1));
                    if (len > 0 && len < 127) {
                        char varname[128];
                        memcpy(varname, src + 1, len);
                        varname[len] = '\0';

                        int idx = -1;
                        for (int i = 0; i < name_count; i++)
                            if (strcmp(names[i], varname) == 0) { idx = i; break; }
                        if (idx < 0 && name_count < 16) {
                            size_t cpylen = strlen(varname) < sizeof(names[0]) - 1 ? strlen(varname) : sizeof(names[0]) - 1;
                            memcpy(names[name_count], varname, cpylen);
                            names[name_count][cpylen] = '\0';
                            idx = name_count++;
                        }

                        char numbuf[16];
                        int nlen = snprintf(numbuf, sizeof(numbuf), "{%d}", idx < 0 ? 0 : idx);
                        if (out_len + nlen >= cap) { cap = (out_len + nlen) * 2; out = realloc(out, cap); }
                        memcpy(out + out_len, numbuf, nlen);
                        out_len += nlen;
                    }
                    src = *end ? end + 1 : end;
                } else {
                    if (out_len + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
                    out[out_len++] = *src++;
                }
            }
            out[out_len] = '\0';

            MIRValue *args = name_count > 0 ? malloc(name_count * sizeof(MIRValue)) : NULL;
            for (int i = 0; i < name_count; i++) {
                MIRType t = tenv_get_var(&env, names[i]);
                /* IROP_NONE-shaped lookup: reuse LOAD_VAR's own convention -
                   the variable's *current value*, not its address, is what
                   {varname} has always meant here (matches the old codegen's
                   {varname} -> "rbp - N" being used as/inside a memory operand
                   by the asm author themselves, e.g. writing {ptr} directly
                   where a register holding a pointer value is expected). */
                MIRInstr *ld = mir_emit(fn, MIR_LOAD);
                int v = mir_new_vreg(fn);
                ld->dest = mir_vreg(v, t);
                ld->src1 = var_location(&env, names[i], t);
                args[i] = mir_vreg(v, t);
            }

            MIRInstr *m = mir_emit(fn, MIR_ASM_TEXT);
            m->asm_text = out; /* transfers ownership - freed with the MIR module's lifetime is
                                   out of scope here; this leaks today, same as other str_extra
                                   copies in this pass - not solved in this session */
            m->args = args;
            m->arg_count = name_count;
            break;
        }

        case IR_NOP:
            mir_emit(fn, MIR_NOP);
            break;

        default:
            warn_unhandled(ins);
            mir_emit(fn, MIR_NOP);
            break;
        }
    }

    tenv_free(&env);
    return mod;
}
