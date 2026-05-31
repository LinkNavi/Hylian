#include "lower.h"
#include "ir.h"
#include "ast.h"
#include "typecheck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *module_names[64];
static int   module_count = 0;

static int lower_is_module(const char *name) {
    for (int i = 0; i < module_count; i++)
        if (strcmp(module_names[i], name) == 0) return 1;
    return 0;
}



#define MAX_LOOP_DEPTH 32
#define MAX_STATIC_LOWER 256
#define MAX_LOCAL_STATIC_ALIASES 64

typedef struct {
    int break_lbl;
    int cont_lbl;
} LoopEntry;

typedef struct {
    const char *name;
    const char *type_name;
    int         is_aggregate;
} LowerStaticVar;

/* Maps an original local-static name to its mangled .data label */
typedef struct {
    const char *orig_name;    /* as written in source, e.g. "buf"            */
    const char *mangled_name; /* unique .data label, e.g. "__static_3__buf"  */
    int         array_size;   /* 0 = scalar, >0 = fixed array                */
} LocalStaticAlias;

typedef struct {
    IRModule  *mod;
    int        next_temp;    /* monotonically increasing temp ID (per-function) */
    int        next_label;   /* monotonically increasing label ID (global)      */
    LoopEntry  loop_stack[MAX_LOOP_DEPTH];
    int        loop_top;
    LowerStaticVar static_vars[MAX_STATIC_LOWER];
    int        static_var_count;
    /* Function-local static variable name → mangled .data label */
    LocalStaticAlias local_static_aliases[MAX_LOCAL_STATIC_ALIASES];
    int              local_static_alias_count;
    /* Top-level / module-level static arrays (flat layout, no heap header) */
    const char *top_static_array_names[MAX_STATIC_LOWER];
    const char *top_static_array_types[MAX_STATIC_LOWER];
    int         top_static_array_count;
    /* Current class context (non-NULL inside method / ctor) */
    const char *class_name;
    /* Current module context (non-NULL inside a module function) */
    const char *current_module;
    /* Sibling function names in the current module (for intra-module call mangling) */
    const char *module_func_names[64];
    int         module_func_count;
    /* Arena / unsafe tracking */
    int         in_unsafe;          /* 1 when inside an unsafe { } block    */
    int         has_arena;          /* 1 when current function owns an arena */
} LowerState;


static int alloc_temp(LowerState *s)  { return s->next_temp++; }
static int alloc_label(LowerState *s) { return s->next_label++; }

static int lower_has_class(LowerState *s, const char *type_name) {
    if (!s || !s->mod || !type_name) return 0;
    for (int i = 0; i < s->mod->class_count; i++) {
        ClassNode *cls = s->mod->classes[i];
        if (cls && cls->name && strcmp(cls->name, type_name) == 0)
            return 1;
    }
    return 0;
}

static void lower_register_static_var(LowerState *s, const char *name, const char *type_name) {
    if (!s || !name || s->static_var_count >= MAX_STATIC_LOWER) return;
    LowerStaticVar *sv = &s->static_vars[s->static_var_count++];
    sv->name = name;
    sv->type_name = type_name;
    sv->is_aggregate = lower_has_class(s, type_name);
}

static const char *lower_static_aggregate_type(LowerState *s, const char *name) {
    if (!s || !name) return NULL;
    for (int i = 0; i < s->static_var_count; i++) {
        LowerStaticVar *sv = &s->static_vars[i];
        if (sv->is_aggregate && sv->name && strcmp(sv->name, name) == 0)
            return sv->type_name;
    }
    return NULL;
}

/*
 * Look up a function-local static alias.  Returns the mangled .data label
 * for `name` if one was registered with lower_stmt / NODE_STATIC_VAR,
 * or NULL if the name is not a local static in the current function.
 */
static const LocalStaticAlias *lower_find_local_static(const LowerState *s, const char *name) {
    if (!s || !name) return NULL;
    for (int i = 0; i < s->local_static_alias_count; i++) {
        const LocalStaticAlias *a = &s->local_static_aliases[i];
        if (a->orig_name && strcmp(a->orig_name, name) == 0)
            return a;
    }
    return NULL;
}

/*
 * Return the element type name for a top-level or module-level static array,
 * or NULL if `name` is not in that table.
 */
static const char *lower_find_top_static_array_type(const LowerState *s, const char *name) {
    if (!s || !name) return NULL;
    for (int i = 0; i < s->top_static_array_count; i++) {
        if (s->top_static_array_names[i] &&
            strcmp(s->top_static_array_names[i], name) == 0)
            return s->top_static_array_types[i];
    }
    return NULL;
}



static void loop_push(LowerState *s, int brk, int cont) {
    if (s->loop_top < MAX_LOOP_DEPTH) {
        s->loop_stack[s->loop_top].break_lbl = brk;
        s->loop_stack[s->loop_top].cont_lbl  = cont;
        s->loop_top++;
    }
}
static void loop_pop(LowerState *s) { if (s->loop_top > 0) s->loop_top--; }
static int  loop_break(LowerState *s) { return s->loop_top > 0 ? s->loop_stack[s->loop_top-1].break_lbl : -1; }
static int  loop_cont(LowerState *s)  { return s->loop_top > 0 ? s->loop_stack[s->loop_top-1].cont_lbl  : -1; }

/* Helper: emit a ALLOCA + optional STORE_VAR 0 for a local variable */
static void emit_alloca(LowerState *s, const char *name, const char *type_name, int type_kind) {
    IRInstr *a      = ir_emit(s->mod, IR_ALLOCA);
    a->str_extra    = strdup(name);
    a->str_extra2   = type_name ? strdup(type_name) : NULL;
    a->extra_int    = type_kind;
}

/* Helper: classify argument type for print/println dispatch */
static int classify_print_arg(ASTNode *arg) {
    if (!arg) return PRINT_ARG_INT;
    if (arg->type == NODE_INTERP_STRING) return PRINT_ARG_INTERP;
    if (arg->type == NODE_LITERAL && ((LiteralNode *)arg)->lit_type == LIT_STRING)
        return PRINT_ARG_STR_LIT;
    if (arg->type == NODE_LITERAL && ((LiteralNode *)arg)->lit_type == LIT_FLOAT)
        return PRINT_ARG_FLOAT;
    /* Check resolved type for float */
    if (arg->resolved_type.kind == TYPE_SIMPLE && arg->resolved_type.name &&
        strcmp(arg->resolved_type.name, "float") == 0)
        return PRINT_ARG_FLOAT;
    /* Check resolved type for str pointer */
    if (arg->resolved_type.kind == TYPE_SIMPLE && arg->resolved_type.name &&
        strcmp(arg->resolved_type.name, "str") == 0)
        return PRINT_ARG_STR_PTR;
    /* Method call returning str (e.g. .message()) */
    if (arg->type == NODE_METHOD_CALL &&
        strcmp(((MethodCallNode *)arg)->method, "message") == 0)
        return PRINT_ARG_STR_PTR;
    return PRINT_ARG_INT;
}


static int  lower_expr(ASTNode *node, LowerState *s);
static void lower_stmt(ASTNode *node, LowerState *s);
static void lower_stmts(ASTNode **stmts, int count, LowerState *s);

/*
 * Compute the byte width of a struct field, mirroring codegen_asm.c's
 * field_total_byte_width(). Handles primitives, nested class types, and
 * fixed-size array fields (e.g. uint8[56] = 56 bytes). Crucially, this must
 * agree with the codegen — otherwise nested struct field chains compute
 * the wrong cumulative offset and end up reading garbage memory.
 */
static int lower_field_byte_width(LowerState *s, FieldNode *f) {
    if (!f) return 8;
    int prim = 8;
    const char *elem_name = NULL;

    if (f->field_type.kind == TYPE_ARRAY && f->field_type.fixed_size > 0 &&
        f->field_type.elem_type_count > 0 && f->field_type.elem_types) {
        elem_name = f->field_type.elem_types[0].name;
    } else {
        elem_name = f->field_type.name ? f->field_type.name : "int";
    }

    if (elem_name) {
        if      (strcmp(elem_name, "int8")   == 0 || strcmp(elem_name, "uint8")  == 0) prim = 1;
        else if (strcmp(elem_name, "int16")  == 0 || strcmp(elem_name, "uint16") == 0) prim = 2;
        else if (strcmp(elem_name, "int32")  == 0 || strcmp(elem_name, "uint32") == 0 ||
                 strcmp(elem_name, "float32") == 0) prim = 4;
        /* All other names default to 8 bytes (int, int64, ptr, str, float, ...). */

        /* If the element is itself a class, use the class's full size. */
        if (s && s->mod) {
            for (int i = 0; i < s->mod->class_count; i++) {
                ClassNode *cn = s->mod->classes[i];
                if (cn && cn->name && strcmp(cn->name, elem_name) == 0) {
                    /* Match codegen's packing math: non-packed classes round
                       up to multiple of 16; unions take the max field width.
                       For nested-field offset purposes we mirror that exactly. */
                    int sz = 0;
                    int max_w = 0;
                    for (int j = 0; j < cn->field_count; j++) {
                        int w = lower_field_byte_width(s, cn->fields[j]);
                        if (cn->is_union) { if (w > max_w) max_w = w; }
                        else { sz += w; }
                    }
                    if (cn->is_union) {
                        sz = max_w ? max_w : 8;
                        if (sz % 8 != 0) sz += 8 - sz % 8;
                    } else {
                        if (sz == 0) sz = 8;
                        if (!cn->is_packed && sz % 16 != 0)
                            sz += 16 - sz % 16;
                    }
                    prim = sz;
                    break;
                }
            }
        }
    }

    if (f->field_type.kind == TYPE_ARRAY && f->field_type.fixed_size > 0)
        return prim * f->field_type.fixed_size;
    return prim;
}


/*
 * Walk a chain of NODE_MEMBER_ACCESS nodes on a stack struct (no-ctor class)
 * and return a temp whose value is the base address of the innermost struct
 * object (the named variable). Also accumulates the byte offset of all the
 * intermediate fields into *out_byte_offset so the caller can emit a single
 * IR_GET_FIELD at (base + out_byte_offset) + final_field_offset.
 *
 * Returns -1 if the chain is not a plain stack-struct access (in which case
 * the caller should fall back to lower_expr for the object).
 *
 * out_type_name is set to the type of the object at the point where the
 * chain bottoms out (i.e. the type whose field we'll finally read).
 */
