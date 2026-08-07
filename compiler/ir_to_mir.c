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

/* MIR local slot ids are per-function-frame (see local_offset() in
   lower_x64.c), but `vars` is one flat table shared across the whole
   module's IR_FUNC_BEGIN...IR_FUNC_END stream. Without clearing local
   (is_local=1) entries between functions, two different functions that
   happen to share a parameter/local name (e.g. two functions each with an
   `int size` param) alias onto whichever one registered first - the second
   function's references silently resolve to a slot id that belongs to a
   completely different stack frame. Global/static entries (is_local=0) are
   registered once at module scope and must survive this, so only locals
   are dropped. Call at the start of every IR_FUNC_BEGIN. */
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

/* lower.c restarts its temp counter at 0 for every function, so temp ids are
   only unique WITHIN a function. The temp table is one flat map for the whole
   module, so without clearing it here, function B's temp 3 inherits whatever
   type function A's temp 3 had. That's not a cosmetic mislabel: tenv_get_temp
   feeds the int-vs-float dispatch below, so a stale MIR_F64 turns an ordinary
   integer compare into MIR_FCMP_* on a GPR value - the comparison then reads
   an integer bit pattern as a double and the branch goes whichever way the
   garbage says. Cleared at every IR_FUNC_BEGIN, alongside tenv_clear_locals. */
