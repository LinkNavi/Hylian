/* Monomorphization: generic classes/structs and functions, plus the built-in
 * tuple struct, are expanded into ordinary declarations before lowering.
 *
 *   class Box<T> { T value; }          template (removed from the program)
 *   Box<int> b;                        use  -> the type becomes `Box_of_int`
 *                                      and a copy of the template with T := int
 *                                      is appended to the program.
 *
 * mono_run() rewrites every explicit use up front. A call to a generic
 * function that names no type arguments (`max(1, 2)`) can only be resolved
 * once argument types are known, so the typechecker asks for that instance
 * through mono_instantiate_inferred() while it works.
 *
 * A tuple type (A, B) is the built-in generic `__tuple<A, B>`: a struct whose
 * fields are _0, _1, ... Because it is just another instance it needs no
 * support from the later passes, which already know how to pass and return
 * structs by value.
 */
#include "mono.h"
#include "diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const char *current_parse_file;
extern int yylineno;

/* ── bookkeeping ─────────────────────────────────────────────────────────── */

typedef struct {
    char *key;           /* mangled name, e.g. Box_of_int */
    int   is_func;
    void *tmpl;          /* ClassNode* / FuncNode*; NULL for the tuple pseudo-template */
    Type *args;
    int   nargs;
} Instance;

static ProgramNode *g_prog = NULL;
static ClassNode  **g_class_tmpls = NULL; static int g_class_tmpl_count = 0;
static FuncNode   **g_func_tmpls  = NULL; static int g_func_tmpl_count  = 0;
static Instance    *g_insts = NULL;       static int g_inst_count = 0;

/* generic methods: kept out of their class and expanded per use (see
   mono_instantiate_method). `sub` is the owning class's own substitution. */
typedef struct { ClassNode *cn; MethodNode *tmpl; char **names; Type *types; int count; } MethodTmpl;
static MethodTmpl *g_mtmpls = NULL;       static int g_mtmpl_count = 0;
static int g_used = 0;                    /* any generic syntax seen by the parser */
static MonoHooks g_hooks;
static const char *g_file = "<unknown>";

/* uses recorded by the parser in places that only carry a type NAME (casts,
   sizeof): they still need the instance to exist */
static Type *g_requests = NULL; static int g_request_count = 0;

typedef struct {
    char **names;
    Type  *types;
    int    count;
} Sub;

void mono_note_use(void) { g_used = 1; }

void mono_set_hooks(const MonoHooks *h) {
    if (h) g_hooks = *h; else memset(&g_hooks, 0, sizeof g_hooks);
}

/* ── names ───────────────────────────────────────────────────────────────── */

static void mangle_into(const Type *t, char *buf, size_t n);

static void mangle_list(const Type *ts, int count, char *buf, size_t n) {
    for (int i = 0; i < count; i++) {
        size_t o = strlen(buf);
        if (i && o + 1 < n) { buf[o++] = '_'; buf[o] = 0; }
        mangle_into(&ts[i], buf + o, n - o);
    }
}

static void mangle_into(const Type *t, char *buf, size_t n) {
    char tmp[512] = "";
    switch (t->kind) {
    case TYPE_ARRAY:
        snprintf(tmp, sizeof tmp, "array_");
        if (t->elem_type_count > 0) mangle_into(&t->elem_types[0], tmp + 6, sizeof tmp - 6);
        break;
    case TYPE_REF:    snprintf(tmp, sizeof tmp, "ref_");
        if (t->elem_type_count > 0) mangle_into(&t->elem_types[0], tmp + 4, sizeof tmp - 4);
        break;
    case TYPE_RAWPTR: snprintf(tmp, sizeof tmp, "ptr_");
        if (t->elem_type_count > 0) mangle_into(&t->elem_types[0], tmp + 4, sizeof tmp - 4);
        break;
    case TYPE_MULTI:  snprintf(tmp, sizeof tmp, "multi"); break;
    default:
        if (t->is_fn) {
            snprintf(tmp, sizeof tmp, "fn_");
            mangle_list(t->elem_types, t->elem_type_count, tmp, sizeof tmp);
            size_t o = strlen(tmp);
            snprintf(tmp + o, sizeof tmp - o, "_to_");
            if (t->fn_ret) mangle_into(t->fn_ret, tmp + o + 4, sizeof tmp - o - 4);
        } else {
            snprintf(tmp, sizeof tmp, "%s", t->name ? t->name : "unknown");
            if (t->type_arg_count > 0) {
                size_t o = strlen(tmp);
                snprintf(tmp + o, sizeof tmp - o, "_of_");
                mangle_list(t->type_args, t->type_arg_count, tmp, sizeof tmp);
            }
        }
    }
    if (t->nullable) strncat(tmp, "_n", sizeof tmp - strlen(tmp) - 1);
    snprintf(buf, n, "%s", tmp);
}