static int lower_stack_struct_addrof(ASTNode *node, LowerState *s,
                                     int *out_byte_offset,
                                     const char **out_type_name) {
    if (!node) return -1;

    if (node->type == NODE_IDENTIFIER) {
        /* Bottom of the chain — must be a known stack struct variable */
        if (node->resolved_type.kind != TYPE_SIMPLE || !node->resolved_type.name)
            return -1;
        /* Check it is a no-ctor (stack) class */
        int is_stack = 0;
        if (s->mod) {
            for (int i = 0; i < s->mod->class_count; i++) {
                ClassNode *cn = s->mod->classes[i];
                if (cn && cn->name &&
                    strcmp(cn->name, node->resolved_type.name) == 0) {
                    is_stack = !cn->has_ctor;
                    break;
                }
            }
        }
        if (!is_stack) return -1;
        /* Emit IR_ADDROF for the named variable */
        const char *vname = ((IdentifierNode *)node)->name;
        int t = alloc_temp(s);
        IRInstr *av   = ir_emit(s->mod, IR_ADDROF);
        av->dest      = irop_temp(t);
        av->str_extra = strdup(vname);
        *out_byte_offset = 0;
        *out_type_name   = node->resolved_type.name;
        return t;
    }

    if (node->type == NODE_MEMBER_ACCESS) {
        MemberAccessNode *ma = (MemberAccessNode *)node;
        if (!ma->object || ma->object->resolved_type.kind != TYPE_SIMPLE ||
            !ma->object->resolved_type.name)
            return -1;
        /* Recurse into the inner object */
        int inner_offset = 0;
        const char *inner_type = NULL;
        int tbase = lower_stack_struct_addrof(ma->object, s, &inner_offset, &inner_type);
        if (tbase < 0) return -1;
        /* Find the byte offset of ma->member within inner_type.
         * We look it up from the class registry on the AST side (ClassNode fields). */
        if (!inner_type || !s->mod) return -1;
        ClassNode *cls = NULL;
        for (int i = 0; i < s->mod->class_count; i++) {
            if (s->mod->classes[i] && s->mod->classes[i]->name &&
                strcmp(s->mod->classes[i]->name, inner_type) == 0) {
                cls = s->mod->classes[i];
                break;
            }
        }
        if (!cls) return -1;
        /* Walk fields with the correct width helper so nested classes and
           fixed-size arrays contribute their real size, not 8 bytes. */
        int foff = -1;
        const char *ftype = NULL;
        int running = 0;
        for (int i = 0; i < cls->field_count; i++) {
            FieldNode *f = cls->fields[i];
            if (!f) continue;
            const char *tname = f->field_type.name ? f->field_type.name : "int";
            int w = lower_field_byte_width(s, f);
            if (f->name && strcmp(f->name, ma->member) == 0) {
                /* For union classes all fields are at offset 0 */
                foff  = cls->is_union ? 0 : running;
                ftype = tname;
                break;
            }
            if (!cls->is_union) running += w;
        }
        if (foff < 0) return -1;
        *out_byte_offset = inner_offset + foff;
        *out_type_name   = ftype; /* type of the field we just stepped through */
        return tbase;
    }

    return -1; /* not a simple chain */
}


