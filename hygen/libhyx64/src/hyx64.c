#define _POSIX_C_SOURCE 200809L
#include "hyx64.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- buffer ---- */

void x64_buf_init(X64Buf *b) {
    b->cap = 256;
    b->data = malloc(b->cap);
    b->len = 0;
}

void x64_buf_free(X64Buf *b) {
    free(b->data);
    b->data = NULL; b->len = b->cap = 0;
}

static void ensure(X64Buf *b, size_t extra) {
    if (b->len + extra > b->cap) {
        while (b->len + extra > b->cap) b->cap *= 2;
        b->data = realloc(b->data, b->cap);
    }
}

void x64_buf_push(X64Buf *b, uint8_t byte) {
    ensure(b, 1);
    b->data[b->len++] = byte;
}

void x64_buf_write(X64Buf *b, const void *data, size_t n) {
    ensure(b, n);
    memcpy(b->data + b->len, data, n);
    b->len += n;
}

void x64_buf_write32(X64Buf *b, uint32_t v) { x64_buf_write(b, &v, 4); }
void x64_buf_write64(X64Buf *b, uint64_t v) { x64_buf_write(b, &v, 8); }

/* ---- float constant pool ---- */

static X64ConstPool *g_pool = NULL;
void x64_set_const_pool(X64ConstPool *pool) { g_pool = pool; }
X64ConstPool *x64_get_const_pool(void) { return g_pool; }

void x64_const_pool_free(X64ConstPool *p) {
    for (int i = 0; i < p->n; i++) free(p->v[i].label);
    free(p->v);
    p->v = NULL; p->n = p->cap = 0;
}

/* the (deduplicated) label of an 8- or 4-byte constant with this bit pattern */
const char *x64_const_label(uint64_t bits, int size) {
    X64ConstPool *p = g_pool;
    for (int i = 0; i < p->n; i++)
        if (p->v[i].bits == bits && p->v[i].size == size) return p->v[i].label;
    if (p->n == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 16;
        p->v = realloc(p->v, p->cap * sizeof(X64Const));
    }
    char buf[48];
    snprintf(buf, sizeof buf, "_Lfc%d_%016llx", size, (unsigned long long)bits);
    p->v[p->n].bits = bits; p->v[p->n].size = size; p->v[p->n].label = strdup(buf);
    return p->v[p->n++].label;
}

/* ---- relocations ---- */

void x64_reloc_list_init(X64RelocList *r) {
    r->cap = 8; r->count = 0;
    r->relocs = malloc(r->cap * sizeof(X64Reloc));
}

void x64_reloc_list_free(X64RelocList *r) {
    free(r->relocs); r->relocs = NULL; r->count = r->cap = 0;
}

void x64_reloc_add(X64RelocList *r, size_t offset, const char *symbol, X64RelocKind kind) {
    if (r->count == r->cap) {
        r->cap *= 2;
        r->relocs = realloc(r->relocs, r->cap * sizeof(X64Reloc));
    }
    r->relocs[r->count].offset = offset;
    r->relocs[r->count].symbol = symbol;
    r->relocs[r->count].kind = kind;
    r->count++;
}

/* ---- register pool: order IS load-bearing now. regalloc treats ids
   0..num_callee_saved_int_regs-1 as "survives a call" and will only put a
   value whose live range spans a call into one of them (spilling otherwise),
   so the callee-saved registers MUST come first and the count in hyx64.h
   (X64_NUM_CALLEE_SAVED_INT_REGS) MUST match how many of them there are.
   SysV x86-64 callee-saved: rbx, rbp, rsp, r12-r15. rbp/rsp are the frame,
   leaving rbx + r12-r15 = 5 allocatable ones. Everything after is
   caller-saved and is destroyed by any call. ---- */
const X64Reg x64_int_reg_pool[X64_NUM_ALLOCATABLE_INT_REGS] = {
    /* callee-saved (ids 0-4) */
    X64_RBX, X64_R12, X64_R13, X64_R14, X64_R15,
    /* caller-saved (ids 5-9) */
    X64_RSI, X64_RDI, X64_R8, X64_R9, X64_R10,
    /* caller-saved, but clobbered by divides / variable shifts (ids 10-11) */
    X64_RDX, X64_RCX,
};

unsigned x64_regalloc_clobbers(const MIRInstr *ins) {
    switch (ins->op) {
    case MIR_SDIV: case MIR_UDIV: case MIR_SMOD: case MIR_UMOD:
    case MIR_SMULH: case MIR_UMULH:
        return 1u << X64_POOL_RDX;
    case MIR_SHL: case MIR_SHR_A: case MIR_SHR_L:
        return ins->src2.kind == MIRV_IMM_INT ? 0 : 1u << X64_POOL_RCX;
    default:
        return 0;
    }
}

int x64_func_needs_fixed_regs(const MIRFunc *fn) {
    for (int i = 0; i < fn->count; i++) {
        switch (fn->instrs[i].op) {
        case MIR_ASM_TEXT: case MIR_ASM_RAW: case MIR_SAVE_REGS: case MIR_RESTORE_REGS:
        case MIR_CLI: case MIR_STI: case MIR_IRET: case MIR_SYSRET: case MIR_HLT:
        case MIR_LGDT: case MIR_LIDT: case MIR_LTR: case MIR_INVLPG:
        case MIR_WRMSR: case MIR_RDMSR: case MIR_READ_CR: case MIR_WRITE_CR:
        case MIR_OUTB: case MIR_INB: case MIR_OUTW: case MIR_INW:
            return 1;
        default: break;
        }
    }
    return fn->is_naked;
}

const int x64_float_reg_pool[X64_NUM_ALLOCATABLE_FLOAT_REGS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
};

const X64Reg x64_sysv_arg_regs[6] = {
    X64_RDI, X64_RSI, X64_RDX, X64_RCX, X64_R8, X64_R9,
};

const X64Reg x64_syscall_arg_regs[6] = {
    X64_RDI, X64_RSI, X64_RDX, X64_R10, X64_R8, X64_R9,
};
