#include "codegen.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

/* ─── Nullable variable tracker ─────────────────────────────────────────────
   Tracks which variable names in the current scope are pointer (nullable)
   types, so method calls on them emit -> instead of .              */

#define NULLABLE_MAX 256
static char *_nullable_vars[NULLABLE_MAX];
static int   _nullable_count = 0;

static void nullable_push(const char *name) {
    if (_nullable_count < NULLABLE_MAX)
        _nullable_vars[_nullable_count++] = strdup(name);
}

static void nullable_clear(void) {
    for (int i = 0; i < _nullable_count; i++) free(_nullable_vars[i]);
    _nullable_count = 0;
}

static int nullable_is(const char *name) {
    for (int i = 0; i < _nullable_count; i++)
        if (strcmp(_nullable_vars[i], name) == 0) return 1;
    return 0;
}

/* Returns 1 if the expr is a nullable/pointer identifier */
static int expr_is_nullable(ASTNode *expr) {
    if (!expr) return 0;
    if (expr->type == NODE_IDENTIFIER)
        return nullable_is(((IdentifierNode *)expr)->name);
    return 0;
}

static const char *map_type(const char *t) {
    if (strcmp(t, "str") == 0)  return "std::string";
    if (strcmp(t, "bool") == 0) return "bool";
    if (strcmp(t, "auto") == 0) return "auto";
    return t;
}

/* Returns a heap-allocated C++ type string for a Type */
char *codegen_type(Type t) {
    if (t.kind == TYPE_SIMPLE) {
        const char *base = map_type(t.name);
        if (t.nullable) {
            /* pointer type: "T*" */
            size_t len = strlen(base) + 2;
            char *result = malloc(len);
            snprintf(result, len, "%s*", base);
            return result;
        } else {
            return strdup(base);
        }
    }

    if (t.kind == TYPE_ARRAY) {
        /* Get element type string */
        char *elem_str = codegen_type(t.elem_types[0]);
        char *result;
        if (t.fixed_size > 0) {
            /* std::array<T, N> */
            size_t len = strlen("std::array<") + strlen(elem_str) + 32;
            result = malloc(len);
            snprintf(result, len, "std::array<%s, %d>", elem_str, t.fixed_size);
        } else {
            /* std::vector<T> */
            size_t len = strlen("std::vector<") + strlen(elem_str) + 2;
            result = malloc(len);
            snprintf(result, len, "std::vector<%s>", elem_str);
        }
        free(elem_str);
        return result;
    }

    if (t.kind == TYPE_MULTI) {
        char *inner;
        if (t.is_any) {
            inner = strdup("std::any");
        } else {
            /* Build std::variant<A, B, ...> */
            /* First compute total length */
            char **elem_strs = malloc(t.elem_type_count * sizeof(char *));
            size_t total = strlen("std::variant<") + 1;
            for (int i = 0; i < t.elem_type_count; i++) {
                elem_strs[i] = codegen_type(t.elem_types[i]);
                total += strlen(elem_strs[i]) + 2; /* ", " */
            }
            inner = malloc(total);
            strcpy(inner, "std::variant<");
            for (int i = 0; i < t.elem_type_count; i++) {
                strcat(inner, elem_strs[i]);
                if (i < t.elem_type_count - 1) strcat(inner, ", ");
                free(elem_strs[i]);
            }
            strcat(inner, ">");
            free(elem_strs);
        }

        char *result;
        if (t.fixed_size > 0) {
            size_t len = strlen("std::array<") + strlen(inner) + 32;
            result = malloc(len);
            snprintf(result, len, "std::array<%s, %d>", inner, t.fixed_size);
        } else {
            size_t len = strlen("std::vector<") + strlen(inner) + 2;
            result = malloc(len);
            snprintf(result, len, "std::vector<%s>", inner);
        }
        free(inner);
        return result;
    }

    return strdup("/* unknown type */");
}

static void indent(int depth, FILE *out) {
    for (int i = 0; i < depth; i++) fprintf(out, "    ");
}

void codegen_args(ASTNode **args, int count, FILE *out) {
    for (int i = 0; i < count; i++) {
        codegen_expr(args[i], out);
        if (i < count - 1) fprintf(out, ", ");
    }
}

static void emit_params(ASTNode **params, int count, FILE *out) {
    for (int i = 0; i < count; i++) {
        VarDeclNode *p = (VarDeclNode *)params[i];
        char *type_str = codegen_type(p->var_type);
        fprintf(out, "%s %s", type_str, p->var_name);
        free(type_str);
        if (i < count - 1) fprintf(out, ", ");
    }
}