/* Emit code for `node`; return the temp ID that holds the result. */
static int lower_expr(ASTNode *node, LowerState *s) {
    if (!node) {
        int t = alloc_temp(s);
        IRInstr *ins = ir_emit(s->mod, IR_CONST_NIL);
        ins->dest = irop_temp(t);
        return t;
    }

    switch (node->type) {

    case NODE_LITERAL: {
        LiteralNode *lit = (LiteralNode *)node;
        int t = alloc_temp(s);
        if (lit->lit_type == LIT_INT) {
            IRInstr *ins = ir_emit(s->mod, IR_CONST_INT);
            ins->dest = irop_temp(t);
            ins->src1 = irop_const_int(atol(lit->value));
        } else if (lit->lit_type == LIT_STRING) {
            /* Strip surrounding quotes */
            const char *v = lit->value;
            size_t len = strlen(v);
            char *unquoted = malloc(len + 1);
            if (len >= 2 && v[0] == '"' && v[len-1] == '"') {
                memcpy(unquoted, v + 1, len - 2);
                unquoted[len - 2] = '\0';
            } else {
                strcpy(unquoted, v);
            }
            IRInstr *ins = ir_emit(s->mod, IR_CONST_STR);
            ins->dest = irop_temp(t);
            ins->src1 = irop_const_str(unquoted); /* ownership transferred */
        } else if (lit->lit_type == LIT_BOOL) {
            IRInstr *ins = ir_emit(s->mod, IR_CONST_BOOL);
            ins->dest    = irop_temp(t);
            ins->src1    = irop_const_bool(strcmp(lit->value, "true") == 0 ? 1 : 0);
        } else if (lit->lit_type == LIT_NIL) {
            IRInstr *ins = ir_emit(s->mod, IR_CONST_NIL);
            ins->dest    = irop_temp(t);
        } else if (lit->lit_type == LIT_FLOAT) {
            IRInstr *ins = ir_emit(s->mod, IR_CONST_FLOAT);
            ins->dest = irop_temp(t);
            ins->src1 = irop_const_float(atof(lit->value));
        } else {
            /* unknown literal kind — zero */
            IRInstr *ins = ir_emit(s->mod, IR_CONST_INT);
            ins->dest = irop_temp(t);
            ins->src1 = irop_const_int(0);
        }
        return t;
    }

    case NODE_IDENTIFIER: {
        IdentifierNode *id = (IdentifierNode *)node;
        int t = alloc_temp(s);
        /* If this identifier is a function-local static, emit LOAD_VAR for
         * the mangled .data label so codegen uses [rel __static_N__name]. */
        const LocalStaticAlias *lsa = lower_find_local_static(s, id->name);
        const char *resolved_name = lsa ? lsa->mangled_name : id->name;
        IRInstr *ins  = ir_emit(s->mod, IR_LOAD_VAR);
        ins->dest     = irop_temp(t);
        ins->str_extra = strdup(resolved_name);
        return t;
    }

    case NODE_BINARY_OP: {
        BinaryOpNode *bin = (BinaryOpNode *)node;
        const char *op = bin->op;

        /* Short-circuit && */
        if (strcmp(op, "&&") == 0) {
            int lbl_false = alloc_label(s);
            int lbl_end   = alloc_label(s);
            /* synthesised local to merge branches */
            char nm[32]; snprintf(nm, sizeof(nm), "__and_%d", lbl_end);
            emit_alloca(s, nm, "bool", TYPE_SIMPLE);
            {
                IRInstr *z = ir_emit(s->mod, IR_STORE_VAR);
                z->str_extra = strdup(nm); z->src1 = irop_const_bool(0);
            }
            int tl = lower_expr(bin->left, s);
            { IRInstr *j = ir_emit(s->mod, IR_JUMP_UNLESS); j->src1 = irop_temp(tl); j->src2 = irop_label(lbl_false); }
            int tr = lower_expr(bin->right, s);
            { IRInstr *j = ir_emit(s->mod, IR_JUMP_UNLESS); j->src1 = irop_temp(tr); j->src2 = irop_label(lbl_false); }
            { IRInstr *sv = ir_emit(s->mod, IR_STORE_VAR); sv->str_extra = strdup(nm); sv->src1 = irop_const_bool(1); }
            { IRInstr *j = ir_emit(s->mod, IR_JUMP); j->src1 = irop_label(lbl_end); }
            { IRInstr *lb = ir_emit(s->mod, IR_LABEL); lb->dest = irop_label(lbl_false); }
            { IRInstr *lb = ir_emit(s->mod, IR_LABEL); lb->dest = irop_label(lbl_end); }
            int t = alloc_temp(s);
            { IRInstr *lv = ir_emit(s->mod, IR_LOAD_VAR); lv->dest = irop_temp(t); lv->str_extra = strdup(nm); }
            return t;
        }

        /* Short-circuit || */
        if (strcmp(op, "||") == 0) {
            int lbl_true = alloc_label(s);
            int lbl_end  = alloc_label(s);
            char nm[32]; snprintf(nm, sizeof(nm), "__or_%d", lbl_end);
            emit_alloca(s, nm, "bool", TYPE_SIMPLE);
            { IRInstr *z = ir_emit(s->mod, IR_STORE_VAR); z->str_extra = strdup(nm); z->src1 = irop_const_bool(0); }
            int tl = lower_expr(bin->left, s);
            { IRInstr *j = ir_emit(s->mod, IR_JUMP_IF); j->src1 = irop_temp(tl); j->src2 = irop_label(lbl_true); }
            int tr = lower_expr(bin->right, s);
            { IRInstr *j = ir_emit(s->mod, IR_JUMP_IF); j->src1 = irop_temp(tr); j->src2 = irop_label(lbl_true); }
            { IRInstr *j = ir_emit(s->mod, IR_JUMP); j->src1 = irop_label(lbl_end); }
            { IRInstr *lb = ir_emit(s->mod, IR_LABEL); lb->dest = irop_label(lbl_true); }
            { IRInstr *sv = ir_emit(s->mod, IR_STORE_VAR); sv->str_extra = strdup(nm); sv->src1 = irop_const_bool(1); }
            { IRInstr *lb = ir_emit(s->mod, IR_LABEL); lb->dest = irop_label(lbl_end); }
            int t = alloc_temp(s);
            { IRInstr *lv = ir_emit(s->mod, IR_LOAD_VAR); lv->dest = irop_temp(t); lv->str_extra = strdup(nm); }
            return t;
        }

        /* Standard binary */
        /* volatile_store: emit IR_STORE_VOLATILE — result is unused */
        if (strcmp(op, "volatile_store") == 0) {
            int tptr = lower_expr(bin->left, s);
            int tval = lower_expr(bin->right, s);
            int t    = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_STORE_VOLATILE);
            ins->src1 = irop_temp(tptr);
            ins->src2 = irop_temp(tval);
            ins->dest = irop_temp(t);
            return t;
        }
        /* deref_store: *ptr = val — plain (non-volatile) pointer write */
        if (strcmp(op, "deref_store") == 0) {
            int tptr = lower_expr(bin->left, s);
            int tval = lower_expr(bin->right, s);
            int t    = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_STORE_PTR);
            ins->src1 = irop_temp(tptr);
            ins->src2 = irop_temp(tval);
            ins->dest = irop_temp(t);
            return t;
        }

        /* cast<T>(expr) — represented as binary_op("cast", expr, LIT_STRING(typename)) */
        if (strcmp(op, "cast") == 0) {
            int te = lower_expr(bin->left, s);
            int t  = alloc_temp(s);
            /* right child is a LiteralNode carrying the type name as a string */
            const char *tname = "void";
            if (bin->right && bin->right->type == NODE_LITERAL) {
                LiteralNode *lt = (LiteralNode *)bin->right;
                tname = lt->value ? lt->value : "void";
                /* strip quotes if present */
                size_t tlen = strlen(tname);
                if (tlen >= 2 && tname[0] == '"' && tname[tlen-1] == '"') {
                    char *uq = malloc(tlen - 1);
                    memcpy(uq, tname + 1, tlen - 2);
                    uq[tlen - 2] = '\0';
                    IRInstr *ins = ir_emit(s->mod, IR_CAST);
                    ins->dest      = irop_temp(t);
                    ins->src1      = irop_temp(te);
                    ins->str_extra = uq;
                    return t;
                }
            }
            IRInstr *ins = ir_emit(s->mod, IR_CAST);
            ins->dest      = irop_temp(t);
            ins->src1      = irop_temp(te);
            ins->str_extra = strdup(tname);
            return t;
        }

        int tl = lower_expr(bin->left, s);
        int tr = lower_expr(bin->right, s);
        int t  = alloc_temp(s);
        IROpcode irop =
            strcmp(op, "+") == 0  ? IR_ADD    :
            strcmp(op, "-") == 0  ? IR_SUB    :
            strcmp(op, "*") == 0  ? IR_MUL    :
            strcmp(op, "/") == 0  ? IR_DIV    :
            strcmp(op, "%") == 0  ? IR_MOD    :
            strcmp(op, "==") == 0 ? IR_EQ     :
            strcmp(op, "!=") == 0 ? IR_NEQ    :
            strcmp(op, "<") == 0  ? IR_LT     :
            strcmp(op, "<=") == 0 ? IR_LE     :
            strcmp(op, ">") == 0  ? IR_GT     :
            strcmp(op, ">=") == 0 ? IR_GE     :
            strcmp(op, "&") == 0  ? IR_BITAND :
            strcmp(op, "|") == 0  ? IR_BITOR  :
            strcmp(op, "^") == 0  ? IR_BITXOR :
            strcmp(op, "<<") == 0 ? IR_SHL    :
            strcmp(op, ">>") == 0 ? IR_SHR    : IR_ADD; /* fallback */
        IRInstr *ins = ir_emit(s->mod, irop);
        ins->dest = irop_temp(t);
        ins->src1 = irop_temp(tl);
        ins->src2 = irop_temp(tr);
        return t;
    }

    case NODE_UNARY_OP: {
        UnaryOpNode *un = (UnaryOpNode *)node;
    if (strcmp(un->op, "addrof") == 0) {
        int t = alloc_temp(s);
        IRInstr *ins = ir_emit(s->mod, IR_ADDROF);
        ins->dest = irop_temp(t);
        if (un->operand->type == NODE_IDENTIFIER)
            ins->str_extra = strdup(((IdentifierNode*)un->operand)->name);
        return t;
    }
    if (strcmp(un->op, "deref") == 0) {
        int te = lower_expr(un->operand, s);
        int t  = alloc_temp(s);
        IROpcode irop = un->is_volatile ? IR_LOAD_VOLATILE : IR_LOAD_PTR;
        IRInstr *ins = ir_emit(s->mod, irop);
        ins->dest = irop_temp(t);
        ins->src1 = irop_temp(te);
        return t;
    }
    if (strcmp(un->op, "~") == 0) {
        int te = lower_expr(un->operand, s);
        int t  = alloc_temp(s);
        IRInstr *ins = ir_emit(s->mod, IR_BITNOT);
        ins->dest = irop_temp(t);
        ins->src1 = irop_temp(te);
        return t;
    }
    {
        int te2 = lower_expr(un->operand, s);
        int t2  = alloc_temp(s);
        IROpcode unary_irop = strcmp(un->op, "-") == 0 ? IR_NEG : IR_NOT;
        IRInstr *ins2  = ir_emit(s->mod, unary_irop);
        ins2->dest = irop_temp(t2);
        ins2->src1 = irop_temp(te2);
        return t2;
    }
    }

    case NODE_FUNC_CALL: {
        FuncCallNode *call = (FuncCallNode *)node;

        /* ── Err("msg") ────────────────────────────────────────────────── */
        if (strcmp(call->name, "Err") == 0) {
            int t = alloc_temp(s);
            IROperand err_src1 = irop_const_str(strdup("error"));
            if (call->arg_count >= 1) {
                ASTNode *arg = call->args[0];
                if (arg->type == NODE_LITERAL && ((LiteralNode *)arg)->lit_type == LIT_STRING) {
                    LiteralNode *lit = (LiteralNode *)arg;
                    const char *v = lit->value; size_t len = strlen(v);
                    char *uq = malloc(len + 1);
                    if (len >= 2 && v[0] == '"' && v[len-1] == '"') {
                        memcpy(uq, v+1, len-2); uq[len-2] = '\0';
                    } else { strcpy(uq, v); }
                    err_src1 = irop_const_str(uq);
                } else {
                    int ta = lower_expr(arg, s);
                    err_src1 = irop_temp(ta);
                }
            }
            IRInstr *ins = ir_emit(s->mod, IR_ERR);
            ins->dest = irop_temp(t);
            ins->src1 = err_src1;
            return t;
        }

        /* ── cli() / sti() ─────────────────────────────────────────────── */
        if (strcmp(call->name, "cli") == 0) {
            int t = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_CLI);
            ins->dest = irop_temp(t);
            return t;
        }
        if (strcmp(call->name, "sti") == 0) {
            int t = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_STI);
            ins->dest = irop_temp(t);
            return t;
        }

        /* ── lgdt(base, limit) / lidt(base, limit) ─────────────────────────── */
        if (strcmp(call->name, "lgdt") == 0 && call->arg_count >= 2) {
            int tbase  = lower_expr(call->args[0], s);
            int tlimit = lower_expr(call->args[1], s);
            int t      = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_LGDT);
            ins->dest = irop_temp(t);
            ins->src1 = irop_temp(tbase);
            ins->src2 = irop_temp(tlimit);
            return t;
        }
        if (strcmp(call->name, "lidt") == 0 && call->arg_count >= 2) {
            int tbase  = lower_expr(call->args[0], s);
            int tlimit = lower_expr(call->args[1], s);
            int t      = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_LIDT);
            ins->dest = irop_temp(t);
            ins->src1 = irop_temp(tbase);
            ins->src2 = irop_temp(tlimit);
            return t;
        }

        /* ── ltr(selector) ──────────────────────────────────────────────────── */
        if (strcmp(call->name, "ltr") == 0 && call->arg_count >= 1) {
            int tsel = lower_expr(call->args[0], s);
            int t    = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_LTR);
            ins->dest = irop_temp(t);
            ins->src1 = irop_temp(tsel);
            return t;
        }

        /* ── invlpg(vaddr) ──────────────────────────────────────────────────── */
        if (strcmp(call->name, "invlpg") == 0 && call->arg_count >= 1) {
            int tvaddr = lower_expr(call->args[0], s);
            int t      = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_INVLPG);
            ins->dest = irop_temp(t);
            ins->src1 = irop_temp(tvaddr);
            return t;
        }

        /* ── wrmsr(msr, val) / rdmsr(msr) ───────────────────────────────────── */
        if (strcmp(call->name, "wrmsr") == 0 && call->arg_count >= 2) {
            int tmsr = lower_expr(call->args[0], s);
            int tval = lower_expr(call->args[1], s);
            int t    = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_WRMSR);
            ins->dest = irop_temp(t);
            ins->src1 = irop_temp(tmsr);
            ins->src2 = irop_temp(tval);
            return t;
        }
        if (strcmp(call->name, "rdmsr") == 0 && call->arg_count >= 1) {
            int tmsr = lower_expr(call->args[0], s);
            int t    = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_RDMSR);
            ins->dest = irop_temp(t);
            ins->src1 = irop_temp(tmsr);
            return t;
        }

        /* ── read_cr(n) / write_cr(n, val) ────────────────────────────────── */
        if (strcmp(call->name, "read_cr") == 0 && call->arg_count >= 1) {
            long n = 0;
            if (call->args[0]->type == NODE_LITERAL)
                n = atol(((LiteralNode *)call->args[0])->value);
            int t = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_READ_CR);
            ins->dest      = irop_temp(t);
            ins->extra_int = (int)n;
            return t;
        }
        if (strcmp(call->name, "write_cr") == 0 && call->arg_count >= 2) {
            long n = 0;
            if (call->args[0]->type == NODE_LITERAL)
                n = atol(((LiteralNode *)call->args[0])->value);
            int tval = lower_expr(call->args[1], s);
            int t    = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_WRITE_CR);
            ins->src1      = irop_temp(tval);
            ins->dest      = irop_temp(t);
            ins->extra_int = (int)n;
            return t;
        }

        /* ── save_regs() / restore_regs() / iret() ─────────────────────────── */
        /* save_regs()          — push all GPRs (for ISRs without an error code) */
        /* save_regs(1)         — push dummy 0 first, then all GPRs              */
        /* restore_regs()       — pop all GPRs                                   */
        /* restore_regs(1)      — discard error-code slot, then pop all GPRs     */
        /* iret()               — iretq                                          */
        if (strcmp(call->name, "save_regs") == 0) {
            int t = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_SAVE_REGS);
            ins->dest      = irop_temp(t);
            ins->extra_int = (call->arg_count >= 1) ? 1 : 0;
            return t;
        }
        if (strcmp(call->name, "restore_regs") == 0) {
            int t = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_RESTORE_REGS);
            ins->dest      = irop_temp(t);
            ins->extra_int = (call->arg_count >= 1) ? 1 : 0;
            return t;
        }
        if (strcmp(call->name, "iret") == 0) {
            int t = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_IRET);
            ins->dest = irop_temp(t);
            return t;
        }
        if (strcmp(call->name, "outb") == 0 && call->arg_count >= 2) {
            int tport = lower_expr(call->args[0], s);
            int tval  = lower_expr(call->args[1], s);
            int t     = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_OUTB);
            ins->src1 = irop_temp(tport);
            ins->src2 = irop_temp(tval);
            ins->dest = irop_temp(t);
            return t;
        }
        if (strcmp(call->name, "inb") == 0 && call->arg_count >= 1) {
            int tport = lower_expr(call->args[0], s);
            int t     = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_INB);
            ins->src1 = irop_temp(tport);
            ins->dest = irop_temp(t);
            return t;
        }

        /* ── memset(ptr, val, count) ────────────────────────────────────── */
        if (strcmp(call->name, "memset") == 0 && call->arg_count >= 3) {
            int tptr   = lower_expr(call->args[0], s);
            int tval   = lower_expr(call->args[1], s);
            int tcount = lower_expr(call->args[2], s);
            int t      = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_MEMSET);
            ins->dest      = irop_temp(t);
            ins->src1      = irop_temp(tptr);
            ins->src2      = irop_temp(tval);
            ins->extra_src = irop_temp(tcount);
            return t;
        }

        /* ── memcpy(dst, src, count) ────────────────────────────────────── */
        if (strcmp(call->name, "memcpy") == 0 && call->arg_count >= 3) {
            int tdst   = lower_expr(call->args[0], s);
            int tsrc   = lower_expr(call->args[1], s);
            int tcount = lower_expr(call->args[2], s);
            int t      = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_MEMCPY);
            ins->dest      = irop_temp(t);
            ins->src1      = irop_temp(tdst);
            ins->src2      = irop_temp(tsrc);
            ins->extra_src = irop_temp(tcount);
            return t;
        }

        /* ── __size_of__(TypeName) ──────────────────────────────────────── */
        if (strcmp(call->name, "__size_of__") == 0 && call->arg_count >= 1) {
            const char *tname = NULL;
            if (call->args[0]->type == NODE_IDENTIFIER)
                tname = ((IdentifierNode *)call->args[0])->name;
            long sz = 8; /* default: pointer/int size */
            if (tname) {
                if (strcmp(tname, "uint8")  == 0 || strcmp(tname, "int8")   == 0) sz = 1;
                else if (strcmp(tname, "uint16") == 0 || strcmp(tname, "int16") == 0) sz = 2;
                else if (strcmp(tname, "uint32") == 0 || strcmp(tname, "int32") == 0) sz = 4;
                else if (strcmp(tname, "uint64") == 0 || strcmp(tname, "int64") == 0) sz = 8;
                else if (strcmp(tname, "usize")  == 0 || strcmp(tname, "isize") == 0) sz = 8;
                else if (strcmp(tname, "int")    == 0) sz = 8;
                else if (strcmp(tname, "bool")   == 0) sz = 1;
                else if (strcmp(tname, "void")   == 0) sz = 0;
                /* for classes: look up in the lowerer state; default 8 */
            }
            int t = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_CONST_INT);
            ins->dest = irop_temp(t);
            ins->src1 = irop_const_int(sz);
            return t;
        }
        if (strcmp(call->name, "__addrof_fn__") == 0 && call->arg_count >= 1) {
            const char *fname = NULL;
            if (call->args[0]->type == NODE_IDENTIFIER)
                fname = ((IdentifierNode *)call->args[0])->name;
            int t = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_ADDROF_FN);
            ins->dest = irop_temp(t);
            ins->str_extra = fname ? strdup(fname) : strdup("unknown");
            return t;
        }
        /* ── panic("msg") ──────────────────────────────────────────────── */
        if (strcmp(call->name, "panic") == 0) {
            int t = alloc_temp(s);
            IROperand panic_src1 = irop_const_str(strdup("panic"));
            if (call->arg_count >= 1)
                panic_src1 = irop_temp(lower_expr(call->args[0], s));
            IRInstr *ins = ir_emit(s->mod, IR_PANIC);
            ins->dest = irop_temp(t);
            ins->src1 = panic_src1;
            return t;
        }

        /* ── print / println ───────────────────────────────────────────── */
        if (strcmp(call->name, "print") == 0 || strcmp(call->name, "println") == 0) {
            int t = alloc_temp(s);
            IROpcode irop = strcmp(call->name, "print") == 0 ? IR_PRINT : IR_PRINTLN;
            /* Evaluate the argument BEFORE emitting the print instruction so that
               any IR_LOAD_VAR / IR_CONST_* instructions appear first in the stream.
               Also avoids using a stale pointer if ir_emit() reallocates instrs[]. */
            int cat = PRINT_ARG_STR_LIT;
            IROperand src1 = irop_const_str(strdup(""));
            InterpSegment *extra_segs = NULL;
            int extra_seg_count = 0;
            if (call->arg_count >= 1) {
                ASTNode *arg = call->args[0];
                cat = classify_print_arg(arg);
                if (cat == PRINT_ARG_STR_LIT) {
                    LiteralNode *lit = (LiteralNode *)arg;
                    const char *v = lit->value; size_t len = strlen(v);
                    char *uq = malloc(len + 1);
                    if (len >= 2 && v[0] == '"' && v[len-1] == '"') {
                        memcpy(uq, v+1, len-2); uq[len-2] = '\0';
                    } else { strcpy(uq, v); }
                    src1 = irop_const_str(uq);
                } else if (cat == PRINT_ARG_INTERP) {
                    InterpStringNode *istr = (InterpStringNode *)arg;
                    extra_segs      = istr->segments;
                    extra_seg_count = istr->seg_count;
                    src1 = irop_none();
                } else {
                    int ta = lower_expr(arg, s);
                    src1 = irop_temp(ta);
                }
            }
            IRInstr *ins = ir_emit(s->mod, irop);
            ins->dest           = irop_temp(t);
            ins->extra_int      = cat;
            ins->src1           = src1;
            ins->extra_segs     = extra_segs;
            ins->extra_seg_count = extra_seg_count;
            return t;
        }

        /* ── General function call ─────────────────────────────────────── */
        {
            int t = alloc_temp(s);
            int nargs = call->arg_count;
            IROperand *arg_ops = NULL;
            if (nargs > 0) {
                arg_ops = malloc(nargs * sizeof(IROperand));
                for (int i = 0; i < nargs; i++)
                    arg_ops[i] = irop_temp(lower_expr(call->args[i], s));
            }
            /* If we're inside a module and the callee is a sibling function,
               mangle it to ModuleName__funcname so it resolves correctly. */
            const char *resolved_name = call->name;
            char mangled_sibling[256];
            if (s->current_module) {
                for (int _mi = 0; _mi < s->module_func_count; _mi++) {
                    if (s->module_func_names[_mi] &&
                        strcmp(s->module_func_names[_mi], call->name) == 0) {
                        snprintf(mangled_sibling, sizeof(mangled_sibling),
                                 "%s__%s", s->current_module, call->name);
                        resolved_name = mangled_sibling;
                        break;
                    }
                }
            }
            IRInstr *ins = ir_emit(s->mod, IR_CALL);
            ins->dest      = irop_temp(t);
            ins->str_extra = strdup(resolved_name);
            ins->args      = arg_ops;
            ins->arg_count = nargs;
            ins->extra_int = tc_func_return_is_bool(resolved_name);
            return t;
        }
    }

    case NODE_METHOD_CALL: {
        MethodCallNode *mc = (MethodCallNode *)node;
        int t = alloc_temp(s);

        /* Array intrinsics */
        int is_arr = (mc->object &&
                      mc->object->resolved_type.kind == TYPE_ARRAY);
        if (is_arr && strcmp(mc->method, "push") == 0 && mc->arg_count >= 1) {
            char *arr_name = (mc->object->type == NODE_IDENTIFIER)
                             ? strdup(((IdentifierNode *)mc->object)->name) : NULL;
            int tobj = lower_expr(mc->object, s);
            int tval = lower_expr(mc->args[0], s);
            IRInstr *ins = ir_emit(s->mod, IR_ARRAY_PUSH);
            ins->dest      = irop_temp(t);
            ins->src1      = irop_temp(tobj);
            ins->src2      = irop_temp(tval);
            ins->str_extra = arr_name;
            return t;
        }
        if (is_arr && strcmp(mc->method, "pop") == 0) {
            int tobj = lower_expr(mc->object, s);
            IRInstr *ins = ir_emit(s->mod, IR_ARRAY_POP);
            ins->dest = irop_temp(t);
            ins->src1 = irop_temp(tobj);
            return t;
        }

        /* Generic method call — evaluate self and args before emitting IR_CALL */
        {
            /* Check if the object is a known module name — if so, emit a direct call
               to the mangled name ModuleName__method without passing self. */
            if (mc->object && mc->object->type == NODE_IDENTIFIER) {
                const char *obj_name = ((IdentifierNode *)mc->object)->name;
                if (lower_is_module(obj_name)) {
                    int t2 = alloc_temp(s);
                    int nargs2 = mc->arg_count;
                    IROperand *arg_ops2 = NULL;
                    if (nargs2 > 0) {
                        arg_ops2 = malloc(nargs2 * sizeof(IROperand));
                        for (int i = 0; i < nargs2; i++)
                            arg_ops2[i] = irop_temp(lower_expr(mc->args[i], s));
                    }
                    size_t msz = strlen(obj_name) + 2 + strlen(mc->method) + 1;
                    char *mname2 = malloc(msz);
                    snprintf(mname2, msz, "%s__%s", obj_name, mc->method);
                    IRInstr *ins2 = ir_emit(s->mod, IR_CALL);
                    ins2->dest      = irop_temp(t2);
                    ins2->str_extra = mname2;
                    ins2->args      = arg_ops2;
                    ins2->arg_count = nargs2;
                    ins2->extra_int = tc_func_return_is_bool(mname2);
                    return t2;
                }
            }

            const char *cname = NULL;
            if (mc->object && mc->object->resolved_type.kind == TYPE_SIMPLE &&
                mc->object->resolved_type.name)
                cname = mc->object->resolved_type.name;
            char *mname;
            if (cname) {
                size_t sz = strlen(cname) + 1 + strlen(mc->method) + 1;
                mname = malloc(sz);
                snprintf(mname, sz, "%s_%s", cname, mc->method);
            } else {
                mname = strdup(mc->method);
            }
            int total = 1 + mc->arg_count;
            IROperand *arg_ops = malloc(total * sizeof(IROperand));
            arg_ops[0] = irop_temp(lower_expr(mc->object, s));
            for (int i = 0; i < total - 1; i++)
                arg_ops[i + 1] = irop_temp(lower_expr(mc->args[i], s));
            IRInstr *ins = ir_emit(s->mod, IR_CALL);
            ins->dest      = irop_temp(t);
            ins->str_extra = mname;
            ins->args      = arg_ops;
            ins->arg_count = total;
            ins->extra_int = tc_func_return_is_bool(mname);
            return t;
        }
    }

    case NODE_MEMBER_ACCESS: {
        MemberAccessNode *ma = (MemberAccessNode *)node;
        int t = alloc_temp(s);

        /* array.len / array.cap */
        if (ma->object && ma->object->resolved_type.kind == TYPE_ARRAY) {
            IROpcode irop = strcmp(ma->member, "cap") == 0 ? IR_ARRAY_CAP : IR_ARRAY_LEN;
            int tobj = lower_expr(ma->object, s);
            IRInstr *ins = ir_emit(s->mod, irop);
            ins->dest = irop_temp(t); ins->src1 = irop_temp(tobj);
            return t;
        }

        /* multi.tag / multi.value */
        if (ma->object && ma->object->resolved_type.kind == TYPE_MULTI) {
            IROpcode irop = strcmp(ma->member, "value") == 0 ? IR_ARRAY_CAP : IR_ARRAY_LEN;
            /* reuse ARRAY_LEN/CAP for tag(0)/value(8) layout — same offset pattern */
            int tobj = lower_expr(ma->object, s);
            IRInstr *ins = ir_emit(s->mod, irop);
            ins->dest = irop_temp(t); ins->src1 = irop_temp(tobj);
            return t;
        }

        /* Class field access — must come before enum check because a class
           instance variable is also a NODE_IDENTIFIER.

           Three cases:
             - heap object (created with `new`, has_ctor == 1): the local
               variable holds the malloc'd pointer directly — use IR_LOAD_VAR.
             - stack struct identifier (declared bare, no `new`): the local IS
               the struct data — use IR_ADDROF to get its address.
             - chained stack struct access (ev.key.keysym): walk the whole chain
               with lower_stack_struct_addrof to get the root address + cumulative
               byte offset, then adjust the base pointer before emitting
               IR_GET_FIELD.  This avoids treating intermediate field values as
               pointers, which caused the SIGSEGV. */
        if (ma->object && ma->object->resolved_type.kind == TYPE_SIMPLE &&
                    ma->object->resolved_type.name) {
                    int tobj;
                    if (ma->object->type == NODE_IDENTIFIER) {
                        const char *vname = ((IdentifierNode *)ma->object)->name;
                        int is_heap = 0;
                        for (int _ci = 0; _ci < s->mod->class_count; _ci++) {
                            ClassNode *_cn = s->mod->classes[_ci];
                            if (_cn && _cn->name &&
                                strcmp(_cn->name, ma->object->resolved_type.name) == 0) {
                                is_heap = _cn->has_ctor;
                                break;
                            }
                        }
                        tobj = alloc_temp(s);
                        if (is_heap) {
                            IRInstr *lv   = ir_emit(s->mod, IR_LOAD_VAR);
                            lv->dest      = irop_temp(tobj);
                            lv->str_extra = strdup(vname);
                        } else {
                            IRInstr *av   = ir_emit(s->mod, IR_ADDROF);
                            av->dest      = irop_temp(tobj);
                            av->str_extra = strdup(vname);
                        }
                    } else if (ma->object->type == NODE_MEMBER_ACCESS) {
                        /* Chained stack-struct access: compute root address +
                         * accumulated byte offset to avoid double-dereference. */
                        int cum_offset = 0;
                        const char *chain_type = NULL;
                        int tbase = lower_stack_struct_addrof(
                            ma->object, s, &cum_offset, &chain_type);
                        if (tbase >= 0 && cum_offset > 0) {
                            /* Adjust the base pointer by the accumulated offset
                             * so IR_GET_FIELD sees a pointer to the nested struct. */
                            int tadj = alloc_temp(s);
                            IRInstr *add   = ir_emit(s->mod, IR_ADD);
                            add->dest      = irop_temp(tadj);
                            add->src1      = irop_temp(tbase);
                            add->src2      = irop_const_int((long)cum_offset);
                            tobj = tadj;
                        } else if (tbase >= 0) {
                            /* Zero offset — base pointer is already correct */
                            tobj = tbase;
                        } else {
                            /* Fallback: not a pure stack-struct chain */
                            tobj = lower_expr(ma->object, s);
                        }
                    } else {
                        tobj = lower_expr(ma->object, s);
                    }
                    IRInstr *ins    = ir_emit(s->mod, IR_GET_FIELD);
                    ins->dest       = irop_temp(t);
                    ins->src1       = irop_temp(tobj);
                    ins->str_extra  = strdup(ma->object->resolved_type.name);
                    ins->str_extra2 = strdup(ma->member);
                    return t;
                }

        /* Module.CONST — resolve to the mangled static/const name */
        if (ma->object && ma->object->type == NODE_IDENTIFIER) {
            IdentifierNode *id = (IdentifierNode *)ma->object;
            if (lower_is_module(id->name)) {
                /* Emit IR_LOAD_VAR for ModuleName__member */
                char mangled[256];
                snprintf(mangled, sizeof(mangled), "%s__%s", id->name, ma->member);
                IRInstr *ins  = ir_emit(s->mod, IR_LOAD_VAR);
                ins->dest     = irop_temp(t);
                ins->str_extra = strdup(mangled);
                return t;
            }
            /* EnumName.Variant */
            IRInstr *ins    = ir_emit(s->mod, IR_ENUM_VAL);
            ins->dest       = irop_temp(t);
            ins->str_extra  = strdup(id->name);
            ins->str_extra2 = strdup(ma->member);
            return t;
        }

        /* Fallback: load as var */
        IRInstr *ins  = ir_emit(s->mod, IR_LOAD_VAR);
        ins->dest     = irop_temp(t);
        ins->str_extra = strdup(ma->member);
        return t;
    }

    case NODE_NEW: {
        NewNode *nn = (NewNode *)node;
        int t = alloc_temp(s);
        int nargs = nn->arg_count > 5 ? 5 : nn->arg_count;
        IROperand *arg_ops = NULL;
        if (nargs > 0) {
            arg_ops = malloc(nargs * sizeof(IROperand));
            for (int i = 0; i < nargs; i++)
                arg_ops[i] = irop_temp(lower_expr(nn->args[i], s));
        }

        if (s->in_unsafe) {
            /* Inside unsafe block — emit a plain malloc call, programmer manages memory */
            IRInstr *ins = ir_emit(s->mod, IR_NEW);
            ins->dest      = irop_temp(t);
            ins->str_extra = strdup(nn->class_name);
            ins->extra_int = 1; /* flag: malloc path (unsafe) */
            ins->args      = arg_ops;
            ins->arg_count = nargs;
        } else {
            /* Normal path — allocate from the function's implicit arena */
            int t_aptr = alloc_temp(s);
            IRInstr *addrof = ir_emit(s->mod, IR_ADDROF);
            addrof->dest      = irop_temp(t_aptr);
            addrof->str_extra = strdup("__arena__");

            IRInstr *ins   = ir_emit(s->mod, IR_ARENA_ALLOC);
            ins->dest      = irop_temp(t);
            ins->src1      = irop_temp(t_aptr); /* arena pointer */
            ins->str_extra = strdup(nn->class_name);
            ins->args      = arg_ops;
            ins->arg_count = nargs;
        }
        return t;
    }

    case NODE_ARRAY_LITERAL: {
        ArrayLiteralNode *al = (ArrayLiteralNode *)node;
        int t = alloc_temp(s);
        IROperand *elem_ops = NULL;
        if (al->elem_count > 0) {
            elem_ops = malloc(al->elem_count * sizeof(IROperand));
            for (int i = 0; i < al->elem_count; i++)
                elem_ops[i] = irop_temp(lower_expr(al->elements[i], s));
        }
        IRInstr *ins = ir_emit(s->mod, IR_ARRAY_INIT);
        ins->dest      = irop_temp(t);
        ins->args      = elem_ops;
        ins->arg_count = al->elem_count;
        return t;
    }

    case NODE_INDEX: {
        IndexNode *ix = (IndexNode *)node;
        int tobj;
        int is_flat = 0;
        const char *flat_elem_type = NULL;
        /* Detect static array access — use IR_ADDROF instead of IR_LOAD_VAR
         * so we get the address of the flat .data block, not its first value. */
        if (ix->object->type == NODE_IDENTIFIER) {
            const char *oname = ((IdentifierNode *)ix->object)->name;
            const LocalStaticAlias *lsa = lower_find_local_static(s, oname);
            if (lsa && lsa->array_size > 0) {
                /* Local-function static array: ADDROF the mangled label */
                is_flat = 1;
                tobj = alloc_temp(s);
                IRInstr *af   = ir_emit(s->mod, IR_ADDROF);
                af->dest      = irop_temp(tobj);
                af->str_extra = strdup(lsa->mangled_name);
                /* Find elem type */
                for (int _i = 0; _i < s->static_var_count; _i++) {
                    if (s->static_vars[_i].name &&
                        strcmp(s->static_vars[_i].name, lsa->mangled_name) == 0) {
                        flat_elem_type = s->static_vars[_i].type_name;
                        break;
                    }
                }
            } else {
                const char *top_type = lower_find_top_static_array_type(s, oname);
                if (top_type) {
                    /* Top-level / module static array: ADDROF the global label */
                    is_flat = 1;
                    flat_elem_type = top_type;
                    tobj = alloc_temp(s);
                    IRInstr *af   = ir_emit(s->mod, IR_ADDROF);
                    af->dest      = irop_temp(tobj);
                    af->str_extra = strdup(oname);
                } else {
                    tobj = lower_expr(ix->object, s);
                }
            }
        } else {
            tobj = lower_expr(ix->object, s);
        }
        int tidx = lower_expr(ix->index, s);
        int t    = alloc_temp(s);
        IRInstr *ins = ir_emit(s->mod, IR_ARRAY_LOAD);
        ins->dest = irop_temp(t);
        ins->src1 = irop_temp(tobj);
        ins->src2 = irop_temp(tidx);
        if (is_flat) {
            ins->extra_int = 1;
            ins->str_extra = flat_elem_type ? strdup(flat_elem_type) : NULL;
        }
        return t;
    }

    case NODE_INTERP_STRING: {
        InterpStringNode *istr = (InterpStringNode *)node;
        int t = alloc_temp(s);
        IRInstr *ins = ir_emit(s->mod, IR_INTERP_STR);
        ins->dest          = irop_temp(t);
        ins->extra_segs    = istr->segments;
        ins->extra_seg_count = istr->seg_count;
        return t;
    }

    case NODE_TUPLE: {
        TupleNode *tup = (TupleNode *)node;
        /* Lower each element; return the last one (tuples are simple here) */
        int t = -1;
        for (int i = 0; i < tup->elem_count; i++)
            t = lower_expr(tup->elements[i], s);
        if (t < 0) { t = alloc_temp(s); ir_emit(s->mod, IR_CONST_NIL)->dest = irop_temp(t); }
        return t;
    }

    case NODE_STRUCT_LITERAL: {
        StructLiteralNode *sl = (StructLiteralNode *)node;
        /* Allocate a hidden named stack slot for the struct */
        int uid = alloc_label(s);
        char hidden_name[64];
        snprintf(hidden_name, sizeof(hidden_name), "__struct_%d", uid);
        emit_alloca(s, hidden_name, sl->class_name, TYPE_SIMPLE);
        /* Get address of the stack slot */
        int taddr = alloc_temp(s);
        {
            IRInstr *af   = ir_emit(s->mod, IR_ADDROF);
            af->dest      = irop_temp(taddr);
            af->str_extra = strdup(hidden_name);
        }
        /* Initialize each named field */
        for (int i = 0; i < sl->field_count; i++) {
            int tval = lower_expr(sl->field_values[i], s);
            IRInstr *sf    = ir_emit(s->mod, IR_SET_FIELD);
            sf->src1       = irop_temp(taddr);
            sf->src2       = irop_temp(tval);
            sf->str_extra  = strdup(sl->class_name);
            sf->str_extra2 = strdup(sl->field_names[i]);
        }
        return taddr;  /* caller gets a pointer to the stack struct */
    }

    default: {
        int t = alloc_temp(s);
        IRInstr *ins = ir_emit(s->mod, IR_CONST_INT);
        ins->dest = irop_temp(t);
        ins->src1 = irop_const_int(0);
        return t;
    }
    } /* switch */
}


