#include "ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void zero_resolved_type(ASTNode *n);

/* Parse a raw interpolated string (with surrounding quotes) into segments.
   "hello {{name}}, you have {{count}} items!"
   becomes: [lit:"hello ", expr:"name", lit:", you have ", expr:"count", lit:" items!"] */
InterpStringNode *make_interp_string(const char *raw) {
    InterpStringNode *n = malloc(sizeof(InterpStringNode));
    n->base.type = NODE_INTERP_STRING;
    zero_resolved_type(&n->base);

    int raw_len = strlen(raw);
    // work in-place on a copy, no extra strncpy
    char *inner = strndup(raw + 1, raw_len - 2);

    // pre-count segments to avoid realloc loop
    int seg_cap = 4;
    n->seg_count = 0;
    n->segments = malloc(seg_cap * sizeof(InterpSegment));

    char *p = inner;
    char *start = p;

    while (*p) {
        if (p[0] == '{' && p[1] == '{') {
            if (p > start) {
                if (n->seg_count == seg_cap) { seg_cap *= 2; n->segments = realloc(n->segments, seg_cap * sizeof(InterpSegment)); }
                n->segments[n->seg_count++] = (InterpSegment){ .is_expr = 0, .text = strndup(start, p - start) };
            }
            p += 2;
            start = p;
            while (*p && !(p[0] == '}' && p[1] == '}')) p++;
            if (n->seg_count == seg_cap) { seg_cap *= 2; n->segments = realloc(n->segments, seg_cap * sizeof(InterpSegment)); }
            n->segments[n->seg_count++] = (InterpSegment){ .is_expr = 1, .text = strndup(start, p - start) };
            if (*p) p += 2;
            start = p;
        } else {
            p++;
        }
    }
    if (p > start)
        n->segments[n->seg_count++] = (InterpSegment){ .is_expr = 0, .text = strndup(start, p - start) };

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
    n->is_union = 0;
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
    n->is_naked = 0;
    n->is_public = 0;
    n->module_name = NULL;
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
    n->is_ufcs = 0;
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

BreakNode *make_break() {
    BreakNode *n = malloc(sizeof(BreakNode));
    n->base.type = NODE_BREAK;
    zero_resolved_type(&n->base);
    return n;
}

UnsafeBlockNode *make_unsafe_block(ASTNode **body, int body_count) {
    UnsafeBlockNode *n = malloc(sizeof(UnsafeBlockNode));
    n->base.type = NODE_UNSAFE;
    zero_resolved_type(&n->base);
    n->body       = body;
    n->body_count = body_count;
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

/* Every make_* constructor funnels through here, which makes it the one place
   to stamp source position onto a node. Reading the lexer's yylineno at
   construction time is approximate (a node is built when its rule reduces, so
   a multi-line construct reports its LAST line) but it is vastly better than
   what the frontend had before: no line info at all, which is why compiler
   diagnostics all came out as "file:0:" and why the LSP had to maintain a
   whole separate AST just to carry positions. */
extern int yylineno;

static void zero_resolved_type(ASTNode *n) {
    n->resolved_type.kind = TYPE_SIMPLE;
    n->resolved_type.nullable = 0;
    n->resolved_type.name = NULL;
    n->resolved_type.elem_types = NULL;
    n->resolved_type.elem_type_count = 0;
    n->resolved_type.is_any = 0;
    n->resolved_type.fixed_size = 0;
    n->line = yylineno;
    n->from_include = 0;
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

Type make_ref_type(Type inner) {
    Type t; t.kind=TYPE_REF; t.name=NULL; t.nullable=0; t.is_any=0; t.fixed_size=0;
    t.elem_types=malloc(sizeof(Type)); t.elem_types[0]=inner; t.elem_type_count=1;
    return t;
}
Type make_rawptr_type(Type inner) {
    Type t; t.kind=TYPE_RAWPTR; t.name=NULL; t.nullable=0; t.is_any=0; t.fixed_size=0;
    t.elem_types=malloc(sizeof(Type)); t.elem_types[0]=inner; t.elem_type_count=1;
    return t;
}

ModuleNode *make_module(char *name) {
    ModuleNode *n = malloc(sizeof(ModuleNode));
    n->base.type = NODE_MODULE;
    n->base.resolved_type.kind = TYPE_SIMPLE;
    n->base.resolved_type.nullable = 0;
    n->base.resolved_type.name = NULL;
    n->base.resolved_type.elem_types = NULL;
    n->base.resolved_type.elem_type_count = 0;
    n->base.resolved_type.is_any = 0;
    n->base.resolved_type.fixed_size = 0;
    n->name = strdup(name);
    n->funcs = NULL;
    n->func_count = 0;
    n->func_is_public = NULL;
    n->statics = NULL;
    n->static_count = 0;
    return n;
}

/* ── struct layout ──────────────────────────────────────────────────────────
 * See the contract in ast.h. This is the single source of truth for offsets;
 * lower.c and ir_to_mir.c both call in here rather than each doing their own
 * arithmetic, because two passes disagreeing about a field offset produces
 * silent memory corruption rather than any kind of error.
 */

static ClassNode *ast_find_class(ClassNode **classes, int class_count,
                                 const char *name) {
    if (!classes || !name) return NULL;
    for (int i = 0; i < class_count; i++)
        if (classes[i] && classes[i]->name && strcmp(classes[i]->name, name) == 0)
            return classes[i];
    return NULL;
}

/* Width of a primitive type name. Anything unrecognized is pointer-width. */
static int ast_prim_width(const char *name) {
    if (!name) return 8;
    /* NOTE: `bool` is deliberately NOT 1 byte here. Hylian stores bools as 0/1
       in a full 8-byte slot (type_name_to_mir() in ir_to_mir.c maps bool ->
       MIR_I64), so a bool field is written with an 8-byte store. If the layout
       handed it 1 byte, that store would run over the following 7 bytes and
       silently corrupt whichever field came next. The two must agree. */
    if (strcmp(name, "int8")  == 0 || strcmp(name, "uint8")  == 0) return 1;
    if (strcmp(name, "int16") == 0 || strcmp(name, "uint16") == 0) return 2;
    if (strcmp(name, "int32") == 0 || strcmp(name, "uint32") == 0 ||
        strcmp(name, "float32") == 0) return 4;
    return 8;
}

int ast_field_byte_width(ClassNode **classes, int class_count, FieldNode *f) {
    if (!f) return 8;

    const char *elem_name;
    int is_fixed_array = (f->field_type.kind == TYPE_ARRAY &&
                          f->field_type.fixed_size > 0);

    if (is_fixed_array && f->field_type.elem_type_count > 0 && f->field_type.elem_types)
        elem_name = f->field_type.elem_types[0].name;
    else
        elem_name = f->field_type.name ? f->field_type.name : "int";

    int width = ast_prim_width(elem_name);

    /* A field whose type is another class contributes that class's full size,
       not a pointer — stack structs are stored inline. */
    ClassNode *nested = ast_find_class(classes, class_count, elem_name);
    if (nested) width = ast_class_byte_size(classes, class_count, elem_name);

    if (is_fixed_array) return width * f->field_type.fixed_size;
    return width;
}

int ast_class_byte_size(ClassNode **classes, int class_count, const char *name) {
    ClassNode *cn = ast_find_class(classes, class_count, name);
    if (!cn) return 8;

    if (cn->is_union) {
        int max_w = 0;
        for (int i = 0; i < cn->field_count; i++) {
            int w = ast_field_byte_width(classes, class_count, cn->fields[i]);
            if (w > max_w) max_w = w;
        }
        int sz = max_w ? max_w : 8;
        if (sz % 8 != 0) sz += 8 - sz % 8;
        return sz;
    }

    int sz = 0;
    for (int i = 0; i < cn->field_count; i++)
        sz += ast_field_byte_width(classes, class_count, cn->fields[i]);
    if (sz == 0) sz = 8;
    if (!cn->is_packed && sz % 16 != 0) sz += 16 - sz % 16;
    return sz;
}

int ast_field_offset(ClassNode **classes, int class_count,
                     const char *cls, const char *field,
                     int *out_width, const char **out_type_name) {
    if (out_width)     *out_width = 8;
    if (out_type_name) *out_type_name = NULL;

    ClassNode *cn = ast_find_class(classes, class_count, cls);
    if (!cn || !field) return -1;

    int running = 0;
    for (int i = 0; i < cn->field_count; i++) {
        FieldNode *f = cn->fields[i];
        if (!f) continue;
        int w = ast_field_byte_width(classes, class_count, f);
        if (f->name && strcmp(f->name, field) == 0) {
            if (out_width) {
                /* A fixed-size array field is addressed as a whole block, so
                   report pointer width rather than the block's total size —
                   callers load its base address, not its contents. */
                if (f->field_type.kind == TYPE_ARRAY && f->field_type.fixed_size > 0)
                    *out_width = 8;
                else
                    *out_width = w;
            }
            if (out_type_name)
                *out_type_name = f->field_type.name;
            return cn->is_union ? 0 : running;
        }
        if (!cn->is_union) running += w;
    }
    return -1;
}

int ast_is_class(ClassNode **classes, int class_count, const char *name) {
    return ast_find_class(classes, class_count, name) != NULL;
}

/* Is `name` a BY-VALUE aggregate — something whose bytes live inline in a
   stack slot, so a variable of that type is addressed rather than loaded?
 *
 * Three things disqualify a class:
 *   - it has a constructor: it's made with `new`, so the local holds a pointer;
 *   - it has no fields: an interface-only declaration (`class Error { fn
 *     message() -> str }` in a .hyi) is an opaque HANDLE, and its values are
 *     pointers into the runtime. Treating it as by-value made `e.message()`
 *     pass the address of the slot holding the pointer instead of the pointer;
 *   - it isn't a declared class at all (str, ptr, int, an unknown name).
 */
int ast_is_value_aggregate(ClassNode **classes, int class_count, const char *name) {
    ClassNode *cn = ast_find_class(classes, class_count, name);
    if (!cn) return 0;
    if (cn->has_ctor) return 0;
    if (cn->field_count == 0) return 0;
    return 1;
}

int ast_class_has_ctor(ClassNode **classes, int class_count, const char *name) {
    ClassNode *cn = ast_find_class(classes, class_count, name);
    return cn ? cn->has_ctor : 0;
}