char *mono_instance_name(const char *base, const Type *args, int nargs) {
    char buf[512];
    snprintf(buf, sizeof buf, "%s_of_", base);
    mangle_list(args, nargs, buf, sizeof buf);
    return strdup(buf);
}

char *mono_mangle_type(const Type *t) {
    char buf[512];
    if (t->type_arg_count > 0) {
        char *r = mono_instance_name(t->name, t->type_args, t->type_arg_count);
        return r;
    }
    mangle_into(t, buf, sizeof buf);
    return strdup(buf);
}

/* ── deep copy with substitution ─────────────────────────────────────────── */

static ASTNode *clone_node(ASTNode *n, const Sub *sub);
static Type     clone_type(const Type *t, const Sub *sub);

static const Type *sub_lookup(const Sub *sub, const char *name) {
    if (!sub || !name) return NULL;
    for (int i = 0; i < sub->count; i++)
        if (strcmp(sub->names[i], name) == 0) return &sub->types[i];
    return NULL;
}

static Type *clone_types(const Type *ts, int n, const Sub *sub) {
    if (n <= 0 || !ts) return NULL;
    Type *r = malloc(n * sizeof(Type));
    for (int i = 0; i < n; i++) r[i] = clone_type(&ts[i], sub);
    return r;
}

static char *instantiate_class(const char *base, Type *args, int nargs);
static char *instantiate_func(const char *base, Type *args, int nargs);

static Type clone_type(const Type *t, const Sub *sub) {
    /* a bare template parameter: replaced by the concrete type */
    if (t->kind == TYPE_SIMPLE && t->name && !t->is_fn && t->type_arg_count == 0) {
        const Type *r = sub_lookup(sub, t->name);
        if (r) {
            Type c = clone_type(r, NULL);
            if (t->nullable) c.nullable = 1;
            return c;
        }
    }
    Type c = *t;
    if (t->elem_type_count > 0) c.elem_types = clone_types(t->elem_types, t->elem_type_count, sub);
    if (t->fn_ret) { c.fn_ret = malloc(sizeof(Type)); *c.fn_ret = clone_type(t->fn_ret, sub); }
    if (t->type_arg_count > 0) {
        c.type_args = clone_types(t->type_args, t->type_arg_count, sub);
        /* args are fully concrete now: turn Foo<int> into the instance's name */
        if (!strcmp(t->name, "__tuple")) {
            char *nm = instantiate_class("__tuple", c.type_args, c.type_arg_count);
            c.name = nm;
        } else {
            c.name = instantiate_class(t->name, c.type_args, c.type_arg_count);
        }
        c.type_args = NULL; c.type_arg_count = 0;
    }
    return c;
}

static ASTNode **clone_list(ASTNode **a, int n, const Sub *sub) {
    if (n <= 0 || !a) return a;
    ASTNode **r = malloc(n * sizeof(ASTNode *));
    for (int i = 0; i < n; i++) r[i] = clone_node(a[i], sub);
    return r;
}

/* a type NAME held in a string (cast<T>, sizeof(T)): map a template parameter
   to the concrete type's name */
static char *sub_type_name(char *name, const Sub *sub) {
    const Type *r = sub_lookup(sub, name);
    if (!r) return name;
    Type c = clone_type(r, NULL);
    return mono_mangle_type(&c);
}

#define CLONE(T) T *c = malloc(sizeof(T)); memcpy(c, n, sizeof(T))

static FuncNode *clone_func(FuncNode *n, const Sub *sub) {
    CLONE(FuncNode);
    c->return_type = clone_type(&n->return_type, sub);
    c->params = clone_list(n->params, n->param_count, sub);
    c->body = clone_list(n->body, n->body_count, sub);
    return c;
}

