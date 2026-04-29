#include "codegen_asm.h"
#include "ir.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Target ─────────────────────────────────────────────────────────────────── */

static const char *current_target    = "linux";
static int         io_included        = 0;
static const char *current_class_name = NULL;

#define MAX_FN_PREFIXES 32
typedef struct { const char *src_prefix; const char *abi_prefix; } FnPrefix;
static FnPrefix fn_prefixes[MAX_FN_PREFIXES];
static int      fn_prefix_count = 0;

static const char *rewrite_call_name(const char *name) {
    static char buf[256];
    for (int i = 0; i < fn_prefix_count; i++) {
        size_t plen = strlen(fn_prefixes[i].src_prefix);
        if (strncmp(name, fn_prefixes[i].src_prefix, plen) == 0) {
            snprintf(buf, sizeof(buf), "%s%s", fn_prefixes[i].abi_prefix, name + plen);
            return buf;
        }
    }
    return name;
}

static const char *sym(const char *name) {
    static char buf[128];
    if (strcmp(current_target, "macos") == 0) {
        buf[0] = '_'; strncpy(buf + 1, name, sizeof(buf) - 2); buf[sizeof(buf)-1] = '\0';
        return buf;
    }
    return name;
}

/* ─── String constant table ─────────────────────────────────────────────────── */

#define MAX_STR_CONSTS 1024
typedef struct { char *value; char *label; } StrConst;
static StrConst str_consts[MAX_STR_CONSTS];
static int      str_const_count = 0;
static int      _label_counter  = 0;

static int next_label(void) { return _label_counter++; }

static char *nasm_escape_string(const char *s) {
    size_t len = strlen(s);
    char *buf = malloc(len * 6 + 16);
    char *p = buf;
    int in_quotes = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c != '"' && c != '\\') {
            if (!in_quotes) { if (p != buf) *p++ = ','; *p++ = '"'; in_quotes = 1; }
            *p++ = (char)c;
        } else {
            if (in_quotes) { *p++ = '"'; in_quotes = 0; }
            if (p != buf) *p++ = ',';
            p += sprintf(p, "0x%02x", c);
        }
    }
    if (in_quotes) *p++ = '"';
    *p = '\0';
    if (p == buf) strcpy(buf, "0x00");
    return buf;
}

static const char *register_string(const char *value) {
    /* value should already be unquoted */
    if (!value) value = "";
    for (int i = 0; i < str_const_count; i++)
        if (strcmp(str_consts[i].value, value) == 0) return str_consts[i].label;
    if (str_const_count >= MAX_STR_CONSTS) return "_str_overflow";
    char *lbl = malloc(32); snprintf(lbl, 32, "_str%d", str_const_count);
    str_consts[str_const_count].value = strdup(value);
    str_consts[str_const_count].label = lbl;
    str_const_count++;
    return lbl;
}

static size_t string_const_len(const char *value) { return value ? strlen(value) : 0; }

/* ─── Class registry ─────────────────────────────────────────────────────────── */

#define MAX_CLASSES 64
#define MAX_FIELDS  32
typedef struct { char *name; int offset; char *type_name; } FieldInfo;
typedef struct { char *name; FieldInfo fields[MAX_FIELDS]; int field_count; int size; int has_ctor; } ClassInfo;
static ClassInfo class_registry[MAX_CLASSES];
static int       class_count = 0;

static void class_registry_reset(void) { class_count = 0; }

static ClassInfo *class_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < class_count; i++)
        if (strcmp(class_registry[i].name, name) == 0) return &class_registry[i];
    return NULL;
}

static ClassInfo *class_register(ClassNode *cls) {
    if (class_count >= MAX_CLASSES) return NULL;
    ClassInfo *ci = &class_registry[class_count++];
    ci->name = cls->name; ci->field_count = 0; ci->has_ctor = cls->has_ctor;
    int offset = 0;
    for (int i = 0; i < cls->field_count && i < MAX_FIELDS; i++) {
        FieldNode *f = cls->fields[i];
        ci->fields[ci->field_count].name      = f->name;
        ci->fields[ci->field_count].offset    = offset;
        ci->fields[ci->field_count].type_name = f->field_type.name ? f->field_type.name : "int";
        ci->field_count++; offset += 8;
    }
    ci->size = offset ? offset : 8;
    if (ci->size % 16 != 0) ci->size += 16 - ci->size % 16;
    return ci;
}

static int class_field_offset(ClassInfo *ci, const char *field_name) {
    for (int i = 0; i < ci->field_count; i++)
        if (strcmp(ci->fields[i].name, field_name) == 0) return ci->fields[i].offset;
    return -1;
}

/* ─── Enum registry ──────────────────────────────────────────────────────────── */

#define MAX_ENUMS    32
#define MAX_VARIANTS 64
typedef struct { char *variant_name; int value; } EnumVariantInfo;
typedef struct { char *name; EnumVariantInfo variants[MAX_VARIANTS]; int variant_count; } EnumRegEntry;
static EnumRegEntry enum_registry[MAX_ENUMS];
static int          enum_count = 0;

static void enum_registry_reset(void) { enum_count = 0; }

static EnumRegEntry *enum_find(const char *name) {
    for (int i = 0; i < enum_count; i++)
        if (strcmp(enum_registry[i].name, name) == 0) return &enum_registry[i];
    return NULL;
}

static void enum_register(EnumNode *en) {
    if (enum_count >= MAX_ENUMS) return;
    EnumRegEntry *ei = &enum_registry[enum_count++];
    ei->name = en->name; ei->variant_count = 0;
    for (int i = 0; i < en->variant_count && i < MAX_VARIANTS; i++) {
        ei->variants[i].variant_name = en->variants[i].name;
        ei->variants[i].value        = en->variants[i].value;
        ei->variant_count++;
    }
}

static int enum_variant_value(const char *ename, const char *variant) {
    EnumRegEntry *ei = enum_find(ename);
    if (!ei) return -1;
    for (int i = 0; i < ei->variant_count; i++)
        if (strcmp(ei->variants[i].variant_name, variant) == 0) return ei->variants[i].value;
    return -1;
}

/* ─── Named & temp slot tables (per-function) ────────────────────────────────── */

#define MAX_NAMED_SLOTS 512
#define MAX_TEMP_SLOTS  4096

typedef struct { char *name; char *type_name; int rbp_offset; } NamedSlot;
static NamedSlot named_slots[MAX_NAMED_SLOTS];
static int       named_count;

/* temp_slots is indexed directly by temp_id */
static int temp_slot_offset[MAX_TEMP_SLOTS]; /* rbp_offset, -1 = unassigned */
static int max_temp_id;

static int  frame_bytes;
static int  interp_buf_offset; /* 0 = no pre-allocated buffer */