static void lower_stmts(ASTNode **stmts, int count, LowerState *s) {
    for (int i = 0; i < count; i++)
        lower_stmt(stmts[i], s);
}

static void lower_stmt(ASTNode *node, LowerState *s) {
    if (!node) return;

    switch (node->type) {

    case NODE_UNSAFE: {
        UnsafeBlockNode *ub = (UnsafeBlockNode *)node;
        int prev = s->in_unsafe;
        s->in_unsafe = 1;
        lower_stmts(ub->body, ub->body_count, s);
        s->in_unsafe = prev;
        break;
    }

    case NODE_VAR_DECL: {
        VarDeclNode *vd = (VarDeclNode *)node;
        /* Choose a type_name string that codegen can use for dispatch */
        const char *tname =
            vd->var_type.kind == TYPE_SIMPLE ? vd->var_type.name :
            vd->var_type.kind == TYPE_ARRAY  ? "array" :
            vd->var_type.kind == TYPE_MULTI  ? "multi" : NULL;
        emit_alloca(s, vd->var_name, tname, vd->var_type.kind);

        if (vd->var_type.kind == TYPE_MULTI) {
            /* Wrap initializer (or zero) in a heap-allocated tagged union */
            int tag  = 0;
            int tval = -1;
            if (vd->initializer) {
                tval = lower_expr(vd->initializer, s);
                Type init_type = vd->initializer->resolved_type;
                for (int ti2 = 0; ti2 < vd->var_type.elem_type_count; ti2++) {
                    Type *et = &vd->var_type.elem_types[ti2];
                    if (et->name && init_type.name && strcmp(et->name, init_type.name) == 0) {
                        tag = ti2; break;
                    }
                }
            }
            int talloc = alloc_temp(s);
            IRInstr *ma = ir_emit(s->mod, IR_MULTI_ALLOC);
            ma->dest = irop_temp(talloc);
            ma->src1 = irop_const_int(tag);
            ma->src2 = (tval >= 0) ? irop_temp(tval) : irop_const_int(0);
            IRInstr *sv = ir_emit(s->mod, IR_STORE_VAR);
            sv->str_extra = strdup(vd->var_name);
            sv->src1      = irop_temp(talloc);
        } else if (vd->var_type.kind == TYPE_ARRAY && !vd->initializer) {
            /* No initializer for array: allocate an empty 8-slot array */
            int alloc_t = alloc_temp(s);
            IRInstr *alloc = ir_emit(s->mod, IR_ARRAY_ALLOC);
            alloc->dest = irop_temp(alloc_t);
            const char *et = (vd->var_type.elem_type_count > 0 && vd->var_type.elem_types[0].name)
                             ? vd->var_type.elem_types[0].name : "?";
            alloc->str_extra = strdup(et);
            IRInstr *sv = ir_emit(s->mod, IR_STORE_VAR);
            sv->str_extra = strdup(vd->var_name);
            sv->src1      = irop_temp(alloc_t);
        } else if (vd->initializer) {
            int ti = lower_expr(vd->initializer, s);
            IRInstr *sv   = ir_emit(s->mod, IR_STORE_VAR);
            sv->str_extra = strdup(vd->var_name);
            sv->src1      = irop_temp(ti);
        } else {
            IRInstr *sv   = ir_emit(s->mod, IR_STORE_VAR);
            sv->str_extra = strdup(vd->var_name);
            sv->src1      = irop_const_int(0);
        }
        break;
    }

    case NODE_ASSIGN: {
        AssignNode *as = (AssignNode *)node;
        int tv = lower_expr(as->value, s);
        /* Rewrite to mangled label if this is a function-local static */
        const LocalStaticAlias *_as_lsa = lower_find_local_static(s, as->var_name);
        const char *_as_name = _as_lsa ? _as_lsa->mangled_name : as->var_name;
        IRInstr *sv   = ir_emit(s->mod, IR_STORE_VAR);
        sv->str_extra = strdup(_as_name);
        sv->src1      = irop_temp(tv);
        break;
    }

    case NODE_COMPOUND_ASSIGN: {
        CompoundAssignNode *ca = (CompoundAssignNode *)node;
        /* Rewrite to mangled label if this is a function-local static */
        const LocalStaticAlias *_ca_lsa = lower_find_local_static(s, ca->var_name);
        const char *_ca_name = _ca_lsa ? _ca_lsa->mangled_name : ca->var_name;
        /* Load current value */
        int tcur = alloc_temp(s);
        { IRInstr *lv = ir_emit(s->mod, IR_LOAD_VAR); lv->dest = irop_temp(tcur); lv->str_extra = strdup(_ca_name); }
        int trhs = lower_expr(ca->value, s);
        int tres = alloc_temp(s);
        IROpcode irop =
            strcmp(ca->op, "+=") == 0 ? IR_ADD :
            strcmp(ca->op, "-=") == 0 ? IR_SUB :
            strcmp(ca->op, "*=") == 0 ? IR_MUL :
            strcmp(ca->op, "/=") == 0 ? IR_DIV : IR_ADD;
        IRInstr *op_ins = ir_emit(s->mod, irop);
        op_ins->dest = irop_temp(tres);
        op_ins->src1 = irop_temp(tcur);
        op_ins->src2 = irop_temp(trhs);
        IRInstr *sv   = ir_emit(s->mod, IR_STORE_VAR);
        sv->str_extra = strdup(_ca_name);
        sv->src1      = irop_temp(tres);
        break;
    }

    case NODE_MEMBER_ASSIGN: {
        MemberAssignNode *ma = (MemberAssignNode *)node;
        /* Evaluate value and object BEFORE emitting IR_SET_FIELD */
        int tval = lower_expr(ma->value, s);
        int tobj_or_self;
        char *obj_class;
        if (ma->object == NULL) {
            tobj_or_self = alloc_temp(s);
            IRInstr *lv = ir_emit(s->mod, IR_LOAD_VAR);
            lv->dest = irop_temp(tobj_or_self);
            lv->str_extra = strdup("self");
            obj_class = s->class_name ? strdup(s->class_name) : strdup("?");
        } else {
                    if (ma->object && ma->object->type == NODE_IDENTIFIER) {
                        tobj_or_self = alloc_temp(s);
                        IRInstr *av = ir_emit(s->mod, IR_ADDROF);
                        av->dest = irop_temp(tobj_or_self);
                        av->str_extra = strdup(((IdentifierNode *)ma->object)->name);
                    } else {
                        tobj_or_self = lower_expr(ma->object, s);
                    }
                    obj_class =
                        (ma->object->resolved_type.kind == TYPE_SIMPLE && ma->object->resolved_type.name)
                        ? strdup(ma->object->resolved_type.name)
                        : (s->class_name ? strdup(s->class_name) : strdup("?"));
                }
        IRInstr *ins = ir_emit(s->mod, IR_SET_FIELD);
        ins->src1       = irop_temp(tobj_or_self);
        ins->src2       = irop_temp(tval);
        ins->str_extra  = obj_class;
        ins->str_extra2 = strdup(ma->member);
        break;
    }

    case NODE_INDEX_ASSIGN: {
        IndexAssignNode *ia = (IndexAssignNode *)node;
        int tobj;
        int is_flat_store = 0;
        const char *flat_store_elem_type = NULL;
        /* Same logic as NODE_INDEX: use IR_ADDROF for static array objects. */
        if (ia->object->type == NODE_IDENTIFIER) {
            const char *oname = ((IdentifierNode *)ia->object)->name;
            const LocalStaticAlias *lsa = lower_find_local_static(s, oname);
            if (lsa && lsa->array_size > 0) {
                is_flat_store = 1;
                tobj = alloc_temp(s);
                IRInstr *af   = ir_emit(s->mod, IR_ADDROF);
                af->dest      = irop_temp(tobj);
                af->str_extra = strdup(lsa->mangled_name);
                for (int _i = 0; _i < s->static_var_count; _i++) {
                    if (s->static_vars[_i].name &&
                        strcmp(s->static_vars[_i].name, lsa->mangled_name) == 0) {
                        flat_store_elem_type = s->static_vars[_i].type_name;
                        break;
                    }
                }
            } else {
                const char *top_type = lower_find_top_static_array_type(s, oname);
                if (top_type) {
                    is_flat_store = 1;
                    flat_store_elem_type = top_type;
                    tobj = alloc_temp(s);
                    IRInstr *af   = ir_emit(s->mod, IR_ADDROF);
                    af->dest      = irop_temp(tobj);
                    af->str_extra = strdup(oname);
                } else {
                    tobj = lower_expr(ia->object, s);
                }
            }
        } else {
            tobj = lower_expr(ia->object, s);
        }
        int tidx = lower_expr(ia->index, s);
        int tval = lower_expr(ia->value, s);
        IRInstr *ins   = ir_emit(s->mod, IR_ARRAY_STORE);
        ins->src1      = irop_temp(tobj);
        ins->src2      = irop_temp(tidx);
        ins->extra_src = irop_temp(tval);
        if (is_flat_store) {
            ins->extra_int = 1;
            ins->str_extra = flat_store_elem_type ? strdup(flat_store_elem_type) : NULL;
        }
        break;
    }

    case NODE_RETURN: {
        ReturnNode *ret = (ReturnNode *)node;
        IROperand ret_src = {.kind = IROP_NONE};
        if (ret->value)
            ret_src = irop_temp(lower_expr(ret->value, s));
        IRInstr *ins = ir_emit(s->mod, IR_RETURN);
        ins->src1 = ret_src;
        break;
    }

    case NODE_IF: {
        IfNode *nd = (IfNode *)node;
        int lbl_else = alloc_label(s);
        int lbl_end  = alloc_label(s);
        int tc = lower_expr(nd->condition, s);
        if (nd->else_body && nd->else_count > 0) {
            { IRInstr *j = ir_emit(s->mod, IR_JUMP_UNLESS); j->src1 = irop_temp(tc); j->src2 = irop_label(lbl_else); }
            lower_stmts(nd->then_body, nd->then_count, s);
            { IRInstr *j = ir_emit(s->mod, IR_JUMP); j->src1 = irop_label(lbl_end); }
            { IRInstr *lb = ir_emit(s->mod, IR_LABEL); lb->dest = irop_label(lbl_else); }
            lower_stmts(nd->else_body, nd->else_count, s);
            { IRInstr *lb = ir_emit(s->mod, IR_LABEL); lb->dest = irop_label(lbl_end); }
        } else {
            { IRInstr *j = ir_emit(s->mod, IR_JUMP_UNLESS); j->src1 = irop_temp(tc); j->src2 = irop_label(lbl_end); }
            lower_stmts(nd->then_body, nd->then_count, s);
            { IRInstr *lb = ir_emit(s->mod, IR_LABEL); lb->dest = irop_label(lbl_end); }
        }
        break;
    }

    case NODE_WHILE: {
        WhileNode *nd = (WhileNode *)node;
        int lbl_loop = alloc_label(s);
        int lbl_end  = alloc_label(s);
        loop_push(s, lbl_end, lbl_loop);
        { IRInstr *lb = ir_emit(s->mod, IR_LABEL); lb->dest = irop_label(lbl_loop); }
        int tc = lower_expr(nd->condition, s);
        { IRInstr *j = ir_emit(s->mod, IR_JUMP_UNLESS); j->src1 = irop_temp(tc); j->src2 = irop_label(lbl_end); }
        lower_stmts(nd->body, nd->body_count, s);
        { IRInstr *j = ir_emit(s->mod, IR_JUMP); j->src1 = irop_label(lbl_loop); }
        { IRInstr *lb = ir_emit(s->mod, IR_LABEL); lb->dest = irop_label(lbl_end); }
        loop_pop(s);
        break;
    }

    case NODE_FOR: {
        ForNode *nd = (ForNode *)node;
        int lbl_loop = alloc_label(s);
        int lbl_post = alloc_label(s);
        int lbl_end  = alloc_label(s);
        if (nd->init) lower_stmt(nd->init, s);
        loop_push(s, lbl_end, lbl_post);
        { IRInstr *lb = ir_emit(s->mod, IR_LABEL); lb->dest = irop_label(lbl_loop); }
        if (nd->condition) {
            int tc = lower_expr(nd->condition, s);
            IRInstr *j = ir_emit(s->mod, IR_JUMP_UNLESS); j->src1 = irop_temp(tc); j->src2 = irop_label(lbl_end);
        }
        lower_stmts(nd->body, nd->body_count, s);
        { IRInstr *lb = ir_emit(s->mod, IR_LABEL); lb->dest = irop_label(lbl_post); }
        if (nd->post) lower_stmt(nd->post, s);
        { IRInstr *j = ir_emit(s->mod, IR_JUMP); j->src1 = irop_label(lbl_loop); }
        { IRInstr *lb = ir_emit(s->mod, IR_LABEL); lb->dest = irop_label(lbl_end); }
        loop_pop(s);
        break;
    }

    case NODE_FOR_IN: {
        ForInNode *fi = (ForInNode *)node;
        int lbl_loop = alloc_label(s);
        int lbl_end  = alloc_label(s);
        int uid      = alloc_label(s); /* unique id for hidden names */

        char arr_nm[64], len_nm[64], idx_nm[64];
        snprintf(arr_nm, sizeof(arr_nm), "__for_arr_%d", uid);
        snprintf(len_nm, sizeof(len_nm), "__for_len_%d", uid);
        snprintf(idx_nm, sizeof(idx_nm), "__for_idx_%d", uid);

        /* Allocate hidden locals */
        emit_alloca(s, arr_nm, "ptr",  TYPE_SIMPLE);
        emit_alloca(s, len_nm, "int",  TYPE_SIMPLE);
        emit_alloca(s, idx_nm, "int",  TYPE_SIMPLE);
        /* Determine element type for loop variable */
        const char *elem_type = NULL;
        if (fi->collection->resolved_type.kind == TYPE_ARRAY &&
            fi->collection->resolved_type.elem_type_count > 0)
            elem_type = fi->collection->resolved_type.elem_types[0].name;
        emit_alloca(s, fi->var_name, elem_type, TYPE_SIMPLE);

        /* Evaluate collection → store in arr_nm */
        int tarr = lower_expr(fi->collection, s);
        { IRInstr *sv = ir_emit(s->mod, IR_STORE_VAR); sv->str_extra = strdup(arr_nm); sv->src1 = irop_temp(tarr); }

        /* len = arr.len */
        int tlen = alloc_temp(s);
        { IRInstr *al = ir_emit(s->mod, IR_ARRAY_LEN); al->dest = irop_temp(tlen); al->src1 = irop_temp(tarr); }
        { IRInstr *sv = ir_emit(s->mod, IR_STORE_VAR); sv->str_extra = strdup(len_nm); sv->src1 = irop_temp(tlen); }

        /* idx = 0 */
        { IRInstr *sv = ir_emit(s->mod, IR_STORE_VAR); sv->str_extra = strdup(idx_nm); sv->src1 = irop_const_int(0); }

        loop_push(s, lbl_end, lbl_loop);
        { IRInstr *lb = ir_emit(s->mod, IR_LABEL); lb->dest = irop_label(lbl_loop); }

        /* if idx >= len: break */
        int tidx = alloc_temp(s);
        { IRInstr *lv = ir_emit(s->mod, IR_LOAD_VAR); lv->dest = irop_temp(tidx); lv->str_extra = strdup(idx_nm); }
        int tlen2 = alloc_temp(s);
        { IRInstr *lv = ir_emit(s->mod, IR_LOAD_VAR); lv->dest = irop_temp(tlen2); lv->str_extra = strdup(len_nm); }
        int tcmp = alloc_temp(s);
        { IRInstr *ge = ir_emit(s->mod, IR_GE); ge->dest = irop_temp(tcmp); ge->src1 = irop_temp(tidx); ge->src2 = irop_temp(tlen2); }
        { IRInstr *j = ir_emit(s->mod, IR_JUMP_IF); j->src1 = irop_temp(tcmp); j->src2 = irop_label(lbl_end); }

        /* elem = arr[idx] */
        int tarr2 = alloc_temp(s);
        { IRInstr *lv = ir_emit(s->mod, IR_LOAD_VAR); lv->dest = irop_temp(tarr2); lv->str_extra = strdup(arr_nm); }
        int tidx2 = alloc_temp(s);
        { IRInstr *lv = ir_emit(s->mod, IR_LOAD_VAR); lv->dest = irop_temp(tidx2); lv->str_extra = strdup(idx_nm); }
        int telem = alloc_temp(s);
        { IRInstr *al = ir_emit(s->mod, IR_ARRAY_LOAD); al->dest = irop_temp(telem); al->src1 = irop_temp(tarr2); al->src2 = irop_temp(tidx2); }
        { IRInstr *sv = ir_emit(s->mod, IR_STORE_VAR); sv->str_extra = strdup(fi->var_name); sv->src1 = irop_temp(telem); }

        lower_stmts(fi->body, fi->body_count, s);

        /* idx++ */
        int tidx3 = alloc_temp(s);
        { IRInstr *lv = ir_emit(s->mod, IR_LOAD_VAR); lv->dest = irop_temp(tidx3); lv->str_extra = strdup(idx_nm); }
        int tinc = alloc_temp(s);
        { IRInstr *add = ir_emit(s->mod, IR_ADD); add->dest = irop_temp(tinc); add->src1 = irop_temp(tidx3); add->src2 = irop_const_int(1); }
        { IRInstr *sv = ir_emit(s->mod, IR_STORE_VAR); sv->str_extra = strdup(idx_nm); sv->src1 = irop_temp(tinc); }
        { IRInstr *j = ir_emit(s->mod, IR_JUMP); j->src1 = irop_label(lbl_loop); }
        { IRInstr *lb = ir_emit(s->mod, IR_LABEL); lb->dest = irop_label(lbl_end); }
        loop_pop(s);
        break;
    }

    case NODE_SWITCH: {
        SwitchNode *sw = (SwitchNode *)node;
        int uid = alloc_label(s);
        char subj_nm[64]; snprintf(subj_nm, sizeof(subj_nm), "__switch_%d", uid);
        emit_alloca(s, subj_nm, "int", TYPE_SIMPLE);
        int tsubj = lower_expr(sw->subject, s);
        { IRInstr *sv = ir_emit(s->mod, IR_STORE_VAR); sv->str_extra = strdup(subj_nm); sv->src1 = irop_temp(tsubj); }

        int lbl_end = alloc_label(s);
        int lbl_default = lbl_end;
        int *arm_lbls = malloc(sw->case_count * sizeof(int));
        for (int i = 0; i < sw->case_count; i++) arm_lbls[i] = alloc_label(s);

        /* comparison chain */
        for (int i = 0; i < sw->case_count; i++) {
            SwitchCaseNode *arm = sw->cases[i];
            if (!arm || arm->is_default) { lbl_default = arm_lbls[i]; continue; }
            int tsubj2 = alloc_temp(s);
            { IRInstr *lv = ir_emit(s->mod, IR_LOAD_VAR); lv->dest = irop_temp(tsubj2); lv->str_extra = strdup(subj_nm); }
            int tval = lower_expr(arm->value, s);
            int tcmp = alloc_temp(s);
            { IRInstr *eq = ir_emit(s->mod, IR_EQ); eq->dest = irop_temp(tcmp); eq->src1 = irop_temp(tsubj2); eq->src2 = irop_temp(tval); }
            { IRInstr *ji = ir_emit(s->mod, IR_JUMP_IF); ji->src1 = irop_temp(tcmp); ji->src2 = irop_label(arm_lbls[i]); }
        }
        { IRInstr *j = ir_emit(s->mod, IR_JUMP); j->src1 = irop_label(lbl_default); }

        /* arm bodies */
        for (int i = 0; i < sw->case_count; i++) {
            SwitchCaseNode *arm = sw->cases[i];
            if (!arm) continue;
            { IRInstr *lb = ir_emit(s->mod, IR_LABEL); lb->dest = irop_label(arm_lbls[i]); }
            lower_stmts(arm->body, arm->body_count, s);
            { IRInstr *j = ir_emit(s->mod, IR_JUMP); j->src1 = irop_label(lbl_end); }
        }
        { IRInstr *lb = ir_emit(s->mod, IR_LABEL); lb->dest = irop_label(lbl_end); }
        free(arm_lbls);
        break;
    }

    case NODE_BREAK: {
        int lbl = loop_break(s);
        if (lbl >= 0) { IRInstr *j = ir_emit(s->mod, IR_JUMP); j->src1 = irop_label(lbl); }
        break;
    }
    case NODE_CONTINUE: {
        int lbl = loop_cont(s);
        if (lbl >= 0) { IRInstr *j = ir_emit(s->mod, IR_JUMP); j->src1 = irop_label(lbl); }
        break;
    }

    case NODE_ASM_BLOCK: {
        AsmBlockNode *ab = (AsmBlockNode *)node;
        IRInstr *ins   = ir_emit(s->mod, IR_ASM_BLOCK);
        ins->str_extra = strdup(ab->body);
        break;
    }

    case NODE_DEFER:
        ir_emit(s->mod, IR_NOP);
        break;

    case NODE_STATIC_VAR: {

        StaticVarNode *sv = (StaticVarNode *)node;

        /* Guard: cap the alias table */
        if (s->local_static_alias_count >= MAX_LOCAL_STATIC_ALIASES) break;

        int uid = alloc_label(s);
        char mangled[256];
        snprintf(mangled, sizeof(mangled), "__static_%d__%s", uid, sv->var_name);
        char *mangled_name = strdup(mangled);

        const char *type_str =
            (sv->var_type.kind == TYPE_SIMPLE && sv->var_type.name)
            ? sv->var_type.name : "int";

        /* Emit the IR_STATIC_VAR so codegen pre-scan registers the .data label */
        IRInstr *ins    = ir_emit(s->mod, IR_STATIC_VAR);
        ins->str_extra  = mangled_name;
        ins->str_extra2 = strdup(type_str);
        if (sv->initializer && sv->initializer->type == NODE_LITERAL) {
            LiteralNode *lit = (LiteralNode *)sv->initializer;
            if (lit->lit_type == LIT_INT)
                ins->src1 = irop_const_int(atol(lit->value));
            else if (lit->lit_type == LIT_FLOAT)
                ins->src1 = irop_const_float(atof(lit->value));
            else if (lit->lit_type == LIT_STRING)
                ins->src1 = irop_const_str(strdup(lit->value));
            else
                ins->src1 = irop_const_int(0);
        } else if (sv->initializer && sv->initializer->type == NODE_UNARY_OP) {
            /* Handle unary minus applied to a literal: -520.0, -1, etc. */
            UnaryOpNode *un = (UnaryOpNode *)sv->initializer;
            if (strcmp(un->op, "-") == 0 && un->operand &&
                un->operand->type == NODE_LITERAL) {
                LiteralNode *lit = (LiteralNode *)un->operand;
                if (lit->lit_type == LIT_FLOAT)
                    ins->src1 = irop_const_float(-atof(lit->value));
                else if (lit->lit_type == LIT_INT)
                    ins->src1 = irop_const_int(-atol(lit->value));
                else
                    ins->src1 = irop_const_int(0);
            } else {
                ins->src1 = irop_const_int(0);
            }
        } else if (sv->initializer && sv->initializer->type == NODE_IDENTIFIER) {
            /* Resolve identifier — scan already-emitted IR_STATIC_VAR instructions */
            const char *ref_name = ((IdentifierNode *)sv->initializer)->name;
            long resolved = 0;
            for (int j = 0; j < s->mod->instr_count; j++) {
                if (s->mod->instrs[j].op == IR_STATIC_VAR &&
                    s->mod->instrs[j].str_extra &&
                    strcmp(s->mod->instrs[j].str_extra, ref_name) == 0 &&
                    s->mod->instrs[j].src1.kind == IROP_CONST_INT) {
                    resolved = s->mod->instrs[j].src1.int_val;
                    break;
                }
            }
            ins->src1 = irop_const_int(resolved);
        } else {
            ins->src1 = irop_const_int(0);
        }
        ins->extra_int = (sv->is_const ? 1 : 0) | (sv->array_size << 1);

        /* Register in LowerState so lower_static_aggregate_type() works */
        lower_register_static_var(s, mangled_name, type_str);

        /*
         * Register the orig → mangled alias so that every LOAD_VAR /
         * STORE_VAR / ADDROF that references sv->var_name by its original
         * name is transparently rewritten to the mangled .data label.
         * This avoids the need for a separate rename pass over the IR.
         */
        LocalStaticAlias *alias =
            &s->local_static_aliases[s->local_static_alias_count++];
        alias->orig_name    = sv->var_name;   /* points into AST, stable */
        alias->mangled_name = mangled_name;   /* heap-allocated above     */
        alias->array_size   = sv->array_size;
        break;
    }

    default:
        lower_expr(node, s); /* result discarded */
        break;
    }
}