static void emit_body(ASTNode **body, int count, FILE *out, int depth) {
    for (int i = 0; i < count; i++) {
        indent(depth, out);
        codegen_stmt(body[i], out, depth);
    }
}

/* Map a dot-separated Hylian include path to a runtime header path.
   e.g. "std.io" -> "../runtime/std/io.hpp"
   Returns a heap-allocated string, or NULL if not a known std path. */
static char *resolve_include(const char *path) {
    /* Only handle paths that start with "std." */
    if (strncmp(path, "std.", 4) != 0) return NULL;
    const char *module = path + 4; /* e.g. "io", "errors", "string", "math" */

    /* Replace any further dots with slashes for nested modules */
    char module_path[256];
    int j = 0;
    for (int i = 0; module[i] && j < 254; i++, j++)
        module_path[j] = (module[i] == '.') ? '/' : module[i];
    module_path[j] = '\0';

    /* Build the final include string */
    char *result = malloc(strlen(module_path) + 32);
    sprintf(result, "runtime/std/%s.hpp", module_path);
    return result;
}

static int includes_path(ProgramNode *root, const char *path) {
    for (int i = 0; i < root->include_count; i++)
        if (strcmp(root->includes[i], path) == 0) return 1;
    return 0;
}

void codegen(ProgramNode *root, FILE *out) {
    fprintf(out, "// Generated by Hylian compiler\n");
    fprintf(out, "#include <string>\n");
    fprintf(out, "#include <sstream>\n");
    fprintf(out, "#include <memory>\n");
    fprintf(out, "#include <optional>\n");
    fprintf(out, "#include <functional>\n");
    fprintf(out, "#include <vector>\n");
    fprintf(out, "#include <array>\n");
    fprintf(out, "#include <variant>\n");
    fprintf(out, "#include <any>\n");
    fprintf(out, "\n");

    /* Emit resolved std includes */
    int has_std_include = 0;
    for (int i = 0; i < root->include_count; i++) {
        char *header = resolve_include(root->includes[i]);
        if (header) {
            fprintf(out, "#include \"%s\"\n", header);
            free(header);
            has_std_include = 1;
        }
    }
    if (has_std_include) fprintf(out, "\n");

    /* Emit ccpinclude paths as raw #include directives */
    for (int i = 0; i < root->cpp_include_count; i++) {
        fprintf(out, "#include \"%s\"\n", root->cpp_includes[i]);
    }
    if (root->cpp_include_count > 0) fprintf(out, "\n");

    /* Only emit the inline Error struct if no std includes are present
       (all std headers include errors.hpp, so it would be a redefinition) */
    if (!has_std_include) {
        fprintf(out, "struct Error {\n");
        fprintf(out, "    std::string message;\n");
        fprintf(out, "    Error(const std::string& msg) : message(msg) {}\n");
        fprintf(out, "};\n\n");
    }

    /* Forward declare classes */
    for (int i = 0; i < root->decl_count; i++) {
        if (root->declarations[i]->type == NODE_CLASS)
            fprintf(out, "class %s;\n", ((ClassNode *)root->declarations[i])->name);
    }
    fprintf(out, "\n");

    for (int i = 0; i < root->decl_count; i++) {
        ASTNode *d = root->declarations[i];
        if (d->type == NODE_CLASS) codegen_class((ClassNode *)d, out);
    }
    for (int i = 0; i < root->decl_count; i++) {
        ASTNode *d = root->declarations[i];
        if (d->type == NODE_FUNC) codegen_func((FuncNode *)d, out);
    }
}

void codegen_class(ClassNode *cls, FILE *out) {
    fprintf(out, "class %s {\n", cls->name);

    /* Private fields */
    int has_private = 0;
    for (int i = 0; i < cls->field_count; i++)
        if (!cls->fields[i]->is_public) { has_private = 1; break; }

    if (has_private) {
        fprintf(out, "private:\n");
        for (int i = 0; i < cls->field_count; i++)
            if (!cls->fields[i]->is_public) {
                fprintf(out, "    ");
                codegen_field(cls->fields[i], out);
            }
    }

    fprintf(out, "public:\n");

    /* Public fields */
    for (int i = 0; i < cls->field_count; i++)
        if (cls->fields[i]->is_public) {
            fprintf(out, "    ");
            codegen_field(cls->fields[i], out);
        }

    /* Constructor */
    if (cls->has_ctor) {
        fprintf(out, "    %s(", cls->name);
        emit_params(cls->ctor_params, cls->ctor_param_count, out);
        fprintf(out, ") {\n");
        emit_body(cls->ctor_body, cls->ctor_body_count, out, 2);
        fprintf(out, "    }\n");
    }

    /* Methods */
    for (int i = 0; i < cls->method_count; i++) {
        fprintf(out, "    ");
        codegen_method(cls->methods[i], out);
    }

    fprintf(out, "};\n\n");
}

