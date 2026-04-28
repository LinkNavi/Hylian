#include "ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declaration */
static void zero_resolved_type(ASTNode *n);

/* Parse a raw interpolated string (with surrounding quotes) into segments.
   "hello {{name}}, you have {{count}} items!"
   becomes: [lit:"hello ", expr:"name", lit:", you have ", expr:"count", lit:" items!"] */
InterpStringNode *make_interp_string(const char *raw) {
    InterpStringNode *n = malloc(sizeof(InterpStringNode));
    n->base.type = NODE_INTERP_STRING;
    zero_resolved_type(&n->base);
    n->segments = NULL;
    n->seg_count = 0;

    /* strip surrounding quotes */
    int raw_len = strlen(raw);
    char *inner = malloc(raw_len - 1);  /* raw_len - 2 chars + NUL */
    strncpy(inner, raw + 1, raw_len - 2);
    inner[raw_len - 2] = '\0';

    /* walk through, splitting on {{ and }} */
    char *p = inner;
    char buf[4096];
    int buf_len = 0;

    while (*p) {
        if (p[0] == '{' && p[1] == '{') {
            /* flush any accumulated literal */
            if (buf_len > 0) {
                buf[buf_len] = '\0';
                n->segments = realloc(n->segments, (n->seg_count + 1) * sizeof(InterpSegment));
                n->segments[n->seg_count].is_expr = 0;
                n->segments[n->seg_count].text = strdup(buf);
                n->seg_count++;
                buf_len = 0;
            }
            p += 2; /* skip {{ */
            /* collect until }} */
            while (*p && !(p[0] == '}' && p[1] == '}')) {
                buf[buf_len++] = *p++;
            }
            buf[buf_len] = '\0';
            n->segments = realloc(n->segments, (n->seg_count + 1) * sizeof(InterpSegment));
            n->segments[n->seg_count].is_expr = 1;
            n->segments[n->seg_count].text = strdup(buf);
            n->seg_count++;
            buf_len = 0;
            if (p[0] == '}' && p[1] == '}') p += 2; /* skip }} */
        } else {
            buf[buf_len++] = *p++;
        }
    }
    /* flush trailing literal */
    if (buf_len > 0) {
        buf[buf_len] = '\0';
        n->segments = realloc(n->segments, (n->seg_count + 1) * sizeof(InterpSegment));
        n->segments[n->seg_count].is_expr = 0;
        n->segments[n->seg_count].text = strdup(buf);
        n->seg_count++;
    }

    free(inner);
    return n;
}

ProgramNode *make_program() {
    ProgramNode *n = malloc(sizeof(ProgramNode));
    n->base.type = NODE_PROGRAM;
    zero_resolved_type(&n->base);
    n->declarations = NULL; n->decl_count = 0;
    n->includes = NULL; n->include_count = 0;
    n->cpp_includes = NULL; n->cpp_include_count = 0;
    return n;
}

EnumNode *make_enum(char *name, int is_public) {
    EnumNode *n = malloc(sizeof(EnumNode));
    n->base.type = NODE_ENUM;
    zero_resolved_type(&n->base);
    n->name = strdup(name);
    n->is_public = is_public;
    n->variants = NULL;
    n->variant_count = 0;
    return n;
}

ClassNode *make_class(char *name, int is_public) {
    ClassNode *n = malloc(sizeof(ClassNode));
    n->base.type = NODE_CLASS;
    zero_resolved_type(&n->base);
    n->name = strdup(name); n->is_public = is_public;
    n->fields = NULL; n->field_count = 0;
    n->methods = NULL; n->method_count = 0;
    n->ctor_params = NULL; n->ctor_param_count = 0;
    n->ctor_body = NULL; n->ctor_body_count = 0;
    n->has_ctor = 0;
    return n;
}

