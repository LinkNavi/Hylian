#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "ast.h"
#include "typecheck.h"
#include "lsp_diag.h"

/* ─── Diagnostics ───────────────────────────────────────────────────────────── */
static const char *current_tc_file = "<unknown>";

static void tc_error(int line, const char *fmt, ...) {
    va_list ap;
    char buf[512];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    int l = line > 0 ? line - 1 : 0;
    lsp_diag_push(l, 0, l, 999, LSP_DIAG_ERROR, buf);
}

static void tc_warn(int line, const char *fmt, ...) {
    va_list ap;
    char buf[512];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    int l = line > 0 ? line - 1 : 0;
    lsp_diag_push(l, 0, l, 999, LSP_DIAG_WARNING, buf);
}

/* ─── Scope / symbol table ──────────────────────────────────────────────────── */

#define MAX_SCOPE_DEPTH 32
#define MAX_SCOPE_SYMS  512

typedef struct {
    char *name;
    Type  type;
} Symbol;

static Symbol symbols[MAX_SCOPE_SYMS];
static int    sym_count = 0;

static int scope_stack[MAX_SCOPE_DEPTH];
static int scope_depth = 0;

static void scope_push(void) {
    if (scope_depth < MAX_SCOPE_DEPTH)
        scope_stack[scope_depth++] = sym_count;
}

static void scope_pop(void) {
    if (scope_depth > 0)
        sym_count = scope_stack[--scope_depth];
}

static void scope_define(const char *name, Type t) {
    if (sym_count < MAX_SCOPE_SYMS) {
        symbols[sym_count].name = (char *)name;
        symbols[sym_count].type = t;
        sym_count++;
    }
}

static Type *scope_lookup(const char *name) {
    for (int i = sym_count - 1; i >= 0; i--)
        if (symbols[i].name && strcmp(symbols[i].name, name) == 0)
            return &symbols[i].type;
    return NULL;
}

/* ─── Function / class registries ───────────────────────────────────────────── */

typedef struct {
    char *name;
    Type  return_type;
    Type  param_types[16];
    int   param_count;
} FuncInfo;

typedef struct {
    char *class_name;
    char *field_name;
    Type  field_type;
} FieldEntry;

static FuncInfo   funcs[256];
static int        func_count = 0;

static FieldEntry fields[1024];
static int        field_count = 0;

static FuncInfo *func_lookup(const char *name) {
    for (int i = 0; i < func_count; i++)
        if (funcs[i].name && strcmp(funcs[i].name, name) == 0)
            return &funcs[i];
    return NULL;
}

static FieldEntry *field_lookup(const char *class_name, const char *fname) {
    for (int i = 0; i < field_count; i++)
        if (fields[i].class_name && fields[i].field_name &&
            strcmp(fields[i].class_name, class_name) == 0 &&
            strcmp(fields[i].field_name, fname) == 0)
            return &fields[i];
    return NULL;
}

/* ─── Unknown type helper ───────────────────────────────────────────────────── */

static Type unknown_type(void) {
    return make_simple_type(NULL, 0);
}

/* ─── Type name helper ──────────────────────────────────────────────────────── */

static const char *type_name(Type t) {
    static char buf[256];
    if (t.kind == TYPE_ARRAY) {
        if (t.elem_type_count > 0 && t.elem_types[0].name)
            snprintf(buf, sizeof(buf), "array<%s>", t.elem_types[0].name);
        else
            snprintf(buf, sizeof(buf), "array<unknown>");
        return buf;
    }
    if (t.kind == TYPE_MULTI) {
        if (t.is_any) return "multi<any>";
        snprintf(buf, sizeof(buf), "multi<...>");
        return buf;
    }
    if (t.kind == TYPE_TUPLE) {
        char tmp[256] = "(";
        for (int i = 0; i < t.elem_type_count; i++) {
            if (i > 0) strncat(tmp, ", ", sizeof(tmp) - strlen(tmp) - 1);
            strncat(tmp, t.elem_types[i].name ? t.elem_types[i].name : "?",
                    sizeof(tmp) - strlen(tmp) - 1);
        }
        strncat(tmp, ")", sizeof(tmp) - strlen(tmp) - 1);
        snprintf(buf, sizeof(buf), "%s", tmp);
        return buf;
    }
    if (t.name) return t.name;
    return "unknown";
}