static int get_named_slot(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < named_count; i++)
        if (named_slots[i].name && strcmp(named_slots[i].name, name) == 0)
            return named_slots[i].rbp_offset;
    return -1;
}

static const char *get_named_slot_type(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < named_count; i++)
        if (named_slots[i].name && strcmp(named_slots[i].name, name) == 0)
            return named_slots[i].type_name;
    return NULL;
}

static int get_temp_slot(int temp_id) {
    if (temp_id >= 0 && temp_id < MAX_TEMP_SLOTS) return temp_slot_offset[temp_id];
    return -1;
}

/* ─── Per-function pre-scan ──────────────────────────────────────────────────── */

static void prescan_function(const IRModule *mod, int begin_idx, int end_idx) {
    named_count       = 0;
    max_temp_id       = -1;
    interp_buf_offset = 0;
    for (int i = 0; i < MAX_TEMP_SLOTS; i++) temp_slot_offset[i] = -1;

    int needs_interp = 0;

    for (int i = begin_idx; i <= end_idx && i < mod->instr_count; i++) {
        const IRInstr *ins = &mod->instrs[i];

        if (ins->op == IR_ALLOCA && ins->str_extra && named_count < MAX_NAMED_SLOTS) {
            named_slots[named_count].name      = ins->str_extra;
            named_slots[named_count].type_name = ins->str_extra2;
            named_slots[named_count].rbp_offset = (named_count + 1) * 8;
            named_count++;
        }

        if (ins->op == IR_INTERP_STR) needs_interp = 1;
        if ((ins->op == IR_PRINT || ins->op == IR_PRINTLN) &&
            ins->extra_int == PRINT_ARG_INTERP) needs_interp = 1;

        /* Find max temp id */
        if (ins->dest.kind      == IROP_TEMP && ins->dest.temp_id      > max_temp_id) max_temp_id = ins->dest.temp_id;
        if (ins->src1.kind      == IROP_TEMP && ins->src1.temp_id      > max_temp_id) max_temp_id = ins->src1.temp_id;
        if (ins->src2.kind      == IROP_TEMP && ins->src2.temp_id      > max_temp_id) max_temp_id = ins->src2.temp_id;
        if (ins->extra_src.kind == IROP_TEMP && ins->extra_src.temp_id > max_temp_id) max_temp_id = ins->extra_src.temp_id;
        for (int j = 0; j < ins->arg_count; j++)
            if (ins->args[j].kind == IROP_TEMP && ins->args[j].temp_id > max_temp_id) max_temp_id = ins->args[j].temp_id;
    }

    /* Assign temp slot offsets — after all named slots */
    int base_offset = named_count * 8;
    for (int t = 0; t <= max_temp_id && t < MAX_TEMP_SLOTS; t++)
        temp_slot_offset[t] = base_offset + (t + 1) * 8;

    /* Interp buffer: placed after all named+temp slots */
    int slot_bytes = named_count * 8 + (max_temp_id >= 0 ? (max_temp_id + 1) * 8 : 0);
    if (needs_interp) {
        interp_buf_offset = slot_bytes + 512; /* buf at [rbp - interp_buf_offset] */
        frame_bytes = interp_buf_offset;
    } else {
        frame_bytes = slot_bytes;
    }
    frame_bytes = (frame_bytes + 15) & ~15;
    if (frame_bytes < 16) frame_bytes = 16;
}

/* ─── Operand load / store helpers ───────────────────────────────────────────── */

static void load_operand_reg(const IROperand *op, const char *reg, FILE *out) {
    switch (op->kind) {
    case IROP_TEMP: {
        int off = get_temp_slot(op->temp_id);
        if (off > 0) fprintf(out, "    mov %s, [rbp - %d]\n", reg, off);
        else         fprintf(out, "    xor %s, %s\n", reg, reg);
        break;
    }
    case IROP_CONST_INT:
        fprintf(out, "    mov %s, %ld\n", reg, op->int_val);
        break;
    case IROP_CONST_STR: {
        const char *lbl = register_string(op->str_val ? op->str_val : "");
        fprintf(out, "    lea %s, [rel %s]\n", reg, lbl);
        break;
    }
    case IROP_CONST_BOOL:
        if (op->bool_val) fprintf(out, "    mov %s, 1\n", reg);
        else              fprintf(out, "    xor %s, %s\n", reg, reg);
        break;
    case IROP_NONE:
        fprintf(out, "    xor %s, %s\n", reg, reg);
        break;
    default: break;
    }
}

static void store_dest_rax(const IROperand *dest, FILE *out) {
    if (dest->kind == IROP_TEMP) {
        int off = get_temp_slot(dest->temp_id);
        if (off > 0) fprintf(out, "    mov [rbp - %d], rax\n", off);
    }
}

/* ─── Interpolated string segment emission ───────────────────────────────────── */

static void emit_interp_segments(const InterpSegment *segs, int seg_count, FILE *out) {
    /* On entry: r13 = buffer base, r14 = write pointer */
    for (int i = 0; i < seg_count; i++) {
        const InterpSegment *seg = &segs[i];
        if (!seg->is_expr) {
            size_t len = strlen(seg->text);
            if (len > 0) {
                const char *lbl = register_string(seg->text);
                fprintf(out, "    ; interp literal \"%s\"\n", seg->text);
                fprintf(out, "    lea rsi, [rel %s]\n", lbl);
                fprintf(out, "    mov rdi, r14\n");
                fprintf(out, "    mov rcx, %zu\n", len);
                fprintf(out, "    rep movsb\n");
                fprintf(out, "    mov r14, rdi\n");
            }
        } else {
            const char *varname = seg->text;
            int off = get_named_slot(varname);
            if (off > 0) {
                const char *vtype = get_named_slot_type(varname);
                int is_str = vtype && strcmp(vtype, "str") == 0;
                fprintf(out, "    ; interp expr: %s (%s)\n", varname, vtype ? vtype : "int");
                fprintf(out, "    mov rax, [rbp - %d]\n", off);
                if (is_str) {
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
                    fprintf(out, "    push r14\n");
                    fprintf(out, "    sub rsp, 32\n");
                    fprintf(out, "    mov rdi, rax\n");
                    fprintf(out, "    mov rsi, rsp\n");
                    fprintf(out, "    mov rdx, 32\n");
                    fprintf(out, "    call %s\n", sym("hylian_int_to_str"));
                    fprintf(out, "    mov rcx, rax\n");
                    fprintf(out, "    mov rsi, rsp\n");
                    fprintf(out, "    add rsp, 32\n");
                    fprintf(out, "    pop r14\n");
                    fprintf(out, "    mov rdi, r14\n");
                    fprintf(out, "    rep movsb\n");
                    fprintf(out, "    mov r14, rdi\n");
                }
            } else {
                fprintf(out, "    ; interp expr '%s' not in scope — skipped\n", varname);
            }
        }
    }
    fprintf(out, "    mov byte [r14], 0\n"); /* null-terminate */
}

