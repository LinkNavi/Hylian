#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "miniasm.h"
#include "encode_x64.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>

/* ---- operand model ---- */
typedef enum { OPK_REG, OPK_IMM, OPK_MEM, OPK_LABEL, OPK_NONE } OperandKind;

typedef struct {
    OperandKind kind;
    X64Reg  reg;      /* OPK_REG, and OPK_MEM's base */
    int64_t imm;      /* OPK_IMM, and OPK_MEM's disp */
    char    label[64]; /* OPK_LABEL */
} Operand;

/* ---- register name table ---- */
static int reg_from_name(const char *s, X64Reg *out) {
    struct { const char *name; X64Reg reg; } tbl[] = {
        {"rax", X64_RAX}, {"rcx", X64_RCX}, {"rdx", X64_RDX}, {"rbx", X64_RBX},
        {"rsp", X64_RSP}, {"rbp", X64_RBP}, {"rsi", X64_RSI}, {"rdi", X64_RDI},
        {"r8", X64_R8}, {"r9", X64_R9}, {"r10", X64_R10}, {"r11", X64_R11},
        {"r12", X64_R12}, {"r13", X64_R13}, {"r14", X64_R14}, {"r15", X64_R15},
    };
    for (size_t i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++) {
        if (strcasecmp(s, tbl[i].name) == 0) { *out = tbl[i].reg; return 1; }
    }
    return 0;
}

static int cc_from_suffix(const char *s, CondCode *out) {
    struct { const char *name; CondCode cc; } tbl[] = {
        {"e", CC_E}, {"ne", CC_NE}, {"l", CC_L}, {"le", CC_LE}, {"g", CC_G}, {"ge", CC_GE},
        {"b", CC_B}, {"be", CC_BE}, {"a", CC_A}, {"ae", CC_AE},
    };
    for (size_t i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++) {
        if (strcasecmp(s, tbl[i].name) == 0) { *out = tbl[i].cc; return 1; }
    }
    return 0;
}

/* ---- small string helpers ---- */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return s;
}

static int parse_int(const char *s, int64_t *out) {
    char *end;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    long long v;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) v = strtoll(s + 2, &end, 16);
    else v = strtoll(s, &end, 10);
    if (end == s || *end != '\0') return 0;
    *out = neg ? -v : v;
    return 1;
}

static Operand parse_operand(char *tok, char *errbuf, size_t errcap) {
    Operand op = {0};
    tok = trim(tok);
    if (*tok == '[') {
        char *close = strchr(tok, ']');
        if (!close) { snprintf(errbuf, errcap, "unterminated '[' in operand: %s", tok); op.kind = OPK_NONE; return op; }
        *close = '\0';
        char *inner = trim(tok + 1);
        op.kind = OPK_MEM;
        op.imm = 0;
        /* split on first +/- (not at position 0, which would be a sign on the base - not supported) */
        char *plus = strchr(inner, '+');
        char *minus = strchr(inner, '-');
        char *split = plus;
        int sign = 1;
        if (minus && (!plus || minus < plus)) { split = minus; sign = -1; }
        if (split) {
            *split = '\0';
            char *basepart = trim(inner);
            char *disppart = trim(split + 1);
            if (!reg_from_name(basepart, &op.reg)) {
                snprintf(errbuf, errcap, "unknown base register in memory operand: %s", basepart);
                op.kind = OPK_NONE; return op;
            }
            int64_t d;
            if (!parse_int(disppart, &d)) {
                snprintf(errbuf, errcap, "bad displacement in memory operand: %s", disppart);
                op.kind = OPK_NONE; return op;
            }
            op.imm = sign * d;
        } else {
            if (!reg_from_name(inner, &op.reg)) {
                snprintf(errbuf, errcap, "unknown base register in memory operand: %s", inner);
                op.kind = OPK_NONE; return op;
            }
        }
        return op;
    }
    X64Reg r;
    if (reg_from_name(tok, &r)) { op.kind = OPK_REG; op.reg = r; return op; }
    int64_t v;
    if (parse_int(tok, &v)) { op.kind = OPK_IMM; op.imm = v; return op; }
    if (strlen(tok) < sizeof(op.label)) {
        op.kind = OPK_LABEL;
        strncpy(op.label, tok, sizeof(op.label) - 1);
        return op;
    }
    snprintf(errbuf, errcap, "operand too long or unrecognized: %s", tok);
    op.kind = OPK_NONE;
    return op;
}