/* ─── Forward declarations ──────────────────────────────────────────────────── */

static Type infer_expr(ASTNode *node);
static void infer_stmt(ASTNode *node);

/* ─── Expression inference ──────────────────────────────────────────────────── */

static Type infer_expr(ASTNode *node) {
    if (!node) return unknown_type();

    Type result = unknown_type();

    switch (node->type) {

    case NODE_LITERAL: {
        LiteralNode *ln = (LiteralNode *)node;
        switch (ln->lit_type) {
        case LIT_INT:    result = make_simple_type("int",   0); break;
        case LIT_STRING: result = make_simple_type("str",   0); break;
        case LIT_BOOL:   result = make_simple_type("bool",  0); break;
        case LIT_NIL:    result = make_simple_type("void",  0); break;
        case LIT_FLOAT:  result = make_simple_type("float", 0); break;
        }
        break;
    }

    case NODE_IDENTIFIER: {
        IdentifierNode *id = (IdentifierNode *)node;
        Type *t = scope_lookup(id->name);
        if (t) {
            result = *t;
        } else {
            tc_error(node->line, "undefined variable '%s'", id->name);
        }
        break;
    }

    case NODE_BINARY_OP: {
        BinaryOpNode *bn = (BinaryOpNode *)node;
        Type left  = infer_expr(bn->left);
        Type right = infer_expr(bn->right);
        (void)right;
        const char *op = bn->op;
        if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
            strcmp(op, "<")  == 0 || strcmp(op, ">")  == 0 ||
            strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
            strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
            result = make_simple_type("bool", 0);
        } else {
            /* Warn on arithmetic applied directly to a boolean */
            if (left.name && strcmp(left.name, "bool") == 0 &&
                (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 ||
                 strcmp(op, "*") == 0 || strcmp(op, "/") == 0 ||
                 strcmp(op, "%") == 0)) {
                tc_warn(node->line, "arithmetic on boolean value with operator '%s'", op);
            }
            result = left;
        }
        break;
    }

    case NODE_UNARY_OP: {
        UnaryOpNode *un = (UnaryOpNode *)node;
        Type operand = infer_expr(un->operand);
        if (un->op && strcmp(un->op, "!") == 0)
            result = make_simple_type("bool", 0);
        else
            result = operand;
        break;
    }

    case NODE_NEW: {
        NewNode *nn = (NewNode *)node;
        result = make_simple_type(nn->class_name, 0);
        break;
    }

    case NODE_FUNC_CALL: {
        FuncCallNode *fc = (FuncCallNode *)node;
        /* Infer all args first */
        for (int i = 0; i < fc->arg_count; i++)
            infer_expr(fc->args[i]);
        /* Special built-ins that are not registered in the func table */
        if (fc->name && strcmp(fc->name, "Err") == 0) {
            result = make_simple_type("Error", 0);
        } else if (fc->name && strcmp(fc->name, "panic") == 0) {
            result = make_simple_type("void", 0);
        } else if (fc->name && strcmp(fc->name, "print") == 0) {
            result = make_simple_type("void", 0);
        } else if (fc->name && strcmp(fc->name, "len") == 0) {
            result = make_simple_type("int", 0);
        } else if (fc->name && strcmp(fc->name, "push") == 0) {
            result = make_simple_type("void", 0);
        } else if (fc->name && strcmp(fc->name, "pop") == 0) {
            result = unknown_type();
        } else if (fc->name && strcmp(fc->name, "exit") == 0) {
            result = make_simple_type("void", 0);
        } else if (fc->name) {
            FuncInfo *fi = func_lookup(fc->name);
            if (fi) {
                result = fi->return_type;
            } else {
                tc_error(node->line, "call to undefined function '%s'", fc->name);
            }
        }
        break;
    }

    case NODE_METHOD_CALL: {
        MethodCallNode *mc = (MethodCallNode *)node;
        Type obj_type = infer_expr(mc->object);
        /* Infer all args */
        for (int i = 0; i < mc->arg_count; i++)
            infer_expr(mc->args[i]);
        /* Special: .message() on Error -> str */
        if (obj_type.name && strcmp(obj_type.name, "Error") == 0 &&
            mc->method && strcmp(mc->method, "message") == 0) {
            result = make_simple_type("str", 0);
        } else if (obj_type.name && mc->method) {
            /* Look for mangled name ClassName__method */
            char mangled[256];
            snprintf(mangled, sizeof(mangled), "%s__%s", obj_type.name, mc->method);
            FuncInfo *fi = func_lookup(mangled);
            if (fi) {
                result = fi->return_type;
            } else {
                tc_error(node->line, "no method '%s' on type '%s'", mc->method,
                         obj_type.name ? obj_type.name : "unknown");
            }
        } else if (mc->method && obj_type.kind == TYPE_ARRAY) {
            /* Built-in array methods */
            if (strcmp(mc->method, "push") == 0 || strcmp(mc->method, "pop") == 0 ||
                strcmp(mc->method, "len")  == 0 || strcmp(mc->method, "cap")  == 0) {
                result = make_simple_type("int", 0);
            } else {
                tc_error(node->line, "no method '%s' on type '%s'", mc->method, type_name(obj_type));
            }
        } else if (mc->method && !obj_type.name) {
            tc_error(node->line, "no method '%s' on type 'unknown'", mc->method);
        }
        break;
    }

    case NODE_MEMBER_ACCESS: {
        MemberAccessNode *ma = (MemberAccessNode *)node;
        Type obj_type = infer_expr(ma->object);
        if (obj_type.kind == TYPE_ARRAY) {
            if (ma->member && (strcmp(ma->member, "len") == 0 ||
                               strcmp(ma->member, "cap") == 0)) {
                result = make_simple_type("int", 0);
            }
        } else if (obj_type.kind == TYPE_MULTI) {
            if (ma->member && strcmp(ma->member, "tag") == 0)
                result = make_simple_type("int", 0);
            /* "value" stays unknown */
        } else if (obj_type.name) {
            /* Special array name check (stored as TYPE_SIMPLE "array") */
            if (strcmp(obj_type.name, "array") == 0) {
                if (ma->member && (strcmp(ma->member, "len") == 0 ||
                                   strcmp(ma->member, "cap") == 0)) {
                    result = make_simple_type("int", 0);
                }
            } else if (strcmp(obj_type.name, "multi") == 0) {
                if (ma->member && strcmp(ma->member, "tag") == 0)
                    result = make_simple_type("int", 0);
            } else {
                FieldEntry *fe = field_lookup(obj_type.name, ma->member);
                if (fe) {
                    result = fe->field_type;
                } else {
                    tc_error(node->line, "no field '%s' on type '%s'", ma->member, obj_type.name);
                }
            }
        }
        break;
    }

    case NODE_ARRAY_LITERAL: {
        ArrayLiteralNode *al = (ArrayLiteralNode *)node;
        Type elem = unknown_type();
        for (int i = 0; i < al->elem_count; i++) {
            Type et = infer_expr(al->elements[i]);
            if (i == 0) elem = et;
        }
        /* Build TYPE_ARRAY with elem type */
        result.kind = TYPE_ARRAY;
        result.nullable = 0;
        result.name = "array";
        result.elem_types = malloc(sizeof(Type));
        if (result.elem_types) {
            result.elem_types[0] = elem;
            result.elem_type_count = 1;
        } else {
            result.elem_type_count = 0;
        }
        result.is_any = 0;
        result.fixed_size = 0;
        break;
    }

    case NODE_TUPLE: {
        TupleNode *tn = (TupleNode *)node;
        Type *elem_types = malloc(tn->elem_count * sizeof(Type));
        for (int i = 0; i < tn->elem_count; i++)
            elem_types[i] = infer_expr(tn->elements[i]);
        result = make_tuple_type(elem_types, tn->elem_count);
        free(elem_types);
        break;
    }

    case NODE_INDEX: {
        IndexNode *in_node = (IndexNode *)node;
        Type obj_type = infer_expr(in_node->object);
        infer_expr(in_node->index);
        if (obj_type.kind == TYPE_ARRAY && obj_type.elem_type_count > 0)
            result = obj_type.elem_types[0];
        break;
    }

    case NODE_INTERP_STRING: {
        result = make_simple_type("str", 0);
        break;
    }

    case NODE_ASSIGN: {
        AssignNode *as = (AssignNode *)node;
        result = infer_expr(as->value);
        break;
    }

    case NODE_COMPOUND_ASSIGN: {
        CompoundAssignNode *ca = (CompoundAssignNode *)node;
        result = infer_expr(ca->value);
        break;
    }

    default:
        break;
    }

    node->resolved_type = result;
    return result;
}