/* ─── IR instruction emission ────────────────────────────────────────────────── */

static void emit_ir_instr(const IRInstr *ins, FILE *out) {
    static const char *arg_regs[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };

    switch (ins->op) {

    case IR_NOP: break;

    case IR_ALLOCA:
        /* Slot already assigned during prescan; just emit a zero store for safety */
        {
            int off = get_named_slot(ins->str_extra);
            if (off > 0) fprintf(out, "    mov qword [rbp - %d], 0\n", off);
        }
        break;

    /* ── Constants ──────────────────────────────────────────────────────── */
    case IR_CONST_INT:
    case IR_CONST_BOOL:
    case IR_CONST_STR:
        load_operand_reg(&ins->src1, "rax", out);
        store_dest_rax(&ins->dest, out);
        break;

    case IR_CONST_NIL:
        fprintf(out, "    xor rax, rax\n");
        store_dest_rax(&ins->dest, out);
        break;

    /* ── Variable access ────────────────────────────────────────────────── */
    case IR_LOAD_VAR: {
        if (!ins->str_extra) { fprintf(out, "    xor rax, rax\n"); store_dest_rax(&ins->dest, out); break; }
        int off = get_named_slot(ins->str_extra);
        if (off > 0) {
            fprintf(out, "    mov rax, [rbp - %d]\n", off);
        } else if (current_class_name) {
            ClassInfo *ci = class_find(current_class_name);
            if (ci) {
                int foff = class_field_offset(ci, ins->str_extra);
                if (foff >= 0) {
                    int self_off = get_named_slot("self");
                    if (self_off > 0) {
                        fprintf(out, "    mov rax, [rbp - %d]\n", self_off);
                        fprintf(out, "    mov rax, [rax + %d]\n", foff);
                        store_dest_rax(&ins->dest, out);
                        break;
                    }
                }
            }
            fprintf(out, "    ; LOAD_VAR '%s' not found\n", ins->str_extra);
            fprintf(out, "    xor rax, rax\n");
        } else {
            fprintf(out, "    ; LOAD_VAR '%s' not found\n", ins->str_extra);
            fprintf(out, "    xor rax, rax\n");
        }
        store_dest_rax(&ins->dest, out);
        break;
    }

    case IR_STORE_VAR: {
        if (!ins->str_extra) break;
        load_operand_reg(&ins->src1, "rax", out);
        int off = get_named_slot(ins->str_extra);
        if (off > 0) {
            fprintf(out, "    mov [rbp - %d], rax\n", off);
        } else if (current_class_name) {
            ClassInfo *ci = class_find(current_class_name);
            if (ci) {
                int foff = class_field_offset(ci, ins->str_extra);
                if (foff >= 0) {
                    int self_off = get_named_slot("self");
                    if (self_off > 0) {
                        fprintf(out, "    push rax\n");
                        fprintf(out, "    mov rcx, [rbp - %d]\n", self_off);
                        fprintf(out, "    pop rax\n");
                        fprintf(out, "    mov [rcx + %d], rax\n", foff);
                        break;
                    }
                }
            }
            fprintf(out, "    ; STORE_VAR '%s' not found\n", ins->str_extra);
        } else {
            fprintf(out, "    ; STORE_VAR '%s' not found\n", ins->str_extra);
        }
        break;
    }

    /* ── Arithmetic ─────────────────────────────────────────────────────── */
    case IR_ADD:
        load_operand_reg(&ins->src1, "rax", out);
        fprintf(out, "    push rax\n");
        load_operand_reg(&ins->src2, "rax", out);
        fprintf(out, "    pop rcx\n");
        fprintf(out, "    add rax, rcx\n");
        store_dest_rax(&ins->dest, out);
        break;

    case IR_SUB:
        load_operand_reg(&ins->src1, "rcx", out);
        load_operand_reg(&ins->src2, "rax", out);
        fprintf(out, "    sub rcx, rax\n");
        fprintf(out, "    mov rax, rcx\n");
        store_dest_rax(&ins->dest, out);
        break;

    case IR_MUL:
        load_operand_reg(&ins->src1, "rax", out);
        fprintf(out, "    push rax\n");
        load_operand_reg(&ins->src2, "rax", out);
        fprintf(out, "    pop rcx\n");
        fprintf(out, "    imul rax, rcx\n");
        store_dest_rax(&ins->dest, out);
        break;

    case IR_DIV:
        load_operand_reg(&ins->src1, "rax", out);
        fprintf(out, "    push rax\n");
        load_operand_reg(&ins->src2, "rax", out);
        fprintf(out, "    mov rcx, rax\n"); /* rcx = divisor */
        fprintf(out, "    pop rax\n");       /* rax = dividend */
        fprintf(out, "    cqo\n");
        fprintf(out, "    idiv rcx\n");
        store_dest_rax(&ins->dest, out);
        break;

    case IR_MOD:
        load_operand_reg(&ins->src1, "rax", out);
        fprintf(out, "    push rax\n");
        load_operand_reg(&ins->src2, "rax", out);
        fprintf(out, "    mov rcx, rax\n");
        fprintf(out, "    pop rax\n");
        fprintf(out, "    cqo\n");
        fprintf(out, "    idiv rcx\n");
        fprintf(out, "    mov rax, rdx\n");
        store_dest_rax(&ins->dest, out);
        break;

    case IR_NEG:
        load_operand_reg(&ins->src1, "rax", out);
        fprintf(out, "    neg rax\n");
        store_dest_rax(&ins->dest, out);
        break;

    case IR_NOT:
        load_operand_reg(&ins->src1, "rax", out);
        fprintf(out, "    test rax, rax\n");
        fprintf(out, "    sete al\n");
        fprintf(out, "    movzx rax, al\n");
        store_dest_rax(&ins->dest, out);
        break;

    /* ── Comparisons ────────────────────────────────────────────────────── */
    case IR_EQ: case IR_NEQ: case IR_LT: case IR_LE: case IR_GT: case IR_GE: {
        load_operand_reg(&ins->src1, "rax", out);
        fprintf(out, "    push rax\n");
        load_operand_reg(&ins->src2, "rax", out);
        fprintf(out, "    pop rcx\n");
        fprintf(out, "    cmp rcx, rax\n");
        const char *setcc =
            ins->op == IR_EQ  ? "sete"  :
            ins->op == IR_NEQ ? "setne" :
            ins->op == IR_LT  ? "setl"  :
            ins->op == IR_LE  ? "setle" :
            ins->op == IR_GT  ? "setg"  : "setge";
        fprintf(out, "    %s al\n", setcc);
        fprintf(out, "    movzx rax, al\n");
        store_dest_rax(&ins->dest, out);
        break;
    }

    /* ── Control flow ───────────────────────────────────────────────────── */
    case IR_LABEL:
        fprintf(out, ".L%d:\n", ins->dest.label_id);
        break;

    case IR_JUMP:
        fprintf(out, "    jmp .L%d\n", ins->src1.label_id);
        break;

    case IR_JUMP_IF:
        load_operand_reg(&ins->src1, "rax", out);
        fprintf(out, "    test rax, rax\n");
        fprintf(out, "    jnz .L%d\n", ins->src2.label_id);
        break;

    case IR_JUMP_UNLESS:
        load_operand_reg(&ins->src1, "rax", out);
        fprintf(out, "    test rax, rax\n");
        fprintf(out, "    jz .L%d\n", ins->src2.label_id);
        break;

    /* ── Function calls ─────────────────────────────────────────────────── */
    case IR_CALL: {
        int nargs = ins->arg_count > 6 ? 6 : ins->arg_count;
        for (int i = 0; i < nargs; i++) {
            load_operand_reg(&ins->args[i], "rax", out);
            fprintf(out, "    push rax\n");
        }
        for (int i = nargs - 1; i >= 0; i--)
            fprintf(out, "    pop %s\n", arg_regs[i]);
        const char *cname = ins->str_extra ? rewrite_call_name(ins->str_extra) : "unknown";
        fprintf(out, "    call %s\n", cname);
        store_dest_rax(&ins->dest, out);
        break;
    }

    case IR_RETURN: {
        if (ins->src1.kind != IROP_NONE)
            load_operand_reg(&ins->src1, "rax", out);
        else
            fprintf(out, "    xor rax, rax\n");
        fprintf(out, "    mov rsp, rbp\n");
        fprintf(out, "    pop rbp\n");
        fprintf(out, "    ret\n");
        break;
    }

    /* ── OOP ────────────────────────────────────────────────────────────── */
    case IR_NEW: {
        ClassInfo *ci = ins->str_extra ? class_find(ins->str_extra) : NULL;
        if (!ci) {
            fprintf(out, "    ; new: class '%s' not registered\n", ins->str_extra ? ins->str_extra : "?");
            fprintf(out, "    xor rax, rax\n");
            store_dest_rax(&ins->dest, out);
            break;
        }
        fprintf(out, "    ; new %s (size=%d)\n", ci->name, ci->size);
        fprintf(out, "    mov rdi, %d\n", ci->size);
        fprintf(out, "    call malloc\n");
        fprintf(out, "    push rax\n");
        fprintf(out, "    mov rdi, rax\n");
        fprintf(out, "    xor al, al\n");
        fprintf(out, "    mov rcx, %d\n", ci->size);
        fprintf(out, "    rep stosb\n");
        fprintf(out, "    pop rax\n");
        if (ci->has_ctor) {
            char ctor_name[256]; snprintf(ctor_name, sizeof(ctor_name), "%s__ctor", ci->name);
            int nargs = ins->arg_count > 5 ? 5 : ins->arg_count;
            fprintf(out, "    push rax\n");
            for (int i = 0; i < nargs; i++) {
                load_operand_reg(&ins->args[i], "rax", out);
                fprintf(out, "    push rax\n");
            }
            for (int i = nargs - 1; i >= 0; i--)
                fprintf(out, "    pop %s\n", arg_regs[i + 1]);
            fprintf(out, "    pop rdi\n");
            fprintf(out, "    push rdi\n");
            fprintf(out, "    call %s\n", ctor_name);
            fprintf(out, "    pop rax\n");
        }
        store_dest_rax(&ins->dest, out);
        break;
    }

    case IR_GET_FIELD: {
        const char *cname2 = ins->str_extra;
        const char *fname  = ins->str_extra2;
        /* Check enum first */
        if (ins->src1.kind == IROP_NONE && cname2 && fname) {
            EnumRegEntry *ei = enum_find(cname2);
            if (ei) {
                int val = enum_variant_value(cname2, fname);
                fprintf(out, "    mov rax, %d\n", val >= 0 ? val : 0);
                store_dest_rax(&ins->dest, out);
                break;
            }
        }
        ClassInfo *ci2 = cname2 ? class_find(cname2) : NULL;
        if (!ci2 || !fname) {
            fprintf(out, "    ; GET_FIELD: class/field not found\n");
            fprintf(out, "    xor rax, rax\n");
            store_dest_rax(&ins->dest, out);
            break;
        }
        int foff = class_field_offset(ci2, fname);
        if (foff < 0) {
            fprintf(out, "    ; GET_FIELD: field '%s' not in '%s'\n", fname, ci2->name);
            fprintf(out, "    xor rax, rax\n");
        } else {
            load_operand_reg(&ins->src1, "rax", out);
            fprintf(out, "    mov rax, [rax + %d]\n", foff);
        }
        store_dest_rax(&ins->dest, out);
        break;
    }

    case IR_SET_FIELD: {
        const char *cname3 = ins->str_extra;
        const char *fname2 = ins->str_extra2;
        ClassInfo *ci3 = cname3 ? class_find(cname3) : NULL;
        if (!ci3 || !fname2) { fprintf(out, "    ; SET_FIELD: class/field not found\n"); break; }
        int foff2 = class_field_offset(ci3, fname2);
        if (foff2 < 0) { fprintf(out, "    ; SET_FIELD: field '%s' not in '%s'\n", fname2, ci3->name); break; }
        load_operand_reg(&ins->src2, "rax", out);  /* value */
        fprintf(out, "    push rax\n");
        load_operand_reg(&ins->src1, "rcx", out);  /* object pointer */
        fprintf(out, "    pop rax\n");
        fprintf(out, "    mov [rcx + %d], rax\n", foff2);
        break;
    }

    /* ── Array intrinsics ───────────────────────────────────────────────── */
    case IR_ARRAY_ALLOC: {
        fprintf(out, "    ; array<%s> alloc (8 slots)\n", ins->str_extra ? ins->str_extra : "?");
        fprintf(out, "    mov rdi, %d\n", (8 + 2) * 8);
        fprintf(out, "    call malloc\n");
        fprintf(out, "    mov qword [rax + 0], 0\n");
        fprintf(out, "    mov qword [rax + 8], 8\n");
        fprintf(out, "    push r11\n");
        fprintf(out, "    mov r11, rax\n");
        for (int i = 0; i < 8; i++)
            fprintf(out, "    mov qword [r11 + %d], 0\n", 16 + i * 8);
        fprintf(out, "    mov rax, r11\n");
        fprintf(out, "    pop r11\n");
        store_dest_rax(&ins->dest, out);
        break;
    }

    case IR_ARRAY_INIT: {
        int count    = ins->arg_count;
        int capacity = count < 8 ? 8 : count;
        fprintf(out, "    ; array literal (%d elements)\n", count);
        fprintf(out, "    mov rdi, %d\n", (capacity + 2) * 8);
        fprintf(out, "    call malloc\n");
        fprintf(out, "    push r11\n");
        fprintf(out, "    mov r11, rax\n");
        fprintf(out, "    mov qword [r11 + 0], %d\n", count);
        fprintf(out, "    mov qword [r11 + 8], %d\n", capacity);
        for (int i = 0; i < count; i++) {
            load_operand_reg(&ins->args[i], "rax", out);
            fprintf(out, "    mov [r11 + %d], rax\n", 16 + i * 8);
        }
        fprintf(out, "    mov rax, r11\n");
        fprintf(out, "    pop r11\n");
        store_dest_rax(&ins->dest, out);
        break;
    }

    case IR_ARRAY_PUSH: {
        int var_off = ins->str_extra ? get_named_slot(ins->str_extra) : -1;
        if (var_off < 0) {
            fprintf(out, "    ; array.push: var '%s' not found\n", ins->str_extra ? ins->str_extra : "?");
            fprintf(out, "    xor rax, rax\n");
            store_dest_rax(&ins->dest, out);
            break;
        }
        int lbl_fast  = next_label();
        int lbl_dbl   = next_label();
        int lbl_alloc = next_label();
        load_operand_reg(&ins->src2, "rax", out);
        fprintf(out, "    push rax\n");
        fprintf(out, "    mov r11, [rbp - %d]\n", var_off);
        fprintf(out, "    mov rax, [r11]\n");
        fprintf(out, "    cmp rax, [r11 + 8]\n");
        fprintf(out, "    jl .L%d\n", lbl_fast);
        fprintf(out, "    mov rax, [r11 + 8]\n");
        fprintf(out, "    test rax, rax\n");
        fprintf(out, "    jnz .L%d\n", lbl_dbl);
        fprintf(out, "    mov rax, 8\n");
        fprintf(out, "    jmp .L%d\n", lbl_alloc);
        fprintf(out, ".L%d:\n", lbl_dbl);
        fprintf(out, "    shl rax, 1\n");
        fprintf(out, ".L%d:\n", lbl_alloc);
        fprintf(out, "    push rax\n");
        fprintf(out, "    mov rsi, rax\n");
        fprintf(out, "    shl rsi, 3\n");
        fprintf(out, "    add rsi, 16\n");
        fprintf(out, "    mov rdi, r11\n");
        fprintf(out, "    call realloc\n");
        fprintf(out, "    mov r11, rax\n");
        fprintf(out, "    mov [rbp - %d], r11\n", var_off);
        fprintf(out, "    pop rax\n");
        fprintf(out, "    mov [r11 + 8], rax\n");
        fprintf(out, ".L%d:\n", lbl_fast);
        fprintf(out, "    mov rax, [r11]\n");
        fprintf(out, "    pop rcx\n");
        fprintf(out, "    lea rdx, [r11 + 16]\n");
        fprintf(out, "    mov [rdx + rax*8], rcx\n");
        fprintf(out, "    inc qword [r11]\n");
        fprintf(out, "    xor rax, rax\n");
        store_dest_rax(&ins->dest, out);
        break;
    }

    case IR_ARRAY_POP:
        load_operand_reg(&ins->src1, "rdi", out);
        fprintf(out, "    mov r11, rdi\n");
        fprintf(out, "    dec qword [r11]\n");
        fprintf(out, "    mov rax, [r11]\n");
        fprintf(out, "    lea rdx, [r11 + 16]\n");
        fprintf(out, "    mov rax, [rdx + rax*8]\n");
        store_dest_rax(&ins->dest, out);
        break;

    case IR_ARRAY_LEN:
        load_operand_reg(&ins->src1, "rax", out);
        fprintf(out, "    mov rax, [rax + 0]\n");
        store_dest_rax(&ins->dest, out);
        break;

    case IR_ARRAY_CAP:
        load_operand_reg(&ins->src1, "rax", out);
        fprintf(out, "    mov rax, [rax + 8]\n");
        store_dest_rax(&ins->dest, out);
        break;

    case IR_ARRAY_LOAD:
        load_operand_reg(&ins->src1, "rax", out);
        fprintf(out, "    push r11\n");
        fprintf(out, "    mov r11, rax\n");
        load_operand_reg(&ins->src2, "rax", out);
        fprintf(out, "    mov rcx, rax\n");
        fprintf(out, "    imul rcx, rcx, 8\n");
        fprintf(out, "    add rcx, 16\n");
        fprintf(out, "    add rcx, r11\n");
        fprintf(out, "    mov rax, [rcx]\n");
        fprintf(out, "    pop r11\n");
        store_dest_rax(&ins->dest, out);
        break;

    case IR_ARRAY_STORE: {
        /* src1 = array ptr, src2 = index, extra_src = value */
        load_operand_reg(&ins->extra_src, "rax", out);
        fprintf(out, "    push rax\n");
        load_operand_reg(&ins->src1, "rax", out);
        fprintf(out, "    push r11\n");
        fprintf(out, "    mov r11, rax\n");
        load_operand_reg(&ins->src2, "rax", out);
        fprintf(out, "    mov rcx, rax\n");
        fprintf(out, "    imul rcx, rcx, 8\n");
        fprintf(out, "    add rcx, 16\n");
        fprintf(out, "    add rcx, r11\n");
        fprintf(out, "    pop r11\n");
        fprintf(out, "    pop rdx\n");
        fprintf(out, "    mov [rcx], rdx\n");
        break;
    }

    /* ── Tagged union ───────────────────────────────────────────────────── */
    case IR_MULTI_ALLOC:
        /* src1 = tag, src2 = value */
        load_operand_reg(&ins->src2, "rax", out);
        fprintf(out, "    push rax\n");
        load_operand_reg(&ins->src1, "rax", out);
        fprintf(out, "    push rax\n");
        fprintf(out, "    mov rdi, 16\n");
        fprintf(out, "    call malloc\n");
        fprintf(out, "    pop rcx\n");  /* tag */
        fprintf(out, "    mov qword [rax + 0], rcx\n");
        fprintf(out, "    pop rcx\n");  /* value */
        fprintf(out, "    mov [rax + 8], rcx\n");
        store_dest_rax(&ins->dest, out);
        break;

    /* ── Enum constant ──────────────────────────────────────────────────── */
    case IR_ENUM_VAL: {
        int val = -1;
        if (ins->str_extra && ins->str_extra2)
            val = enum_variant_value(ins->str_extra, ins->str_extra2);
        if (val >= 0) fprintf(out, "    mov rax, %d\n", val);
        else {
            fprintf(out, "    ; unknown enum %s.%s\n",
                    ins->str_extra ? ins->str_extra : "?",
                    ins->str_extra2 ? ins->str_extra2 : "?");
            fprintf(out, "    xor rax, rax\n");
        }
        store_dest_rax(&ins->dest, out);
        break;
    }

    /* ── Interpolated string ────────────────────────────────────────────── */
    case IR_INTERP_STR: {
        if (interp_buf_offset <= 0) {
            fprintf(out, "    ; IR_INTERP_STR: no buffer allocated\n");
            fprintf(out, "    xor rax, rax\n");
            store_dest_rax(&ins->dest, out);
            break;
        }
        /* Save r13 and r14 (r15 intentionally holds length at end) */
        fprintf(out, "    push r13\n");
        fprintf(out, "    push r14\n");
        fprintf(out, "    push r15\n");
        fprintf(out, "    lea r13, [rbp - %d]\n", interp_buf_offset);
        fprintf(out, "    mov r14, r13\n");
        emit_interp_segments(ins->extra_segs, ins->extra_seg_count, out);
        fprintf(out, "    mov rax, r13\n");  /* result = buf ptr */
        fprintf(out, "    sub r14, r13\n");
        fprintf(out, "    mov r15, r14\n");  /* r15 = length */
        /* Restore r13 and r14 from saved stack; r15 stays as length */
        fprintf(out, "    mov r13, [rsp + 16]\n");
        fprintf(out, "    mov r14, [rsp + 8]\n");
        fprintf(out, "    add rsp, 24\n");
        store_dest_rax(&ins->dest, out);
        break;
    }

    /* ── Inline assembly ────────────────────────────────────────────────── */
    case IR_ASM_BLOCK:
        if (ins->str_extra) fprintf(out, "%s\n", ins->str_extra);
        break;

    /* ── Print / println ────────────────────────────────────────────────── */
    case IR_PRINT:
    case IR_PRINTLN: {
        switch (ins->extra_int) {
        case PRINT_ARG_STR_LIT: {
            const char *sv = ins->src1.str_val ? ins->src1.str_val : "";
            const char *lbl = register_string(sv);
            size_t len = string_const_len(sv);
            fprintf(out, "    lea rdi, [rel %s]\n", lbl);
            fprintf(out, "    mov rsi, %zu\n", len);
            break;
        }
        case PRINT_ARG_INT:
            load_operand_reg(&ins->src1, "rax", out);
            fprintf(out, "    sub rsp, 32\n");
            fprintf(out, "    mov rdi, rax\n");
            fprintf(out, "    mov rsi, rsp\n");
            fprintf(out, "    mov rdx, 32\n");
            fprintf(out, "    call %s\n", sym("hylian_int_to_str"));
            fprintf(out, "    mov rsi, rax\n");
            fprintf(out, "    mov rdi, rsp\n");
            break;
        case PRINT_ARG_STR_PTR:
            load_operand_reg(&ins->src1, "rax", out);
            fprintf(out, "    push rax\n");
            fprintf(out, "    mov rdi, rax\n");
            fprintf(out, "    call %s\n", sym("strlen"));
            fprintf(out, "    mov rsi, rax\n");
            fprintf(out, "    pop rdi\n");
            break;
        case PRINT_ARG_INTERP:
            if (ins->extra_segs && ins->extra_seg_count > 0 && interp_buf_offset > 0) {
                /* Emit interp string inline into pre-allocated buffer */
                fprintf(out, "    push r13\n");
                fprintf(out, "    push r14\n");
                fprintf(out, "    push r15\n");
                fprintf(out, "    lea r13, [rbp - %d]\n", interp_buf_offset);
                fprintf(out, "    mov r14, r13\n");
                emit_interp_segments(ins->extra_segs, ins->extra_seg_count, out);
                fprintf(out, "    mov rax, r13\n");
                fprintf(out, "    mov r13, [rsp + 16]\n");
                fprintf(out, "    mov r14, [rsp + 8]\n");
                fprintf(out, "    add rsp, 24\n");
                /* rax = buf ptr; use strlen for length */
                fprintf(out, "    push rax\n");
                fprintf(out, "    mov rdi, rax\n");
                fprintf(out, "    call %s\n", sym("strlen"));
                fprintf(out, "    mov rsi, rax\n");
                fprintf(out, "    pop rdi\n");
            } else if (ins->src1.kind != IROP_NONE) {
                /* Buffer ptr from a preceding IR_INTERP_STR */
                load_operand_reg(&ins->src1, "rax", out);
                fprintf(out, "    push rax\n");
                fprintf(out, "    mov rdi, rax\n");
                fprintf(out, "    call %s\n", sym("strlen"));
                fprintf(out, "    mov rsi, rax\n");
                fprintf(out, "    pop rdi\n");
            } else {
                const char *empty = register_string("");
                fprintf(out, "    lea rdi, [rel %s]\n", empty);
                fprintf(out, "    mov rsi, 0\n");
            }
            break;
        default: { /* no arg */
            const char *empty = register_string("");
            fprintf(out, "    lea rdi, [rel %s]\n", empty);
            fprintf(out, "    mov rsi, 0\n");
            break;
        }
        }
        const char *print_fn = (ins->op == IR_PRINT) ? "hylian_print" : "hylian_println";
        fprintf(out, "    call %s\n", sym(print_fn));
        if (ins->extra_int == PRINT_ARG_INT)
            fprintf(out, "    add rsp, 32\n");
        break;
    }

    /* ── Err / panic ────────────────────────────────────────────────────── */
    case IR_ERR: {
        if (ins->src1.kind == IROP_CONST_STR) {
            const char *sv2 = ins->src1.str_val ? ins->src1.str_val : "error";
            const char *lbl = register_string(sv2);
            size_t len = string_const_len(sv2);
            fprintf(out, "    lea rdi, [rel %s]\n", lbl);
            fprintf(out, "    mov rsi, %zu\n", len);
        } else {
            load_operand_reg(&ins->src1, "rdi", out);
            fprintf(out, "    mov rsi, 0\n");
        }
        fprintf(out, "    call %s\n", sym("hylian_make_error"));
        store_dest_rax(&ins->dest, out);
        break;
    }

    case IR_PANIC: {
        if (ins->src1.kind == IROP_CONST_STR) {
            const char *sv3 = ins->src1.str_val ? ins->src1.str_val : "panic";
            const char *lbl = register_string(sv3);
            size_t len = string_const_len(sv3);
            fprintf(out, "    lea rdi, [rel %s]\n", lbl);
            fprintf(out, "    mov rsi, %zu\n", len);
        } else {
            load_operand_reg(&ins->src1, "rdi", out);
            fprintf(out, "    mov rsi, 0\n");
        }
        fprintf(out, "    call %s\n", sym("hylian_panic"));
        break;
    }

    case IR_FUNC_BEGIN: case IR_FUNC_END: break; /* handled by emit_ir_function */

    default:
        fprintf(out, "    ; unhandled IR op %d\n", (int)ins->op);
        break;
    }
}

