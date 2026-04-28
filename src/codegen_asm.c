#include "codegen_asm.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Target ─────────────────────────────────────────────────────────────────── */

static const char *current_target    = "linux";
static int         io_included        = 0; /* set to 1 when std.io is in the include list */
static const char *current_class_name = NULL; /* set while emitting a class method/ctor */

/* Runtime call-name rewrite table: populated by codegen_asm() once the
   module list is known.
   src_prefix  — the prefix as written in Hylian source (e.g. "crypto_")
   abi_prefix  — the full ABI symbol prefix (e.g. "hylian_crypto_")
   So "crypto_hash" -> "hylian_crypto_hash",
      "tcp_connect" -> "hylian_net_tcp_connect", etc. */
#define MAX_FN_PREFIXES 32
typedef struct {
    const char *src_prefix; /* prefix as seen in Hylian source */
    const char *abi_prefix; /* full prefix of the C symbol        */
} FnPrefix;
static FnPrefix fn_prefixes[MAX_FN_PREFIXES];
static int      fn_prefix_count = 0;

/* Rewrite a Hylian source call name to its runtime symbol name.
   Strips src_prefix and prepends abi_prefix. */
static const char *rewrite_call_name(const char *name) {
    static char buf[256];
    for (int i = 0; i < fn_prefix_count; i++) {
        size_t plen = strlen(fn_prefixes[i].src_prefix);
        if (strncmp(name, fn_prefixes[i].src_prefix, plen) == 0) {
            snprintf(buf, sizeof(buf), "%s%s",
                     fn_prefixes[i].abi_prefix, name + plen);
            return buf;
        }
    }
    return name;
}

/* Returns the symbol name with a leading underscore on macOS, plain otherwise. */
static const char *sym(const char *name) {
    static char buf[128];
    if (strcmp(current_target, "macos") == 0) {
        buf[0] = '_';
        strncpy(buf + 1, name, sizeof(buf) - 2);
        buf[sizeof(buf) - 1] = '\0';
        return buf;
    }
    return name;
}

/* ─── String constant table ─────────────────────────────────────────────────── */

#define MAX_STR_CONSTS 1024

typedef struct {
    char *value;
    char *label;
} StrConst;

static StrConst str_consts[MAX_STR_CONSTS];
static int      str_const_count = 0;

static int _label_counter = 0;

static int next_label(void) {
    return _label_counter++;
}

/* Escape a string value for NASM db directive.
   Returns a heap-allocated string. */
static char *nasm_escape_string(const char *s) {
    /* worst case: every char becomes 4 chars e.g. ",0x0a," */
    size_t len = strlen(s);
    char *buf = malloc(len * 6 + 16);
    char *p = buf;
    int in_quotes = 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c != '"' && c != '\\') {
            if (!in_quotes) {
                if (p != buf) { *p++ = ','; }
                *p++ = '"';
                in_quotes = 1;
            }
            *p++ = (char)c;
        } else {
            if (in_quotes) {
                *p++ = '"';
                in_quotes = 0;
            }
            if (p != buf) { *p++ = ','; }
            p += sprintf(p, "0x%02x", c);
        }
    }
    if (in_quotes) {
        *p++ = '"';
    }
    *p = '\0';

    /* If nothing was written (empty string), emit a single 0 placeholder
       that we'll handle by having length 0; emit 0x00 as sentinel */
    if (p == buf) {
        strcpy(buf, "0x00");
    }
    return buf;
}

/* Register a string literal and return its label (not heap-allocated — static storage). */
/* Strip surrounding double-quotes from a string literal value as it comes
   from the lexer, e.g. `"hello"` -> `hello`.  Returns a heap-allocated
   unquoted copy. */
static char *unquote_string(const char *value) {
    size_t len = strlen(value);
    if (len >= 2 && value[0] == '"' && value[len - 1] == '"') {
        char *s = malloc(len - 1);
        memcpy(s, value + 1, len - 2);
        s[len - 2] = '\0';
        return s;
    }
    return strdup(value);
}

static const char *register_string(const char *value) {
    /* Strip quotes for the canonical form we store & compare */
    char *unquoted = unquote_string(value);

    /* Check if already registered */
    for (int i = 0; i < str_const_count; i++) {
        if (strcmp(str_consts[i].value, unquoted) == 0) {
            free(unquoted);
            return str_consts[i].label;
        }
    }
    if (str_const_count >= MAX_STR_CONSTS) {
        fprintf(stderr, "codegen_asm: too many string constants\n");
        free(unquoted);
        return "_str_overflow";
    }
    char *lbl = malloc(32);
    snprintf(lbl, 32, "_str%d", str_const_count);
    str_consts[str_const_count].value = unquoted; /* already unquoted */
    str_consts[str_const_count].label = lbl;
    str_const_count++;
    return lbl;
}

/* Return the byte-length of a string literal value (strips quotes first). */
static size_t string_literal_len(const char *value) {
    char *unquoted = unquote_string(value);
    size_t len = strlen(unquoted);
    free(unquoted);
    return len;
}

/* ─── Local variable table ───────────────────────────────────────────────────── */

#define MAX_LOCALS 256

typedef struct {
    char *name;
    int   offset;    /* positive offset from rbp, so address = [rbp - offset] */
    char *type_name; /* NULL for primitives, class name for objects */
} Local;

static Local  locals[MAX_LOCALS];
static int    local_count  = 0;
static int    stack_depth  = 0; /* bytes allocated so far (multiples of 8) */

static void locals_reset(void) {
    local_count = 0;
    stack_depth = 0;
}

/* Returns the rbp-relative offset (positive) for a new local. */
static int locals_alloc(const char *name) {
    if (local_count >= MAX_LOCALS) {
        fprintf(stderr, "codegen_asm: too many locals\n");
        return 8;
    }
    stack_depth += 8;
    locals[local_count].name      = strdup(name);
    locals[local_count].offset    = stack_depth;
    locals[local_count].type_name = NULL;
    local_count++;
    return stack_depth;
}

/* Like locals_alloc but also records the type name (for OOP). */
static int locals_alloc_typed(const char *name, const char *type_name) {
    int off = locals_alloc(name);
    locals[local_count - 1].type_name = type_name ? (char *)type_name : NULL;
    return off;
}

/* Find offset for an existing local; returns -1 if not found. */
static int locals_find(const char *name) {
    for (int i = 0; i < local_count; i++)
        if (strcmp(locals[i].name, name) == 0)
            return locals[i].offset;
    return -1;
}

/* Return the type name of a local variable (NULL if not found / primitive). */
static const char *locals_type(const char *name) {
    for (int i = 0; i < local_count; i++)
        if (strcmp(locals[i].name, name) == 0)
            return locals[i].type_name;
    return NULL;
}

/* ─── Class registry ─────────────────────────────────────────────────────────── */

#define MAX_CLASSES 64
#define MAX_FIELDS  32

typedef struct {
    char *name;
    int   offset;    /* byte offset from object base */
    char *type_name; /* field type name, e.g. "int", "str", "Player" */
} FieldInfo;

typedef struct {
    char *name;          /* class name */
    FieldInfo fields[MAX_FIELDS];
    int  field_count;
    int  size;           /* total size in bytes, padded to 16 */
    int  has_ctor;
} ClassInfo;

static ClassInfo class_registry[MAX_CLASSES];
static int       class_count = 0;

static void class_registry_reset(void) {
    /* strings point into the AST — do not free them */
    class_count = 0;
}

static ClassInfo *class_find(const char *name) {
    for (int i = 0; i < class_count; i++)
        if (strcmp(class_registry[i].name, name) == 0)
            return &class_registry[i];
    return NULL;
}

static ClassInfo *class_register(ClassNode *cls) {
    if (class_count >= MAX_CLASSES) {
        fprintf(stderr, "codegen_asm: too many classes\n");
        return NULL;
    }
    ClassInfo *ci = &class_registry[class_count++];
    ci->name        = cls->name;
    ci->field_count = 0;
    ci->has_ctor    = cls->has_ctor;
    int offset = 0;
    for (int i = 0; i < cls->field_count; i++) {
        FieldNode *f = cls->fields[i];
        if (ci->field_count >= MAX_FIELDS) break;
        ci->fields[ci->field_count].name      = f->name;
        ci->fields[ci->field_count].offset    = offset;
        ci->fields[ci->field_count].type_name = f->field_type.name ? f->field_type.name : "int";
        ci->field_count++;
        offset += 8; /* all fields are 8-byte (64-bit) slots */
    }
    ci->size = offset;
    if (ci->size == 0) ci->size = 8;
    if (ci->size % 16 != 0) ci->size += (16 - ci->size % 16);
    return ci;
}

static int class_field_offset(ClassInfo *ci, const char *field_name) {
    for (int i = 0; i < ci->field_count; i++)
        if (strcmp(ci->fields[i].name, field_name) == 0)
            return ci->fields[i].offset;
    return -1;
}

/* ─── Enum registry ──────────────────────────────────────────────────────────── */

#define MAX_ENUMS    32
#define MAX_VARIANTS 64

typedef struct {
    char *variant_name;
    int   value;
} EnumVariantInfo;

typedef struct {
    char *name;
    EnumVariantInfo variants[MAX_VARIANTS];
    int  variant_count;
} EnumRegEntry;

static EnumRegEntry enum_registry[MAX_ENUMS];
static int          enum_count = 0;

static void enum_registry_reset(void) { enum_count = 0; }

static EnumRegEntry *enum_find(const char *name) {
    for (int i = 0; i < enum_count; i++)
        if (strcmp(enum_registry[i].name, name) == 0)
            return &enum_registry[i];
    return NULL;
}

static void enum_register(EnumNode *en) {
    if (enum_count >= MAX_ENUMS) return;
    EnumRegEntry *ei = &enum_registry[enum_count++];
    ei->name = en->name;
    ei->variant_count = 0;
    for (int i = 0; i < en->variant_count && i < MAX_VARIANTS; i++) {
        ei->variants[i].variant_name = en->variants[i].name;
        ei->variants[i].value        = en->variants[i].value;
        ei->variant_count++;
    }
}

static int enum_variant_value(const char *enum_name, const char *variant) {
    EnumRegEntry *ei = enum_find(enum_name);
    if (!ei) return -1;
    for (int i = 0; i < ei->variant_count; i++)
        if (strcmp(ei->variants[i].variant_name, variant) == 0)
            return ei->variants[i].value;
    return -1;
}

static const char *class_field_type(ClassInfo *ci, const char *field_name) {
    for (int i = 0; i < ci->field_count; i++)
        if (strcmp(ci->fields[i].name, field_name) == 0)
            return ci->fields[i].type_name;
    return NULL;
}

/* ─── Method name mangling ───────────────────────────────────────────────────── */

/* Returns heap-allocated "ClassName_methodName" */
static char *mangle_method(const char *class_name, const char *method_name) {
    int len = (int)strlen(class_name) + 1 + (int)strlen(method_name) + 1;
    char *buf = malloc(len);
    snprintf(buf, len, "%s_%s", class_name, method_name);
    return buf;
}