static ASTNode *clone_node(ASTNode *n, const Sub *sub) {
    if (!n) return NULL;
    switch (n->type) {
    case NODE_FUNC: return (ASTNode *)clone_func((FuncNode *)n, sub);
    case NODE_METHOD: {
        CLONE(MethodNode);
        c->return_type = clone_type(&((MethodNode *)n)->return_type, sub);
        c->params = clone_list(((MethodNode *)n)->params, c->param_count, sub);
        c->body = clone_list(((MethodNode *)n)->body, c->body_count, sub);
        return (ASTNode *)c;
    }
    case NODE_FIELD: {
        CLONE(FieldNode);
        c->field_type = clone_type(&((FieldNode *)n)->field_type, sub);
        return (ASTNode *)c;
    }
    case NODE_CLASS: {
        ClassNode *o = (ClassNode *)n;
        CLONE(ClassNode);
        c->fields = NULL; c->methods = NULL;
        if (o->field_count) {
            c->fields = malloc(o->field_count * sizeof(FieldNode *));
            for (int i = 0; i < o->field_count; i++) c->fields[i] = (FieldNode *)clone_node((ASTNode *)o->fields[i], sub);
        }
        if (o->method_count) {
            c->methods = malloc(o->method_count * sizeof(MethodNode *));
            c->method_count = 0;
            for (int i = 0; i < o->method_count; i++) {
                MethodNode *m = o->methods[i];
                if (m && m->type_param_count > 0) {
                    g_mtmpls = realloc(g_mtmpls, (g_mtmpl_count + 1) * sizeof(MethodTmpl));
                    g_mtmpls[g_mtmpl_count++] = (MethodTmpl){ c, m, sub ? sub->names : NULL, sub ? sub->types : NULL, sub ? sub->count : 0 };
                    continue;
                }
                c->methods[c->method_count++] = (MethodNode *)clone_node((ASTNode *)m, sub);
            }
        }
        c->ctor_params = clone_list(o->ctor_params, o->ctor_param_count, sub);
        c->ctor_body = clone_list(o->ctor_body, o->ctor_body_count, sub);
        return (ASTNode *)c;
    }
    case NODE_VAR_DECL: {
        CLONE(VarDeclNode);
        c->var_type = clone_type(&c->var_type, sub);
        c->initializer = clone_node(c->initializer, sub);
        return (ASTNode *)c;
    }
    case NODE_STATIC_VAR: {
        CLONE(StaticVarNode);
        c->var_type = clone_type(&c->var_type, sub);
        c->initializer = clone_node(c->initializer, sub);
        return (ASTNode *)c;
    }
    case NODE_RETURN: { CLONE(ReturnNode); c->value = clone_node(c->value, sub); return (ASTNode *)c; }
    case NODE_BINARY_OP: {
        CLONE(BinaryOpNode);
        c->left = clone_node(c->left, sub);
        c->right = clone_node(c->right, sub);
        /* cast<T>(x) / x as T keep T as a string literal on the right */
        if (c->op && !strcmp(c->op, "cast") && c->right && c->right->type == NODE_LITERAL &&
            ((LiteralNode *)c->right)->lit_type == LIT_STRING) {
            LiteralNode *lit = malloc(sizeof(LiteralNode));
            memcpy(lit, c->right, sizeof(LiteralNode));
            lit->value = sub_type_name(lit->value, sub);
            c->right = (ASTNode *)lit;
        }
        return (ASTNode *)c;
    }
    case NODE_UNARY_OP: { CLONE(UnaryOpNode); c->operand = clone_node(c->operand, sub); return (ASTNode *)c; }
    case NODE_LITERAL: case NODE_IDENTIFIER: case NODE_BREAK: case NODE_CONTINUE:
    case NODE_ASM_BLOCK: case NODE_ENUM: case NODE_INTERP_STRING: case NODE_TEMP_REF:
    case NODE_INCLUDE: case NODE_CPP_INCLUDE: {
        size_t sz = n->type == NODE_LITERAL ? sizeof(LiteralNode) :
                    n->type == NODE_IDENTIFIER ? sizeof(IdentifierNode) :
                    n->type == NODE_BREAK ? sizeof(BreakNode) :
                    n->type == NODE_CONTINUE ? sizeof(ContinueNode) :
                    n->type == NODE_ASM_BLOCK ? sizeof(AsmBlockNode) :
                    n->type == NODE_ENUM ? sizeof(EnumNode) :
                    n->type == NODE_INTERP_STRING ? sizeof(InterpStringNode) :
                    n->type == NODE_TEMP_REF ? sizeof(TempRefNode) :
                    n->type == NODE_INCLUDE ? sizeof(IncludeNode) : sizeof(CppIncludeNode);
        ASTNode *c = malloc(sz);
        memcpy(c, n, sz);
        return c;
    }
    case NODE_MEMBER_ACCESS: { CLONE(MemberAccessNode); c->object = clone_node(c->object, sub); return (ASTNode *)c; }
    case NODE_MEMBER_ASSIGN: {
        CLONE(MemberAssignNode);
        c->object = clone_node(c->object, sub);
        c->value = clone_node(c->value, sub);
        return (ASTNode *)c;
    }
    case NODE_METHOD_CALL: {
        CLONE(MethodCallNode);
        c->object = clone_node(c->object, sub);
        c->args = clone_list(c->args, c->arg_count, sub);
        if (c->type_arg_count > 0) c->type_args = clone_types(c->type_args, c->type_arg_count, sub);
        return (ASTNode *)c;
    }
    case NODE_FUNC_CALL: {
        CLONE(FuncCallNode);
        c->args = clone_list(c->args, c->arg_count, sub);
        /* sizeof(T): the lone argument is a type name */
        if (c->name && !strcmp(c->name, "__size_of__") && c->arg_count == 1 &&
            c->args[0] && c->args[0]->type == NODE_IDENTIFIER) {
            IdentifierNode *id = malloc(sizeof(IdentifierNode));
            memcpy(id, c->args[0], sizeof(IdentifierNode));
            id->name = sub_type_name(id->name, sub);
            c->args[0] = (ASTNode *)id;
        }
        if (c->type_arg_count > 0) {
            Type *targs = clone_types(c->type_args, c->type_arg_count, sub);
            c->name = instantiate_func(c->name, targs, c->type_arg_count);
            c->type_args = NULL; c->type_arg_count = 0;
        }
        return (ASTNode *)c;
    }
    case NODE_LAMBDA: {
        CLONE(LambdaNode);
        c->params = clone_list(c->params, c->param_count, sub);
        c->ret = clone_type(&c->ret, sub);
        c->body = clone_list(c->body, c->body_count, sub);
        return (ASTNode *)c;
    }
    case NODE_NEW: {
        CLONE(NewNode);
        c->args = clone_list(c->args, c->arg_count, sub);
        if (c->type_arg_count > 0) {
            Type *targs = clone_types(c->type_args, c->type_arg_count, sub);
            c->class_name = instantiate_class(c->class_name, targs, c->type_arg_count);
            c->type_args = NULL; c->type_arg_count = 0;
        } else {
            c->class_name = sub_type_name(c->class_name, sub);
        }
        return (ASTNode *)c;
    }
    case NODE_STRUCT_LITERAL: {
        CLONE(StructLiteralNode);
        c->field_values = clone_list(c->field_values, c->field_count, sub);
        if (c->type_arg_count > 0) {
            Type *targs = clone_types(c->type_args, c->type_arg_count, sub);
            c->class_name = instantiate_class(c->class_name, targs, c->type_arg_count);
            c->type_args = NULL; c->type_arg_count = 0;
        } else {
            c->class_name = sub_type_name(c->class_name, sub);
        }
        return (ASTNode *)c;
    }
    case NODE_IF: {
        CLONE(IfNode);
        c->condition = clone_node(c->condition, sub);
        c->then_body = clone_list(c->then_body, c->then_count, sub);
        c->else_body = clone_list(c->else_body, c->else_count, sub);
        return (ASTNode *)c;
    }
    case NODE_WHILE: {
        CLONE(WhileNode);
        c->condition = clone_node(c->condition, sub);
        c->body = clone_list(c->body, c->body_count, sub);
        return (ASTNode *)c;
    }
    case NODE_FOR: {
        CLONE(ForNode);
        c->init = clone_node(c->init, sub);
        c->condition = clone_node(c->condition, sub);
        c->post = clone_node(c->post, sub);
        c->body = clone_list(c->body, c->body_count, sub);
        return (ASTNode *)c;
    }
    case NODE_FOR_IN: {
        CLONE(ForInNode);
        c->collection = clone_node(c->collection, sub);
        c->body = clone_list(c->body, c->body_count, sub);
        return (ASTNode *)c;
    }
    case NODE_ASSIGN: { CLONE(AssignNode); c->value = clone_node(c->value, sub); return (ASTNode *)c; }
    case NODE_COMPOUND_ASSIGN: { CLONE(CompoundAssignNode); c->value = clone_node(c->value, sub); return (ASTNode *)c; }
    case NODE_ARRAY_LITERAL: { CLONE(ArrayLiteralNode); c->elements = clone_list(c->elements, c->elem_count, sub); return (ASTNode *)c; }
    case NODE_INDEX: {
        CLONE(IndexNode);
        c->object = clone_node(c->object, sub);
        c->index = clone_node(c->index, sub);
        return (ASTNode *)c;
    }
    case NODE_INDEX_ASSIGN: {
        CLONE(IndexAssignNode);
        c->object = clone_node(c->object, sub);
        c->index = clone_node(c->index, sub);
        c->value = clone_node(c->value, sub);
        return (ASTNode *)c;
    }
    case NODE_UNSAFE: { CLONE(UnsafeBlockNode); c->body = clone_list(c->body, c->body_count, sub); return (ASTNode *)c; }
    case NODE_DEFER: { CLONE(DeferNode); c->body = clone_list(c->body, c->body_count, sub); return (ASTNode *)c; }
    case NODE_SWITCH: {
        CLONE(SwitchNode);
        c->subject = clone_node(c->subject, sub);
        if (c->case_count) {
            c->cases = malloc(c->case_count * sizeof(SwitchCaseNode *));
            for (int i = 0; i < c->case_count; i++)
                c->cases[i] = (SwitchCaseNode *)clone_node((ASTNode *)((SwitchNode *)n)->cases[i], sub);
        }
        return (ASTNode *)c;
    }
    case NODE_CASE: {
        CLONE(SwitchCaseNode);
        c->value = clone_node(c->value, sub);
        c->body = clone_list(c->body, c->body_count, sub);
        return (ASTNode *)c;
    }
    case NODE_TERNARY: {
        CLONE(TernaryNode);
        c->cond = clone_node(c->cond, sub);
        c->then_expr = clone_node(c->then_expr, sub);
        c->else_expr = clone_node(c->else_expr, sub);
        return (ASTNode *)c;
    }
    case NODE_TUPLE: {
        CLONE(TupleNode);
        c->elems = clone_list(c->elems, c->elem_count, sub);
        return (ASTNode *)c;
    }
    case NODE_TRY: { CLONE(TryNode); c->operand = clone_node(c->operand, sub); return (ASTNode *)c; }
    case NODE_SPLICE: { CLONE(SpliceNode); c->items = clone_list(c->items, c->count, sub); return (ASTNode *)c; }
    case NODE_MODULE: {
        ModuleNode *o = (ModuleNode *)n;
        CLONE(ModuleNode);
        if (o->func_count) {
            c->funcs = malloc(o->func_count * sizeof(FuncNode *));
            for (int i = 0; i < o->func_count; i++) c->funcs[i] = clone_func(o->funcs[i], sub);
        }
        if (o->static_count) {
            c->statics = malloc(o->static_count * sizeof(StaticVarNode *));
            for (int i = 0; i < o->static_count; i++)
                c->statics[i] = (StaticVarNode *)clone_node((ASTNode *)o->statics[i], sub);
        }
        return (ASTNode *)c;
    }
    default:
        return n;
    }
}

