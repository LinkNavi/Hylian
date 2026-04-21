#include "codegen_asm.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Target ─────────────────────────────────────────────────────────────────── */

static const char *current_target = "linux";

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
    int   offset; /* positive offset from rbp, so address = [rbp - offset] */
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
    locals[local_count].name   = strdup(name);
    locals[local_count].offset = stack_depth;
    local_count++;
    return stack_depth;
}

/* Find offset for an existing local; returns -1 if not found. */
static int locals_find(const char *name) {
    for (int i = 0; i < local_count; i++)
        if (strcmp(locals[i].name, name) == 0)
            return locals[i].offset;
    return -1;
}

/* ─── Forward declarations ───────────────────────────────────────────────────── */

static void emit_expr(ASTNode *node, FILE *out, const char *fn_name);
static void emit_stmt(ASTNode *node, FILE *out, const char *fn_name);
static void emit_stmts(ASTNode **stmts, int count, FILE *out, const char *fn_name);

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
        if (off < 0) {
            fprintf(out, "    ; unknown variable '%s'\n", id->name);
            fprintf(out, "    xor rax, rax\n");
        } else {
            fprintf(out, "    mov rax, [rbp - %d]\n", off);
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
        int is_print   = strcmp(call->name, "print")   == 0;
        int is_println = strcmp(call->name, "println") == 0;

        if (is_print || is_println) {
            if (call->arg_count >= 1) {
                ASTNode *arg = call->args[0];
                int need_stack_cleanup = 0;

                if (arg->type == NODE_LITERAL && ((LiteralNode *)arg)->lit_type == LIT_STRING) {
                    /* String literal: direct pointer + length */
                    LiteralNode *lit = (LiteralNode *)arg;
                    const char *lbl = register_string(lit->value);
                    size_t len = string_literal_len(lit->value);
                    fprintf(out, "    lea rdi, [rel %s]\n", lbl);
                    fprintf(out, "    mov rsi, %zu\n", len);
                } else {
                    /* Integer or other expression: convert to string */
                    emit_expr(arg, out, fn_name);
                    fprintf(out, "    sub rsp, 32\n");
                    need_stack_cleanup = 1;
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
                    fprintf(out, "    add rsp, 32\n");
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
        fprintf(out, "    call %s\n", call->name);
        /* result in rax */
        break;
    }

    case NODE_MEMBER_ACCESS: {
        /* Emit a comment — full OOP not supported in asm backend */
        MemberAccessNode *ma = (MemberAccessNode *)node;
        fprintf(out, "    ; member access .%s not supported in asm backend\n", ma->member);
        fprintf(out, "    xor rax, rax\n");
        break;
    }

    case NODE_INTERP_STRING: {
        /* Interpolated strings partially supported: emit the first segment */
        InterpStringNode *istr = (InterpStringNode *)node;
        if (istr->seg_count > 0 && !istr->segments[0].is_expr) {
            const char *lbl = register_string(istr->segments[0].text);
            fprintf(out, "    lea rax, [rel %s]\n", lbl);
        } else {
            fprintf(out, "    xor rax, rax\n");
        }
        fprintf(out, "    ; interp string not fully supported in asm backend\n");
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
        int off = locals_alloc(vd->var_name);
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
        if (off < 0) {
            fprintf(out, "    ; assign to unknown var '%s'\n", as->var_name);
        } else {
            fprintf(out, "    mov [rbp - %d], rax\n", off);
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

        fprintf(out, ".L%d:\n", lbl_loop);
        emit_expr(nd->condition, out, fn_name);
        fprintf(out, "    test rax, rax\n");
        fprintf(out, "    jz .L%d\n", lbl_end);
        emit_stmts(nd->body, nd->body_count, out, fn_name);
        fprintf(out, "    jmp .L%d\n", lbl_loop);
        fprintf(out, ".L%d:\n", lbl_end);
        break;
    }

    case NODE_FOR: {
        ForNode *nd = (ForNode *)node;
        int lbl_loop = next_label();
        int lbl_end  = next_label();

        if (nd->init)
            emit_stmt(nd->init, out, fn_name);

        fprintf(out, ".L%d:\n", lbl_loop);
        if (nd->condition) {
            emit_expr(nd->condition, out, fn_name);
            fprintf(out, "    test rax, rax\n");
            fprintf(out, "    jz .L%d\n", lbl_end);
        }
        emit_stmts(nd->body, nd->body_count, out, fn_name);
        if (nd->post)
            emit_stmt(nd->post, out, fn_name);
        fprintf(out, "    jmp .L%d\n", lbl_loop);
        fprintf(out, ".L%d:\n", lbl_end);
        break;
    }

    case NODE_FUNC_CALL: {
        /* Statement-level function call: emit and discard result */
        emit_expr(node, out, fn_name);
        break;
    }

    case NODE_BREAK: {
        /* For simplicity, emitting a comment — proper break support needs
           a label stack which is beyond this minimal backend */
        fprintf(out, "    ; break (not fully supported without label stack)\n");
        break;
    }

    case NODE_CONTINUE: {
        fprintf(out, "    ; continue (not fully supported without label stack)\n");
        break;
    }

    case NODE_DEFER: {
        fprintf(out, "    ; defer not supported in asm backend\n");
        break;
    }

    case NODE_MEMBER_ASSIGN: {
        fprintf(out, "    ; member assign not supported in asm backend\n");
        break;
    }

    case NODE_METHOD_CALL: {
        /* Emit as a comment + try to call the method as a plain function */
        MethodCallNode *mc = (MethodCallNode *)node;
        fprintf(out, "    ; method call .%s() not supported in asm backend\n", mc->method);
        break;
    }

    case NODE_INDEX_ASSIGN: {
        fprintf(out, "    ; index assign not supported in asm backend\n");
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
        if (p->type == NODE_VAR_DECL) {
            pname = ((VarDeclNode *)p)->var_name;
        } else if (p->type == NODE_IDENTIFIER) {
            pname = ((IdentifierNode *)p)->name;
        }
        if (pname) {
            int off = locals_alloc(pname);
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
        } else if (decl->type == NODE_VAR_DECL) {
            /* Top-level var decl: emit in a synthetic _init function or
               directly in main if we detect there's no main function.
               For now, emit as a comment. */
            fprintf(text_out, "    ; top-level var decl not emitted as code\n");
        }
        /* Classes, includes etc. are not supported in asm backend */
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

    /* Target comment block with assemble/link instructions */
    if (strcmp(current_target, "macos") == 0) {
        fprintf(out, "; Target: macos\n");
        fprintf(out, "; Assemble: nasm -f macho64 <file>.asm -o <file>.o\n");
        fprintf(out, "; Runtime:  nasm -f macho64 runtime/std/io_macos.asm -o io.o\n");
        fprintf(out, "; Link:     gcc <file>.o io.o -o <program>\n");
    } else if (strcmp(current_target, "windows") == 0) {
        fprintf(out, "; Target: windows\n");
        fprintf(out, "; Assemble: nasm -f win64 <file>.asm -o <file>.o\n");
        fprintf(out, "; Runtime:  nasm -f win64 runtime/std/io_windows.asm -o io.o\n");
        fprintf(out, "; Link:     gcc <file>.o io.o -o <program>.exe\n");
    } else {
        fprintf(out, "; Target: linux\n");
        fprintf(out, "; Assemble: nasm -f elf64 <file>.asm -o <file>.o\n");
        fprintf(out, "; Runtime:  nasm -f elf64 runtime/std/io_linux.asm -o io.o\n");
        fprintf(out, "; Link:     gcc <file>.o io.o -o <program> -no-pie\n");
    }
    fprintf(out, "\n");

    fprintf(out, "extern %s\n", sym("hylian_print"));
    fprintf(out, "extern %s\n", sym("hylian_println"));
    fprintf(out, "extern %s\n", sym("hylian_int_to_str"));
    fprintf(out, "\n");

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