/* ─── Statement inference ───────────────────────────────────────────────────── */

static void infer_stmt(ASTNode *node) {
    if (!node) return;

    switch (node->type) {

    case NODE_VAR_DECL: {
        VarDeclNode *vd = (VarDeclNode *)node;
        Type init_type = unknown_type();
        if (vd->initializer)
            init_type = infer_expr(vd->initializer);
        Type decl_type = vd->var_type;
        /* Warn when 'auto' type cannot be resolved from the initializer */
        if (decl_type.name && strcmp(decl_type.name, "auto") == 0 &&
            (!init_type.name || strcmp(init_type.name, "unknown") == 0) &&
            init_type.kind == TYPE_SIMPLE && !init_type.name) {
            tc_warn(node->line, "cannot infer type for '%s' from initializer", vd->var_name);
        }
        scope_define(vd->var_name, decl_type);
        node->resolved_type = decl_type;
        break;
    }

    case NODE_ASSIGN: {
        AssignNode *as = (AssignNode *)node;
        infer_expr(as->value);
        break;
    }

    case NODE_COMPOUND_ASSIGN: {
        CompoundAssignNode *ca = (CompoundAssignNode *)node;
        infer_expr(ca->value);
        break;
    }

    case NODE_RETURN: {
        ReturnNode *ret = (ReturnNode *)node;
        if (ret->value)
            infer_expr(ret->value);
        break;
    }

    case NODE_IF: {
        IfNode *nd = (IfNode *)node;
        infer_expr(nd->condition);
        scope_push();
        for (int i = 0; i < nd->then_count; i++)
            infer_stmt(nd->then_body[i]);
        scope_pop();
        if (nd->else_body && nd->else_count > 0) {
            scope_push();
            for (int i = 0; i < nd->else_count; i++)
                infer_stmt(nd->else_body[i]);
            scope_pop();
        }
        break;
    }

    case NODE_WHILE: {
        WhileNode *nd = (WhileNode *)node;
        infer_expr(nd->condition);
        scope_push();
        for (int i = 0; i < nd->body_count; i++)
            infer_stmt(nd->body[i]);
        scope_pop();
        break;
    }

    case NODE_FOR: {
        ForNode *nd = (ForNode *)node;
        scope_push();
        if (nd->init)  infer_stmt(nd->init);
        if (nd->condition) infer_expr(nd->condition);
        if (nd->post)  infer_stmt(nd->post);
        for (int i = 0; i < nd->body_count; i++)
            infer_stmt(nd->body[i]);
        scope_pop();
        break;
    }

    case NODE_FOR_IN: {
        ForInNode *fi = (ForInNode *)node;
        Type coll_type = infer_expr(fi->collection);
        Type elem_type = unknown_type();
        if (coll_type.kind == TYPE_ARRAY && coll_type.elem_type_count > 0)
            elem_type = coll_type.elem_types[0];
        scope_push();
        scope_define(fi->var_name, elem_type);
        for (int i = 0; i < fi->body_count; i++)
            infer_stmt(fi->body[i]);
        scope_pop();
        break;
    }

    case NODE_FUNC_CALL:
        infer_expr(node);
        break;

    case NODE_METHOD_CALL:
        infer_expr(node);
        break;

    case NODE_INDEX_ASSIGN: {
        IndexAssignNode *ia = (IndexAssignNode *)node;
        infer_expr(ia->object);
        infer_expr(ia->index);
        infer_expr(ia->value);
        break;
    }

    case NODE_MEMBER_ASSIGN: {
        MemberAssignNode *ma = (MemberAssignNode *)node;
        if (ma->object) infer_expr(ma->object);
        infer_expr(ma->value);
        break;
    }

    case NODE_DEFER: {
        DeferNode *dn = (DeferNode *)node;
        if (dn->expr) infer_expr(dn->expr);
        break;
    }

    default:
        /* Try as expression */
        infer_expr(node);
        break;
    }
}