/* Returns heap-allocated "ClassName__ctor" */
static char *mangle_ctor(const char *class_name) {
    int len = (int)strlen(class_name) + 7; /* "__ctor\0" */
    char *buf = malloc(len);
    snprintf(buf, len, "%s__ctor", class_name);
    return buf;
}

/* ─── Loop label stack (for break/continue) ─────────────────────────────────── */

#define MAX_LOOP_DEPTH 32

typedef struct {
    int continue_label; /* label to jump to for `continue` */
    int break_label;    /* label to jump to for `break`    */
} LoopLabels;

static LoopLabels loop_stack[MAX_LOOP_DEPTH];
static int        loop_stack_top = 0;

static void loop_stack_push(int continue_lbl, int break_lbl) {
    if (loop_stack_top >= MAX_LOOP_DEPTH) {
        fprintf(stderr, "codegen_asm: loop nesting too deep\n");
        return;
    }
    loop_stack[loop_stack_top].continue_label = continue_lbl;
    loop_stack[loop_stack_top].break_label    = break_lbl;
    loop_stack_top++;
}

static void loop_stack_pop(void) {
    if (loop_stack_top > 0) loop_stack_top--;
}

static LoopLabels *loop_stack_peek(void) {
    if (loop_stack_top == 0) return NULL;
    return &loop_stack[loop_stack_top - 1];
}

/* ─── Forward declarations ───────────────────────────────────────────────────── */

static void emit_expr(ASTNode *node, FILE *out, const char *fn_name);
static void emit_stmt(ASTNode *node, FILE *out, const char *fn_name);
static void emit_stmts(ASTNode **stmts, int count, FILE *out, const char *fn_name);

/* ─── OOP helpers (depend on forward-declared emit_expr) ────────────────────── */

/* Given an expression node, return the class name if statically determinable. */
static const char *expr_class_name(ASTNode *node) {
    if (!node) return NULL;
    /* Use resolved_type if available (set by typecheck pass) */
    if (node->resolved_type.kind == TYPE_ARRAY) return "array";
    if (node->resolved_type.kind == TYPE_MULTI) return "multi";
    if (node->resolved_type.kind == TYPE_SIMPLE && node->resolved_type.name)
        return node->resolved_type.name;
    /* Fallback: identifier lookup in locals */
    if (node->type == NODE_IDENTIFIER) {
        const char *tn = locals_type(((IdentifierNode *)node)->name);
        return tn;
    }
    if (node->type == NODE_VAR_DECL) {
        VarDeclNode *vd = (VarDeclNode *)node;
        if (vd->var_type.kind == TYPE_SIMPLE && vd->var_type.name)
            return vd->var_type.name;
        if (vd->var_type.kind == TYPE_ARRAY)
            return "array";
        if (vd->var_type.kind == TYPE_MULTI)
            return "multi";
    }
    return NULL;
}

/* Emit: lea <reg>, [rbp - offset] for the named local. */
static void emit_local_addr(const char *var_name, const char *reg, FILE *out) {
    int off = locals_find(var_name);
    if (off < 0) {
        fprintf(out, "    ; unknown var '%s' for address\n", var_name);
        fprintf(out, "    xor %s, %s\n", reg, reg);
    } else {
        fprintf(out, "    lea %s, [rbp - %d]\n", reg, off);
    }
}

/* Emit the *address* of an lvalue into rdi (for self passing).
   For a simple identifier with a known local slot: lea rdi, [rbp-off].
   Fallback: emit value into rax and mov rdi, rax (treats value as pointer). */
static void emit_self_addr(ASTNode *obj, FILE *out, const char *fn_name) {
    if (obj->type == NODE_IDENTIFIER) {
        const char *name = ((IdentifierNode *)obj)->name;
        int off = locals_find(name);
        if (off >= 0) {
            /* Objects from `new` are heap-allocated; the local slot stores
               the heap pointer value — load it with mov, not lea. */
            fprintf(out, "    mov rdi, [rbp - %d]\n", off);
            return;
        }
    }
    /* fallback: evaluate expression, result in rax is already a pointer */
    emit_expr(obj, out, fn_name);
    fprintf(out, "    mov rdi, rax\n");
}

/* ─── Count VarDecl nodes recursively (for prologue sizing) ─────────────────── */

static int count_var_decls(ASTNode **stmts, int count) {
    int n = 0;
    for (int i = 0; i < count; i++) {
        ASTNode *s = stmts[i];
        if (!s) continue;
        switch (s->type) {
        case NODE_VAR_DECL:
            n++;
            break;
        case NODE_IF: {
            IfNode *nd = (IfNode *)s;
            n += count_var_decls(nd->then_body, nd->then_count);
            n += count_var_decls(nd->else_body, nd->else_count);
            break;
        }
        case NODE_WHILE: {
            WhileNode *nd = (WhileNode *)s;
            n += count_var_decls(nd->body, nd->body_count);
            break;
        }
        case NODE_FOR: {
            ForNode *nd = (ForNode *)s;
            if (nd->init) {
                ASTNode *init_arr[1] = { nd->init };
                n += count_var_decls(init_arr, 1);
            }
            n += count_var_decls(nd->body, nd->body_count);
            break;
        }
        case NODE_FOR_IN: {
            ForInNode *nd = (ForInNode *)s;
            n += 4; /* __for_arr_N, __for_len_N, __for_idx_N, loop var */
            n += count_var_decls(nd->body, nd->body_count);
            break;
        }
        default:
            break;
        }
    }
    return n;
}

/* ─── Expression emission ────────────────────────────────────────────────────── */

/* Emit code to evaluate an argument for print/println.
   For string literals: load ptr into rdi, length into rsi.
   For integer expressions: compute into rax, call hylian_int_to_str,
     then rdi=buf ptr, rsi=length. */
static void emit_print_arg(ASTNode *arg, FILE *out, const char *fn_name) {
    if (arg->type == NODE_LITERAL) {
        LiteralNode *lit = (LiteralNode *)arg;
        if (lit->lit_type == LIT_STRING) {
            const char *lbl = register_string(lit->value);
            size_t len = string_literal_len(lit->value);
            fprintf(out, "    lea rdi, [rel %s]\n", lbl);
            fprintf(out, "    mov rsi, %zu\n", len);
            return;
        }
    }
    /* Integer (or expression) path */
    emit_expr(arg, out, fn_name);
    /* rax = integer value; convert to string */
    fprintf(out, "    ; int_to_str\n");
    fprintf(out, "    sub rsp, 32\n");
    fprintf(out, "    mov rdi, rax\n");
    fprintf(out, "    mov rsi, rsp\n");
    fprintf(out, "    mov rdx, 32\n");
    fprintf(out, "    call %s\n", sym("hylian_int_to_str"));
    /* rax = length, rsp = buffer */
    fprintf(out, "    mov rsi, rax\n");
    fprintf(out, "    mov rdi, rsp\n");
    /* We'll clean up the 32 bytes after the call to hylian_print/println */
}