void codegen_field(FieldNode *field, FILE *out) {
    char *type_str = codegen_type(field->field_type);
    fprintf(out, "%s %s;\n", type_str, field->name);
    free(type_str);
}

void codegen_method(MethodNode *method, FILE *out) {
    /* reset nullable tracking per method */
    nullable_clear();

    char *ret = codegen_type(method->return_type);
    fprintf(out, "%s %s(", ret, method->name);
    free(ret);
    emit_params(method->params, method->param_count, out);
    fprintf(out, ") {\n");
    emit_body(method->body, method->body_count, out, 2);
    fprintf(out, "    }\n");
}

static int is_main_func(FuncNode *func) {
    return strcmp(func->name, "main") == 0;
}

static void emit_body_main(ASTNode **body, int count, FILE *out, int depth) {
    for (int i = 0; i < count; i++) {
        /* Convert returns inside main to proper process exit codes:
           - return nil / return;  -> return 0
           - return <non-nil>      -> return 1
        */
        if (body[i]->type == NODE_RETURN) {
            ReturnNode *r = (ReturnNode *)body[i];

            /* bare return; or return nil; */
            if (!r->value ||
                (r->value->type == NODE_LITERAL &&
                 ((LiteralNode *)r->value)->lit_type == LIT_NIL)) {
                indent(depth, out);
                fprintf(out, "return 0;\n");
                continue;
            }

            /* any non-nil return => failure exit code */
            indent(depth, out);
            fprintf(out, "return 1;\n");
            continue;
        }

        indent(depth, out);
        codegen_stmt(body[i], out, depth);
    }
}

/* Returns 1 if main has a single array<str> parameter (args) */
static int main_takes_args(FuncNode *func) {
    if (func->param_count != 1) return 0;
    VarDeclNode *p = (VarDeclNode *)func->params[0];
    return p->var_type.kind == TYPE_ARRAY &&
           p->var_type.elem_type_count == 1 &&
           p->var_type.elem_types[0].kind == TYPE_SIMPLE &&
           strcmp(p->var_type.elem_types[0].name, "str") == 0;
}

void codegen_func(FuncNode *func, FILE *out) {
    /* reset nullable tracking per function */
    nullable_clear();

    /* Special-case: void main() or Error? main() -> int main() */
    if (is_main_func(func)) {
        if (main_takes_args(func)) {
            /* main(array<str> args) -> int main(int argc, char** argv) */
            VarDeclNode *p = (VarDeclNode *)func->params[0];
            fprintf(out, "int main(int argc, char** argv) {\n");
            fprintf(out, "    std::vector<std::string> %s(argv + 1, argv + argc);\n", p->var_name);
        } else {
            fprintf(out, "int main(");
            emit_params(func->params, func->param_count, out);
            fprintf(out, ") {\n");
        }
        emit_body_main(func->body, func->body_count, out, 1);
        fprintf(out, "}\n\n");
        return;
    }

    char *ret = codegen_type(func->return_type);
    fprintf(out, "%s %s(", ret, func->name);
    free(ret);
    emit_params(func->params, func->param_count, out);
    fprintf(out, ") {\n");
    emit_body(func->body, func->body_count, out, 1);
    fprintf(out, "}\n\n");
}