static void tenv_clear_temps(TypeEnv *e) {
    e->temp_count = 0;
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

/* Hylian's surface type names -> MIRType. */
static MIRType type_name_to_mir(const char *name) {
    if (!name) return MIR_I64;
    if (strcmp(name, "float") == 0) return MIR_F64;
    if (strcmp(name, "float32") == 0) return MIR_F32;
    if (strcmp(name, "bool") == 0)  return MIR_I64; /* bools stored as 0/1 in I64 for now */
    if (strcmp(name, "rawptr") == 0 || strcmp(name, "ptr") == 0) return MIR_PTR;
    /* Sized integers. These matter for pointer dereference width - see
       MIR_LOAD/MIR_STORE in lower_x64.c. Getting them wrong doesn't just
       widen a value, it reads or writes neighbouring bytes. */
    if (strcmp(name, "int8")   == 0) return MIR_I8;
    if (strcmp(name, "uint8")  == 0) return MIR_U8;
    if (strcmp(name, "int16")  == 0) return MIR_I16;
    if (strcmp(name, "uint16") == 0) return MIR_U16;
    if (strcmp(name, "int32")  == 0) return MIR_I32;
    if (strcmp(name, "uint32") == 0) return MIR_U32;
    if (strcmp(name, "uint64") == 0 || strcmp(name, "usize") == 0) return MIR_U64;
    return MIR_I64; /* int, int64, isize, str (as ptr-ish), classes, enums */
}

/* lower_operand can emit (e.g. a bare CONST_STR operand needs a LEA_GLOBAL
   to actually get its address), so it needs the module + current function.
   IMPORTANT: because it can emit, every caller must resolve ALL of its
   operands into locals BEFORE calling mir_emit() for the instruction being
   built. Writing `m = mir_emit(...); m->src1 = lower_operand(...)` is wrong
   twice over: the LEA_GLOBAL lands *after* m in the stream (so m reads a vreg
   that isn't defined yet - mir_verify catches this as "uses vN before it's
   defined"), and mir_emit()'s realloc of fn->instrs can leave `m` dangling. */
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

/* ir_to_mir uses IR temp ids DIRECTLY as MIR vreg ids (mir_vreg(temp_id, ..))
   for everything that came from lower.c, but a few lowerings also need extra
   scratch values of their own (print's int buffer, a bare CONST_STR operand's
   address, asm-block operand loads) and get those from mir_new_vreg(), which
   hands out fn->vreg_count++ starting at 0. Those two id spaces overlap: the
   first mir_new_vreg() returns 0, which is already IR temp 0 - two unrelated
   values sharing one vreg, so regalloc gives them one register and each
   silently overwrites the other.
   Fix: at IR_FUNC_BEGIN, scan this function's IR range for the highest temp id
   it uses and start fn->vreg_count above it, so the two id spaces are disjoint. */
static void mark_max_temp(const IROperand *op, int *max_id) {
    if (op->kind == IROP_TEMP && op->temp_id > *max_id) *max_id = op->temp_id;
}

/* Same idea, for label ids: the null-guard synthesized for PRINT_ARG_STR_PTR
   below (see IR_PRINT/IR_PRINTLN) needs a couple of fresh label ids of its
   own. Labels are used as direct array indices for fixup resolution in
   hygen/libhyx64/lower_x64.c (`label_offset = malloc((max_label+1) * ...)`),
   so picking an arbitrarily large synthetic id would try to allocate a
   multi-gigabyte array. Scanning this function's IR range for its highest
   already-used label id and counting up from there keeps ids small and
   collision-free. */
static void mark_max_label(const IROperand *op, int *max_id) {
    if (op->kind == IROP_LABEL_ID && op->label_id > *max_id) *max_id = op->label_id;
}

static int scan_func_max_label(const IRModule *ir, int begin_idx) {
    int max_id = -1;
    for (int i = begin_idx; i < ir->instr_count; i++) {
        const IRInstr *ins = &ir->instrs[i];
        if (i > begin_idx && ins->op == IR_FUNC_BEGIN) break;
        if (ins->op == IR_FUNC_END) break;
        mark_max_label(&ins->dest, &max_id);
        mark_max_label(&ins->src1, &max_id);
        mark_max_label(&ins->src2, &max_id);
        mark_max_label(&ins->extra_src, &max_id);
    }
    return max_id + 1;
}

static int scan_func_max_temp(const IRModule *ir, int begin_idx) {
    int max_id = -1;
    for (int i = begin_idx; i < ir->instr_count; i++) {
        const IRInstr *ins = &ir->instrs[i];
        if (i > begin_idx && ins->op == IR_FUNC_BEGIN) break;
        if (ins->op == IR_FUNC_END) break;
        mark_max_temp(&ins->dest, &max_id);
        mark_max_temp(&ins->src1, &max_id);
        mark_max_temp(&ins->src2, &max_id);
        mark_max_temp(&ins->extra_src, &max_id);
        for (int a = 0; a < ins->arg_count; a++)
            mark_max_temp(&ins->args[a], &max_id);
    }
    return max_id + 1;
}

static int mir_ends_in_terminator(const MIRFunc *fn) {
    if (!fn || fn->count == 0) return 0;
    switch (fn->instrs[fn->count - 1].op) {
    case MIR_RET: case MIR_JMP: case MIR_JMP_IF: case MIR_JMP_UNLESS: case MIR_IRET:
        return 1;
    default:
        return 0;
    }
}

static void warn_unhandled(const IRInstr *ins) {
    fprintf(stderr, "[ir_to_mir] not lowered yet: %s (left as nop, not silently correct)\n",
            ir_opcode_name(ins->op));
}


/* Emit the constructor call for a freshly allocated object, if the class has
   one. `new Foo(a, b)` previously lowered to an allocation and NOTHING ELSE —
   the constructor body was compiled into `Foo__ctor` and then never called, so
   every field of every heap object stayed at whatever the allocator left
   there. The ctor takes the object as its hidden first parameter (`self`),
   matching how lower_func_body lays out methods. */
static void emit_ctor_call(MIRFunc *fn, const IRModule *ir, const IRInstr *ins,
                           TypeEnv *env, MIRModule *mod, int obj_vreg) {
    if (!ins->str_extra) return;
    if (!ast_class_has_ctor(ir->classes, ir->class_count, ins->str_extra)) return;

    int n = ins->arg_count + 1;
    MIRValue *cargs = malloc(n * sizeof(MIRValue));
    cargs[0] = mir_vreg(obj_vreg, MIR_PTR);
    for (int a = 0; a < ins->arg_count; a++)
        cargs[a + 1] = lower_operand(&ins->args[a], env, mod, fn);

    size_t len = strlen(ins->str_extra) + 8;
    char *ctor = malloc(len);
    snprintf(ctor, len, "%s__ctor", ins->str_extra);
    mir_build_call(fn, ctor, cargs, n, mir_none());
}

MIRModule *lower_ir_to_mir(const IRModule *ir) {
    MIRModule *mod = mir_module_new();
    MIRFunc *fn = NULL;
    int next_synth_label = 0; /* see scan_func_max_label()'s comment */
    TypeEnv env;
    tenv_init(&env);

    for (int i = 0; i < ir->instr_count; i++) {
        const IRInstr *ins = &ir->instrs[i];

        switch (ins->op) {

        case IR_FUNC_BEGIN: {
            tenv_clear_locals(&env);
            tenv_clear_temps(&env);
            fn = mir_func_new(mod, ins->str_extra ? ins->str_extra : "?");
            fn->param_count = ins->param_count;
            /* keep mir_new_vreg()'s ids clear of the IR temp ids we reuse verbatim */
            fn->vreg_count = scan_func_max_temp(ir, i);
            next_synth_label = scan_func_max_label(ir, i);
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
            /* Guarantee the function ends in a terminator, whatever the IR
               looked like. opt_branch_fold's unreachable-code sweep legally
               NOPs out everything between an early `return` and the next
               label - and for a function whose last statement is unreachable,
               that includes the trailing arena_free + return that lower.c
               appended, leaving the function ending in NOPs. That's correct as
               an optimization but violates mir_verify's "must end in a
               terminator" rule and would leave x64_lower_func emitting a
               function that falls off its own end. Re-adding a ret here keeps
               the invariant an invariant no matter what the optimizer did. */
            if (fn && (fn->count == 0 || !mir_ends_in_terminator(fn)))
                mir_emit(fn, MIR_RET);
            fn = NULL;
            break;

        case IR_ALLOCA: {
            /* str_extra = var name, str_extra2 = type name */
            MIRType t = type_name_to_mir(ins->str_extra2);
            if (ins->str_extra) {
                /* Parameters already got a local slot from IR_FUNC_BEGIN,
                   and the function prologue stores the incoming register
                   arg into that exact slot id. lower.c also emits an
                   ALLOCA for every parameter (so codegen has type info for
                   locals generally) - allocating a *second* slot here would
                   silently repoint every later LOAD_VAR/STORE_VAR at an
                   unrelated, never-written slot while the prologue keeps
                   writing to the original one. Reuse the existing slot
                   instead of shadowing it. */
                int existing = tenv_local_id(&env, ins->str_extra);
                if (existing >= 0) {
                    tenv_update_type(&env, ins->str_extra, t);
                } else {
                    /* A local slot is 8 bytes, but a BY-VALUE struct local is
                       as big as the struct. With only one slot reserved, a
                       `Point p;` has p.y (offset 8) landing on whatever local
                       sits next in the frame — in practice the hidden
                       __arena__ pointer, which then gets silently overwritten
                       and the next `new` crashes.

                       Slot ids grow DOWNWARD in memory (local_offset() is
                       -8*(id+1)) while a struct's fields grow UPWARD from its
                       base. So the base must be the HIGHEST id of the span,
                       not the lowest: reserve all the slots first, then bind
                       the variable to the last one. Binding it to the first
                       would put the struct's fields on top of the slots that
                       came before it. Heap classes only ever store a pointer
                       in the local, so they need exactly one slot. */
                    int slots = 1;
                    if (ast_is_value_aggregate(ir->classes, ir->class_count,
                                               ins->str_extra2)) {
                        int struct_size = ast_class_byte_size(ir->classes, ir->class_count,
                                                              ins->str_extra2);
                        slots = (struct_size + 7) / 8;
                        if (slots < 1) slots = 1;
                    }
                    int lid = mir_new_local(fn);
                    for (int e = 1; e < slots; e++) lid = mir_new_local(fn);
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
            MIRValue val = lower_operand(&ins->src1, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, MIR_STORE);
            m->dest = var_location(&env, ins->str_extra, t);
            m->src1 = val;
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
            MIRValue cond = lower_operand(&ins->src1, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, ins->op == IR_JUMP_IF ? MIR_JMP_IF : MIR_JMP_UNLESS);
            m->src1 = cond;
            m->dest = mir_label(ins->src2.label_id);
            break;
        }

        case IR_RETURN: {
            int has_val = (ins->src1.kind != IROP_NONE);
            MIRValue rv = has_val ? lower_operand(&ins->src1, &env, mod, fn) : mir_none();
            MIRInstr *m = mir_emit(fn, MIR_RET);
            if (has_val) m->src1 = rv;
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
            /* `new` inside an unsafe block: raw malloc, caller frees. This used
               to call `<Class>_new`, a function nothing ever generated, so it
               was an undefined symbol at link time. */
            int size = ast_class_byte_size(ir->classes, ir->class_count, ins->str_extra);
            MIRValue *args = malloc(sizeof(MIRValue));
            args[0] = mir_imm_int(size, MIR_I64);
            mir_build_call(fn, "malloc", args, 1, mir_vreg(ins->dest.temp_id, MIR_PTR));
            tenv_set_temp(&env, ins->dest.temp_id, MIR_PTR);
            emit_ctor_call(fn, ir, ins, &env, mod, ins->dest.temp_id);
            break;
        }

        /* ---- struct fields: a real load/store at a known offset ----

           These used to lower to calls to `<Class>_get_<field>` and
           `<Class>_set_<field>`. Nothing anywhere generated those functions, so
           every single field access in the language was an undefined symbol at
           link time — classes were effectively unusable, which is why the whole
           stdlib is written with raw pointers instead.

           Doing it as a call would also have been the wrong shape even if the
           accessors existed: a function call per field read, for what is one
           `mov` once the offset is known. The offset IS known — ast_field_offset()
           computes it from the class table the IR already carries — so the field
           access becomes a single load/store at base+offset, correctly sized for
           the field's own type. */
        case IR_GET_FIELD: {
            int width = 8;
            const char *ftype = NULL;
            int off = ast_field_offset(ir->classes, ir->class_count,
                                       ins->str_extra, ins->str_extra2,
                                       &width, &ftype);
            if (off < 0) {
                fprintf(stderr,
                    "[ir_to_mir] unknown field '%s.%s' — no layout for it, "
                    "emitting a zero rather than a wrong address\n",
                    ins->str_extra ? ins->str_extra : "?",
                    ins->str_extra2 ? ins->str_extra2 : "?");
                MIRInstr *z = mir_emit(fn, MIR_MOV);
                z->dest = mir_vreg(ins->dest.temp_id, MIR_I64);
                z->src1 = mir_imm_int(0, MIR_I64);
                tenv_set_temp(&env, ins->dest.temp_id, MIR_I64);
                break;
            }
            MIRType ft = type_name_to_mir(ftype);
            MIRValue base = lower_operand(&ins->src1, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, MIR_LOAD);
            m->dest = mir_vreg(ins->dest.temp_id, ft);
            m->src1 = base;
            m->mem_offset = off;
            tenv_set_temp(&env, ins->dest.temp_id, ft);
            break;
        }

        case IR_SET_FIELD: {
            int width = 8;
            const char *ftype = NULL;
            int off = ast_field_offset(ir->classes, ir->class_count,
                                       ins->str_extra, ins->str_extra2,
                                       &width, &ftype);
            if (off < 0) {
                fprintf(stderr,
                    "[ir_to_mir] unknown field '%s.%s' — no layout for it, "
                    "dropping the store rather than writing to a wrong address\n",
                    ins->str_extra ? ins->str_extra : "?",
                    ins->str_extra2 ? ins->str_extra2 : "?");
                break;
            }
            MIRType ft = type_name_to_mir(ftype);
            MIRValue base = lower_operand(&ins->src1, &env, mod, fn);
            MIRValue val  = lower_operand(&ins->src2, &env, mod, fn);
            val.type = ft; /* decides the store width */
            MIRInstr *m = mir_emit(fn, MIR_STORE);
            m->dest = base;
            m->src1 = val;
            m->mem_offset = off;
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

        /* A multi is two 8-byte words: tag at offset 0, value at offset 8.
           Both are plain loads through the multi's pointer. */
        case IR_MULTI_TAG: case IR_MULTI_VALUE: {
            MIRValue base = lower_operand(&ins->src1, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, MIR_LOAD);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_I64);
            m->src1 = base;
            m->mem_offset = (ins->op == IR_MULTI_VALUE) ? 8 : 0;
            tenv_set_temp(&env, ins->dest.temp_id, MIR_I64);
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
            /* arena_alloc(Arena *a, size_t size) - src1 is the __arena__
               pointer computed by lower.c's IR_ADDROF; extra_int is the
               class size. Both must reach the call, in that order, to
               match runtime/std/mem.c's signature - dropping src1 leaves
               the size in the pointer slot and garbage in the size slot. */
            MIRValue *args = malloc(2 * sizeof(MIRValue));
            args[0] = lower_operand(&ins->src1, &env, mod, fn);
            /* Size comes from the class layout, not from extra_int: lower.c
               never filled that in, so every `new` asked the arena for 0 bytes
               and then the constructor wrote fields past the end of it. */
            args[1] = mir_imm_int(ast_class_byte_size(ir->classes, ir->class_count,
                                                     ins->str_extra), MIR_I64);
            mir_build_call(fn, "arena_alloc", args, 2, mir_vreg(ins->dest.temp_id, MIR_PTR));
            tenv_set_temp(&env, ins->dest.temp_id, MIR_PTR);
            emit_ctor_call(fn, ir, ins, &env, mod, ins->dest.temp_id);
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
            MIRValue base = lower_operand(&ins->src1, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, MIR_LOAD);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_I64);
            m->src1 = base;
            tenv_set_temp(&env, ins->dest.temp_id, MIR_I64);
            break;
        }

        case IR_ARRAY_CAP: {
            /* cap is the 8-byte field right after len; encoder can special-case
               a nonzero extra_int as a byte offset added to the load address */
            MIRValue base = lower_operand(&ins->src1, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, MIR_LOAD);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_I64);
            m->src1 = base;
            m->mem_offset = 8;
            tenv_set_temp(&env, ins->dest.temp_id, MIR_I64);
            break;
        }

        /* ---- raw pointers / volatile ---- */
        case IR_LOAD_PTR: case IR_LOAD_VOLATILE: {
            /* str_extra carries the pointee type name (set by lower.c from the
               deref's resolved type). The load width is decided by it: reading
               8 bytes through a `*uint8` both returns garbage in the upper
               bytes and can run off the end of the object. */
            MIRType lt = type_name_to_mir(ins->str_extra);
            MIRValue src = lower_operand(&ins->src1, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, MIR_LOAD);
            m->dest = mir_vreg(ins->dest.temp_id, lt);
            m->src1 = src;
            m->extra_int = (ins->op == IR_LOAD_VOLATILE) ? 1 : 0;
            tenv_set_temp(&env, ins->dest.temp_id, lt);
            break;
        }

        case IR_STORE_PTR: case IR_STORE_VOLATILE: {
            MIRValue addr = lower_operand(&ins->src1, &env, mod, fn);
            MIRValue val  = lower_operand(&ins->src2, &env, mod, fn);
            /* str_extra is the stored value's type name (lower.c fills it from
               the RHS, falling back to the LHS). It, not the register width,
               decides how many bytes actually get written. */
            if (ins->str_extra) val.type = type_name_to_mir(ins->str_extra);
            MIRInstr *m = mir_emit(fn, MIR_STORE);
            m->dest = addr;
            m->src1 = val;
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
            if (ins->str_extra3) {
                /* @section("...") global: a custom section is always emitted
                   with explicit bytes (no .bss/NOBITS placement for it), so
                   a value-less declaration still needs has_init=1 to get its
                   (zero) bytes written into that section rather than treated
                   as uninitialized. */
                mir_module_add_global_ex(mod, ins->str_extra, 1, init_val, 8,
                                         ins->str_extra3, NULL);
            } else {
                mir_module_add_global(mod, ins->str_extra, has_init, init_val, 8);
            }
            tenv_set_var(&env, ins->str_extra, type_name_to_mir(ins->str_extra2));
            break;
        }

        /* ---- memory ---- */
        case IR_MEMSET: {
            MIRValue p = lower_operand(&ins->src1, &env, mod, fn);
            MIRValue v = lower_operand(&ins->src2, &env, mod, fn);
            MIRValue n = lower_operand(&ins->extra_src, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, MIR_MEMSET);
            m->dest = p; m->src1 = v; m->src2 = n;
            break;
        }

        case IR_MEMCPY: {
            MIRValue d = lower_operand(&ins->src1, &env, mod, fn);
            MIRValue s2 = lower_operand(&ins->src2, &env, mod, fn);
            MIRValue n = lower_operand(&ins->extra_src, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, MIR_MEMCPY);
            m->dest = d; m->src1 = s2; m->src2 = n;
            break;
        }

        /* ---- privileged / kernel intrinsics: direct 1:1 to dedicated MIR ops ---- */
        case IR_CLI: mir_emit(fn, MIR_CLI); break;
        case IR_STI: mir_emit(fn, MIR_STI); break;
        case IR_IRET: mir_emit(fn, MIR_IRET); break;

        case IR_LGDT: case IR_LIDT: {
            MIRValue a = lower_operand(&ins->src1, &env, mod, fn);
            MIRValue b = lower_operand(&ins->src2, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, ins->op == IR_LGDT ? MIR_LGDT : MIR_LIDT);
            m->src1 = a; m->src2 = b;
            break;
        }

        case IR_LTR: case IR_INVLPG: {
            MIRValue a = lower_operand(&ins->src1, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, ins->op == IR_LTR ? MIR_LTR : MIR_INVLPG);
            m->src1 = a;
            break;
        }

        case IR_WRMSR: {
            MIRValue a = lower_operand(&ins->src1, &env, mod, fn);
            MIRValue b = lower_operand(&ins->src2, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, MIR_WRMSR);
            m->src1 = a; m->src2 = b;
            break;
        }

        case IR_RDMSR: {
            MIRValue a = lower_operand(&ins->src1, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, MIR_RDMSR);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_U64);
            m->src1 = a;
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
            MIRValue a = lower_operand(&ins->src1, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, MIR_WRITE_CR);
            m->src1 = a;
            m->extra_int = ins->extra_int;
            break;
        }

        case IR_OUTB: {
            MIRValue a = lower_operand(&ins->src1, &env, mod, fn);
            MIRValue b = lower_operand(&ins->src2, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, MIR_OUTB);
            m->src1 = a; m->src2 = b;
            break;
        }

        case IR_INB: {
            MIRValue a = lower_operand(&ins->src1, &env, mod, fn);
            MIRInstr *m = mir_emit(fn, MIR_INB);
            m->dest = mir_vreg(ins->dest.temp_id, MIR_U64);
            m->src1 = a;
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
                /* A `str` local can legitimately be nil (e.g. read_all()
                   returns nil on failure) — calling libc strlen() on it
                   unconditionally segfaulted the moment any such value
                   reached print/println. Guard it: skip the strlen call
                   and print zero bytes when the pointer is null. */
                MIRValue ptrv = lower_operand(&ins->src1, &env, mod, fn);
                int lenv = mir_new_vreg(fn);
                int condv = mir_new_vreg(fn);
                int lbl_null = next_synth_label++;
                int lbl_done = next_synth_label++;

                MIRInstr *cmp = mir_emit(fn, MIR_CMP_EQ);
                cmp->dest = mir_vreg(condv, MIR_I64);
                cmp->src1 = ptrv;
                cmp->src2 = mir_imm_int(0, ptrv.type);

                MIRInstr *jif = mir_emit(fn, MIR_JMP_IF);
                jif->src1 = mir_vreg(condv, MIR_I64);
                jif->dest = mir_label(lbl_null);

                MIRValue *len_args = malloc(sizeof(MIRValue));
                len_args[0] = ptrv;
                mir_build_call(fn, "strlen", len_args, 1, mir_vreg(lenv, MIR_I64));

                MIRInstr *jdone = mir_emit(fn, MIR_JMP);
                jdone->dest = mir_label(lbl_done);

                MIRInstr *lbl1 = mir_emit(fn, MIR_LABEL);
                lbl1->dest = mir_label(lbl_null);
                MIRInstr *movz = mir_emit(fn, MIR_MOV);
                movz->dest = mir_vreg(lenv, MIR_I64);
                movz->src1 = mir_imm_int(0, MIR_I64);

                MIRInstr *lbl2 = mir_emit(fn, MIR_LABEL);
                lbl2->dest = mir_label(lbl_done);

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