/* ---- label fixups, same two-pass approach as lower_x64.c ---- */
typedef struct { char name[64]; int offset; } LocalLabel;
typedef struct { size_t code_offset; char label[64]; } LabelFixup;

#define MAX_LOCAL_LABELS 64
#define MAX_FIXUPS 128

int miniasm_assemble(const char *text, X64Buf *out, X64RelocList *out_relocs,
                      char *err, size_t err_cap) {
    LocalLabel labels[MAX_LOCAL_LABELS];
    int label_count = 0;
    LabelFixup fixups[MAX_FIXUPS];
    int fixup_count = 0;

    char *buf = strdup(text);
    char *saveptr1;
    char *line = strtok_r(buf, "\n", &saveptr1);

    while (line) {
        char linecopy[512];
        strncpy(linecopy, line, sizeof(linecopy) - 1);
        linecopy[sizeof(linecopy) - 1] = '\0';

        char *comment = strchr(linecopy, ';');
        if (comment) *comment = '\0';
        char *t = trim(linecopy);

        if (*t == '\0') { line = strtok_r(NULL, "\n", &saveptr1); continue; }

        size_t tlen = strlen(t);
        if (t[tlen - 1] == ':') {
            t[tlen - 1] = '\0';
            char *name = trim(t);
            if (label_count >= MAX_LOCAL_LABELS) {
                snprintf(err, err_cap, "too many labels in one asm block (max %d)", MAX_LOCAL_LABELS);
                free(buf); return 0;
            }
            strncpy(labels[label_count].name, name, sizeof(labels[0].name) - 1);
            labels[label_count].offset = (int)out->len;
            label_count++;
            line = strtok_r(NULL, "\n", &saveptr1);
            continue;
        }

        /* split into mnemonic + rest */
        char *space = t;
        while (*space && !isspace((unsigned char)*space)) space++;
        char mnemonic[32];
        size_t mlen = (size_t)(space - t);
        if (mlen >= sizeof(mnemonic)) mlen = sizeof(mnemonic) - 1;
        memcpy(mnemonic, t, mlen); mnemonic[mlen] = '\0';
        char *rest = trim(space);

        /* split rest into up to 2 comma-separated operands */
        Operand ops[2]; int opcount = 0;
        if (*rest) {
            char *comma = strchr(rest, ',');
            if (comma) {
                *comma = '\0';
                ops[0] = parse_operand(rest, err, err_cap);
                if (ops[0].kind == OPK_NONE) { free(buf); return 0; }
                ops[1] = parse_operand(comma + 1, err, err_cap);
                if (ops[1].kind == OPK_NONE) { free(buf); return 0; }
                opcount = 2;
            } else {
                ops[0] = parse_operand(rest, err, err_cap);
                if (ops[0].kind == OPK_NONE) { free(buf); return 0; }
                opcount = 1;
            }
        }

        #define ERR(...) do { snprintf(err, err_cap, __VA_ARGS__); free(buf); return 0; } while (0)
        #define NEED_REG(o) ((o).kind == OPK_REG)
        #define NEED_MEM(o) ((o).kind == OPK_MEM)
        #define NEED_IMM(o) ((o).kind == OPK_IMM)

        CondCode cc;

        if (strcasecmp(mnemonic, "ret") == 0) { enc_ret(out); }
        else if (strcasecmp(mnemonic, "syscall") == 0) { enc_syscall(out); }
        else if (strcasecmp(mnemonic, "cli") == 0) { enc_cli(out); }
        else if (strcasecmp(mnemonic, "sti") == 0) { enc_sti(out); }
        else if (strcasecmp(mnemonic, "iretq") == 0 || strcasecmp(mnemonic, "iret") == 0) { enc_iretq(out); }
        else if (strcasecmp(mnemonic, "wrmsr") == 0) { enc_wrmsr(out); }
        else if (strcasecmp(mnemonic, "rdmsr") == 0) { enc_rdmsr(out); }
        else if (strcasecmp(mnemonic, "nop") == 0) { x64_buf_push(out, 0x90); }

        else if (strcasecmp(mnemonic, "push") == 0) {
            if (opcount != 1 || !NEED_REG(ops[0])) ERR("push needs one register operand");
            enc_push(out, ops[0].reg);
        }
        else if (strcasecmp(mnemonic, "pop") == 0) {
            if (opcount != 1 || !NEED_REG(ops[0])) ERR("pop needs one register operand");
            enc_pop(out, ops[0].reg);
        }
        else if (strcasecmp(mnemonic, "neg") == 0) {
            if (opcount != 1 || !NEED_REG(ops[0])) ERR("neg needs one register operand");
            enc_neg_r(out, ops[0].reg);
        }
        else if (strcasecmp(mnemonic, "not") == 0) {
            if (opcount != 1 || !NEED_REG(ops[0])) ERR("not needs one register operand");
            enc_not_r(out, ops[0].reg);
        }
        else if (strcasecmp(mnemonic, "jmp") == 0 || strcasecmp(mnemonic, "call") == 0) {
            if (opcount != 1 || ops[0].kind != OPK_LABEL) ERR("%s needs a label/symbol operand", mnemonic);
            size_t off = strcasecmp(mnemonic, "jmp") == 0 ? enc_jmp_rel32(out) : enc_call_rel32(out);
            if (fixup_count >= MAX_FIXUPS) ERR("too many jump/call targets in one asm block");
            strncpy(fixups[fixup_count].label, ops[0].label, sizeof(fixups[0].label) - 1);
            fixups[fixup_count].code_offset = off;
            fixup_count++;
        }
        else if (mnemonic[0] == 'j' && cc_from_suffix(mnemonic + 1, &cc)) {
            if (opcount != 1 || ops[0].kind != OPK_LABEL) ERR("%s needs a label operand", mnemonic);
            size_t off = enc_jcc_rel32(out, cc);
            if (fixup_count >= MAX_FIXUPS) ERR("too many jump/call targets in one asm block");
            strncpy(fixups[fixup_count].label, ops[0].label, sizeof(fixups[0].label) - 1);
            fixups[fixup_count].code_offset = off;
            fixup_count++;
        }
        else if (strncasecmp(mnemonic, "set", 3) == 0 && cc_from_suffix(mnemonic + 3, &cc)) {
            if (opcount != 1 || !NEED_REG(ops[0])) ERR("%s needs one register operand", mnemonic);
            enc_setcc(out, cc, ops[0].reg);
        }

        else if (strcasecmp(mnemonic, "mov") == 0) {
            if (opcount != 2) ERR("mov needs two operands");
            /* mov crN, reg / mov reg, crN - crN parsed as a bare label "cr0".."cr8" */
            if (ops[0].kind == OPK_LABEL && strncasecmp(ops[0].label, "cr", 2) == 0 && NEED_REG(ops[1])) {
                int n = atoi(ops[0].label + 2);
                enc_mov_crN_from_r(out, n, ops[1].reg);
            } else if (ops[1].kind == OPK_LABEL && strncasecmp(ops[1].label, "cr", 2) == 0 && NEED_REG(ops[0])) {
                int n = atoi(ops[1].label + 2);
                enc_mov_r_from_crN(out, ops[0].reg, n);
            } else if (NEED_REG(ops[0]) && NEED_REG(ops[1])) {
                enc_mov_rr(out, ops[0].reg, ops[1].reg);
            } else if (NEED_REG(ops[0]) && NEED_IMM(ops[1])) {
                enc_mov_ri64(out, ops[0].reg, ops[1].imm);
            } else if (NEED_REG(ops[0]) && NEED_MEM(ops[1])) {
                enc_mov_load(out, ops[0].reg, ops[1].reg, (int32_t)ops[1].imm);
            } else if (NEED_MEM(ops[0]) && NEED_REG(ops[1])) {
                enc_mov_store(out, ops[0].reg, (int32_t)ops[0].imm, ops[1].reg);
            } else {
                ERR("unsupported mov operand combination");
            }
        }
        else if (strcasecmp(mnemonic, "lea") == 0) {
            if (opcount != 2 || !NEED_REG(ops[0]) || !NEED_MEM(ops[1])) ERR("lea needs reg, [mem]");
            /* no dedicated lea-with-disp primitive exists yet - reuse mov_load's
               addressing but emit opcode 0x8D instead of 0x8B via a tiny local encode */
            x64_buf_push(out, x64_rex(1, ops[0].reg, ops[1].reg));
            x64_buf_push(out, 0x8D);
            x64_buf_push(out, x64_modrm(2, ops[0].reg, ops[1].reg));
            x64_buf_write32(out, (uint32_t)(int32_t)ops[1].imm);
        }
        else if (strcasecmp(mnemonic, "add") == 0 || strcasecmp(mnemonic, "sub") == 0 ||
                 strcasecmp(mnemonic, "and") == 0 || strcasecmp(mnemonic, "or") == 0 ||
                 strcasecmp(mnemonic, "xor") == 0 || strcasecmp(mnemonic, "cmp") == 0) {
            if (opcount != 2 || !NEED_REG(ops[0])) ERR("%s needs reg as first operand", mnemonic);
            AluOp aop = strcasecmp(mnemonic, "add") == 0 ? ALU_ADD
                      : strcasecmp(mnemonic, "sub") == 0 ? ALU_SUB
                      : strcasecmp(mnemonic, "and") == 0 ? ALU_AND
                      : strcasecmp(mnemonic, "or") == 0  ? ALU_OR
                      : strcasecmp(mnemonic, "xor") == 0 ? ALU_XOR : ALU_CMP;
            if (NEED_REG(ops[1])) enc_alu_rr(out, aop, ops[0].reg, ops[1].reg);
            else if (NEED_IMM(ops[1])) enc_alu_ri32(out, aop, ops[0].reg, (int32_t)ops[1].imm);
            else ERR("%s: second operand must be a register or immediate", mnemonic);
        }
        else if (strcasecmp(mnemonic, "imul") == 0) {
            if (opcount != 2 || !NEED_REG(ops[0]) || !NEED_REG(ops[1])) ERR("imul needs reg, reg");
            enc_imul_rr(out, ops[0].reg, ops[1].reg);
        }
        else if (strcasecmp(mnemonic, "shl") == 0 || strcasecmp(mnemonic, "shr") == 0 || strcasecmp(mnemonic, "sar") == 0) {
            if (opcount != 2 || !NEED_REG(ops[0]) || ops[1].kind != OPK_LABEL || strcasecmp(ops[1].label, "cl") != 0)
                ERR("%s only supports 'reg, cl' in this assembler (no immediate shift count yet)", mnemonic);
            ShiftKind k = strcasecmp(mnemonic, "shl") == 0 ? SHIFT_SHL
                        : strcasecmp(mnemonic, "shr") == 0 ? SHIFT_SHR : SHIFT_SAR;
            enc_shift_cl(out, k, ops[0].reg);
        }
        else if (strcasecmp(mnemonic, "in") == 0) {
            if (opcount != 2 || ops[0].kind != OPK_LABEL || strcasecmp(ops[0].label, "al") != 0 ||
                ops[1].kind != OPK_LABEL || strcasecmp(ops[1].label, "dx") != 0)
                ERR("this assembler only supports 'in al, dx'");
            enc_in_al_dx(out);
        }
        else if (strcasecmp(mnemonic, "out") == 0) {
            if (opcount != 2 || ops[0].kind != OPK_LABEL || strcasecmp(ops[0].label, "dx") != 0 ||
                ops[1].kind != OPK_LABEL || strcasecmp(ops[1].label, "al") != 0)
                ERR("this assembler only supports 'out dx, al'");
            enc_out_dx_al(out);
        }
        else {
            ERR("unrecognized instruction: %s", mnemonic);
        }

        #undef ERR
        #undef NEED_REG
        #undef NEED_MEM
        #undef NEED_IMM

        line = strtok_r(NULL, "\n", &saveptr1);
    }

    /* resolve local-label fixups; anything not defined in this block is an
       external symbol relocation instead (e.g. calling a runtime function
       by name from inside an asm block) */
    for (int f = 0; f < fixup_count; f++) {
        int found = -1;
        for (int l = 0; l < label_count; l++)
            if (strcmp(labels[l].name, fixups[f].label) == 0) { found = l; break; }
        if (found >= 0) {
            int32_t rel = (int32_t)(labels[found].offset - (int)(fixups[f].code_offset + 4));
            memcpy(out->data + fixups[f].code_offset, &rel, 4);
        } else {
            x64_reloc_add(out_relocs, fixups[f].code_offset, strdup(fixups[f].label), X64_RELOC_REL32);
        }
    }

    free(buf);
    return 1;
}