MethodNode *make_method(Type return_type, char *name) {
    MethodNode *n = malloc(sizeof(MethodNode));
    n->base.type = NODE_METHOD;
    zero_resolved_type(&n->base);
    n->return_type = return_type; n->name = strdup(name);
    n->params = NULL; n->param_count = 0;
    n->body = NULL; n->body_count = 0;
    return n;
}

FuncNode *make_func(Type return_type, char *name) {
    FuncNode *n = malloc(sizeof(FuncNode));
    n->base.type = NODE_FUNC;
    zero_resolved_type(&n->base);
    n->return_type = return_type; n->name = strdup(name);
    n->params = NULL; n->param_count = 0;
    n->body = NULL; n->body_count = 0;
    return n;
}

FieldNode *make_field(Type field_type, char *name, int is_public) {
    FieldNode *n = malloc(sizeof(FieldNode));
    n->base.type = NODE_FIELD;
    zero_resolved_type(&n->base);
    n->field_type = field_type; n->name = strdup(name); n->is_public = is_public;
    return n;
}

LiteralNode *make_literal(char *value, int lit_type) {
    LiteralNode *n = malloc(sizeof(LiteralNode));
    n->base.type = NODE_LITERAL;
    zero_resolved_type(&n->base);
    n->value = strdup(value); n->lit_type = lit_type;
    return n;
}

IdentifierNode *make_identifier(char *name) {
    IdentifierNode *n = malloc(sizeof(IdentifierNode));
    n->base.type = NODE_IDENTIFIER;
    zero_resolved_type(&n->base);
    n->name = strdup(name);
    return n;
}

ReturnNode *make_return(ASTNode *value) {
    ReturnNode *n = malloc(sizeof(ReturnNode));
    n->base.type = NODE_RETURN;
    zero_resolved_type(&n->base);
    n->value = value;
    return n;
}

IfNode *make_if(ASTNode *condition) {
    IfNode *n = malloc(sizeof(IfNode));
    n->base.type = NODE_IF;
    zero_resolved_type(&n->base);
    n->condition = condition;
    n->then_body = NULL; n->then_count = 0;
    n->else_body = NULL; n->else_count = 0;
    return n;
}

WhileNode *make_while(ASTNode *condition) {
    WhileNode *n = malloc(sizeof(WhileNode));
    n->base.type = NODE_WHILE;
    zero_resolved_type(&n->base);
    n->condition = condition;
    n->body = NULL; n->body_count = 0;
    return n;
}

ForNode *make_for(ASTNode *init, ASTNode *condition, ASTNode *post) {
    ForNode *n = malloc(sizeof(ForNode));
    n->base.type = NODE_FOR;
    zero_resolved_type(&n->base);
    n->init = init; n->condition = condition; n->post = post;
    n->body = NULL; n->body_count = 0;
    return n;
}

ForInNode *make_for_in(char *var_name, int use_ref, ASTNode *collection) {
    ForInNode *n = malloc(sizeof(ForInNode));
    n->base.type = NODE_FOR_IN;
    zero_resolved_type(&n->base);
    n->var_name = strdup(var_name);
    n->use_ref = use_ref;
    n->collection = collection;
    n->body = NULL; n->body_count = 0;
    return n;
}

VarDeclNode *make_var_decl(Type type, char *name, ASTNode *init) {
    VarDeclNode *n = malloc(sizeof(VarDeclNode));
    n->base.type = NODE_VAR_DECL;
    zero_resolved_type(&n->base);
    n->var_type = type; n->var_name = strdup(name); n->initializer = init;
    return n;
}

AssignNode *make_assign(char *name, ASTNode *value) {
    AssignNode *n = malloc(sizeof(AssignNode));
    n->base.type = NODE_ASSIGN;
    zero_resolved_type(&n->base);
    n->var_name = strdup(name); n->value = value;
    return n;
}

CompoundAssignNode *make_compound_assign(char *op, char *name, ASTNode *value) {
    CompoundAssignNode *n = malloc(sizeof(CompoundAssignNode));
    n->base.type = NODE_COMPOUND_ASSIGN;
    zero_resolved_type(&n->base);
    n->op = strdup(op); n->var_name = strdup(name); n->value = value;
    return n;
}