static void emit_expr(ASTNode *node, FILE *out, const char *fn_name) {
    if (!node) {
        fprintf(out, "    xor rax, rax\n");
        return;
    }

    switch (node->type) {

    case NODE_LITERAL: {
        LiteralNode *lit = (LiteralNode *)node;
        if (lit->lit_type == LIT_INT) {
            fprintf(out, "    mov rax, %s\n", lit->value);
        } else if (lit->lit_type == LIT_STRING) {
            const char *lbl = register_string(lit->value); /* registers unquoted */
            fprintf(out, "    lea rax, [rel %s]\n", lbl);
        } else if (lit->lit_type == LIT_NIL) {
            fprintf(out, "    xor rax, rax\n");
        } else if (lit->lit_type == LIT_BOOL) {
            if (strcmp(lit->value, "true") == 0)
                fprintf(out, "    mov rax, 1\n");
            else
                fprintf(out, "    xor rax, rax\n");
        } else if (lit->lit_type == LIT_FLOAT) {
            fprintf(out, "    ; float literal not fully supported\n");
            fprintf(out, "    xor rax, rax\n");
        } else {
            fprintf(out, "    xor rax, rax\n");
        }
        break;
    }

    case NODE_IDENTIFIER: {
        IdentifierNode *id = (IdentifierNode *)node;
        int off = locals_find(id->name);
        if (off >= 0) {
            fprintf(out, "    mov rax, [rbp - %d]\n", off);
        } else if (current_class_name) {
            /* Try to resolve as a field of self */
            ClassInfo *ci = class_find(current_class_name);
            if (ci) {
                int field_off = class_field_offset(ci, id->name);
                if (field_off >= 0) {
                    int self_off = locals_find("self");
                    if (self_off >= 0) {
                        fprintf(out, "    mov rax, [rbp - %d]\n", self_off);   /* load self ptr */
                        fprintf(out, "    mov rax, [rax + %d]\n", field_off);  /* load field    */
                        break;
                    }
                }
            }
            fprintf(out, "    ; unknown identifier '%s'\n", id->name);
            fprintf(out, "    xor rax, rax\n");
        } else {
            fprintf(out, "    ; unknown variable '%s'\n", id->name);
            fprintf(out, "    xor rax, rax\n");
        }
        break;
    }

    case NODE_BINARY_OP: {
        BinaryOpNode *bin = (BinaryOpNode *)node;
        const char *op = bin->op;

        /* Logical && and || need short-circuit, but for simplicity we
           evaluate both sides (correct for pure expressions without side-effects
           in most simple programs). */
        if (strcmp(op, "&&") == 0) {
            int lbl_false = next_label();
            int lbl_end   = next_label();
            emit_expr(bin->left, out, fn_name);
            fprintf(out, "    test rax, rax\n");
            fprintf(out, "    jz .L%d\n", lbl_false);
            emit_expr(bin->right, out, fn_name);
            fprintf(out, "    test rax, rax\n");
            fprintf(out, "    jz .L%d\n", lbl_false);
            fprintf(out, "    mov rax, 1\n");
            fprintf(out, "    jmp .L%d\n", lbl_end);
            fprintf(out, ".L%d:\n", lbl_false);
            fprintf(out, "    xor rax, rax\n");
            fprintf(out, ".L%d:\n", lbl_end);
            break;
        }
        if (strcmp(op, "||") == 0) {
            int lbl_true = next_label();
            int lbl_end  = next_label();
            emit_expr(bin->left, out, fn_name);
            fprintf(out, "    test rax, rax\n");
            fprintf(out, "    jnz .L%d\n", lbl_true);
            emit_expr(bin->right, out, fn_name);
            fprintf(out, "    test rax, rax\n");
            fprintf(out, "    jnz .L%d\n", lbl_true);
            fprintf(out, "    xor rax, rax\n");
            fprintf(out, "    jmp .L%d\n", lbl_end);
            fprintf(out, ".L%d:\n", lbl_true);
            fprintf(out, "    mov rax, 1\n");
            fprintf(out, ".L%d:\n", lbl_end);
            break;
        }

        /* Standard: evaluate left -> push, evaluate right, pop left into rcx */
        emit_expr(bin->left, out, fn_name);
        fprintf(out, "    push rax\n");
        emit_expr(bin->right, out, fn_name);
        /* Now: rcx = left, rax = right */
        fprintf(out, "    pop rcx\n");

        if (strcmp(op, "+") == 0) {
            fprintf(out, "    add rax, rcx\n");
        } else if (strcmp(op, "-") == 0) {
            /* left - right = rcx - rax */
            fprintf(out, "    sub rcx, rax\n");
            fprintf(out, "    mov rax, rcx\n");
        } else if (strcmp(op, "*") == 0) {
            fprintf(out, "    imul rax, rcx\n");
        } else if (strcmp(op, "/") == 0) {
            /* left/right = rcx/rax */
            fprintf(out, "    xchg rax, rcx\n"); /* rax=left, rcx=right */
            fprintf(out, "    cqo\n");
            fprintf(out, "    idiv rcx\n");
            /* rax = quotient */
        } else if (strcmp(op, "%") == 0) {
            fprintf(out, "    xchg rax, rcx\n");
            fprintf(out, "    cqo\n");
            fprintf(out, "    idiv rcx\n");
            fprintf(out, "    mov rax, rdx\n"); /* remainder */
        } else if (strcmp(op, "==") == 0) {
            fprintf(out, "    cmp rcx, rax\n");
            fprintf(out, "    sete al\n");
            fprintf(out, "    movzx rax, al\n");
        } else if (strcmp(op, "!=") == 0) {
            fprintf(out, "    cmp rcx, rax\n");
            fprintf(out, "    setne al\n");
            fprintf(out, "    movzx rax, al\n");
        } else if (strcmp(op, "<") == 0) {
            fprintf(out, "    cmp rcx, rax\n");
            fprintf(out, "    setl al\n");
            fprintf(out, "    movzx rax, al\n");
        } else if (strcmp(op, ">") == 0) {
            fprintf(out, "    cmp rcx, rax\n");
            fprintf(out, "    setg al\n");
            fprintf(out, "    movzx rax, al\n");
        } else if (strcmp(op, "<=") == 0) {
            fprintf(out, "    cmp rcx, rax\n");
            fprintf(out, "    setle al\n");
            fprintf(out, "    movzx rax, al\n");
        } else if (strcmp(op, ">=") == 0) {
            fprintf(out, "    cmp rcx, rax\n");
            fprintf(out, "    setge al\n");
            fprintf(out, "    movzx rax, al\n");
        } else {
            fprintf(out, "    ; unsupported binary op '%s'\n", op);
        }
        break;
    }

    case NODE_UNARY_OP: {
        UnaryOpNode *un = (UnaryOpNode *)node;
        emit_expr(un->operand, out, fn_name);
        if (strcmp(un->op, "!") == 0) {
            fprintf(out, "    test rax, rax\n");
            fprintf(out, "    sete al\n");
            fprintf(out, "    movzx rax, al\n");
        } else if (strcmp(un->op, "-") == 0) {
            fprintf(out, "    neg rax\n");
        } else {
            fprintf(out, "    ; unsupported unary op '%s'\n", un->op);
        }
        break;
    }

    case NODE_FUNC_CALL: {
        FuncCallNode *call = (FuncCallNode *)node;
        int is_print   = io_included && strcmp(call->name, "print")   == 0;
        int is_println = io_included && strcmp(call->name, "println") == 0;
        int is_err     = strcmp(call->name, "Err")   == 0;
        int is_panic   = strcmp(call->name, "panic") == 0;

        /* ── Err("message") ─────────────────────────────────────────────── */
        if (is_err) {
            if (call->arg_count >= 1) {
                ASTNode *arg = call->args[0];
                if (arg->type == NODE_LITERAL && ((LiteralNode *)arg)->lit_type == LIT_STRING) {
                    LiteralNode *lit = (LiteralNode *)arg;
                    const char *lbl = register_string(lit->value);
                    size_t len = string_literal_len(lit->value);
                    fprintf(out, "    ; Err(\"%s\")\n", ((LiteralNode *)arg)->value);
                    fprintf(out, "    lea rdi, [rel %s]\n", lbl);
                    fprintf(out, "    mov rsi, %zu\n", len);
                } else {
                    /* expression argument — evaluate and treat as pointer+length */
                    emit_expr(arg, out, fn_name);
                    fprintf(out, "    mov rdi, rax\n");
                    fprintf(out, "    mov rsi, 0\n");
                }
            } else {
                /* Err() with no message */
                const char *lbl = register_string("error");
                fprintf(out, "    lea rdi, [rel %s]\n", lbl);
                fprintf(out, "    mov rsi, 5\n");
            }
            fprintf(out, "    call %s\n", sym("hylian_make_error"));
            break;
        }

        /* ── panic("message") ───────────────────────────────────────────── */
        if (is_panic) {
            if (call->arg_count >= 1) {
                ASTNode *arg = call->args[0];
                if (arg->type == NODE_LITERAL && ((LiteralNode *)arg)->lit_type == LIT_STRING) {
                    LiteralNode *lit = (LiteralNode *)arg;
                    const char *lbl = register_string(lit->value);
                    size_t len = string_literal_len(lit->value);
                    fprintf(out, "    ; panic(\"%s\")\n", lit->value);
                    fprintf(out, "    lea rdi, [rel %s]\n", lbl);
                    fprintf(out, "    mov rsi, %zu\n", len);
                } else if (arg->type == NODE_METHOD_CALL) {
                    /* panic(err.message()) — deref offset 0 of the Error struct */
                    MethodCallNode *mc = (MethodCallNode *)arg;
                    if (strcmp(mc->method, "message") == 0) {
                        emit_expr(mc->object, out, fn_name);
                        fprintf(out, "    ; .message() -> deref Error.message at offset 0\n");
                        fprintf(out, "    mov rdi, [rax + 0]\n");
                        /* compute strlen for the length arg */
                        fprintf(out, "    push rdi\n");
                        fprintf(out, "    call strlen\n");
                        fprintf(out, "    pop rdi\n");
                        fprintf(out, "    mov rsi, rax\n");
                    } else {
                        emit_expr(arg, out, fn_name);
                        fprintf(out, "    mov rdi, rax\n");
                        fprintf(out, "    mov rsi, 0\n");
                    }
                } else {
                    emit_expr(arg, out, fn_name);
                    fprintf(out, "    mov rdi, rax\n");
                    fprintf(out, "    mov rsi, 0\n");
                }
            } else {
                const char *lbl = register_string("panic");
                fprintf(out, "    lea rdi, [rel %s]\n", lbl);
                fprintf(out, "    mov rsi, 5\n");
            }
            fprintf(out, "    call %s\n", sym("hylian_panic"));
            break;
        }

        if (is_print || is_println) {
            if (call->arg_count >= 1) {
                ASTNode *arg = call->args[0];
                int need_stack_cleanup = 0;  /* bytes to add rsp by after call */

                if (arg->type == NODE_LITERAL && ((LiteralNode *)arg)->lit_type == LIT_STRING) {
                    /* String literal: direct pointer + length */
                    LiteralNode *lit = (LiteralNode *)arg;
                    const char *lbl = register_string(lit->value);
                    size_t len = string_literal_len(lit->value);
                    fprintf(out, "    lea rdi, [rel %s]\n", lbl);
                    fprintf(out, "    mov rsi, %zu\n", len);
                } else if (arg->type == NODE_INTERP_STRING) {
                    /* Interpolated string: emit_expr leaves buffer on stack,
                       rax=ptr, r15=len.  We must clean up 544 bytes after
                       the call. */
                    emit_expr(arg, out, fn_name);
                    fprintf(out, "    mov rdi, rax\n");
                    fprintf(out, "    mov rsi, r15\n");
                    need_stack_cleanup = 544;
                } else if ((arg->type == NODE_METHOD_CALL &&
                            strcmp(((MethodCallNode *)arg)->method, "message") == 0) ||
                           (arg->type == NODE_IDENTIFIER &&
                            (locals_type(((IdentifierNode *)arg)->name)) &&
                            strcmp(locals_type(((IdentifierNode *)arg)->name), "str") == 0)) {
                    /* str pointer result (method call returning str e.g. .message(), or str local):
                       use strlen to get length, then call hylian_println directly */
                    emit_expr(arg, out, fn_name);
                    /* rax = char* pointer */
                    fprintf(out, "    push rax\n");
                    fprintf(out, "    mov rdi, rax\n");
                    fprintf(out, "    call %s\n", sym("strlen"));
                    fprintf(out, "    mov rsi, rax\n");
                    fprintf(out, "    pop rdi\n");
                    need_stack_cleanup = 0;
                } else {
                    /* Integer or other expression: convert to string */
                    emit_expr(arg, out, fn_name);
                    fprintf(out, "    sub rsp, 32\n");
                    need_stack_cleanup = 32;
                    fprintf(out, "    mov rdi, rax\n");
                    fprintf(out, "    mov rsi, rsp\n");
                    fprintf(out, "    mov rdx, 32\n");
                    fprintf(out, "    call %s\n", sym("hylian_int_to_str"));
                    fprintf(out, "    mov rsi, rax\n");
                    fprintf(out, "    mov rdi, rsp\n");
                }

                if (is_print)
                    fprintf(out, "    call %s\n", sym("hylian_print"));
                else
                    fprintf(out, "    call %s\n", sym("hylian_println"));

                if (need_stack_cleanup)
                    fprintf(out, "    add rsp, %d\n", need_stack_cleanup);
            } else {
                /* print() with no args — print empty string */
                const char *lbl = register_string("");
                fprintf(out, "    lea rdi, [rel %s]\n", lbl);
                fprintf(out, "    mov rsi, 0\n");
                if (is_print)
                    fprintf(out, "    call %s\n", sym("hylian_print"));
                else
                    fprintf(out, "    call %s\n", sym("hylian_println"));
            }
            break;
        }

        /* General function call: evaluate args into arg registers
           System V AMD64: rdi, rsi, rdx, rcx, r8, r9 */
        static const char *arg_regs[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
        int nargs = call->arg_count;
        if (nargs > 6) nargs = 6; /* we only support up to 6 args */

        /* Push args onto stack in reverse to preserve them across evaluations */
        for (int i = 0; i < nargs; i++) {
            emit_expr(call->args[i], out, fn_name);
            fprintf(out, "    push rax\n");
        }
        /* Pop into registers in reverse order */
        for (int i = nargs - 1; i >= 0; i--) {
            fprintf(out, "    pop %s\n", arg_regs[i]);
        }
        fprintf(out, "    call %s\n", rewrite_call_name(call->name));
        /* result in rax */
        break;
    }

    case NODE_MEMBER_ACCESS: {
        MemberAccessNode *ma = (MemberAccessNode *)node;
        const char *class_name = expr_class_name(ma->object);

        /* ── array.len ─────────────────────────────────────────────────── */
        if (class_name && strcmp(class_name, "array") == 0) {
            if (strcmp(ma->member, "len") == 0) {
                emit_expr(ma->object, out, fn_name);
                fprintf(out, "    mov rax, [rax + 0]\n"); /* length at offset 0 */
                break;
            }
            if (strcmp(ma->member, "cap") == 0) {
                emit_expr(ma->object, out, fn_name);
                fprintf(out, "    mov rax, [rax + 8]\n"); /* capacity at offset 8 */
                break;
            }
            fprintf(out, "    ; array: unknown member '%s'\n", ma->member);
            fprintf(out, "    xor rax, rax\n");
            break;
        }

        /* ── multi.tag / multi.value ────────────────────────────────────── */
        if (class_name && strcmp(class_name, "multi") == 0) {
            if (strcmp(ma->member, "tag") == 0) {
                emit_expr(ma->object, out, fn_name);
                fprintf(out, "    mov rax, [rax + 0]\n"); /* tag at offset 0 */
                break;
            }
            if (strcmp(ma->member, "value") == 0) {
                emit_expr(ma->object, out, fn_name);
                fprintf(out, "    mov rax, [rax + 8]\n"); /* value at offset 8 */
                break;
            }
            fprintf(out, "    ; multi: unknown member '%s'\n", ma->member);
            fprintf(out, "    xor rax, rax\n");
            break;
        }

        /* ── EnumName.Variant → integer constant ────────────────────────── */
        if (ma->object->type == NODE_IDENTIFIER) {
            const char *id = ((IdentifierNode *)ma->object)->name;
            EnumRegEntry *ei = enum_find(id);
            if (ei) {
                int val = enum_variant_value(id, ma->member);
                if (val >= 0) {
                    fprintf(out, "    mov rax, %d\n", val);
                } else {
                    fprintf(out, "    ; unknown enum variant '%s::%s'\n", id, ma->member);
                    fprintf(out, "    xor rax, rax\n");
                }
                break;
            }
        }

        if (!class_name) {
            fprintf(out, "    ; member access: unknown class for .%s\n", ma->member);
            fprintf(out, "    xor rax, rax\n");
            break;
        }
        ClassInfo *ci = class_find(class_name);
        if (!ci) {
            fprintf(out, "    ; member access: class '%s' not registered\n", class_name);
            fprintf(out, "    xor rax, rax\n");
            break;
        }
        int field_off = class_field_offset(ci, ma->member);
        if (field_off < 0) {
            fprintf(out, "    ; member access: field '%s' not found in '%s'\n", ma->member, class_name);
            fprintf(out, "    xor rax, rax\n");
            break;
        }
        if (ma->object->type == NODE_IDENTIFIER) {
            const char *varname = ((IdentifierNode *)ma->object)->name;
            int local_off = locals_find(varname);
            if (local_off >= 0) {
                fprintf(out, "    mov rax, [rbp - %d]\n", local_off); /* load pointer */
                fprintf(out, "    mov rax, [rax + %d]\n", field_off); /* deref field  */
            } else {
                fprintf(out, "    ; unknown local '%s'\n", varname);
                fprintf(out, "    xor rax, rax\n");
            }
        } else {
            emit_expr(ma->object, out, fn_name);
            fprintf(out, "    mov rax, [rax + %d]\n", field_off);
        }
        break;
    }

    case NODE_METHOD_CALL: {
        MethodCallNode *mc = (MethodCallNode *)node;
        const char *class_name = expr_class_name(mc->object);
        if (!class_name) {
            fprintf(out, "    ; method call: unknown class for .%s()\n", mc->method);
            fprintf(out, "    xor rax, rax\n");
            break;
        }
        /* ── .message() on an Error* ────────────────────────────────────── */
        if (strcmp(mc->method, "message") == 0) {
            /* Error.message field is at offset 0 — just dereference the ptr */
            emit_expr(mc->object, out, fn_name);
            fprintf(out, "    ; .message() — load Error.message ptr at offset 0\n");
            fprintf(out, "    mov rax, [rax + 0]\n");
            break;
        }

        /* ── array.push(val) ─────────────────────────────────────────────── */
        if (strcmp(class_name, "array") == 0 && strcmp(mc->method, "push") == 0) {
            if (mc->arg_count < 1) {
                fprintf(out, "    ; array.push(): missing argument\n");
                fprintf(out, "    xor rax, rax\n");
                break;
            }
            /* Realloc may move the array block, so we need the variable's stack
               slot to update the stored pointer.  Only supported for simple
               local identifier objects. */
            int var_off = -1;
            if (mc->object->type == NODE_IDENTIFIER)
                var_off = locals_find(((IdentifierNode *)mc->object)->name);
            if (var_off < 0) {
                fprintf(out, "    ; array.push(): object is not a simple local — unsupported\n");
                fprintf(out, "    xor rax, rax\n");
                break;
            }

            int lbl_fast  = next_label();
            int lbl_dbl   = next_label();
            int lbl_alloc = next_label();

            /* 1: evaluate argument, save on stack */
            emit_expr(mc->args[0], out, fn_name);
            fprintf(out, "    push rax\n");                         /* stack: [val] */

            /* 2: load array pointer into r11 */
            fprintf(out, "    mov r11, [rbp - %d]\n", var_off);

            /* 3: bounds-check — compare length vs capacity */
            fprintf(out, "    mov rax, [r11]\n");                   /* rax = length  */
            fprintf(out, "    cmp rax, [r11 + 8]\n");              /* vs capacity   */
            fprintf(out, "    jl .L%d\n", lbl_fast);               /* room? → skip realloc */

            /* 4: realloc — double capacity (bootstrap to 8 if currently 0) */
            fprintf(out, "    mov rax, [r11 + 8]\n");              /* rax = capacity */
            fprintf(out, "    test rax, rax\n");
            fprintf(out, "    jnz .L%d\n", lbl_dbl);
            fprintf(out, "    mov rax, 8\n");
            fprintf(out, "    jmp .L%d\n", lbl_alloc);
            fprintf(out, ".L%d:\n", lbl_dbl);
            fprintf(out, "    shl rax, 1\n");                      /* new_cap = cap * 2 */
            fprintf(out, ".L%d:\n", lbl_alloc);
            /* rax = new_cap; push it so it survives the call */
            fprintf(out, "    push rax\n");                        /* stack: [new_cap, val] */
            fprintf(out, "    mov rsi, rax\n");
            fprintf(out, "    shl rsi, 3\n");                      /* rsi = new_cap * 8   */
            fprintf(out, "    add rsi, 16\n");                     /* rsi = new byte size */
            fprintf(out, "    mov rdi, r11\n");                    /* rdi = old ptr       */
            fprintf(out, "    call realloc\n");
            fprintf(out, "    mov r11, rax\n");                    /* r11 = new ptr       */
            fprintf(out, "    mov [rbp - %d], r11\n", var_off);   /* update variable     */
            fprintf(out, "    pop rax\n");                         /* stack: [val]; rax = new_cap */
            fprintf(out, "    mov [r11 + 8], rax\n");             /* update capacity field */

            /* 5: write element and bump length */
            fprintf(out, ".L%d:\n", lbl_fast);
            fprintf(out, "    mov rax, [r11]\n");                  /* rax = length (fresh load) */
            fprintf(out, "    pop rcx\n");                         /* stack: []; rcx = val */
            fprintf(out, "    lea rdx, [r11 + 16]\n");            /* element base  */
            fprintf(out, "    mov [rdx + rax*8], rcx\n");         /* elements[len] = val */
            fprintf(out, "    inc qword [r11]\n");                 /* length++      */
            fprintf(out, "    xor rax, rax\n");                   /* push → void   */
            break;
        }

        /* ── array.pop() ──────────────────────────────────────────────────── */
        if (strcmp(class_name, "array") == 0 && strcmp(mc->method, "pop") == 0) {
            /* Load array pointer; emit_self_addr puts it in rdi */
            emit_self_addr(mc->object, out, fn_name);
            fprintf(out, "    mov r11, rdi\n");                    /* r11 = array ptr */
            fprintf(out, "    dec qword [r11]\n");                 /* length--        */
            fprintf(out, "    mov rax, [r11]\n");                  /* rax = new length = last index */
            fprintf(out, "    lea rdx, [r11 + 16]\n");            /* element base    */
            fprintf(out, "    mov rax, [rdx + rax*8]\n");         /* rax = elements[last] */
            break;
        }

        char *mangled = mangle_method(class_name, mc->method);
        fprintf(out, "    ; call %s.%s()\n", class_name, mc->method);

        static const char *mc_arg_regs[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
        int nargs = mc->arg_count;
        if (nargs > 5) nargs = 5; /* rdi reserved for self */

        /* Evaluate args and push */
        for (int i = 0; i < nargs; i++) {
            emit_expr(mc->args[i], out, fn_name);
            fprintf(out, "    push rax\n");
        }
        /* Get self address into rdi, then push it */
        emit_self_addr(mc->object, out, fn_name);
        fprintf(out, "    push rdi\n");

        /* Pop: self -> rdi, args -> rsi..r9 */
        fprintf(out, "    pop rdi\n");
        for (int i = nargs - 1; i >= 0; i--) {
            fprintf(out, "    pop %s\n", mc_arg_regs[i + 1]);
        }
        fprintf(out, "    call %s\n", mangled);
        free(mangled);
        break;
    }

    case NODE_NEW: {
        NewNode *nn = (NewNode *)node;
        ClassInfo *ci = class_find(nn->class_name);
        if (!ci) {
            fprintf(out, "    ; new: unknown class '%s'\n", nn->class_name);
            fprintf(out, "    xor rax, rax\n");
            break;
        }
        fprintf(out, "    ; new %s (size=%d)\n", nn->class_name, ci->size);
        /* Allocate via malloc */
        fprintf(out, "    mov rdi, %d\n", ci->size);
        fprintf(out, "    call malloc\n");
        /* Zero-fill with rep stosb */
        fprintf(out, "    push rax\n");          /* save ptr */
        fprintf(out, "    mov rdi, rax\n");
        fprintf(out, "    xor al, al\n");
        fprintf(out, "    mov rcx, %d\n", ci->size);
        fprintf(out, "    rep stosb\n");
        fprintf(out, "    pop rax\n");            /* restore ptr */

        if (ci->has_ctor) {
            static const char *new_arg_regs[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
            int nargs = nn->arg_count;
            if (nargs > 5) nargs = 5;

            char *ctor_name = mangle_ctor(nn->class_name);

            /* Save object pointer */
            fprintf(out, "    push rax\n");      /* save obj ptr for args */

            /* Evaluate args and push */
            for (int i = 0; i < nargs; i++) {
                emit_expr(nn->args[i], out, fn_name);
                fprintf(out, "    push rax\n");
            }
            /* Pop args into rsi..r9 */
            for (int i = nargs - 1; i >= 0; i--) {
                fprintf(out, "    pop %s\n", new_arg_regs[i + 1]);
            }
            /* Pop self into rdi */
            fprintf(out, "    pop rdi\n");
            fprintf(out, "    push rdi\n");      /* save self for after ctor */
            fprintf(out, "    call %s\n", ctor_name);
            free(ctor_name);

            /* Result = object pointer */
            fprintf(out, "    pop rax\n");
        }
        break;
    }

    case NODE_INTERP_STRING: {
        InterpStringNode *istr = (InterpStringNode *)node;

        /* Save callee-saved registers we will clobber */
        fprintf(out, "    push r13\n");
        fprintf(out, "    push r14\n");
        fprintf(out, "    push r15\n");
        /* 3 pushes = 24 bytes.  Sub 512 more; total = 536 — not 16-byte
           aligned (536 % 16 = 8), so sub 520 to make total 544 (16-byte aligned). */
        fprintf(out, "    sub rsp, 520\n");
        fprintf(out, "    mov r13, rsp\n");  /* r13 = start of 512-byte result buffer */
        fprintf(out, "    mov r14, rsp\n");  /* r14 = write pointer                   */

        for (int i = 0; i < istr->seg_count; i++) {
            InterpSegment *seg = &istr->segments[i];
            if (!seg->is_expr) {
                /* Literal text segment */
                size_t len = strlen(seg->text);
                if (len > 0) {
                    const char *lbl = register_string(seg->text);
                    fprintf(out, "    ; interp literal: \"%s\"\n", seg->text);
                    fprintf(out, "    lea rsi, [rel %s]\n", lbl);
                    fprintf(out, "    mov rdi, r14\n");
                    fprintf(out, "    mov rcx, %zu\n", len);
                    fprintf(out, "    rep movsb\n");
                    fprintf(out, "    mov r14, rdi\n");
                }
            } else {
                /* Expression segment: look up as local variable name */
                const char *varname = seg->text;
                int off = locals_find(varname);
                if (off >= 0) {
                    const char *vtype = locals_type(varname);
                    int is_str = (vtype && strcmp(vtype, "str") == 0);
                    fprintf(out, "    ; interp expr: %s (type: %s)\n", varname, vtype ? vtype : "int");
                    fprintf(out, "    mov rax, [rbp - %d]\n", off);
                    if (is_str) {
                        /* rax = char* pointer — get its length with strlen, then copy */
                        fprintf(out, "    push r14\n");
                        fprintf(out, "    mov rdi, rax\n");
                        fprintf(out, "    call %s\n", sym("strlen"));
                        fprintf(out, "    mov rcx, rax\n");
                        fprintf(out, "    pop r14\n");
                        fprintf(out, "    mov rsi, [rbp - %d]\n", off);
                        fprintf(out, "    mov rdi, r14\n");
                        fprintf(out, "    rep movsb\n");
                        fprintf(out, "    mov r14, rdi\n");
                    } else {
                        /* integer — convert to string into a 32-byte scratch area */
                        fprintf(out, "    push r14\n");
                        fprintf(out, "    sub rsp, 32\n");
                        fprintf(out, "    mov rdi, rax\n");
                        fprintf(out, "    mov rsi, rsp\n");
                        fprintf(out, "    mov rdx, 32\n");
                        fprintf(out, "    call %s\n", sym("hylian_int_to_str"));
                        /* rax = length written into scratch buffer */
                        fprintf(out, "    mov rcx, rax\n");
                        fprintf(out, "    mov rsi, rsp\n");
                        fprintf(out, "    add rsp, 32\n");
                        fprintf(out, "    pop r14\n");
                        fprintf(out, "    mov rdi, r14\n");
                        fprintf(out, "    rep movsb\n");
                        fprintf(out, "    mov r14, rdi\n");
                    }
                } else {
                    fprintf(out, "    ; interp expr '%s' not found as local — skipped\n", varname);
                }
            }
        }

        /* Null-terminate the result buffer */
        fprintf(out, "    mov byte [r14], 0\n");

        /* rax = pointer to buffer, r15 = length.
           Stack layout from current rsp upward:
             [0 .. 519]  512-byte buffer + 8 pad
             [520]       saved r15  (push r15 — 8 bytes)
             [528]       saved r14  (push r14)
             [536]       saved r13  (push r13)
           We restore r13/r14 now but leave rsp pointing at the buffer so
           it stays alive.  The caller is responsible for doing
               add rsp, 544
           after it has finished reading rax/r15. */
        fprintf(out, "    mov rax, r13\n");
        fprintf(out, "    sub r14, r13\n");
        fprintf(out, "    mov r15, r14\n");
        fprintf(out, "    mov r13, [rsp + 536]\n");
        fprintf(out, "    mov r14, [rsp + 528]\n");
        /* r15 = length; rax = ptr to buffer still on stack.
           Caller must emit: add rsp, 544   to clean up. */
        break;
    }

    case NODE_ARRAY_LITERAL: {
        ArrayLiteralNode *al = (ArrayLiteralNode *)node;
        int count = al->elem_count;
        int capacity = count < 8 ? 8 : count;
        /* malloc((capacity + 2) * 8) bytes: 2 words header + capacity elements */
        fprintf(out, "    ; array literal with %d elements\n", count);
        fprintf(out, "    mov rdi, %d\n", (capacity + 2) * 8);
        fprintf(out, "    call malloc\n");
        /* rax = array pointer; save it in r11 across element evaluations */
        fprintf(out, "    push r11\n");
        fprintf(out, "    mov r11, rax\n");
        /* write header: length = count, capacity = capacity */
        fprintf(out, "    mov qword [r11 + 0], %d\n", count);
        fprintf(out, "    mov qword [r11 + 8], %d\n", capacity);
        /* evaluate each element and store */
        for (int i = 0; i < count; i++) {
            emit_expr(al->elements[i], out, fn_name);
            fprintf(out, "    mov [r11 + %d], rax\n", 16 + i * 8);
        }
        fprintf(out, "    mov rax, r11\n");
        fprintf(out, "    pop r11\n");
        break;
    }

    case NODE_INDEX: {
        IndexNode *ix = (IndexNode *)node;
        /* evaluate object -> rax (array pointer), save in r11 */
        emit_expr(ix->object, out, fn_name);
        fprintf(out, "    push r11\n");
        fprintf(out, "    mov r11, rax\n");
        /* evaluate index -> rax */
        emit_expr(ix->index, out, fn_name);
        fprintf(out, "    mov rcx, rax\n");
        /* compute offset: 16 + rcx*8 */
        fprintf(out, "    imul rcx, rcx, 8\n");
        fprintf(out, "    add rcx, 16\n");
        fprintf(out, "    add rcx, r11\n");
        fprintf(out, "    mov rax, [rcx]\n");
        fprintf(out, "    pop r11\n");
        break;
    }

    default:
        fprintf(out, "    ; unsupported expr node type %d\n", node->type);
        fprintf(out, "    xor rax, rax\n");
        break;
    }
}

/* ─── Statement emission ─────────────────────────────────────────────────────── */

static void emit_stmts(ASTNode **stmts, int count, FILE *out, const char *fn_name) {
    for (int i = 0; i < count; i++)
        emit_stmt(stmts[i], out, fn_name);
}

static void emit_stmt(ASTNode *node, FILE *out, const char *fn_name) {
    if (!node) return;

    switch (node->type) {

    case NODE_VAR_DECL: {
        VarDeclNode *vd = (VarDeclNode *)node;

        if (vd->var_type.kind == TYPE_ARRAY) {
            /* Heap-allocated array: [ptr+0]=length, [ptr+8]=capacity, [ptr+16..]=elements */
            int off = locals_alloc_typed(vd->var_name, "array");
            if (vd->initializer && vd->initializer->type == NODE_ARRAY_LITERAL) {
                /* emit_expr for NODE_ARRAY_LITERAL already does the right thing */
                emit_expr(vd->initializer, out, fn_name);
            } else if (vd->initializer) {
                /* Some other expression that yields an array pointer */
                emit_expr(vd->initializer, out, fn_name);
            } else {
                /* No initializer: allocate 8-slot empty array */
                fprintf(out, "    ; array<%s> default alloc (8 slots)\n",
                        vd->var_type.elem_type_count > 0 ? vd->var_type.elem_types[0].name : "?");
                fprintf(out, "    mov rdi, %d\n", (8 + 2) * 8); /* 10 * 8 = 80 bytes */
                fprintf(out, "    call malloc\n");
                fprintf(out, "    mov qword [rax + 0], 0\n");  /* length = 0 */
                fprintf(out, "    mov qword [rax + 8], 8\n");  /* capacity = 8 */
                /* zero the 8 element slots */
                fprintf(out, "    push r11\n");
                fprintf(out, "    mov r11, rax\n");
                for (int i = 0; i < 8; i++)
                    fprintf(out, "    mov qword [r11 + %d], 0\n", 16 + i * 8);
                fprintf(out, "    mov rax, r11\n");
                fprintf(out, "    pop r11\n");
            }
            fprintf(out, "    mov [rbp - %d], rax\n", off);
            break;
        }

        if (vd->var_type.kind == TYPE_MULTI) {
            /* Heap-allocated tagged union: [ptr+0]=tag, [ptr+8]=value */
            int off = locals_alloc_typed(vd->var_name, "multi");
            if (vd->initializer) {
                /* Determine tag from resolved_type of initializer vs multi elem_types */
                int tag = 0;
                Type init_type = vd->initializer->resolved_type;
                for (int ti = 0; ti < vd->var_type.elem_type_count; ti++) {
                    Type *et = &vd->var_type.elem_types[ti];
                    if (et->name && init_type.name &&
                        strcmp(et->name, init_type.name) == 0) {
                        tag = ti;
                        break;
                    }
                }
                emit_expr(vd->initializer, out, fn_name);
                fprintf(out, "    push rax\n");           /* save init value */
                fprintf(out, "    mov rdi, 16\n");
                fprintf(out, "    call malloc\n");
                fprintf(out, "    pop rcx\n");            /* rcx = init value */
                fprintf(out, "    mov qword [rax + 0], %d\n", tag); /* tag */
                fprintf(out, "    mov [rax + 8], rcx\n");            /* value */
            } else {
                fprintf(out, "    mov rdi, 16\n");
                fprintf(out, "    call malloc\n");
                fprintf(out, "    mov qword [rax + 0], 0\n");
                fprintf(out, "    mov qword [rax + 8], 0\n");
            }
            fprintf(out, "    mov [rbp - %d], rax\n", off);
            break;
        }

        /* Default: simple type */
        int off = locals_alloc_typed(vd->var_name, vd->var_type.name);
        if (vd->initializer) {
            emit_expr(vd->initializer, out, fn_name);
        } else {
            fprintf(out, "    xor rax, rax\n");
        }
        fprintf(out, "    mov [rbp - %d], rax\n", off);
        break;
    }

    case NODE_ASSIGN: {
        AssignNode *as = (AssignNode *)node;
        emit_expr(as->value, out, fn_name);
        int off = locals_find(as->var_name);
        if (off >= 0) {
            fprintf(out, "    mov [rbp - %d], rax\n", off);
        } else if (current_class_name) {
            /* Try to resolve as a field of self */
            ClassInfo *ci = class_find(current_class_name);
            if (ci) {
                int field_off = class_field_offset(ci, as->var_name);
                if (field_off >= 0) {
                    int self_off = locals_find("self");
                    if (self_off >= 0) {
                        fprintf(out, "    push rax\n");                        /* save value  */
                        fprintf(out, "    mov rcx, [rbp - %d]\n", self_off);  /* load self   */
                        fprintf(out, "    pop rax\n");
                        fprintf(out, "    mov [rcx + %d], rax\n", field_off);
                        break;
                    }
                }
            }
            fprintf(out, "    ; assign to unknown var '%s'\n", as->var_name);
        } else {
            fprintf(out, "    ; assign to unknown var '%s'\n", as->var_name);
        }
        break;
    }

    case NODE_COMPOUND_ASSIGN: {
        CompoundAssignNode *ca = (CompoundAssignNode *)node;
        int off = locals_find(ca->var_name);
        if (off < 0) {
            fprintf(out, "    ; compound assign to unknown var '%s'\n", ca->var_name);
            break;
        }
        /* Load current value */
        fprintf(out, "    mov rcx, [rbp - %d]\n", off);
        emit_expr(ca->value, out, fn_name);
        /* rax = RHS, rcx = current */
        if (strcmp(ca->op, "+=") == 0) {
            fprintf(out, "    add rcx, rax\n");
            fprintf(out, "    mov rax, rcx\n");
        } else if (strcmp(ca->op, "-=") == 0) {
            fprintf(out, "    sub rcx, rax\n");
            fprintf(out, "    mov rax, rcx\n");
        } else if (strcmp(ca->op, "*=") == 0) {
            fprintf(out, "    imul rcx, rax\n");
            fprintf(out, "    mov rax, rcx\n");
        } else if (strcmp(ca->op, "/=") == 0) {
            fprintf(out, "    xchg rax, rcx\n");
            fprintf(out, "    cqo\n");
            fprintf(out, "    idiv rcx\n");
        } else {
            fprintf(out, "    ; unsupported compound op '%s'\n", ca->op);
            fprintf(out, "    mov rax, rcx\n");
        }
        fprintf(out, "    mov [rbp - %d], rax\n", off);
        break;
    }

    case NODE_RETURN: {
        ReturnNode *ret = (ReturnNode *)node;
        if (ret->value) {
            emit_expr(ret->value, out, fn_name);
        } else {
            fprintf(out, "    xor rax, rax\n");
        }
        fprintf(out, "    mov rsp, rbp\n");
        fprintf(out, "    pop rbp\n");
        fprintf(out, "    ret\n");
        break;
    }

    case NODE_IF: {
        IfNode *nd = (IfNode *)node;
        int lbl_else = next_label();
        int lbl_end  = next_label();

        emit_expr(nd->condition, out, fn_name);
        fprintf(out, "    test rax, rax\n");
        if (nd->else_body && nd->else_count > 0) {
            fprintf(out, "    jz .L%d\n", lbl_else);
            emit_stmts(nd->then_body, nd->then_count, out, fn_name);
            fprintf(out, "    jmp .L%d\n", lbl_end);
            fprintf(out, ".L%d:\n", lbl_else);
            emit_stmts(nd->else_body, nd->else_count, out, fn_name);
            fprintf(out, ".L%d:\n", lbl_end);
        } else {
            fprintf(out, "    jz .L%d\n", lbl_end);
            emit_stmts(nd->then_body, nd->then_count, out, fn_name);
            fprintf(out, ".L%d:\n", lbl_end);
        }
        break;
    }

    case NODE_WHILE: {
        WhileNode *nd = (WhileNode *)node;
        int lbl_loop = next_label();
        int lbl_end  = next_label();

        loop_stack_push(lbl_loop, lbl_end);
        fprintf(out, ".L%d:\n", lbl_loop);
        emit_expr(nd->condition, out, fn_name);
        fprintf(out, "    test rax, rax\n");
        fprintf(out, "    jz .L%d\n", lbl_end);
        emit_stmts(nd->body, nd->body_count, out, fn_name);
        fprintf(out, "    jmp .L%d\n", lbl_loop);
        fprintf(out, ".L%d:\n", lbl_end);
        loop_stack_pop();
        break;
    }

    case NODE_FOR: {
        ForNode *nd = (ForNode *)node;
        int lbl_loop = next_label();
        int lbl_post = next_label();
        int lbl_end  = next_label();

        if (nd->init)
            emit_stmt(nd->init, out, fn_name);

        /* continue jumps to lbl_post (post-increment), break jumps to lbl_end */
        loop_stack_push(lbl_post, lbl_end);
        fprintf(out, ".L%d:\n", lbl_loop);
        if (nd->condition) {
            emit_expr(nd->condition, out, fn_name);
            fprintf(out, "    test rax, rax\n");
            fprintf(out, "    jz .L%d\n", lbl_end);
        }
        emit_stmts(nd->body, nd->body_count, out, fn_name);
        fprintf(out, ".L%d:\n", lbl_post);
        if (nd->post)
            emit_stmt(nd->post, out, fn_name);
        fprintf(out, "    jmp .L%d\n", lbl_loop);
        fprintf(out, ".L%d:\n", lbl_end);
        loop_stack_pop();
        break;
    }

    case NODE_FUNC_CALL: {
        /* Statement-level function call: emit and discard result */
        emit_expr(node, out, fn_name);
        break;
    }

    case NODE_BREAK: {
        LoopLabels *ll = loop_stack_peek();
        if (ll) {
            fprintf(out, "    jmp .L%d\n", ll->break_label);
        } else {
            fprintf(out, "    ; break outside loop\n");
        }
        break;
    }

    case NODE_CONTINUE: {
        LoopLabels *ll = loop_stack_peek();
        if (ll) {
            fprintf(out, "    jmp .L%d\n", ll->continue_label);
        } else {
            fprintf(out, "    ; continue outside loop\n");
        }
        break;
    }

    case NODE_ASM_BLOCK: {
        AsmBlockNode *ab = (AsmBlockNode *)node;
        fprintf(out, "%s\n", ab->body);
        break;
    }

    case NODE_DEFER: {
        fprintf(out, "    ; defer not supported in asm backend\n");
        break;
    }

    case NODE_MEMBER_ASSIGN: {
        MemberAssignNode *ma = (MemberAssignNode *)node;
        const char *class_name = NULL;
        int obj_local_off = -1;

        if (ma->object == NULL) {
            /* Implicit self — inside a method */
            obj_local_off = locals_find("self");
            class_name = current_class_name;
        } else if (ma->object->type == NODE_IDENTIFIER) {
            const char *varname = ((IdentifierNode *)ma->object)->name;
            obj_local_off = locals_find(varname);
            class_name = locals_type(varname);
        }

        if (!class_name) {
            fprintf(out, "    ; member assign: unknown class\n");
            break;
        }
        ClassInfo *ci_ma = class_find(class_name);
        if (!ci_ma) {
            fprintf(out, "    ; member assign: class '%s' not registered\n", class_name);
            break;
        }
        int field_off = class_field_offset(ci_ma, ma->member);
        if (field_off < 0) {
            fprintf(out, "    ; member assign: field '%s' not in '%s'\n", ma->member, class_name);
            break;
        }

        /* Evaluate value */
        emit_expr(ma->value, out, fn_name);
        fprintf(out, "    push rax\n");   /* save value */

        /* Get object pointer into rcx */
        if (obj_local_off >= 0) {
            fprintf(out, "    mov rcx, [rbp - %d]\n", obj_local_off);
        } else if (ma->object) {
            emit_expr(ma->object, out, fn_name);
            fprintf(out, "    mov rcx, rax\n");
        } else {
            fprintf(out, "    ; member assign: no object\n");
            fprintf(out, "    add rsp, 8\n");
            break;
        }

        fprintf(out, "    pop rax\n");
        fprintf(out, "    mov [rcx + %d], rax\n", field_off);
        break;
    }

    case NODE_METHOD_CALL: {
        /* Delegate to emit_expr which handles NODE_METHOD_CALL */
        emit_expr(node, out, fn_name);
        break;
    }

    case NODE_INDEX_ASSIGN: {
        IndexAssignNode *ia = (IndexAssignNode *)node;
        /* Evaluate value first, push it */
        emit_expr(ia->value, out, fn_name);
        fprintf(out, "    push rax\n");              /* save value */
        /* Evaluate object (array pointer) -> rax, save to r11 */
        emit_expr(ia->object, out, fn_name);
        fprintf(out, "    push r11\n");
        fprintf(out, "    mov r11, rax\n");          /* r11 = array ptr */
        /* Evaluate index -> rax */
        emit_expr(ia->index, out, fn_name);
        fprintf(out, "    mov rcx, rax\n");          /* rcx = index */
        /* Compute address: r11 + 16 + rcx*8 */
        fprintf(out, "    imul rcx, rcx, 8\n");
        fprintf(out, "    add rcx, 16\n");
        fprintf(out, "    add rcx, r11\n");
        /* Pop value into rdx */
        fprintf(out, "    pop r11\n");               /* restore r11 first */
        fprintf(out, "    pop rdx\n");               /* rdx = value */
        fprintf(out, "    mov [rcx], rdx\n");
        break;
    }

    case NODE_FOR_IN: {
        ForInNode *fi = (ForInNode *)node;
        int lbl_loop = next_label();
        int lbl_end  = next_label();
        int idx_lbl  = next_label(); /* used to make unique hidden var names */

        /* Hidden locals: __for_idx_N (loop counter), __for_arr_N (array ptr),
           __for_len_N (cached length).  We use synthesised names with the
           label number so nested for-in loops don't collide. */
        char idx_name[64], arr_name[64], len_name[64];
        snprintf(idx_name, sizeof(idx_name), "__for_idx_%d", idx_lbl);
        snprintf(arr_name, sizeof(arr_name), "__for_arr_%d", idx_lbl);
        snprintf(len_name, sizeof(len_name), "__for_len_%d", idx_lbl);

        /* Evaluate collection -> rax (array pointer) */
        emit_expr(fi->collection, out, fn_name);
        fprintf(out, "    push r11\n");
        fprintf(out, "    mov r11, rax\n");          /* r11 = array ptr */

        /* Allocate hidden locals */
        int arr_off = locals_alloc(arr_name);
        int len_off = locals_alloc(len_name);
        int idx_off = locals_alloc(idx_name);
        /* Allocate loop variable — use element type from resolved_type if available */
        const char *elem_type_name = NULL;
        if (fi->collection->resolved_type.kind == TYPE_ARRAY &&
            fi->collection->resolved_type.elem_type_count > 0) {
            elem_type_name = fi->collection->resolved_type.elem_types[0].name;
        }
        int var_off = locals_alloc_typed(fi->var_name, elem_type_name);

        /* Store array ptr */
        fprintf(out, "    mov [rbp - %d], r11\n", arr_off);
        /* Load and store length */
        fprintf(out, "    mov rax, [r11 + 0]\n");
        fprintf(out, "    mov [rbp - %d], rax\n", len_off);
        /* index = 0 */
        fprintf(out, "    mov qword [rbp - %d], 0\n", idx_off);
        fprintf(out, "    pop r11\n");

        loop_stack_push(lbl_loop, lbl_end);
        fprintf(out, ".L%d:\n", lbl_loop);

        /* if index >= length, exit */
        fprintf(out, "    mov rax, [rbp - %d]\n", idx_off);
        fprintf(out, "    cmp rax, [rbp - %d]\n", len_off);
        fprintf(out, "    jge .L%d\n", lbl_end);

        /* Load element: arr[idx] = [arr_ptr + 16 + idx*8] */
        fprintf(out, "    push r11\n");
        fprintf(out, "    mov r11, [rbp - %d]\n", arr_off);
        fprintf(out, "    mov rcx, [rbp - %d]\n", idx_off);
        fprintf(out, "    imul rcx, rcx, 8\n");
        fprintf(out, "    add rcx, 16\n");
        fprintf(out, "    add rcx, r11\n");
        fprintf(out, "    mov rax, [rcx]\n");
        fprintf(out, "    pop r11\n");
        fprintf(out, "    mov [rbp - %d], rax\n", var_off);

        emit_stmts(fi->body, fi->body_count, out, fn_name);

        /* increment index */
        fprintf(out, "    mov rax, [rbp - %d]\n", idx_off);
        fprintf(out, "    inc rax\n");
        fprintf(out, "    mov [rbp - %d], rax\n", idx_off);
        fprintf(out, "    jmp .L%d\n", lbl_loop);
        fprintf(out, ".L%d:\n", lbl_end);
        loop_stack_pop();
        break;
    }

    default:
        /* Try to handle as expression */
        fprintf(out, "    ; stmt node type %d — emitting as expr\n", node->type);
        emit_expr(node, out, fn_name);
        break;
    }
}

/* ─── Function emission ──────────────────────────────────────────────────────── */

/* Returns 1 if the last statement in the list is a NODE_RETURN (so the
   implicit fallthrough epilogue can be skipped). */
static int last_stmt_is_return(ASTNode **body, int body_count) {
    if (body_count == 0) return 0;
    ASTNode *last = body[body_count - 1];
    if (!last) return 0;
    return last->type == NODE_RETURN;
}

static void emit_function(const char *name, ASTNode **params, int param_count,
                          ASTNode **body, int body_count,
                          int is_main, FILE *out) {
    locals_reset();

    /* Pre-allocate parameter locals */
    static const char *arg_regs[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
    int nparams = param_count;
    if (nparams > 6) nparams = 6;

    /* Count all var decls to determine stack frame size */
    int var_count = count_var_decls(body, body_count) + nparams;

    /* We allocate: nparams + all var decls.  Round up to 16-byte alignment. */
    int frame_bytes = var_count * 8;
    /* Align to 16: after push rbp the stack is 16-byte aligned.
       sub rsp, N  must keep it 16-byte aligned. */
    if (frame_bytes % 16 != 0)
        frame_bytes += 8;
    if (frame_bytes == 0)
        frame_bytes = 16; /* always allocate at least 16 for scratch space */

    /* Emit label */
    if (is_main)
        fprintf(out, "main:\n");
    else
        fprintf(out, "%s:\n", name);

    /* Prologue */
    fprintf(out, "    push rbp\n");
    fprintf(out, "    mov rbp, rsp\n");
    fprintf(out, "    sub rsp, %d\n", frame_bytes);

    /* Spill parameters to their stack slots */
    for (int i = 0; i < nparams; i++) {
        ASTNode *p = params[i];
        const char *pname = NULL;
        const char *ptype = NULL;
        if (p->type == NODE_VAR_DECL) {
            pname = ((VarDeclNode *)p)->var_name;
            ptype = ((VarDeclNode *)p)->var_type.name;
        } else if (p->type == NODE_IDENTIFIER) {
            pname = ((IdentifierNode *)p)->name;
        }
        if (pname) {
            int off = locals_alloc_typed(pname, ptype);
            fprintf(out, "    mov [rbp - %d], %s\n", off, arg_regs[i]);
        }
    }

    /* Emit body */
    emit_stmts(body, body_count, out, name);

    /* Implicit return — only emit if the body doesn't already end with a ret */
    if (!last_stmt_is_return(body, body_count)) {
        fprintf(out, "    xor rax, rax\n");
        fprintf(out, "    mov rsp, rbp\n");
        fprintf(out, "    pop rbp\n");
        fprintf(out, "    ret\n");
    }
    fprintf(out, "\n");
}

/* ─── Top-level codegen entry ────────────────────────────────────────────────── */

void codegen_asm(ProgramNode *root, FILE *out, const char *src_filename, const char *target) {
    current_target = target ? target : "linux";
    /* Reset global state */
    str_const_count = 0;
    _label_counter  = 0;
    loop_stack_top  = 0;
    fn_prefix_count = 0;

    /* Scan includes up front so io_included and fn_prefix rewrites are set
       before we emit any function bodies into the memstream buffer.
       We inline a minimal copy of the STD_MODULES table here so we don't
       have to forward-declare the full struct. */
    io_included = 0;
    {
        typedef struct {
            const char *include_path;
            const char *src_prefix; /* NULL = no rewrite */
            const char *abi_prefix; /* full C symbol prefix */
            int         sets_io;
        } EarlyMod;
        static const EarlyMod EARLY_MODS[] = {
            { "std.io",                    NULL,       NULL,                    1 },
            { "std.errors",                NULL,       NULL,                    0 },
            { "std.strings",               "str_",     "hylian_",               0 },
            { "std.system.filesystem",     NULL,       NULL,                    0 },
            { "std.system.env",            NULL,       NULL,                    0 },
            { "std.crypto",                "crypto_",  "hylian_crypto_",        0 },
            { "std.networking.tcp",        "tcp_",     "hylian_net_tcp_",       0 },
            { "std.networking.udp",        "udp_",     "hylian_net_udp_",       0 },
            { "std.networking.https",      "https_",   "hylian_net_https_",     0 },
        };
        static const int EARLY_MOD_COUNT =
            (int)(sizeof(EARLY_MODS) / sizeof(EARLY_MODS[0]));

        for (int i = 0; i < root->include_count; i++) {
            const char *inc = root->includes[i];
            if (!inc) continue;
            for (int m = 0; m < EARLY_MOD_COUNT; m++) {
                if (strcmp(inc, EARLY_MODS[m].include_path) == 0) {
                    if (EARLY_MODS[m].sets_io) io_included = 1;
                    if (EARLY_MODS[m].src_prefix &&
                        fn_prefix_count < MAX_FN_PREFIXES) {
                        fn_prefixes[fn_prefix_count].src_prefix =
                            EARLY_MODS[m].src_prefix;
                        fn_prefixes[fn_prefix_count].abi_prefix =
                            EARLY_MODS[m].abi_prefix;
                        fn_prefix_count++;
                    }
                }
            }
        }
    }

    /* We do a two-pass approach:
       Pass 1: walk declarations and register all string constants.
       Pass 2: emit the file.
       Instead of a true two-pass, we buffer function output and emit
       the .data section before .text. */

    /* We use a temporary in-memory buffer for .text section output,
       so we can emit .data first. */
    char  *text_buf  = NULL;
    size_t text_size = 0;
    FILE  *text_out  = open_memstream(&text_buf, &text_size);
    if (!text_out) {
        fprintf(stderr, "codegen_asm: open_memstream failed\n");
        return;
    }

    /* Emit global/extern declarations into text_out header area —
       we'll move these to the real out later. */

    /* Walk top-level declarations and emit functions */
    int found_main = 0;

    /* Pre-pass: register all classes and enums so they're known during emission */
    class_registry_reset();
    enum_registry_reset();
    for (int i = 0; i < root->decl_count; i++) {
        ASTNode *decl = root->declarations[i];
        if (decl && decl->type == NODE_CLASS)
            class_register((ClassNode *)decl);
        if (decl && decl->type == NODE_ENUM)
            enum_register((EnumNode *)decl);
    }

    static const char *emit_arg_regs[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };

    for (int i = 0; i < root->decl_count; i++) {
        ASTNode *decl = root->declarations[i];
        if (!decl) continue;

        if (decl->type == NODE_FUNC) {
            FuncNode *fn = (FuncNode *)decl;
            int is_main_fn = strcmp(fn->name, "main") == 0;
            if (is_main_fn) found_main = 1;
            emit_function(fn->name, fn->params, fn->param_count,
                          fn->body, fn->body_count,
                          is_main_fn, text_out);
        } else if (decl->type == NODE_ENUM) {
            EnumNode *en = (EnumNode *)decl;
            fprintf(text_out, "    ; enum %s\n", en->name);
            for (int j = 0; j < en->variant_count; j++)
                fprintf(text_out, "    ; %s::%s = %d\n",
                        en->name, en->variants[j].name, en->variants[j].value);
        } else if (decl->type == NODE_VAR_DECL) {
            /* Top-level var decl: emit as comment for now */
            fprintf(text_out, "    ; top-level var decl not emitted as code\n");
        } else if (decl->type == NODE_CLASS) {
            ClassNode *cls = (ClassNode *)decl;
            current_class_name = cls->name;

            /* ── Emit constructor ── */
            if (cls->has_ctor) {
                char *ctor_name = mangle_ctor(cls->name);
                locals_reset();

                int nparams = cls->ctor_param_count;
                if (nparams > 5) nparams = 5; /* rdi = self */

                int var_count = count_var_decls(cls->ctor_body, cls->ctor_body_count)
                                + nparams + 1; /* +1 for self */
                int frame_bytes = var_count * 8;
                if (frame_bytes % 16 != 0) frame_bytes += 8;
                if (frame_bytes == 0) frame_bytes = 16;

                fprintf(text_out, "%s:\n", ctor_name);
                fprintf(text_out, "    push rbp\n");
                fprintf(text_out, "    mov rbp, rsp\n");
                fprintf(text_out, "    sub rsp, %d\n", frame_bytes);

                /* Spill self */
                int self_off = locals_alloc_typed("self", cls->name);
                fprintf(text_out, "    mov [rbp - %d], rdi\n", self_off);

                /* Spill user params (rsi, rdx, ...) */
                for (int pi = 0; pi < nparams; pi++) {
                    ASTNode *p = cls->ctor_params[pi];
                    const char *pname = NULL;
                    const char *ptype = NULL;
                    if (p->type == NODE_VAR_DECL) {
                        pname = ((VarDeclNode *)p)->var_name;
                        ptype = ((VarDeclNode *)p)->var_type.name;
                    }
                    if (pname) {
                        int off = locals_alloc_typed(pname, ptype);
                        fprintf(text_out, "    mov [rbp - %d], %s\n", off, emit_arg_regs[pi + 1]);
                    }
                }

                emit_stmts(cls->ctor_body, cls->ctor_body_count, text_out, ctor_name);

                fprintf(text_out, "    mov rsp, rbp\n");
                fprintf(text_out, "    pop rbp\n");
                fprintf(text_out, "    ret\n\n");
                free(ctor_name);
            }

            /* ── Emit methods ── */
            for (int mi = 0; mi < cls->method_count; mi++) {
                MethodNode *m = cls->methods[mi];
                char *mangled = mangle_method(cls->name, m->name);
                locals_reset();

                int nparams = m->param_count;
                if (nparams > 5) nparams = 5;

                int var_count = count_var_decls(m->body, m->body_count)
                                + nparams + 1; /* +1 for self */
                int frame_bytes = var_count * 8;
                if (frame_bytes % 16 != 0) frame_bytes += 8;
                if (frame_bytes == 0) frame_bytes = 16;

                fprintf(text_out, "%s:\n", mangled);
                fprintf(text_out, "    push rbp\n");
                fprintf(text_out, "    mov rbp, rsp\n");
                fprintf(text_out, "    sub rsp, %d\n", frame_bytes);

                /* Spill self */
                int self_off = locals_alloc_typed("self", cls->name);
                fprintf(text_out, "    mov [rbp - %d], rdi\n", self_off);

                /* Spill user params */
                for (int pi = 0; pi < nparams; pi++) {
                    ASTNode *p = m->params[pi];
                    const char *pname = NULL;
                    const char *ptype = NULL;
                    if (p->type == NODE_VAR_DECL) {
                        pname = ((VarDeclNode *)p)->var_name;
                        ptype = ((VarDeclNode *)p)->var_type.name;
                    }
                    if (pname) {
                        int off = locals_alloc_typed(pname, ptype);
                        fprintf(text_out, "    mov [rbp - %d], %s\n", off, emit_arg_regs[pi + 1]);
                    }
                }

                emit_stmts(m->body, m->body_count, text_out, mangled);

                if (!last_stmt_is_return(m->body, m->body_count)) {
                    fprintf(text_out, "    xor rax, rax\n");
                    fprintf(text_out, "    mov rsp, rbp\n");
                    fprintf(text_out, "    pop rbp\n");
                    fprintf(text_out, "    ret\n");
                }
                fprintf(text_out, "\n");
                free(mangled);
            }

            current_class_name = NULL;
        }
    }

    /* If no main was found, create a minimal main */
    if (!found_main) {
        locals_reset();
        fprintf(text_out, "main:\n");
        fprintf(text_out, "    push rbp\n");
        fprintf(text_out, "    mov rbp, rsp\n");
        fprintf(text_out, "    sub rsp, 16\n");
        fprintf(text_out, "    xor rax, rax\n");
        fprintf(text_out, "    mov rsp, rbp\n");
        fprintf(text_out, "    pop rbp\n");
        fprintf(text_out, "    ret\n");
        fprintf(text_out, "\n");
    }

    fclose(text_out);

    /* ── Now emit the complete file ── */

    fprintf(out, "; Generated by Hylian compiler (asm backend)\n");
    if (src_filename)
        fprintf(out, "; Source: %s\n", src_filename);
    fprintf(out, "bits 64\n");
    fprintf(out, "default rel\n");
    fprintf(out, "\n");

    /* ── Stdlib module registry ───────────────────────────────────────────────
     *
     * To add a new stdlib module, add ONE entry here.
     *
     * Fields:
     *   include_path   — the "std.xxx" string users write in include {}
     *   obj_name       — the short .o name used on the link line  (e.g. "io")
     *   stem           — path stem under runtime/std/  (no extension)
     *   externs        — space-separated list of symbols to extern-declare
     *                    (NULL = none beyond always-on symbols)
     *   link_libs      — extra linker flags (e.g. "-lssl -lcrypto"), or NULL
     *   fn_prefix      — if non-NULL, calls whose name starts with this prefix
     *                    are rewritten to "hylian_" + name at the call site.
     *                    e.g. fn_prefix="crypto_" rewrites crypto_hash ->
     *                    hylian_crypto_hash in the emitted asm.
     *   sets_io        — 1 = sets io_included flag (only std.io)
     * ────────────────────────────────────────────────────────────────────────*/
    typedef struct {
        const char *include_path;
        const char *obj_name;
        const char *stem;
        const char *externs;      /* space-separated symbol names, or NULL */
        const char *link_libs;    /* extra -l flags for the link line, or NULL */
        const char *fn_prefix;    /* call-site name rewrite prefix, or NULL */
        int         sets_io;
    } StdModule;

    static const StdModule STD_MODULES[] = {
        /* include path              obj        stem                        externs                                           link_libs        fn_prefix    io? */
        { "std.io",                  "io",      "io",                       "hylian_print hylian_println hylian_int_to_str",  NULL,            NULL,        1 },
        { "std.errors",              "errors",  "errors",                   "hylian_make_error hylian_panic",                 NULL,            NULL,        0 },
        { "std.strings",             "strings", "strings",                  "hylian_length hylian_is_empty hylian_contains hylian_starts_with hylian_ends_with hylian_index_of hylian_slice hylian_trim hylian_trim_start hylian_trim_end hylian_to_upper hylian_to_lower hylian_replace hylian_split hylian_join hylian_to_int hylian_to_float hylian_from_int hylian_equals", NULL, "str_",      0 },
        { "std.system.filesystem",   "fs",      "system/filesystem",        "hylian_file_read hylian_file_write hylian_file_exists hylian_file_size hylian_mkdir", NULL, NULL, 0 },
        { "std.system.env",          "env",     "system/env",               "hylian_getenv hylian_exit",                      NULL,            NULL,        0 },
        { "std.crypto",              "crypto",  "crypto",                   "hylian_crypto_hash hylian_crypto_hash_hex hylian_crypto_hmac hylian_crypto_hmac_hex hylian_crypto_encrypt hylian_crypto_decrypt hylian_crypto_random_bytes hylian_crypto_random_int hylian_crypto_random_float hylian_crypto_constant_time_eq", "-lssl -lcrypto", "crypto_", 0 },
        { "std.networking.tcp",      "tcp",     "networking/tcp",           "hylian_net_tcp_connect hylian_net_tcp_listen hylian_net_tcp_accept hylian_net_tcp_send hylian_net_tcp_recv hylian_net_tcp_close hylian_net_tcp_set_nonblocking hylian_net_tcp_set_reuseaddr hylian_net_tcp_set_timeout", NULL, "tcp_", 0 },
        { "std.networking.udp",      "udp",     "networking/udp",           "hylian_net_udp_socket hylian_net_udp_bind hylian_net_udp_send_to hylian_net_udp_recv_from hylian_net_udp_connect hylian_net_udp_send hylian_net_udp_recv hylian_net_udp_close hylian_net_udp_set_nonblocking hylian_net_udp_set_timeout hylian_net_udp_set_broadcast hylian_net_udp_join_multicast", NULL, "udp_", 0 },
        { "std.networking.https",    "https",   "networking/https",         "hylian_net_https_connect hylian_net_https_send hylian_net_https_recv hylian_net_https_get hylian_net_https_post hylian_net_https_close hylian_net_https_body hylian_net_https_status", "-lssl -lcrypto", "https_", 0 },
    };
    static const int STD_MODULE_COUNT = (int)(sizeof(STD_MODULES) / sizeof(STD_MODULES[0]));

    /* Which modules are needed for this compilation unit */
    int mod_needed[sizeof(STD_MODULES) / sizeof(STD_MODULES[0])];
    for (int m = 0; m < STD_MODULE_COUNT; m++) mod_needed[m] = 0;

    for (int i = 0; i < root->include_count; i++) {
        const char *inc = root->includes[i];
        if (!inc) continue;
        for (int m = 0; m < STD_MODULE_COUNT; m++) {
            if (strcmp(inc, STD_MODULES[m].include_path) == 0) {
                mod_needed[m] = 1;
                if (STD_MODULES[m].sets_io) io_included = 1;
                if (STD_MODULES[m].fn_prefix && fn_prefix_count < MAX_FN_PREFIXES) {
                                /* second-pass registration is a no-op now — early scan
                                   already populated fn_prefixes before body emission */
                            }
            }
        }
    }

    /* ── Build comment block ─────────────────────────────────────────────────*/
    const char *nasm_fmt =
        strcmp(current_target, "macos")   == 0 ? "macho64" :
        strcmp(current_target, "windows") == 0 ? "win64"   : "elf64";
    const char *link_flags =
        strcmp(current_target, "windows") == 0 ? "" : " -no-pie";
    const char *bin_ext =
        strcmp(current_target, "windows") == 0 ? ".exe" : "";

    fprintf(out, "; Target: %s\n", current_target);
    fprintf(out, "; Assemble: nasm -f %s <file>.asm -o <file>.o\n", nasm_fmt);

    for (int m = 0; m < STD_MODULE_COUNT; m++) {
        if (!mod_needed[m]) continue;
        const char *stem = STD_MODULES[m].stem;
        fprintf(out, ";          # prefer pre-built: runtime/std/%s.o\n", stem);
        fprintf(out, ";          # fallback:         gcc -O2 -c runtime/std/%s.c -o %s.o\n",
                stem, STD_MODULES[m].obj_name);
    }

    fprintf(out, "; Link:     gcc <file>.o");
    for (int m = 0; m < STD_MODULE_COUNT; m++) {
        if (mod_needed[m]) fprintf(out, " %s.o", STD_MODULES[m].obj_name);
    }
    for (int m = 0; m < STD_MODULE_COUNT; m++) {
        if (mod_needed[m] && STD_MODULES[m].link_libs)
            fprintf(out, " %s", STD_MODULES[m].link_libs);
    }
    fprintf(out, " -o <program>%s%s\n\n", bin_ext, link_flags);

    /* ── extern declarations ─────────────────────────────────────────────────*/
    /* malloc/strlen are always needed (new + panic(err.message())) */
    fprintf(out, "extern malloc\n");
    fprintf(out, "extern strlen\n");

    for (int m = 0; m < STD_MODULE_COUNT; m++) {
        if (!mod_needed[m] || !STD_MODULES[m].externs) continue;
        /* split the space-separated externs string and emit each one */
        char externs_buf[512];
        strncpy(externs_buf, STD_MODULES[m].externs, sizeof(externs_buf) - 1);
        externs_buf[sizeof(externs_buf) - 1] = '\0';
        char *tok = strtok(externs_buf, " ");
        while (tok) {
            fprintf(out, "extern %s\n", sym(tok));
            tok = strtok(NULL, " ");
        }
    }
    fprintf(out, "\n");

#undef MOD_STEM

    /* .data section */
    fprintf(out, "section .data\n");
    for (int i = 0; i < str_const_count; i++) {
        char *escaped = nasm_escape_string(str_consts[i].value);
        size_t len = strlen(str_consts[i].value);
        fprintf(out, "    %s: db %s, 0\n", str_consts[i].label, escaped);
        fprintf(out, "    %s_len: equ %zu\n", str_consts[i].label, len);
        free(escaped);
    }
    fprintf(out, "\n");

    /* .text section */
    fprintf(out, "section .text\n");
    fprintf(out, "    global main\n");
    fprintf(out, "\n");

    /* Write buffered function code */
    fwrite(text_buf, 1, text_size, out);
    free(text_buf);
}