/* ─── Function emission ──────────────────────────────────────────────────────── */

/* Returns 1 if the last non-NOP instruction before end_idx is IR_RETURN */
static int ends_with_return(const IRModule *mod, int begin_idx, int end_idx) {
    for (int i = end_idx - 1; i > begin_idx; i--) {
        if (mod->instrs[i].op == IR_NOP) continue;
        return mod->instrs[i].op == IR_RETURN;
    }
    return 0;
}

static void emit_ir_function(const IRModule *mod, int begin_idx, FILE *out) {
    const IRInstr *begin = &mod->instrs[begin_idx];

    /* Find end index */
    int end_idx = begin_idx + 1;
    int depth   = 1;
    while (end_idx < mod->instr_count && depth > 0) {
        if (mod->instrs[end_idx].op == IR_FUNC_BEGIN) depth++;
        if (mod->instrs[end_idx].op == IR_FUNC_END)   depth--;
        end_idx++;
    }
    end_idx--; /* points at IR_FUNC_END */

    /* Pre-scan to allocate slots and compute frame size */
    prescan_function(mod, begin_idx, end_idx);

    /* Set class context */
    const char *saved_class = current_class_name;
    /* Detect if this is a method: params[0] == "self" */
    const char *new_class = NULL;
    if (begin->param_count > 0 && begin->params[0].name &&
        strcmp(begin->params[0].name, "self") == 0 && begin->params[0].type_name) {
        new_class = begin->params[0].type_name;
    }
    current_class_name = new_class;

    /* Emit function label */
    const char *fn_name = begin->str_extra ? begin->str_extra : "unknown";
    if (begin->extra_int) /* is_main */
        fprintf(out, "main:\n");
    else
        fprintf(out, "%s:\n", fn_name);

    /* Prologue */
    fprintf(out, "    push rbp\n");
    fprintf(out, "    mov rbp, rsp\n");
    fprintf(out, "    sub rsp, %d\n", frame_bytes);

    /* Spill parameters from arg registers to their named slots */
    static const char *param_regs[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
    int nparams = begin->param_count > 6 ? 6 : begin->param_count;
    for (int pi = 0; pi < nparams; pi++) {
        const char *pname = begin->params[pi].name;
        if (!pname) continue;
        int off = get_named_slot(pname);
        if (off > 0)
            fprintf(out, "    mov [rbp - %d], %s\n", off, param_regs[pi]);
    }

    /* Emit instruction bodies (skip FUNC_BEGIN and FUNC_END) */
    for (int i = begin_idx + 1; i < end_idx; i++) {
        const IRInstr *ins = &mod->instrs[i];
        /* Skip ALLOCA instructions that were already handled as parameter spills */
        if (ins->op == IR_ALLOCA) {
            /* Just emit the zero-init (already done in emit_ir_instr) */
            /* Check if this is a parameter; if so, skip zeroing (value already set) */
            int is_param = 0;
            for (int pi2 = 0; pi2 < nparams; pi2++) {
                if (begin->params[pi2].name && ins->str_extra &&
                    strcmp(begin->params[pi2].name, ins->str_extra) == 0) {
                    is_param = 1; break;
                }
            }
            if (is_param) continue; /* param already spilled above */
        }
        emit_ir_instr(ins, out);
    }

    /* Implicit return (only if the function doesn't already end with IR_RETURN) */
    if (!ends_with_return(mod, begin_idx, end_idx)) {
        fprintf(out, "    xor rax, rax\n");
        fprintf(out, "    mov rsp, rbp\n");
        fprintf(out, "    pop rbp\n");
        fprintf(out, "    ret\n");
    }
    fprintf(out, "\n");
    current_class_name = saved_class;
}

/* ─── Top-level codegen entry point ─────────────────────────────────────────── */

void codegen_ir(IRModule *mod, FILE *out, const char *src_filename, const char *target) {
    current_target  = target ? target : "linux";
    str_const_count = 0;
    _label_counter  = 0;
    fn_prefix_count = 0;
    io_included     = 0;

    /* Scan includes to set up fn_prefix rewrites and io flag */
    typedef struct { const char *path; const char *src; const char *abi; int io; } EarlyMod;
    static const EarlyMod EARLY[] = {
        { "std.io",               NULL,      NULL,                 1 },
        { "std.errors",           NULL,      NULL,                 0 },
        { "std.strings",          "str_",    "hylian_",            0 },
        { "std.system.filesystem",NULL,      NULL,                 0 },
        { "std.system.env",       NULL,      NULL,                 0 },
        { "std.crypto",           "crypto_", "hylian_crypto_",     0 },
        { "std.networking.tcp",   "tcp_",    "hylian_net_tcp_",    0 },
        { "std.networking.udp",   "udp_",    "hylian_net_udp_",    0 },
        { "std.networking.https", "https_",  "hylian_net_https_",  0 },
    };
    static const int EARLY_N = (int)(sizeof(EARLY)/sizeof(EARLY[0]));
    for (int i = 0; i < mod->include_count; i++) {
        for (int m = 0; m < EARLY_N; m++) {
            if (strcmp(mod->includes[i], EARLY[m].path) == 0) {
                if (EARLY[m].io) io_included = 1;
                if (EARLY[m].src && fn_prefix_count < MAX_FN_PREFIXES) {
                    fn_prefixes[fn_prefix_count].src_prefix = EARLY[m].src;
                    fn_prefixes[fn_prefix_count].abi_prefix = EARLY[m].abi;
                    fn_prefix_count++;
                }
            }
        }
    }

    /* Register classes and enums */
    class_registry_reset();
    enum_registry_reset();
    for (int i = 0; i < mod->class_count; i++) class_register(mod->classes[i]);
    for (int i = 0; i < mod->enum_count;  i++) enum_register(mod->enums[i]);

    /* ── Buffer the .text section ── */
    char  *text_buf  = NULL;
    size_t text_size = 0;
    FILE  *text_out  = open_memstream(&text_buf, &text_size);
    if (!text_out) { fprintf(stderr, "codegen_ir: open_memstream failed\n"); return; }

    int found_main = 0;
    for (int i = 0; i < mod->instr_count; i++) {
        const IRInstr *ins = &mod->instrs[i];
        if (ins->op == IR_FUNC_BEGIN) {
            if (ins->extra_int) found_main = 1; /* is_main */
            emit_ir_function(mod, i, text_out);
            /* Skip to FUNC_END */
            int depth = 1;
            while (++i < mod->instr_count && depth > 0) {
                if (mod->instrs[i].op == IR_FUNC_BEGIN) depth++;
                if (mod->instrs[i].op == IR_FUNC_END)   depth--;
            }
        }
    }

    /* Synthesise a minimal main if none found */
    if (!found_main) {
        named_count = 0; max_temp_id = -1; interp_buf_offset = 0; frame_bytes = 16;
        fprintf(text_out, "main:\n");
        fprintf(text_out, "    push rbp\n");
        fprintf(text_out, "    mov rbp, rsp\n");
        fprintf(text_out, "    sub rsp, 16\n");
        fprintf(text_out, "    xor rax, rax\n");
        fprintf(text_out, "    mov rsp, rbp\n");
        fprintf(text_out, "    pop rbp\n");
        fprintf(text_out, "    ret\n\n");
    }
    fclose(text_out);

    /* ── Stdlib module table ── */
    typedef struct {
        const char *path; const char *obj; const char *stem;
        const char *externs; const char *link_libs; const char *fn_prefix; int io;
    } StdMod;
    static const StdMod STD[] = {
        { "std.io",                  "io",      "io",                  "hylian_print hylian_println hylian_int_to_str",  NULL,            NULL,        1 },
        { "std.errors",              "errors",  "errors",              "hylian_make_error hylian_panic",                 NULL,            NULL,        0 },
        { "std.strings",             "strings", "strings",             "hylian_length hylian_is_empty hylian_contains hylian_starts_with hylian_ends_with hylian_index_of hylian_slice hylian_trim hylian_trim_start hylian_trim_end hylian_to_upper hylian_to_lower hylian_replace hylian_split hylian_join hylian_to_int hylian_to_float hylian_from_int hylian_equals", NULL, "str_",      0 },
        { "std.system.filesystem",   "fs",      "system/filesystem",   "hylian_file_read hylian_file_write hylian_file_exists hylian_file_size hylian_mkdir", NULL, NULL, 0 },
        { "std.system.env",          "env",     "system/env",          "hylian_getenv hylian_exit",                      NULL,            NULL,        0 },
        { "std.crypto",              "crypto",  "crypto",              "hylian_crypto_hash hylian_crypto_hash_hex hylian_crypto_hmac hylian_crypto_hmac_hex hylian_crypto_encrypt hylian_crypto_decrypt hylian_crypto_random_bytes hylian_crypto_random_int hylian_crypto_random_float hylian_crypto_constant_time_eq", "-lssl -lcrypto", "crypto_", 0 },
        { "std.networking.tcp",      "tcp",     "networking/tcp",      "hylian_net_tcp_connect hylian_net_tcp_listen hylian_net_tcp_accept hylian_net_tcp_send hylian_net_tcp_recv hylian_net_tcp_close hylian_net_tcp_set_nonblocking hylian_net_tcp_set_reuseaddr hylian_net_tcp_set_timeout", NULL, "tcp_", 0 },
        { "std.networking.udp",      "udp",     "networking/udp",      "hylian_net_udp_socket hylian_net_udp_bind hylian_net_udp_send_to hylian_net_udp_recv_from hylian_net_udp_connect hylian_net_udp_send hylian_net_udp_recv hylian_net_udp_close hylian_net_udp_set_nonblocking hylian_net_udp_set_timeout hylian_net_udp_set_broadcast hylian_net_udp_join_multicast", NULL, "udp_", 0 },
        { "std.networking.https",    "https",   "networking/https",    "hylian_net_https_connect hylian_net_https_send hylian_net_https_recv hylian_net_https_get hylian_net_https_post hylian_net_https_close hylian_net_https_body hylian_net_https_status", "-lssl -lcrypto", "https_", 0 },
    };
    static const int STD_N = (int)(sizeof(STD)/sizeof(STD[0]));
    int mod_needed[sizeof(STD)/sizeof(STD[0])];
    memset(mod_needed, 0, sizeof(mod_needed));
    for (int i = 0; i < mod->include_count; i++)
        for (int m = 0; m < STD_N; m++)
            if (strcmp(mod->includes[i], STD[m].path) == 0) mod_needed[m] = 1;

    /* ── Emit header ── */
    const char *nasm_fmt =
        strcmp(current_target, "macos")   == 0 ? "macho64" :
        strcmp(current_target, "windows") == 0 ? "win64"   : "elf64";
    const char *link_flags =
        strcmp(current_target, "windows") == 0 ? "" : " -no-pie";
    const char *bin_ext =
        strcmp(current_target, "windows") == 0 ? ".exe" : "";

    fprintf(out, "; Generated by Hylian compiler (IR backend)\n");
    if (src_filename) fprintf(out, "; Source: %s\n", src_filename);
    fprintf(out, "bits 64\ndefault rel\n\n");
    fprintf(out, "; Target: %s\n", current_target);
    fprintf(out, "; Assemble: nasm -f %s <file>.asm -o <file>.o\n", nasm_fmt);
    for (int m = 0; m < STD_N; m++) {
        if (!mod_needed[m]) continue;
        fprintf(out, ";          # prefer pre-built: runtime/std/%s.o\n", STD[m].stem);
        fprintf(out, ";          # fallback:         gcc -O2 -c runtime/std/%s.c -o %s.o\n",
                STD[m].stem, STD[m].obj);
    }
    fprintf(out, "; Link:     gcc <file>.o");
    for (int m = 0; m < STD_N; m++) if (mod_needed[m]) fprintf(out, " %s.o", STD[m].obj);
    for (int m = 0; m < STD_N; m++) if (mod_needed[m] && STD[m].link_libs) fprintf(out, " %s", STD[m].link_libs);
    fprintf(out, " -o <program>%s%s\n\n", bin_ext, link_flags);

    /* externs */
    fprintf(out, "extern malloc\nextern realloc\nextern strlen\n");
    for (int m = 0; m < STD_N; m++) {
        if (!mod_needed[m] || !STD[m].externs) continue;
        char ebuf[512]; strncpy(ebuf, STD[m].externs, sizeof(ebuf)-1); ebuf[sizeof(ebuf)-1] = '\0';
        char *tok = strtok(ebuf, " ");
        while (tok) { fprintf(out, "extern %s\n", sym(tok)); tok = strtok(NULL, " "); }
    }
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
    fprintf(out, "section .text\n    global main\n\n");
    fwrite(text_buf, 1, text_size, out);
    free(text_buf);
}