static void lower_func_body(
    const char  *name,
    ASTNode    **params,    int param_count,
    ASTNode    **body,      int body_count,
    int          is_main,
    int          is_naked,
    int          has_self,   /* 1 if method/ctor (rdi = self) */
    const char  *class_name,
    LowerState  *s)
{
    s->next_temp   = 0;
    s->class_name  = class_name;
    s->local_static_alias_count = 0;  /* fresh alias table per function */
    s->in_unsafe   = 0;
    s->has_arena   = !is_naked;       /* naked functions skip arena entirely */

    /* IR_FUNC_BEGIN */
    IRInstr *begin = ir_emit(s->mod, IR_FUNC_BEGIN);
    begin->str_extra = strdup(name);
    begin->extra_int = is_main | (is_naked << 1);

    /* Build param list (self first if method/ctor) */
    int nparams = param_count > (has_self ? 5 : 6) ? (has_self ? 5 : 6) : param_count;
    int total_params = has_self ? nparams + 1 : nparams;
    if (total_params > 0) {
        begin->params     = calloc(total_params, sizeof(IRParam));
        begin->param_count = total_params;
        int pi = 0;
        if (has_self) {
            begin->params[pi].name      = strdup("self");
            begin->params[pi].type_name = class_name ? strdup(class_name) : NULL;
            begin->params[pi].type_kind = TYPE_SIMPLE;
            pi++;
        }
        for (int i = 0; i < nparams; i++, pi++) {
            ASTNode *p = params[i];
            if (p->type == NODE_VAR_DECL) {
                VarDeclNode *vd = (VarDeclNode *)p;
                begin->params[pi].name      = vd->var_name ? strdup(vd->var_name) : NULL;
                begin->params[pi].type_name = (vd->var_type.kind == TYPE_SIMPLE && vd->var_type.name)
                                              ? strdup(vd->var_type.name) : NULL;
                begin->params[pi].type_kind = vd->var_type.kind;
            } else if (p->type == NODE_IDENTIFIER) {
                begin->params[pi].name = strdup(((IdentifierNode *)p)->name);
            }
        }
    }

    /* Emit ALLOCA for each parameter (so the codegen can assign stack slots) */
    for (int i = 0; i < total_params; i++) {
        if (!begin->params[i].name) continue;
        emit_alloca(s, begin->params[i].name,
                    begin->params[i].type_name,
                    begin->params[i].type_kind);
    }

    /* Inject hidden arena local + arena_init call for non-naked functions */
    if (s->has_arena) {
        /* ALLOCA __arena__ as an opaque 16-byte struct on the stack */
        emit_alloca(s, "__arena__", "Arena", TYPE_SIMPLE);

        /* arena_init(&__arena__) */
        int t_aptr = alloc_temp(s);
        IRInstr *addrof = ir_emit(s->mod, IR_ADDROF);
        addrof->dest      = irop_temp(t_aptr);
        addrof->str_extra = strdup("__arena__");

        IRInstr *init_call = ir_emit(s->mod, IR_CALL);
        init_call->str_extra = strdup("arena_init");
        init_call->dest      = irop_temp(alloc_temp(s));
        init_call->args      = malloc(sizeof(IROperand));
        init_call->args[0]   = irop_temp(t_aptr);
        init_call->arg_count = 1;
    }

    /* Lower body statements */
    lower_stmts(body, body_count, s);

    /* Emit arena_free(&__arena__) before function end */
    if (s->has_arena) {
        int t_aptr2 = alloc_temp(s);
        IRInstr *addrof2 = ir_emit(s->mod, IR_ADDROF);
        addrof2->dest      = irop_temp(t_aptr2);
        addrof2->str_extra = strdup("__arena__");

        IRInstr *free_call = ir_emit(s->mod, IR_CALL);
        free_call->str_extra = strdup("arena_free");
        free_call->dest      = irop_temp(alloc_temp(s));
        free_call->args      = malloc(sizeof(IROperand));
        free_call->args[0]   = irop_temp(t_aptr2);
        free_call->arg_count = 1;
    }

    ir_emit(s->mod, IR_FUNC_END);
    s->class_name = NULL;
}