/* ─── Function body inference ───────────────────────────────────────────────── */

static void infer_function(ASTNode **params, int param_count,
                            ASTNode **body, int body_count,
                            Type return_type, const char *class_name) {
    (void)return_type;
    scope_push();
    if (class_name)
        scope_define("self", make_simple_type((char *)class_name, 0));
    for (int i = 0; i < param_count; i++) {
        if (!params[i]) continue;
        VarDeclNode *p = (VarDeclNode *)params[i];
        scope_define(p->var_name, p->var_type);
    }
    for (int i = 0; i < body_count; i++)
        infer_stmt(body[i]);
    scope_pop();
}

/* ─── Register helpers ──────────────────────────────────────────────────────── */

static void register_func(const char *name, Type return_type,
                           ASTNode **params, int param_count) {
    if (func_count >= 256) return;
    FuncInfo *fi = &funcs[func_count++];
    fi->name = (char *)name;
    fi->return_type = return_type;
    fi->param_count = param_count < 16 ? param_count : 16;
    for (int i = 0; i < fi->param_count; i++) {
        if (params && params[i]) {
            VarDeclNode *p = (VarDeclNode *)params[i];
            fi->param_types[i] = p->var_type;
        } else {
            fi->param_types[i] = unknown_type();
        }
    }
}