void codegen_stmt(ASTNode *stmt, FILE *out, int depth) {
    switch (stmt->type) {
    case NODE_RETURN: {
        ReturnNode *r = (ReturnNode *)stmt;
        fprintf(out, "return");
        if (r->value) { fprintf(out, " "); codegen_expr(r->value, out); }
        fprintf(out, ";\n");
        break;
    }
    case NODE_VAR_DECL: {
        VarDeclNode *v = (VarDeclNode *)stmt;
        /* Track explicitly nullable variables so method calls on them emit -> */
        if (v->var_type.nullable) nullable_push(v->var_name);
        char *type_str = codegen_type(v->var_type);
        fprintf(out, "%s %s", type_str, v->var_name);
        free(type_str);
        if (v->initializer) {
            /* For array literals, use brace-init directly */
            if (v->initializer->type == NODE_ARRAY_LITERAL) {
                fprintf(out, " = {");
                ArrayLiteralNode *al = (ArrayLiteralNode *)v->initializer;
                for (int i = 0; i < al->elem_count; i++) {
                    codegen_expr(al->elements[i], out);
                    if (i < al->elem_count - 1) fprintf(out, ", ");
                }
                fprintf(out, "}");
            } else {
                fprintf(out, " = ");
                codegen_expr(v->initializer, out);
            }
        }
        fprintf(out, ";\n");
        break;
    }
    case NODE_ASSIGN: {
        AssignNode *a = (AssignNode *)stmt;
        fprintf(out, "%s = ", a->var_name);
        codegen_expr(a->value, out);
        fprintf(out, ";\n");
        break;
    }
    case NODE_COMPOUND_ASSIGN: {
        CompoundAssignNode *ca = (CompoundAssignNode *)stmt;
        fprintf(out, "%s %s ", ca->var_name, ca->op);
        codegen_expr(ca->value, out);
        fprintf(out, ";\n");
        break;
    }
    case NODE_MEMBER_ASSIGN: {
        MemberAssignNode *ma = (MemberAssignNode *)stmt;
        if (ma->object) { codegen_expr(ma->object, out); fprintf(out, "."); }
        fprintf(out, "%s = ", ma->member);
        codegen_expr(ma->value, out);
        fprintf(out, ";\n");
        break;
    }
    case NODE_INDEX_ASSIGN: {
        IndexAssignNode *ia = (IndexAssignNode *)stmt;
        codegen_expr(ia->object, out);
        fprintf(out, "[");
        codegen_expr(ia->index, out);
        fprintf(out, "] = ");
        codegen_expr(ia->value, out);
        fprintf(out, ";\n");
        break;
    }
    case NODE_IF: {
        IfNode *ifn = (IfNode *)stmt;
        fprintf(out, "if (");
        codegen_expr(ifn->condition, out);
        fprintf(out, ") {\n");
        emit_body(ifn->then_body, ifn->then_count, out, depth + 1);
        indent(depth, out);
        fprintf(out, "}");
        if (ifn->else_count > 0) {
            fprintf(out, " else {\n");
            emit_body(ifn->else_body, ifn->else_count, out, depth + 1);
            indent(depth, out);
            fprintf(out, "}");
        }
        fprintf(out, "\n");
        break;
    }
    case NODE_WHILE: {
        WhileNode *wn = (WhileNode *)stmt;
        fprintf(out, "while (");
        codegen_expr(wn->condition, out);
        fprintf(out, ") {\n");
        emit_body(wn->body, wn->body_count, out, depth + 1);
        indent(depth, out);
        fprintf(out, "}\n");
        break;
    }
    case NODE_FOR_IN: {
        ForInNode *fi = (ForInNode *)stmt;
        if (fi->use_ref)
            fprintf(out, "for (auto& %s : ", fi->var_name);
        else
            fprintf(out, "for (auto %s : ", fi->var_name);
        codegen_expr(fi->collection, out);
        fprintf(out, ") {\n");
        emit_body(fi->body, fi->body_count, out, depth + 1);
        indent(depth, out);
        fprintf(out, "}\n");
        break;
    }
    case NODE_FOR: {
        ForNode *fn = (ForNode *)stmt;
        fprintf(out, "for (");
        if (fn->init) {
            if (fn->init->type == NODE_VAR_DECL) {
                VarDeclNode *v = (VarDeclNode *)fn->init;
                char *type_str = codegen_type(v->var_type);
                fprintf(out, "%s %s", type_str, v->var_name);
                free(type_str);
                if (v->initializer) { fprintf(out, " = "); codegen_expr(v->initializer, out); }
            } else if (fn->init->type == NODE_ASSIGN) {
                AssignNode *a = (AssignNode *)fn->init;
                fprintf(out, "%s = ", a->var_name);
                codegen_expr(a->value, out);
            } else {
                codegen_expr(fn->init, out);
            }
        }
        fprintf(out, "; ");
        if (fn->condition) codegen_expr(fn->condition, out);
        fprintf(out, "; ");
        if (fn->post) codegen_expr(fn->post, out);
        fprintf(out, ") {\n");
        emit_body(fn->body, fn->body_count, out, depth + 1);
        indent(depth, out);
        fprintf(out, "}\n");
        break;
    }
    case NODE_BREAK:
        fprintf(out, "break;\n");
        break;
    case NODE_CONTINUE:
        fprintf(out, "continue;\n");
        break;
    case NODE_DEFER: {
        DeferNode *dn = (DeferNode *)stmt;
        fprintf(out, "struct _D%p{std::function<void()>f;~_D%p(){f();}}_d%p{[&](){",
                (void*)stmt,(void*)stmt,(void*)stmt);
        codegen_expr(dn->expr, out);
        fprintf(out, ";}}; \n");
        break;
    }
    default:
        codegen_expr(stmt, out);
        fprintf(out, ";\n");
        break;
    }
}