/* ── instances ───────────────────────────────────────────────────────────── */

static ClassNode *find_class_tmpl(const char *name) {
    for (int i = 0; i < g_class_tmpl_count; i++)
        if (!strcmp(g_class_tmpls[i]->name, name)) return g_class_tmpls[i];
    return NULL;
}

static FuncNode *find_func_tmpl(const char *name) {
    for (int i = 0; i < g_func_tmpl_count; i++)
        if (!strcmp(g_func_tmpls[i]->name, name)) return g_func_tmpls[i];
    return NULL;
}

static Instance *find_instance(const char *key) {
    for (int i = 0; i < g_inst_count; i++)
        if (!strcmp(g_insts[i].key, key)) return &g_insts[i];
    return NULL;
}

static void append_decl(ASTNode *d) {
    g_prog->declarations = realloc(g_prog->declarations, (g_prog->decl_count + 1) * sizeof(ASTNode *));
    g_prog->declarations[g_prog->decl_count++] = d;
}

static Instance *add_instance(char *key, int is_func, void *tmpl, Type *args, int nargs) {
    g_insts = realloc(g_insts, (g_inst_count + 1) * sizeof(Instance));
    Instance *in = &g_insts[g_inst_count++];
    in->key = key; in->is_func = is_func; in->tmpl = tmpl; in->args = args; in->nargs = nargs;
    return in;
}