BinaryOpNode *make_binary_op(char *op, ASTNode *left, ASTNode *right) {
    BinaryOpNode *n = malloc(sizeof(BinaryOpNode));
    n->base.type = NODE_BINARY_OP;
    zero_resolved_type(&n->base);
    n->op = strdup(op); n->left = left; n->right = right;
    return n;
}

UnaryOpNode *make_unary_op(char *op, ASTNode *operand, int postfix) {
    UnaryOpNode *n = malloc(sizeof(UnaryOpNode));
    n->base.type = NODE_UNARY_OP;
    zero_resolved_type(&n->base);
    n->op = strdup(op); n->operand = operand; n->postfix = postfix;
    return n;
}

CppIncludeNode *make_cpp_include(char *header) {
    CppIncludeNode *n = malloc(sizeof(CppIncludeNode));
    n->base.type = NODE_CPP_INCLUDE;
    zero_resolved_type(&n->base);
    n->header = strdup(header);
    return n;
}

MemberAccessNode *make_member_access(ASTNode *obj, char *member) {
    MemberAccessNode *n = malloc(sizeof(MemberAccessNode));
    n->base.type = NODE_MEMBER_ACCESS;
    zero_resolved_type(&n->base);
    n->object = obj; n->member = strdup(member);
    return n;
}

MemberAssignNode *make_member_assign(ASTNode *obj, char *member, ASTNode *value) {
    MemberAssignNode *n = malloc(sizeof(MemberAssignNode));
    n->base.type = NODE_MEMBER_ASSIGN;
    zero_resolved_type(&n->base);
    n->object = obj; n->member = strdup(member); n->value = value;
    return n;
}

MethodCallNode *make_method_call(ASTNode *obj, char *method) {
    MethodCallNode *n = malloc(sizeof(MethodCallNode));
    n->base.type = NODE_METHOD_CALL;
    zero_resolved_type(&n->base);
    n->object = obj; n->method = strdup(method);
    n->args = NULL; n->arg_count = 0;
    return n;
}

FuncCallNode *make_func_call(char *name) {
    FuncCallNode *n = malloc(sizeof(FuncCallNode));
    n->base.type = NODE_FUNC_CALL;
    zero_resolved_type(&n->base);
    n->name = strdup(name);
    n->args = NULL; n->arg_count = 0;
    return n;
}

NewNode *make_new(char *class_name) {
    NewNode *n = malloc(sizeof(NewNode));
    n->base.type = NODE_NEW;
    zero_resolved_type(&n->base);
    n->class_name = strdup(class_name);
    n->args = NULL; n->arg_count = 0;
    return n;
}

DeferNode *make_defer(ASTNode *expr) {
    DeferNode *n = malloc(sizeof(DeferNode));
    n->base.type = NODE_DEFER;
    zero_resolved_type(&n->base);
    n->expr = expr;
    return n;
}

BreakNode *make_break() {
    BreakNode *n = malloc(sizeof(BreakNode));
    n->base.type = NODE_BREAK;
    zero_resolved_type(&n->base);
    return n;
}

ContinueNode *make_continue() {
    ContinueNode *n = malloc(sizeof(ContinueNode));
    n->base.type = NODE_CONTINUE;
    zero_resolved_type(&n->base);
    return n;
}

Type make_simple_type(char *name, int nullable) {
    Type t;
    t.kind = TYPE_SIMPLE;
    t.name = name ? strdup(name) : NULL;
    t.nullable = nullable;
    t.elem_types = NULL;
    t.elem_type_count = 0;
    t.is_any = 0;
    t.fixed_size = 0;
    return t;
}

static void zero_resolved_type(ASTNode *n) {
    n->resolved_type.kind = TYPE_SIMPLE;
    n->resolved_type.nullable = 0;
    n->resolved_type.name = NULL;
    n->resolved_type.elem_types = NULL;
    n->resolved_type.elem_type_count = 0;
    n->resolved_type.is_any = 0;
    n->resolved_type.fixed_size = 0;
}