void codegen_expr(ASTNode *expr, FILE *out) {
    switch (expr->type) {
    case NODE_LITERAL: {
        LiteralNode *lit = (LiteralNode *)expr;
        if (lit->lit_type == LIT_NIL)  fprintf(out, "nullptr");
        else                            fprintf(out, "%s", lit->value);
        break;
    }
    case NODE_IDENTIFIER:
        fprintf(out, "%s", ((IdentifierNode *)expr)->name);
        break;
    case NODE_BINARY_OP: {
        BinaryOpNode *b = (BinaryOpNode *)expr;
        fprintf(out, "(");
        codegen_expr(b->left, out);
        fprintf(out, " %s ", b->op);
        codegen_expr(b->right, out);
        fprintf(out, ")");
        break;
    }
    case NODE_UNARY_OP: {
        UnaryOpNode *u = (UnaryOpNode *)expr;
        if (u->postfix) {
            codegen_expr(u->operand, out);
            fprintf(out, "%s", u->op);
        } else {
            fprintf(out, "%s", u->op);
            codegen_expr(u->operand, out);
        }
        break;
    }
    case NODE_MEMBER_ACCESS: {
        MemberAccessNode *m = (MemberAccessNode *)expr;
        codegen_expr(m->object, out);
        fprintf(out, ".%s", m->member);
        break;
    }
    case NODE_METHOD_CALL: {
        MethodCallNode *c = (MethodCallNode *)expr;
        codegen_expr(c->object, out);
        /* Use -> for nullable/pointer types, . for value types */
        if (expr_is_nullable(c->object))
            fprintf(out, "->%s(", c->method);
        else
            fprintf(out, ".%s(", c->method);
        codegen_args(c->args, c->arg_count, out);
        fprintf(out, ")");
        break;
    }
    case NODE_FUNC_CALL: {
        FuncCallNode *c = (FuncCallNode *)expr;
        fprintf(out, "%s(", c->name);
        codegen_args(c->args, c->arg_count, out);
        fprintf(out, ")");
        break;
    }
    case NODE_NEW: {
        NewNode *n = (NewNode *)expr;
        fprintf(out, "%s(", n->class_name);
        codegen_args(n->args, n->arg_count, out);
        fprintf(out, ")");
        break;
    }
    case NODE_MEMBER_ASSIGN: {
        MemberAssignNode *ma = (MemberAssignNode *)expr;
        if (ma->object) { codegen_expr(ma->object, out); fprintf(out, "."); }
        fprintf(out, "%s = ", ma->member);
        codegen_expr(ma->value, out);
        break;
    }
    case NODE_ARRAY_LITERAL: {
        ArrayLiteralNode *al = (ArrayLiteralNode *)expr;
        fprintf(out, "{");
        for (int i = 0; i < al->elem_count; i++) {
            codegen_expr(al->elements[i], out);
            if (i < al->elem_count - 1) fprintf(out, ", ");
        }
        fprintf(out, "}");
        break;
    }
    case NODE_INDEX: {
        IndexNode *idx = (IndexNode *)expr;
        codegen_expr(idx->object, out);
        fprintf(out, "[");
        codegen_expr(idx->index, out);
        fprintf(out, "]");
        break;
    }
    case NODE_INTERP_STRING: {
        InterpStringNode *is = (InterpStringNode *)expr;
        /* Use ostringstream so any streamable type works (int, str, float, etc.) */
        fprintf(out, "([&]() -> std::string { std::ostringstream _oss; ");
        for (int i = 0; i < is->seg_count; i++) {
            InterpSegment *seg = &is->segments[i];
            if (seg->is_expr) {
                fprintf(out, "_oss << (%s); ", seg->text);
            } else {
                fprintf(out, "_oss << \"");
                for (char *c = seg->text; *c; c++) {
                    if (*c == '"')       fprintf(out, "\\\"");
                    else if (*c == '\\') fprintf(out, "\\\\");
                    else if (*c == '\n') fprintf(out, "\\n");
                    else if (*c == '\t') fprintf(out, "\\t");
                    else                 fputc(*c, out);
                }
                fprintf(out, "\"; ");
            }
        }
        fprintf(out, "return _oss.str(); }())");
        break;
    }
    default:
        fprintf(out, "/* unknown expr */");
        break;
    }
}