/* the class `base<args>`; creates it on first use. `args` are concrete. */
static char *instantiate_class(const char *base, Type *args, int nargs) {
    char *key = mono_instance_name(base, args, nargs);
    if (find_instance(key)) return key;

    if (!strcmp(base, "__tuple")) {
        add_instance(key, 0, NULL, args, nargs);
        ClassNode *cn = make_class(key, 1);
        for (int i = 0; i < nargs; i++) {
            char fname[16];
            snprintf(fname, sizeof fname, "_%d", i);
            cn->fields = realloc(cn->fields, (cn->field_count + 1) * sizeof(FieldNode *));
            cn->fields[cn->field_count++] = make_field(args[i], fname, 1);
        }
        append_decl((ASTNode *)cn);
        if (g_hooks.on_class) g_hooks.on_class(cn);
        return key;
    }

    ClassNode *tmpl = find_class_tmpl(base);
    if (!tmpl) {
        diag_emit(DIAG_ERROR, g_file, 0, -1, NULL, "'%s' is not a generic class", base);
        goto placeholder;
    }
    if (tmpl->type_param_count != nargs) {
        diag_emit(DIAG_ERROR, g_file, tmpl->base.line, -1, NULL,
                  "'%s' takes %d type argument%s, but %d given", base, tmpl->type_param_count,
                  tmpl->type_param_count == 1 ? "" : "s", nargs);
        goto placeholder;
    }
    add_instance(key, 0, tmpl, args, nargs);
    Sub sub = { tmpl->type_params, args, nargs };
    ClassNode *cn = (ClassNode *)clone_node((ASTNode *)tmpl, &sub);
    cn->name = key;
    cn->type_params = NULL; cn->type_param_count = 0;
    append_decl((ASTNode *)cn);
    if (g_hooks.on_class) g_hooks.on_class(cn);
    return key;

placeholder:
    /* the error is reported; leave an empty class behind so the name is
       declared and nothing cascades from it */
    add_instance(key, 0, NULL, args, nargs);
    {
        ClassNode *dummy = make_class(key, 1);
        append_decl((ASTNode *)dummy);
        if (g_hooks.on_class) g_hooks.on_class(dummy);
    }
    return key;
}