IRModule *lower_program(ProgramNode *prog) {
    IRModule  *mod = ir_module_new();
    LowerState s   = { .mod = mod, .next_temp = 0, .next_label = 0, .loop_top = 0 };

    /* Collect class / enum metadata */
    for (int i = 0; i < prog->decl_count; i++) {
        ASTNode *decl = prog->declarations[i];
        if (!decl) continue;
        if (decl->type == NODE_CLASS) {
            mod->classes = realloc(mod->classes, (mod->class_count + 1) * sizeof(ClassNode *));
            mod->classes[mod->class_count++] = (ClassNode *)decl;
        } else if (decl->type == NODE_ENUM) {
            mod->enums = realloc(mod->enums, (mod->enum_count + 1) * sizeof(EnumNode *));
            mod->enums[mod->enum_count++] = (EnumNode *)decl;
        } else if (decl->type == NODE_MODULE) {
            ModuleNode *mn = (ModuleNode *)decl;
            if (module_count < 64)
                module_names[module_count++] = mn->name;
        }
    }

    /* Collect std includes */
    for (int i = 0; i < prog->include_count; i++) {
        mod->includes = realloc(mod->includes, (mod->include_count + 1) * sizeof(char *));
        mod->includes[mod->include_count++] = prog->includes[i];
    }

    /* Pre-scan: build a name→int_val table for all literal-initialised const
     * statics across the whole program (including .hyi-origin SDL constants
     * that appear later in the declaration list).  This lets us resolve
     * cross-references like `const int EVT_QUIT = SDL_QUIT` even when the
     * referenced constant hasn't been emitted to IR yet. */
    #define MAX_CONST_PRELOAD 4096
    struct { const char *name; long val; } const_preload[MAX_CONST_PRELOAD];
    int const_preload_count = 0;
    for (int i = 0; i < prog->decl_count && const_preload_count < MAX_CONST_PRELOAD; i++) {
        ASTNode *decl = prog->declarations[i];
        if (!decl || decl->type != NODE_STATIC_VAR) continue;
        StaticVarNode *sv_pre = (StaticVarNode *)decl;
        if (!sv_pre->is_const || !sv_pre->initializer) continue;
        if (sv_pre->initializer->type == NODE_LITERAL) {
            LiteralNode *lit = (LiteralNode *)sv_pre->initializer;
            if (lit->lit_type == LIT_INT) {
                const_preload[const_preload_count].name = sv_pre->var_name;
                const_preload[const_preload_count].val  = atol(lit->value);
                const_preload_count++;
            }
        }
    }
    /* Helper: look up a name in the preload table */
    #define RESOLVE_CONST_NAME(ref_name, out_val) do { \
        (out_val) = 0; \
        for (int _pi = 0; _pi < const_preload_count; _pi++) { \
            if (const_preload[_pi].name && strcmp(const_preload[_pi].name, (ref_name)) == 0) { \
                (out_val) = const_preload[_pi].val; break; \
            } \
        } \
    } while(0)

    /* Lower top-level declarations */
    for (int i = 0; i < prog->decl_count; i++) {
        ASTNode *decl = prog->declarations[i];
        if (!decl) continue;

        if (decl->type == NODE_STATIC_VAR) {
            StaticVarNode *sv = (StaticVarNode *)decl;
            IRInstr *ins    = ir_emit(mod, IR_STATIC_VAR);
            ins->str_extra  = strdup(sv->var_name);
            ins->str_extra2 = (sv->var_type.kind == TYPE_SIMPLE && sv->var_type.name)
                              ? strdup(sv->var_type.name) : strdup("int");
            if (sv->initializer && sv->initializer->type == NODE_LITERAL) {
                LiteralNode *lit = (LiteralNode *)sv->initializer;
                if (lit->lit_type == LIT_INT)
                    ins->src1 = irop_const_int(atol(lit->value));
                else if (lit->lit_type == LIT_FLOAT)
                    ins->src1 = irop_const_float(atof(lit->value));
                else if (lit->lit_type == LIT_STRING)
                    ins->src1 = irop_const_str(strdup(lit->value));
                else
                    ins->src1 = irop_const_int(0);
            } else if (sv->initializer && sv->initializer->type == NODE_UNARY_OP) {
                UnaryOpNode *un = (UnaryOpNode *)sv->initializer;
                if (strcmp(un->op, "-") == 0 && un->operand &&
                    un->operand->type == NODE_LITERAL) {
                    LiteralNode *lit = (LiteralNode *)un->operand;
                    if (lit->lit_type == LIT_FLOAT)
                        ins->src1 = irop_const_float(-atof(lit->value));
                    else if (lit->lit_type == LIT_INT)
                        ins->src1 = irop_const_int(-atol(lit->value));
                    else
                        ins->src1 = irop_const_int(0);
                } else {
                    ins->src1 = irop_const_int(0);
                }
            } else if (sv->initializer && sv->initializer->type == NODE_IDENTIFIER) {
                /* Resolve identifier via the pre-scan table */
                const char *ref_name = ((IdentifierNode *)sv->initializer)->name;
                long resolved = 0;
                RESOLVE_CONST_NAME(ref_name, resolved);
                ins->src1 = irop_const_int(resolved);
            } else {
                ins->src1 = irop_const_int(0);
            }
            ins->extra_int = (sv->is_const ? 1 : 0) | (sv->array_size << 1);
            /* Register flat-layout arrays so NODE_INDEX can tag them */
            if (sv->array_size > 0 && s.top_static_array_count < MAX_STATIC_LOWER) {
                const char *type_str =
                    (sv->var_type.kind == TYPE_SIMPLE && sv->var_type.name)
                    ? sv->var_type.name : "int";
                s.top_static_array_names[s.top_static_array_count] = sv->var_name;
                s.top_static_array_types[s.top_static_array_count] = type_str;
                s.top_static_array_count++;
            }

        } else if (decl->type == NODE_FUNC) {
            FuncNode *fn = (FuncNode *)decl;
            /* body_count == 0 means a vendor .hyi stub — no IR to emit */
            if (fn->body_count == 0) continue;
            lower_func_body(fn->name,
                            fn->params, fn->param_count,
                            fn->body,   fn->body_count,
                            strcmp(fn->name, "main") == 0,
                            fn->is_naked,
                            0, NULL, &s);

        } else if (decl->type == NODE_CLASS) {
            ClassNode *cls = (ClassNode *)decl;

            /* Constructor — skip body-less .hyi stubs */
            if (cls->has_ctor && cls->ctor_body_count > 0) {
                char ctor_name[256];
                snprintf(ctor_name, sizeof(ctor_name), "%s__ctor", cls->name);
                lower_func_body(ctor_name,
                                cls->ctor_params, cls->ctor_param_count,
                                cls->ctor_body,   cls->ctor_body_count,
                                0, 0, 1, cls->name, &s);
            }

            /* Methods — skip body-less .hyi stubs */
            for (int mi = 0; mi < cls->method_count; mi++) {
                MethodNode *m = cls->methods[mi];
                if (m->body_count == 0) continue;
                char mname[256];
                snprintf(mname, sizeof(mname), "%s_%s", cls->name, m->name);
                lower_func_body(mname,
                                m->params, m->param_count,
                                m->body,   m->body_count,
                                0, m->is_naked, 1, cls->name, &s);
            }

        } else if (decl->type == NODE_ENUM) {
            /* Enums are metadata only — no IR instructions needed */
        } else if (decl->type == NODE_MODULE) {
            ModuleNode *mn = (ModuleNode *)decl;
            /* Set up the module context so intra-module calls can be mangled */
            s.current_module = mn->name;
            s.module_func_count = mn->func_count < 64 ? mn->func_count : 64;
            for (int _fi = 0; _fi < s.module_func_count; _fi++)
                s.module_func_names[_fi] = mn->funcs[_fi] ? mn->funcs[_fi]->name : NULL;
            for (int fi = 0; fi < mn->func_count; fi++) {
                FuncNode *fn = mn->funcs[fi];
                if (!fn || fn->body_count == 0) continue;
                /* Mangle: ModuleName__funcname */
                char mangled[256];
                snprintf(mangled, sizeof(mangled), "%s__%s", mn->name, fn->name);
                lower_func_body(mangled,
                                fn->params, fn->param_count,
                                fn->body,   fn->body_count,
                                0, fn->is_naked, 0, NULL, &s);
            }
            /* Clear module context */
            s.current_module = NULL;
            s.module_func_count = 0;
            /* Lower static vars / consts inside the module.
               Mangle name to ModuleName__varname to avoid collisions. */
            for (int si = 0; si < mn->static_count; si++) {
                StaticVarNode *sv = mn->statics[si];
                if (!sv) continue;
                char mangled_var[256];
                snprintf(mangled_var, sizeof(mangled_var), "%s__%s", mn->name, sv->var_name);
                IRInstr *ins    = ir_emit(mod, IR_STATIC_VAR);
                ins->str_extra  = strdup(mangled_var);
                ins->str_extra2 = (sv->var_type.kind == TYPE_SIMPLE && sv->var_type.name)
                                  ? strdup(sv->var_type.name) : strdup("int");
                if (sv->initializer && sv->initializer->type == NODE_LITERAL) {
                    LiteralNode *lit = (LiteralNode *)sv->initializer;
                    if (lit->lit_type == LIT_INT)
                        ins->src1 = irop_const_int(atol(lit->value));
                    else if (lit->lit_type == LIT_FLOAT)
                        ins->src1 = irop_const_float(atof(lit->value));
                    else if (lit->lit_type == LIT_STRING)
                        ins->src1 = irop_const_str(strdup(lit->value));
                    else
                        ins->src1 = irop_const_int(0);
                } else if (sv->initializer && sv->initializer->type == NODE_UNARY_OP) {
                    UnaryOpNode *un = (UnaryOpNode *)sv->initializer;
                    if (strcmp(un->op, "-") == 0 && un->operand &&
                        un->operand->type == NODE_LITERAL) {
                        LiteralNode *lit = (LiteralNode *)un->operand;
                        if (lit->lit_type == LIT_FLOAT)
                            ins->src1 = irop_const_float(-atof(lit->value));
                        else if (lit->lit_type == LIT_INT)
                            ins->src1 = irop_const_int(-atol(lit->value));
                        else
                            ins->src1 = irop_const_int(0);
                    } else {
                        ins->src1 = irop_const_int(0);
                    }
                } else if (sv->initializer && sv->initializer->type == NODE_IDENTIFIER) {
                    const char *ref_name = ((IdentifierNode *)sv->initializer)->name;
                    long resolved = 0;
                    RESOLVE_CONST_NAME(ref_name, resolved);
                    /* Also try mangled sibling name if not found */
                    if (resolved == 0) {
                        char mangled_ref[256];
                        snprintf(mangled_ref, sizeof(mangled_ref), "%s__%s", mn->name, ref_name);
                        RESOLVE_CONST_NAME(mangled_ref, resolved);
                    }
                    ins->src1 = irop_const_int(resolved);
                } else {
                    ins->src1 = irop_const_int(0);
                }
                ins->extra_int = (sv->is_const ? 1 : 0) | (sv->array_size << 1);
                /* Register flat-layout module-level array for NODE_INDEX tagging */
                /* Module statics are accessed via their mangled name from inside */
                /* the module's functions, so register the mangled name.          */
                if (sv->array_size > 0 && s.top_static_array_count < MAX_STATIC_LOWER) {
                    const char *type_str =
                        (sv->var_type.kind == TYPE_SIMPLE && sv->var_type.name)
                        ? sv->var_type.name : "int";
                    s.top_static_array_names[s.top_static_array_count] = strdup(mangled_var);
                    s.top_static_array_types[s.top_static_array_count] = type_str;
                    s.top_static_array_count++;
                }
            }
        }
    } /* end for each decl */

    #undef RESOLVE_CONST_NAME
    #undef MAX_CONST_PRELOAD

    return mod;
}
