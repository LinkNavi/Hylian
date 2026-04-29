#include "lower.h"
#include "ir.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Lowering state ─────────────────────────────────────────────────────────── */

#define MAX_LOOP_DEPTH 32

typedef struct {
    int break_lbl;
    int cont_lbl;
} LoopEntry;

typedef struct {
    IRModule  *mod;
    int        next_temp;    /* monotonically increasing temp ID (per-function) */
    int        next_label;   /* monotonically increasing label ID (global)      */
    LoopEntry  loop_stack[MAX_LOOP_DEPTH];
    int        loop_top;
    /* Current class context (non-NULL inside method / ctor) */
    const char *class_name;
} LowerState;

static int alloc_temp(LowerState *s)  { return s->next_temp++; }
static int alloc_label(LowerState *s) { return s->next_label++; }

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

/* ─── Forward declarations ───────────────────────────────────────────────────── */

static int  lower_expr(ASTNode *node, LowerState *s);
static void lower_stmt(ASTNode *node, LowerState *s);
static void lower_stmts(ASTNode **stmts, int count, LowerState *s);

/* ─── Expression lowering ────────────────────────────────────────────────────── */

/* Emit code for `node`; return the temp ID that holds the result. */
static int lower_expr(ASTNode *node, LowerState *s) {
    if (!node) {
        int t = alloc_temp(s);
        IRInstr *ins = ir_emit(s->mod, IR_CONST_NIL);
        ins->dest = irop_temp(t);
        return t;
    }

    switch (node->type) {

    /* ── Literals ─────────────────────────────────────────────────────────── */
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
        } else {
            /* float or unknown — stub */
            IRInstr *ins = ir_emit(s->mod, IR_CONST_INT);
            ins->dest = irop_temp(t);
            ins->src1 = irop_const_int(0);
        }
        return t;
    }

    /* ── Identifier ───────────────────────────────────────────────────────── */
    case NODE_IDENTIFIER: {
        IdentifierNode *id = (IdentifierNode *)node;
        int t = alloc_temp(s);
        IRInstr *ins  = ir_emit(s->mod, IR_LOAD_VAR);
        ins->dest     = irop_temp(t);
        ins->str_extra = strdup(id->name);
        return t;
    }

    /* ── Binary operations ────────────────────────────────────────────────── */
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
        int tl = lower_expr(bin->left, s);
        int tr = lower_expr(bin->right, s);
        int t  = alloc_temp(s);
        IROpcode irop =
            strcmp(op, "+") == 0  ? IR_ADD :
            strcmp(op, "-") == 0  ? IR_SUB :
            strcmp(op, "*") == 0  ? IR_MUL :
            strcmp(op, "/") == 0  ? IR_DIV :
            strcmp(op, "%") == 0  ? IR_MOD :
            strcmp(op, "==") == 0 ? IR_EQ  :
            strcmp(op, "!=") == 0 ? IR_NEQ :
            strcmp(op, "<") == 0  ? IR_LT  :
            strcmp(op, "<=") == 0 ? IR_LE  :
            strcmp(op, ">") == 0  ? IR_GT  :
            strcmp(op, ">=") == 0 ? IR_GE  : IR_ADD; /* fallback */
        IRInstr *ins = ir_emit(s->mod, irop);
        ins->dest = irop_temp(t);
        ins->src1 = irop_temp(tl);
        ins->src2 = irop_temp(tr);
        return t;
    }

    /* ── Unary operations ─────────────────────────────────────────────────── */
    case NODE_UNARY_OP: {
        UnaryOpNode *un = (UnaryOpNode *)node;
        int te = lower_expr(un->operand, s);
        int t  = alloc_temp(s);
        IROpcode irop = strcmp(un->op, "-") == 0 ? IR_NEG : IR_NOT;
        IRInstr *ins  = ir_emit(s->mod, irop);
        ins->dest = irop_temp(t);
        ins->src1 = irop_temp(te);
        return t;
    }

    /* ── Function call ────────────────────────────────────────────────────── */
    case NODE_FUNC_CALL: {
        FuncCallNode *call = (FuncCallNode *)node;

        /* ── Err("msg") ────────────────────────────────────────────────── */
        if (strcmp(call->name, "Err") == 0) {
            int t    = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_ERR);
            ins->dest   = irop_temp(t);
            if (call->arg_count >= 1) {
                ASTNode *arg = call->args[0];
                if (arg->type == NODE_LITERAL && ((LiteralNode *)arg)->lit_type == LIT_STRING) {
                    LiteralNode *lit = (LiteralNode *)arg;
                    const char *v = lit->value; size_t len = strlen(v);
                    char *uq = malloc(len + 1);
                    if (len >= 2 && v[0] == '"' && v[len-1] == '"') {
                        memcpy(uq, v+1, len-2); uq[len-2] = '\0';
                    } else { strcpy(uq, v); }
                    ins->src1 = irop_const_str(uq);
                } else {
                    int ta = lower_expr(arg, s);
                    ins->src1 = irop_temp(ta);
                }
            } else {
                ins->src1 = irop_const_str(strdup("error"));
            }
            return t;
        }

        /* ── panic("msg") ──────────────────────────────────────────────── */
        if (strcmp(call->name, "panic") == 0) {
            int t = alloc_temp(s);
            IRInstr *ins = ir_emit(s->mod, IR_PANIC);
            ins->dest = irop_temp(t);
            if (call->arg_count >= 1) {
                int ta = lower_expr(call->args[0], s);
                ins->src1 = irop_temp(ta);
            } else {
                ins->src1 = irop_const_str(strdup("panic"));
            }
            return t;
        }

        /* ── print / println ───────────────────────────────────────────── */
        if (strcmp(call->name, "print") == 0 || strcmp(call->name, "println") == 0) {
            int t = alloc_temp(s);
            IROpcode irop = strcmp(call->name, "print") == 0 ? IR_PRINT : IR_PRINTLN;
            IRInstr *ins  = ir_emit(s->mod, irop);
            ins->dest = irop_temp(t);
            if (call->arg_count >= 1) {
                ASTNode *arg = call->args[0];
                int cat = classify_print_arg(arg);
                ins->extra_int = cat;
                if (cat == PRINT_ARG_STR_LIT) {
                    LiteralNode *lit = (LiteralNode *)arg;
                    const char *v = lit->value; size_t len = strlen(v);
                    char *uq = malloc(len + 1);
                    if (len >= 2 && v[0] == '"' && v[len-1] == '"') {
                        memcpy(uq, v+1, len-2); uq[len-2] = '\0';
                    } else { strcpy(uq, v); }
                    ins->src1 = irop_const_str(uq);
                } else if (cat == PRINT_ARG_INTERP) {
                    /* embed segments directly for codegen */
                    InterpStringNode *istr = (InterpStringNode *)arg;
                    ins->extra_segs     = istr->segments;
                    ins->extra_seg_count = istr->seg_count;
                    ins->src1 = irop_none();
                } else {
                    int ta = lower_expr(arg, s);
                    ins->src1 = irop_temp(ta);
                }
            } else {
                ins->extra_int = PRINT_ARG_STR_LIT;
                ins->src1 = irop_const_str(strdup(""));
            }
            return t;
        }

        /* ── General function call ─────────────────────────────────────── */
        int t = alloc_temp(s);
        IRInstr *ins = ir_emit(s->mod, IR_CALL);
        ins->dest       = irop_temp(t);
        ins->str_extra  = strdup(call->name);
        int nargs = call->arg_count > 6 ? 6 : call->arg_count;
        if (nargs > 0) {
            ins->args     = malloc(nargs * sizeof(IROperand));
            ins->arg_count = nargs;
            for (int i = 0; i < nargs; i++) {
                int ta = lower_expr(call->args[i], s);
                ins->args[i] = irop_temp(ta);
            }
        }
        return t;
    }

    /* ── Method call ──────────────────────────────────────────────────────── */
    case NODE_METHOD_CALL: {
        MethodCallNode *mc = (MethodCallNode *)node;
        int t = alloc_temp(s);

        /* Array intrinsics */
        int is_arr = (mc->object &&
                      mc->object->resolved_type.kind == TYPE_ARRAY);
        if (is_arr && strcmp(mc->method, "push") == 0 && mc->arg_count >= 1) {
            IRInstr *ins = ir_emit(s->mod, IR_ARRAY_PUSH);
            ins->dest = irop_temp(t);
            int tobj = lower_expr(mc->object, s);
            ins->src1 = irop_temp(tobj);
            int tval = lower_expr(mc->args[0], s);
            ins->src2 = irop_temp(tval);
            /* carry the var name so codegen can update the stack slot */
            if (mc->object->type == NODE_IDENTIFIER)
                ins->str_extra = strdup(((IdentifierNode *)mc->object)->name);
            return t;
        }
        if (is_arr && strcmp(mc->method, "pop") == 0) {
            IRInstr *ins = ir_emit(s->mod, IR_ARRAY_POP);
            ins->dest = irop_temp(t);
            int tobj = lower_expr(mc->object, s);
            ins->src1 = irop_temp(tobj);
            return t;
        }

        /* Generic method call */
        IRInstr *ins = ir_emit(s->mod, IR_CALL);
        ins->dest = irop_temp(t);
        /* Build mangled name: ClassName_methodName */
        const char *cname = NULL;
        if (mc->object && mc->object->resolved_type.kind == TYPE_SIMPLE &&
            mc->object->resolved_type.name)
            cname = mc->object->resolved_type.name;
        if (cname) {
            size_t sz = strlen(cname) + 1 + strlen(mc->method) + 1;
            char *mn = malloc(sz);
            snprintf(mn, sz, "%s_%s", cname, mc->method);
            ins->str_extra = mn;
        } else {
            /* Fallback: method name alone */
            ins->str_extra = strdup(mc->method);
        }
        /* self + args */
        int total = 1 + (mc->arg_count > 5 ? 5 : mc->arg_count);
        ins->args     = malloc(total * sizeof(IROperand));
        ins->arg_count = total;
        int tself = lower_expr(mc->object, s);
        ins->args[0] = irop_temp(tself);
        for (int i = 0; i < total - 1; i++) {
            int ta = lower_expr(mc->args[i], s);
            ins->args[i + 1] = irop_temp(ta);
        }
        return t;
    }

    /* ── Member access ────────────────────────────────────────────────────── */
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

        /* EnumName.Variant */
        if (ma->object && ma->object->type == NODE_IDENTIFIER) {
            IdentifierNode *id = (IdentifierNode *)ma->object;
            IRInstr *ins    = ir_emit(s->mod, IR_ENUM_VAL);
            ins->dest       = irop_temp(t);
            ins->str_extra  = strdup(id->name);
            ins->str_extra2 = strdup(ma->member);
            return t;
        }

        /* Class field access */
        if (ma->object && ma->object->resolved_type.kind == TYPE_SIMPLE &&
            ma->object->resolved_type.name) {
            int tobj = lower_expr(ma->object, s);
            IRInstr *ins    = ir_emit(s->mod, IR_GET_FIELD);
            ins->dest       = irop_temp(t);
            ins->src1       = irop_temp(tobj);
            ins->str_extra  = strdup(ma->object->resolved_type.name);
            ins->str_extra2 = strdup(ma->member);
            return t;
        }

        /* Fallback: load as var */
        IRInstr *ins  = ir_emit(s->mod, IR_LOAD_VAR);
        ins->dest     = irop_temp(t);
        ins->str_extra = strdup(ma->member);
        return t;
    }

    /* ── New ──────────────────────────────────────────────────────────────── */
    case NODE_NEW: {
        NewNode *nn = (NewNode *)node;
        int t = alloc_temp(s);
        IRInstr *ins = ir_emit(s->mod, IR_NEW);
        ins->dest      = irop_temp(t);
        ins->str_extra = strdup(nn->class_name);
        int nargs = nn->arg_count > 5 ? 5 : nn->arg_count;
        if (nargs > 0) {
            ins->args      = malloc(nargs * sizeof(IROperand));
            ins->arg_count = nargs;
            for (int i = 0; i < nargs; i++) {
                int ta = lower_expr(nn->args[i], s);
                ins->args[i] = irop_temp(ta);
            }
        }
        return t;
    }

    /* ── Array literal ────────────────────────────────────────────────────── */
    case NODE_ARRAY_LITERAL: {
        ArrayLiteralNode *al = (ArrayLiteralNode *)node;
        int t = alloc_temp(s);
        IRInstr *ins = ir_emit(s->mod, IR_ARRAY_INIT);
        ins->dest      = irop_temp(t);
        if (al->elem_count > 0) {
            ins->args      = malloc(al->elem_count * sizeof(IROperand));
            ins->arg_count = al->elem_count;
            for (int i = 0; i < al->elem_count; i++) {
                int te = lower_expr(al->elements[i], s);
                ins->args[i] = irop_temp(te);
            }
        }
        return t;
    }

    /* ── Array index ──────────────────────────────────────────────────────── */
    case NODE_INDEX: {
        IndexNode *ix = (IndexNode *)node;
        int tobj = lower_expr(ix->object, s);
        int tidx = lower_expr(ix->index, s);
        int t    = alloc_temp(s);
        IRInstr *ins = ir_emit(s->mod, IR_ARRAY_LOAD);
        ins->dest = irop_temp(t);
        ins->src1 = irop_temp(tobj);
        ins->src2 = irop_temp(tidx);
        return t;
    }

    /* ── Interpolated string ──────────────────────────────────────────────── */
    case NODE_INTERP_STRING: {
        InterpStringNode *istr = (InterpStringNode *)node;
        int t = alloc_temp(s);
        IRInstr *ins = ir_emit(s->mod, IR_INTERP_STR);
        ins->dest          = irop_temp(t);
        ins->extra_segs    = istr->segments;
        ins->extra_seg_count = istr->seg_count;
        return t;
    }

    /* ── Tuple ────────────────────────────────────────────────────────────── */
    case NODE_TUPLE: {
        TupleNode *tup = (TupleNode *)node;
        /* Lower each element; return the last one (tuples are simple here) */
        int t = -1;
        for (int i = 0; i < tup->elem_count; i++)
            t = lower_expr(tup->elements[i], s);
        if (t < 0) { t = alloc_temp(s); ir_emit(s->mod, IR_CONST_NIL)->dest = irop_temp(t); }
        return t;
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

/* ─── Statement lowering ─────────────────────────────────────────────────────── */

static void lower_stmts(ASTNode **stmts, int count, LowerState *s) {
    for (int i = 0; i < count; i++)
        lower_stmt(stmts[i], s);
}

static void lower_stmt(ASTNode *node, LowerState *s) {
    if (!node) return;

    switch (node->type) {

    /* ── Variable declaration ─────────────────────────────────────────────── */
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

    /* ── Assignment ───────────────────────────────────────────────────────── */
    case NODE_ASSIGN: {
        AssignNode *as = (AssignNode *)node;
        int tv = lower_expr(as->value, s);
        IRInstr *sv   = ir_emit(s->mod, IR_STORE_VAR);
        sv->str_extra = strdup(as->var_name);
        sv->src1      = irop_temp(tv);
        break;
    }

    /* ── Compound assignment (+=, -=, *=, /=) ────────────────────────────── */
    case NODE_COMPOUND_ASSIGN: {
        CompoundAssignNode *ca = (CompoundAssignNode *)node;
        /* Load current value */
        int tcur = alloc_temp(s);
        { IRInstr *lv = ir_emit(s->mod, IR_LOAD_VAR); lv->dest = irop_temp(tcur); lv->str_extra = strdup(ca->var_name); }
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
        sv->str_extra = strdup(ca->var_name);
        sv->src1      = irop_temp(tres);
        break;
    }

    /* ── Member assign (obj.field = val) ─────────────────────────────────── */
    case NODE_MEMBER_ASSIGN: {
        MemberAssignNode *ma = (MemberAssignNode *)node;
        int tval = lower_expr(ma->value, s);
        IRInstr *ins = ir_emit(s->mod, IR_SET_FIELD);
        /* src1 = object pointer (NULL object means self) */
        if (ma->object == NULL) {
            /* Implicit self — emit LOAD_VAR "self" */
            int tself = alloc_temp(s);
            IRInstr *lv = ir_emit(s->mod, IR_LOAD_VAR);
            lv->dest = irop_temp(tself); lv->str_extra = strdup("self");
            ins->src1 = irop_temp(tself);
            ins->str_extra = s->class_name ? strdup(s->class_name) : strdup("?");
        } else {
            int tobj = lower_expr(ma->object, s);
            ins->src1 = irop_temp(tobj);
            ins->str_extra =
                (ma->object->resolved_type.kind == TYPE_SIMPLE && ma->object->resolved_type.name)
                ? strdup(ma->object->resolved_type.name)
                : (s->class_name ? strdup(s->class_name) : strdup("?"));
        }
        ins->src2       = irop_temp(tval);
        ins->str_extra2 = strdup(ma->member);
        break;
    }

    /* ── Index assign (arr[i] = val) ─────────────────────────────────────── */
    case NODE_INDEX_ASSIGN: {
        IndexAssignNode *ia = (IndexAssignNode *)node;
        int tobj = lower_expr(ia->object, s);
        int tidx = lower_expr(ia->index, s);
        int tval = lower_expr(ia->value, s);
        IRInstr *ins     = ir_emit(s->mod, IR_ARRAY_STORE);
        ins->src1        = irop_temp(tobj);
        ins->src2        = irop_temp(tidx);
        ins->extra_src   = irop_temp(tval);
        break;
    }

    /* ── Return ───────────────────────────────────────────────────────────── */
    case NODE_RETURN: {
        ReturnNode *ret = (ReturnNode *)node;
        IRInstr *ins = ir_emit(s->mod, IR_RETURN);
        if (ret->value) {
            int tv = lower_expr(ret->value, s);
            ins->src1 = irop_temp(tv);
        }
        break;
    }

    /* ── If ───────────────────────────────────────────────────────────────── */
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

    /* ── While ────────────────────────────────────────────────────────────── */
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

    /* ── For ──────────────────────────────────────────────────────────────── */
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

    /* ── For-in ───────────────────────────────────────────────────────────── */
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

    /* ── Switch ───────────────────────────────────────────────────────────── */
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

    /* ── Break / Continue ─────────────────────────────────────────────────── */
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

    /* ── Inline assembly ──────────────────────────────────────────────────── */
    case NODE_ASM_BLOCK: {
        AsmBlockNode *ab = (AsmBlockNode *)node;
        IRInstr *ins   = ir_emit(s->mod, IR_ASM_BLOCK);
        ins->str_extra = strdup(ab->body);
        break;
    }

    /* ── Defer (not fully supported) ──────────────────────────────────────── */
    case NODE_DEFER:
        ir_emit(s->mod, IR_NOP);
        break;

    /* ── Expression as statement ──────────────────────────────────────────── */
    default:
        lower_expr(node, s); /* result discarded */
        break;
    }
}

/* ─── Function / method lowering ─────────────────────────────────────────────── */

static void lower_func_body(
    const char  *name,
    ASTNode    **params,    int param_count,
    ASTNode    **body,      int body_count,
    int          is_main,
    int          has_self,   /* 1 if method/ctor (rdi = self) */
    const char  *class_name,
    LowerState  *s)
{
    s->next_temp   = 0;
    s->class_name  = class_name;

    /* IR_FUNC_BEGIN */
    IRInstr *begin = ir_emit(s->mod, IR_FUNC_BEGIN);
    begin->str_extra = strdup(name);
    begin->extra_int = is_main;

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

    /* Lower body statements */
    lower_stmts(body, body_count, s);

    ir_emit(s->mod, IR_FUNC_END);
    s->class_name = NULL;
}

/* ─── Top-level entry ────────────────────────────────────────────────────────── */

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
        }
    }

    /* Collect std includes */
    for (int i = 0; i < prog->include_count; i++) {
        mod->includes = realloc(mod->includes, (mod->include_count + 1) * sizeof(char *));
        mod->includes[mod->include_count++] = prog->includes[i];
    }

    /* Lower top-level declarations */
    for (int i = 0; i < prog->decl_count; i++) {
        ASTNode *decl = prog->declarations[i];
        if (!decl) continue;

        if (decl->type == NODE_FUNC) {
            FuncNode *fn = (FuncNode *)decl;
            lower_func_body(fn->name,
                            fn->params, fn->param_count,
                            fn->body,   fn->body_count,
                            strcmp(fn->name, "main") == 0,
                            0, NULL, &s);

        } else if (decl->type == NODE_CLASS) {
            ClassNode *cls = (ClassNode *)decl;

            /* Constructor */
            if (cls->has_ctor) {
                char ctor_name[256];
                snprintf(ctor_name, sizeof(ctor_name), "%s__ctor", cls->name);
                lower_func_body(ctor_name,
                                cls->ctor_params, cls->ctor_param_count,
                                cls->ctor_body,   cls->ctor_body_count,
                                0, 1, cls->name, &s);
            }

            /* Methods */
            for (int mi = 0; mi < cls->method_count; mi++) {
                MethodNode *m = cls->methods[mi];
                char mname[256];
                snprintf(mname, sizeof(mname), "%s_%s", cls->name, m->name);
                lower_func_body(mname,
                                m->params, m->param_count,
                                m->body,   m->body_count,
                                0, 1, cls->name, &s);
            }

        } else if (decl->type == NODE_ENUM) {
            /* Enums are metadata only — no IR instructions needed */
        }
    }

    return mod;
}