static char *instantiate_func(const char *base, Type *args, int nargs) {
    char *key = mono_instance_name(base, args, nargs);
    if (find_instance(key)) return key;
    FuncNode *tmpl = find_func_tmpl(base);
    if (!tmpl) {
        diag_emit(DIAG_ERROR, g_file, 0, -1, NULL, "'%s' is not a generic function", base);
        return strdup(base);
    }
    if (tmpl->type_param_count != nargs) {
        diag_emit(DIAG_ERROR, g_file, tmpl->base.line, -1, NULL,
                  "'%s' takes %d type argument%s, but %d given", base, tmpl->type_param_count,
                  tmpl->type_param_count == 1 ? "" : "s", nargs);
        return strdup(base);
    }
    add_instance(key, 1, tmpl, args, nargs);
    Sub sub = { tmpl->type_params, args, nargs };
    FuncNode *fn = clone_func(tmpl, &sub);
    fn->name = key;
    fn->type_params = NULL; fn->type_param_count = 0;
    append_decl((ASTNode *)fn);
    if (g_hooks.on_func) g_hooks.on_func(fn);
    return key;
}

/* ── driver ──────────────────────────────────────────────────────────────── */

void mono_request(const Type *t) {
    g_used = 1;
    g_requests = realloc(g_requests, (g_request_count + 1) * sizeof(Type));
    g_requests[g_request_count++] = *t;
}

void mono_run(ProgramNode *prog, const char *filename) {
    g_prog = prog;
    g_file = filename ? filename : "<unknown>";
    g_class_tmpl_count = g_func_tmpl_count = g_inst_count = g_mtmpl_count = 0;
    /* pull the templates out of the program */
    int w = 0;
    for (int i = 0; i < prog->decl_count; i++) {
        ASTNode *d = prog->declarations[i];
        if (d && d->type == NODE_CLASS && ((ClassNode *)d)->type_param_count > 0) {
            g_class_tmpls = realloc(g_class_tmpls, (g_class_tmpl_count + 1) * sizeof(ClassNode *));
            g_class_tmpls[g_class_tmpl_count++] = (ClassNode *)d;
            g_used = 1;
        } else if (d && d->type == NODE_FUNC && ((FuncNode *)d)->type_param_count > 0) {
            g_func_tmpls = realloc(g_func_tmpls, (g_func_tmpl_count + 1) * sizeof(FuncNode *));
            g_func_tmpls[g_func_tmpl_count++] = (FuncNode *)d;
            g_used = 1;
        } else {
            prog->declarations[w++] = d;
        }
    }
    prog->decl_count = w;
    if (!g_used) return;

    for (int i = 0; i < g_request_count; i++) {
        Type t = clone_type(&g_requests[i], NULL);
        (void)t;
    }
    int n = prog->decl_count;   /* instances appended below are already resolved */
    for (int i = 0; i < n; i++) {
        /* cloning can append instances (and move the array): index afterwards */
        ASTNode *cloned = clone_node(prog->declarations[i], NULL);
        prog->declarations[i] = cloned;
    }
}

/* ── typechecker interface ───────────────────────────────────────────────── */