static void register_field(const char *class_name, const char *fname, Type ftype) {
    if (field_count >= 1024) return;
    fields[field_count].class_name = (char *)class_name;
    fields[field_count].field_name = (char *)fname;
    fields[field_count].field_type = ftype;
    field_count++;
}

/* ─── Main typecheck entry ──────────────────────────────────────────────────── */

void lsp_typecheck(ProgramNode *program, const char *filename) {
    current_tc_file = filename ? filename : "<unknown>";

    /* Reset state */
    sym_count   = 0;
    scope_depth = 0;
    func_count  = 0;
    field_count = 0;

    /* Pass 1: register all top-level functions and classes */
    for (int i = 0; i < program->decl_count; i++) {
        ASTNode *d = program->declarations[i];
        if (!d) continue;

        if (d->type == NODE_FUNC) {
            FuncNode *fn = (FuncNode *)d;
            register_func(fn->name, fn->return_type, fn->params, fn->param_count);
        }

        if (d->type == NODE_CLASS) {
            ClassNode *cn = (ClassNode *)d;
            /* Register fields */
            for (int f = 0; f < cn->field_count; f++) {
                if (!cn->fields[f]) continue;
                FieldNode *fld = cn->fields[f];
                register_field(cn->name, fld->name, fld->field_type);
            }
            /* Register methods as mangled names */
            for (int m = 0; m < cn->method_count; m++) {
                if (!cn->methods[m]) continue;
                MethodNode *mn = cn->methods[m];
                char mangled[256];
                snprintf(mangled, sizeof(mangled), "%s__%s", cn->name, mn->name);
                /* We need a stable heap copy of the mangled name */
                char *mangled_copy = strdup(mangled);
                register_func(mangled_copy, mn->return_type,
                               mn->params, mn->param_count);
            }
            /* Register constructor as ClassName__ctor or ClassName */
            if (cn->has_ctor) {
                char ctor_mangled[256];
                snprintf(ctor_mangled, sizeof(ctor_mangled), "%s__ctor", cn->name);
                char *ctor_copy = strdup(ctor_mangled);
                Type self_type = make_simple_type(cn->name, 0);
                register_func(ctor_copy, self_type,
                               cn->ctor_params, cn->ctor_param_count);
            }
        }
    }

    /* Pass 2: infer function and class bodies */
    for (int i = 0; i < program->decl_count; i++) {
        ASTNode *d = program->declarations[i];
        if (!d) continue;

        if (d->type == NODE_FUNC) {
            FuncNode *fn = (FuncNode *)d;
            infer_function(fn->params, fn->param_count,
                           fn->body, fn->body_count,
                           fn->return_type, NULL);
        }

        if (d->type == NODE_CLASS) {
            ClassNode *cn = (ClassNode *)d;
            /* Infer constructor body */
            if (cn->has_ctor) {
                infer_function(cn->ctor_params, cn->ctor_param_count,
                               cn->ctor_body, cn->ctor_body_count,
                               make_simple_type(cn->name, 0), cn->name);
            }
            /* Infer each method body */
            for (int m = 0; m < cn->method_count; m++) {
                if (!cn->methods[m]) continue;
                MethodNode *mn = cn->methods[m];
                infer_function(mn->params, mn->param_count,
                               mn->body, mn->body_count,
                               mn->return_type, cn->name);
            }
        }
    }
}