Type make_array_type(Type elem, int fixed_size) {
    Type t;
    t.kind = TYPE_ARRAY;
    t.name = NULL;
    t.nullable = 0;
    t.elem_types = malloc(sizeof(Type));
    t.elem_types[0] = elem;
    t.elem_type_count = 1;
    t.is_any = 0;
    t.fixed_size = fixed_size;
    return t;
}

Type make_multi_type(Type *elems, int count, int is_any, int fixed_size) {
    Type t;
    t.kind = TYPE_MULTI;
    t.name = NULL;
    t.nullable = 0;
    t.is_any = is_any;
    t.fixed_size = fixed_size;
    if (elems && count > 0) {
        t.elem_types = malloc(count * sizeof(Type));
        for (int i = 0; i < count; i++) t.elem_types[i] = elems[i];
        t.elem_type_count = count;
    } else {
        t.elem_types = NULL;
        t.elem_type_count = 0;
    }
    return t;
}

SwitchCaseNode *make_switch_case(ASTNode *value, int is_default) {
    SwitchCaseNode *n = malloc(sizeof(SwitchCaseNode));
    n->base.type = NODE_CASE;
    zero_resolved_type(&n->base);
    n->value = value;
    n->is_default = is_default;
    n->body = NULL;
    n->body_count = 0;
    return n;
}

SwitchNode *make_switch(ASTNode *subject) {
    SwitchNode *n = malloc(sizeof(SwitchNode));
    n->base.type = NODE_SWITCH;
    zero_resolved_type(&n->base);
    n->subject = subject;
    n->cases = NULL;
    n->case_count = 0;
    return n;
}

ArrayLiteralNode *make_array_literal(ASTNode **elems, int count) {
    ArrayLiteralNode *n = malloc(sizeof(ArrayLiteralNode));
    n->base.type = NODE_ARRAY_LITERAL;
    zero_resolved_type(&n->base);
    n->elem_count = count;
    if (count > 0) {
        n->elements = malloc(count * sizeof(ASTNode *));
        for (int i = 0; i < count; i++) n->elements[i] = elems[i];
    } else {
        n->elements = NULL;
    }
    return n;
}

IndexNode *make_index(ASTNode *object, ASTNode *index) {
    IndexNode *n = malloc(sizeof(IndexNode));
    n->base.type = NODE_INDEX;
    zero_resolved_type(&n->base);
    n->object = object;
    n->index = index;
    return n;
}

IndexAssignNode *make_index_assign(ASTNode *object, ASTNode *index, ASTNode *value) {
    IndexAssignNode *n = malloc(sizeof(IndexAssignNode));
    n->base.type = NODE_INDEX_ASSIGN;
    zero_resolved_type(&n->base);
    n->object = object;
    n->index = index;
    n->value = value;
    return n;
}

TupleNode *make_tuple(ASTNode **elems, int count) {
    TupleNode *n = malloc(sizeof(TupleNode));
    n->base.type = NODE_TUPLE;
    zero_resolved_type(&n->base);
    n->elem_count = count;
    if (count > 0) {
        n->elements = malloc(count * sizeof(ASTNode *));
        for (int i = 0; i < count; i++) n->elements[i] = elems[i];
    } else {
        n->elements = NULL;
    }
    return n;
}

Type make_tuple_type(Type *elems, int count) {
    Type t;
    t.kind = TYPE_TUPLE;
    t.name = NULL;
    t.nullable = 0;
    t.is_any = 0;
    t.fixed_size = count;
    if (elems && count > 0) {
        t.elem_types = malloc(count * sizeof(Type));
        for (int i = 0; i < count; i++) t.elem_types[i] = elems[i];
        t.elem_type_count = count;
    } else {
        t.elem_types = NULL;
        t.elem_type_count = 0;
    }
    return t;
}