int mono_is_generic_func(const char *name) { return name && find_func_tmpl(name) != NULL; }

const char *mono_generic_param(const char *name, int i) {
    FuncNode *f = find_func_tmpl(name);
    return f && i < f->type_param_count ? f->type_params[i] : NULL;
}

/* pattern `pat` (mentioning template parameters) against a concrete type */
static int unify(char **tparams, int tcount, const Type *pat, const Type *act, Type *bound, int *have) {
    if (pat->kind == TYPE_SIMPLE && pat->name && !pat->is_fn && pat->type_arg_count == 0) {
        for (int i = 0; i < tcount; i++)
            if (!strcmp(tparams[i], pat->name)) {
                if (!have[i]) {
                    bound[i] = *act;
                    /* `T?` against a nullable argument binds the non-nullable T */
                    if (pat->nullable) bound[i].nullable = 0;
                    have[i] = 1;
                    return 1;
                }
                return 1;   /* already bound: the argument-compatibility check is the typechecker's job */
            }
        return 1;
    }
    if (pat->kind != act->kind) return 1;
    if (pat->kind == TYPE_ARRAY || pat->kind == TYPE_REF || pat->kind == TYPE_RAWPTR) {
        if (pat->elem_type_count > 0 && act->elem_type_count > 0)
            return unify(tparams, tcount, &pat->elem_types[0], &act->elem_types[0], bound, have);
        return 1;
    }
    if (pat->is_fn && act->is_fn && pat->elem_type_count == act->elem_type_count) {
        for (int i = 0; i < pat->elem_type_count; i++)
            unify(tparams, tcount, &pat->elem_types[i], &act->elem_types[i], bound, have);
        if (pat->fn_ret && act->fn_ret) unify(tparams, tcount, pat->fn_ret, act->fn_ret, bound, have);
        return 1;
    }
    /* Box<T> against the already-instantiated Box_of_int */
    if (pat->kind == TYPE_SIMPLE && pat->type_arg_count > 0 && act->name) {
        Instance *in = find_instance(act->name);
        if (in && in->nargs == pat->type_arg_count)
            for (int i = 0; i < in->nargs; i++)
                unify(tparams, tcount, &pat->type_args[i], &in->args[i], bound, have);
    }
    return 1;
}

const char *mono_instantiate_inferred(const char *name, const Type *argtypes, int argc, char *err, size_t errn) {
    FuncNode *tmpl = find_func_tmpl(name);
    if (!tmpl) return NULL;
    Type bound[16]; int have[16] = {0};
    if (tmpl->type_param_count > 16) { snprintf(err, errn, "too many type parameters"); return NULL; }
    int n = argc < tmpl->param_count ? argc : tmpl->param_count;
    for (int i = 0; i < n; i++) {
        VarDeclNode *p = (VarDeclNode *)tmpl->params[i];
        unify(tmpl->type_params, tmpl->type_param_count, &p->var_type, &argtypes[i], bound, have);
    }
    for (int i = 0; i < tmpl->type_param_count; i++)
        if (!have[i]) {
            snprintf(err, errn, "cannot infer type parameter '%s' of '%s' from the arguments; write %s<...>(...)",
                     tmpl->type_params[i], name, name);
            return NULL;
        }
    Type *args = malloc(tmpl->type_param_count * sizeof(Type));
    for (int i = 0; i < tmpl->type_param_count; i++) args[i] = bound[i];
    return instantiate_func(name, args, tmpl->type_param_count);
}

const char *mono_tuple_class(const Type *elems, int n) {
    Type *args = malloc(n * sizeof(Type));
    for (int i = 0; i < n; i++) args[i] = elems[i];
    return instantiate_class("__tuple", args, n);
}

int mono_tuple_info(const char *cls, int *count) {
    if (!cls || strncmp(cls, "__tuple_of_", 11) != 0) return 0;
    Instance *in = find_instance(cls);
    if (!in) return 0;
    if (count) *count = in->nargs;
    return 1;
}

const Type *mono_tuple_elem(const char *cls, int i) {
    Instance *in = find_instance(cls);
    return in && i < in->nargs ? &in->args[i] : NULL;
}

int mono_is_generic_class(const char *name) { return name && find_class_tmpl(name) != NULL; }

/* `new Box(41)`: infer the class's type arguments from the constructor's
   parameter types. Returns the instance's class name, or NULL with a message. */
const char *mono_instantiate_class_inferred(const char *name, const Type *argtypes, int argc,
                                            char *err, size_t errn) {
    ClassNode *tmpl = find_class_tmpl(name);
    if (!tmpl) return NULL;
    Type bound[16]; int have[16] = {0};
    if (tmpl->type_param_count > 16) { snprintf(err, errn, "too many type parameters"); return NULL; }
    int n = argc < tmpl->ctor_param_count ? argc : tmpl->ctor_param_count;
    for (int i = 0; i < n; i++) {
        VarDeclNode *p = (VarDeclNode *)tmpl->ctor_params[i];
        unify(tmpl->type_params, tmpl->type_param_count, &p->var_type, &argtypes[i], bound, have);
    }
    for (int i = 0; i < tmpl->type_param_count; i++)
        if (!have[i]) {
            snprintf(err, errn, "cannot infer type parameter '%s' of class '%s' from the constructor arguments; write new %s<...>(...)",
                     tmpl->type_params[i], name, name);
            return NULL;
        }
    Type *args = malloc(tmpl->type_param_count * sizeof(Type));
    for (int i = 0; i < tmpl->type_param_count; i++) args[i] = bound[i];
    return instantiate_class(name, args, tmpl->type_param_count);
}

/* ── generic methods ─────────────────────────────────────────────────────── */

static MethodTmpl *find_method_tmpl(const char *cls, const char *name) {
    for (int i = 0; i < g_mtmpl_count; i++)
        if (!strcmp(g_mtmpls[i].cn->name, cls) && !strcmp(g_mtmpls[i].tmpl->name, name)) return &g_mtmpls[i];
    return NULL;
}

int mono_is_generic_method(const char *cls, const char *name) {
    return cls && name && find_method_tmpl(cls, name) != NULL;
}

/* `obj.map<U>(f)`: add the instance to the class and return its method name.
   Explicit type arguments win; otherwise they are inferred from the argument
   types, like a generic function's. */
const char *mono_instantiate_method(const char *cls, const char *name, const Type *targs, int ntargs,
                                    const Type *argtypes, int argc, char *err, size_t errn) {
    MethodTmpl *mt = find_method_tmpl(cls, name);
    if (!mt) return NULL;
    MethodNode *tm = mt->tmpl;
    Type bound[16]; int have[16] = {0};
    if (tm->type_param_count > 16) { snprintf(err, errn, "too many type parameters"); return NULL; }
    if (ntargs > 0) {
        if (ntargs != tm->type_param_count) {
            snprintf(err, errn, "'%s' takes %d type argument%s, but %d given", name, tm->type_param_count,
                     tm->type_param_count == 1 ? "" : "s", ntargs);
            return NULL;
        }
        for (int i = 0; i < ntargs; i++) { bound[i] = targs[i]; have[i] = 1; }
    } else {
        int n = argc < tm->param_count ? argc : tm->param_count;
        for (int i = 0; i < n; i++)
            unify(tm->type_params, tm->type_param_count, &((VarDeclNode *)tm->params[i])->var_type, &argtypes[i], bound, have);
        for (int i = 0; i < tm->type_param_count; i++)
            if (!have[i]) {
                snprintf(err, errn, "cannot infer type parameter '%s' of '%s' from the arguments; write %s<...>(...)",
                         tm->type_params[i], name, name);
                return NULL;
            }
    }
    Type *args = malloc(tm->type_param_count * sizeof(Type));
    for (int i = 0; i < tm->type_param_count; i++) args[i] = bound[i];
    char *inst = mono_instance_name(name, args, tm->type_param_count);
    char key[600];
    snprintf(key, sizeof key, "%s__%s", cls, inst);
    if (find_instance(key)) return inst;
    add_instance(strdup(key), 1, tm, args, tm->type_param_count);

    /* the class's own substitution plus the method's */
    int total = mt->count + tm->type_param_count;
    char **names = malloc(total * sizeof(char *));
    Type *types = malloc(total * sizeof(Type));
    for (int i = 0; i < mt->count; i++) { names[i] = mt->names[i]; types[i] = mt->types[i]; }
    for (int i = 0; i < tm->type_param_count; i++) { names[mt->count + i] = tm->type_params[i]; types[mt->count + i] = args[i]; }
    Sub sub = { names, types, total };
    MethodNode *m = (MethodNode *)clone_node((ASTNode *)tm, &sub);
    m->name = inst;
    m->type_params = NULL; m->type_param_count = 0;
    ClassNode *cn = mt->cn;
    cn->methods = realloc(cn->methods, (cn->method_count + 1) * sizeof(MethodNode *));
    cn->methods[cn->method_count++] = m;
    if (g_hooks.on_method) g_hooks.on_method(cn, m);
    return inst;
}
