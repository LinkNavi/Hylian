/*
 * codegen_elf.c — Direct ELF64 object file emitter for Hylian.
 *
 * Replaces the NASM pipeline: IR → x86-64 machine code → ELF64 .o
 * The output is a standard relocatable object linkable with ld/gcc.
 *
 * Architecture: two-pass
 *   Pass 1: walk IR, encode instructions into a byte buffer (.text),
 *           collect string/float constants (.rodata), globals (.data/.bss),
 *           record unresolved symbol references as relocations.
 *   Pass 2: write ELF64 headers, sections, symbol table, relocation table.
 */

#include "codegen_elf.h"
#include "ir.h"
#include "ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

/* ── ELF64 structures ───────────────────────────────────────────────────── */

#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS64   2
#define ELFDATA2LSB  1
#define ET_REL       1
#define EM_X86_64    62
#define EV_CURRENT   1
#define SHT_NULL     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHF_ALLOC    0x2
#define SHF_EXECINSTR 0x4
#define SHF_WRITE    0x1
#define STB_LOCAL    0
#define STB_GLOBAL   1
#define STT_NOTYPE   0
#define STT_OBJECT   1
#define STT_FUNC     2
#define STT_SECTION  3
#define STT_FILE     4
#define STV_DEFAULT  0
#define SHN_UNDEF    0
#define SHN_ABS      0xfff1
#define R_X86_64_64       1
#define R_X86_64_PC32     2
#define R_X86_64_PLT32    4
#define R_X86_64_32S      11
#define R_X86_64_32       10

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum;
    uint16_t e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t sh_name, sh_type;
    uint64_t sh_flags, sh_addr, sh_offset, sh_size;
    uint32_t sh_link, sh_info;
    uint64_t sh_addralign, sh_entsize;
} Elf64_Shdr;

typedef struct {
    uint32_t st_name;
    uint8_t  st_info, st_other;
    uint16_t st_shndx;
    uint64_t st_value, st_size;
} Elf64_Sym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} Elf64_Rela;

#define ELF64_R_INFO(s,t) (((uint64_t)(s)<<32)|(uint32_t)(t))
#define ELF64_ST_INFO(b,t) (((b)<<4)|((t)&0xf))

/* ── Dynamic byte buffers ───────────────────────────────────────────────── */

typedef struct {
    uint8_t *data;
    size_t   len, cap;
} Buf;

static void buf_init(Buf *b) { b->data = NULL; b->len = b->cap = 0; }
static void buf_free(Buf *b) { free(b->data); buf_init(b); }

static void buf_grow(Buf *b, size_t need) {
    if (b->len + need <= b->cap) return;
    size_t nc = b->cap ? b->cap * 2 : 256;
    while (nc < b->len + need) nc *= 2;
    b->data = realloc(b->data, nc);
    b->cap = nc;
}

static void buf_push(Buf *b, uint8_t byte) {
    buf_grow(b, 1);
    b->data[b->len++] = byte;
}

static void buf_write(Buf *b, const void *data, size_t n) {
    buf_grow(b, n);
    memcpy(b->data + b->len, data, n);
    b->len += n;
}

static void buf_write8(Buf *b, uint8_t v)   { buf_push(b, v); }
static void buf_write16(Buf *b, uint16_t v) { buf_write(b, &v, 2); }
static void buf_write32(Buf *b, uint32_t v) { buf_write(b, &v, 4); }
static void buf_write64(Buf *b, uint64_t v) { buf_write(b, &v, 8); }
static void buf_patch32(Buf *b, size_t off, uint32_t v) {
    memcpy(b->data + off, &v, 4);
}
static void buf_align(Buf *b, size_t align) {
    while (b->len % align) buf_push(b, 0);
}

/* String table helper */
static size_t strtab_add(Buf *st, const char *s) {
    size_t off = st->len;
    buf_write(st, s, strlen(s) + 1);
    return off;
}

/* ── Symbol table ───────────────────────────────────────────────────────── */

#define MAX_SYMS 4096

typedef struct {
    char    name[256];
    int     is_global;    /* STB_GLOBAL vs STB_LOCAL */
    int     sym_type;     /* STT_FUNC / STT_OBJECT / STT_NOTYPE */
    int     section;      /* 1=.text, 2=.rodata, 3=.data, 0=undef */
    int     shndx;        /* actual ELF shndx filled at write time */
    uint64_t value;       /* offset within section */
    uint32_t strtab_off;  /* offset in .strtab */
    int      elf_index;   /* index in final symtab */
    size_t   st_size_dummy; /* function size, filled by handle_func_end */
} Sym;

typedef struct {
    Sym  syms[MAX_SYMS];
    int  count;
} SymTab;

static Sym *sym_find(SymTab *st, const char *name) {
    for (int i = 0; i < st->count; i++)
        if (strcmp(st->syms[i].name, name) == 0)
            return &st->syms[i];
    return NULL;
}

static Sym *sym_add(SymTab *st, const char *name) {
    if (st->count >= MAX_SYMS) { fprintf(stderr, "elf: too many symbols\n"); exit(1); }
    Sym *s = &st->syms[st->count++];
    memset(s, 0, sizeof(*s));
    strncpy(s->name, name, 255);
    return s;
}

static Sym *sym_get_or_add(SymTab *st, const char *name) {
    Sym *s = sym_find(st, name);
    return s ? s : sym_add(st, name);
}

/* ── Relocation list ────────────────────────────────────────────────────── */

#define MAX_RELOCS 8192

typedef struct {
    uint64_t offset;   /* offset in .text */
    char     sym[256]; /* target symbol name */
    int      type;     /* R_X86_64_* */
    int64_t  addend;
} Reloc;

typedef struct {
    Reloc relocs[MAX_RELOCS];
    int   count;
} RelocList;

static void reloc_add(RelocList *rl, uint64_t off, const char *sym, int type, int64_t addend) {
    if (rl->count >= MAX_RELOCS) { fprintf(stderr, "elf: too many relocations\n"); exit(1); }
    Reloc *r = &rl->relocs[rl->count++];
    r->offset = off;
    strncpy(r->sym, sym, 255);
    r->type   = type;
    r->addend = addend;
}

/* ── Register encoding ──────────────────────────────────────────────────── */

/* We use a virtual register allocator mapping temp IDs to physical regs.
   For simplicity, temps are spilled to the stack — each temp gets a slot.
   rax = scratch/return, rbx/rcx/rdx/rsi/rdi/r8-r15 = arg passing & scratch.
   We keep it simple: all temps live on the stack, loaded into rax/rdx as needed. */

/* x86-64 register numbers (ModRM/REX encoding) */
#define REG_RAX 0
#define REG_RCX 1
#define REG_RDX 2
#define REG_RBX 3
#define REG_RSP 4
#define REG_RBP 5
#define REG_RSI 6
#define REG_RDI 7
#define REG_R8  8
#define REG_R9  9
#define REG_R10 10
#define REG_R11 11
#define REG_R12 12
#define REG_R13 13
#define REG_R14 14
#define REG_R15 15

/* SysV AMD64 argument registers */
static const int SYSV_ARG_REGS[] = { REG_RDI, REG_RSI, REG_RDX, REG_RCX, REG_R8, REG_R9 };
#define SYSV_ARG_REG_COUNT 6

/* ── Encoder state ──────────────────────────────────────────────────────── */

#define MAX_LOCALS   512
#define MAX_LABELS   4096
#define MAX_PATCHES  4096

typedef struct {
    char    name[128];
    int32_t rbp_off;   /* negative offset from rbp */
    int     size;      /* 8 for most things */
} Local;

typedef struct {
    int     label_id;
    int32_t text_off;  /* -1 = not yet defined */
} Label;

typedef struct {
    size_t  patch_off;  /* offset of the 4-byte field to patch */
    int     label_id;
    int     is_rel32;   /* 1 = PC-relative (jmp/jcc), 0 = absolute */
} Patch;

typedef struct {
    /* Output buffers */
    Buf text;
    Buf rodata;
    Buf data;
    Buf bss_names;  /* for tracking bss symbols */

    /* Symbol + reloc tables */
    SymTab   syms;
    RelocList relocs;
    RelocList rodata_relocs; /* relocations within .rodata (unused for now) */

    /* Per-function state */
    Local  locals[MAX_LOCALS];
    int    local_count;
    int32_t frame_size;   /* current frame size (grows as we see ALLOCA) */

    /* Label resolution */
    Label  labels[MAX_LABELS];
    int    label_count;
    Patch  patches[MAX_PATCHES];
    int    patch_count;

    /* String/float constant pools */
    struct { char *val; char label[64]; size_t rodata_off; } str_consts[1024];
    int str_const_count;
    struct { double val; char label[64]; size_t rodata_off; } flt_consts[512];
    int flt_const_count;

    /* BSS symbols (zero-initialized globals) */
    struct { char name[128]; size_t size; size_t align; } bss_syms[512];
    int bss_sym_count;
    size_t bss_size;

    /* Current function metadata */
    char   cur_fn[128];
    size_t fn_text_start;
    int    is_main;
    int    freestanding;
    const char *target;
} ElfCtx;

static void ctx_init(ElfCtx *ctx, const char *target, int freestanding) {
    memset(ctx, 0, sizeof(*ctx));
    buf_init(&ctx->text);
    buf_init(&ctx->rodata);
    buf_init(&ctx->data);
    ctx->target      = target;
    ctx->freestanding = freestanding;
}

static void ctx_free(ElfCtx *ctx) {
    buf_free(&ctx->text);
    buf_free(&ctx->rodata);
    buf_free(&ctx->data);
    for (int i = 0; i < ctx->str_const_count; i++)
        free(ctx->str_consts[i].val);
}

/* ── Low-level x86-64 instruction emitters ──────────────────────────────── */

/* REX prefix: W=64bit, R=reg ext, X=sib ext, B=rm/base ext */
static void emit_rex(Buf *b, int W, int R, int X, int B) {
    uint8_t rex = 0x40 | (W<<3) | (R<<2) | (X<<1) | B;
    if (rex != 0x40) buf_push(b, rex);  /* omit 0x40 (identity REX) unless needed */
}
static void emit_rex_always(Buf *b, int W, int R, int X, int B) {
    buf_push(b, 0x40 | (W<<3) | (R<<2) | (X<<1) | B);
}

/* ModRM byte: mod=2bits, reg=3bits, rm=3bits */
static void emit_modrm(Buf *b, int mod, int reg, int rm) {
    buf_push(b, (mod<<6) | ((reg&7)<<3) | (rm&7));
}

/* SIB byte */
static void emit_sib(Buf *b, int scale, int idx, int base) {
    buf_push(b, (scale<<6) | ((idx&7)<<3) | (base&7));
}

/* Emit REX + opcode + ModRM for reg-reg op (64-bit) */
static void emit_rr64(Buf *b, uint8_t opc, int dst, int src) {
    emit_rex(b, 1, src>>3, 0, dst>>3);
    buf_push(b, opc);
    emit_modrm(b, 3, src&7, dst&7);
}

/* mov reg, reg */
static void emit_mov_rr(Buf *b, int dst, int src) {
    if (dst == src) return;
    emit_rex(b, 1, src>>3, 0, dst>>3);
    buf_push(b, 0x89);
    emit_modrm(b, 3, src&7, dst&7);
}

/* mov reg, imm64 */
static void emit_mov_ri64(Buf *b, int reg, int64_t imm) {
    emit_rex_always(b, 1, 0, 0, reg>>3);
    buf_push(b, 0xB8 | (reg&7));
    buf_write(b, &imm, 8);
}

/* mov reg, imm32 (sign-extended to 64) */
static void emit_mov_ri32(Buf *b, int reg, int32_t imm) {
    /* use REX.W + C7 /0 for sign-extend */
    emit_rex(b, 1, 0, 0, reg>>3);
    buf_push(b, 0xC7);
    emit_modrm(b, 3, 0, reg&7);
    buf_write32(b, (uint32_t)imm);
}

/* mov [rbp+off], reg (store to stack slot) */
static void emit_store_rbp(Buf *b, int32_t off, int src) {
    emit_rex(b, 1, src>>3, 0, 0);
    buf_push(b, 0x89);
    if (off >= -128 && off <= 127) {
        emit_modrm(b, 1, src&7, REG_RBP);
        buf_push(b, (uint8_t)(int8_t)off);
    } else {
        emit_modrm(b, 2, src&7, REG_RBP);
        buf_write32(b, (uint32_t)off);
    }
}

/* mov reg, [rbp+off] (load from stack slot) */
static void emit_load_rbp(Buf *b, int reg, int32_t off) {
    emit_rex(b, 1, reg>>3, 0, 0);
    buf_push(b, 0x8B);
    if (off >= -128 && off <= 127) {
        emit_modrm(b, 1, reg&7, REG_RBP);
        buf_push(b, (uint8_t)(int8_t)off);
    } else {
        emit_modrm(b, 2, reg&7, REG_RBP);
        buf_write32(b, (uint32_t)off);
    }
}

/* push reg */
static void emit_push(Buf *b, int reg) {
    if (reg >= 8) buf_push(b, 0x41);
    buf_push(b, 0x50 | (reg&7));
}

/* pop reg */
static void emit_pop(Buf *b, int reg) {
    if (reg >= 8) buf_push(b, 0x41);
    buf_push(b, 0x58 | (reg&7));
}

/* ret */
static void emit_ret(Buf *b) { buf_push(b, 0xC3); }

/* nop */
static void emit_nop(Buf *b) { buf_push(b, 0x90); }

/* add/sub/and/or/xor reg, reg */
static void emit_alu_rr(Buf *b, uint8_t opc, int dst, int src) {
    emit_rex(b, 1, src>>3, 0, dst>>3);
    buf_push(b, opc);
    emit_modrm(b, 3, src&7, dst&7);
}

/* add reg, imm32 */
static void emit_add_ri(Buf *b, int reg, int32_t imm) {
    emit_rex(b, 1, 0, 0, reg>>3);
    if (imm >= -128 && imm <= 127) {
        buf_push(b, 0x83); emit_modrm(b, 3, 0, reg&7); buf_push(b, (uint8_t)(int8_t)imm);
    } else {
        buf_push(b, 0x81); emit_modrm(b, 3, 0, reg&7); buf_write32(b, (uint32_t)imm);
    }
}

/* sub reg, imm32 */
static void emit_sub_ri(Buf *b, int reg, int32_t imm) {
    emit_rex(b, 1, 0, 0, reg>>3);
    if (imm >= -128 && imm <= 127) {
        buf_push(b, 0x83); emit_modrm(b, 3, 5, reg&7); buf_push(b, (uint8_t)(int8_t)imm);
    } else {
        buf_push(b, 0x81); emit_modrm(b, 3, 5, reg&7); buf_write32(b, (uint32_t)imm);
    }
}

/* imul dst, src (64-bit) */
static void emit_imul_rr(Buf *b, int dst, int src) {
    emit_rex(b, 1, dst>>3, 0, src>>3);
    buf_push(b, 0x0F); buf_push(b, 0xAF);
    emit_modrm(b, 3, dst&7, src&7);
}

/* cqo (sign-extend rax into rdx:rax) */
static void emit_cqo(Buf *b) { buf_push(b, 0x48); buf_push(b, 0x99); }

/* idiv reg */
static void emit_idiv(Buf *b, int reg) {
    emit_rex(b, 1, 0, 0, reg>>3);
    buf_push(b, 0xF7);
    emit_modrm(b, 3, 7, reg&7);
}

/* neg reg */
static void emit_neg(Buf *b, int reg) {
    emit_rex(b, 1, 0, 0, reg>>3);
    buf_push(b, 0xF7);
    emit_modrm(b, 3, 3, reg&7);
}

/* not reg */
static void emit_not_r(Buf *b, int reg) {
    emit_rex(b, 1, 0, 0, reg>>3);
    buf_push(b, 0xF7);
    emit_modrm(b, 3, 2, reg&7);
}

/* cmp reg, reg */
static void emit_cmp_rr(Buf *b, int a, int bx) {
    emit_rex(b, 1, a>>3, 0, bx>>3);
    buf_push(b, 0x3B);
    emit_modrm(b, 3, a&7, bx&7);
}

/* cmp reg, imm32 */
static void emit_cmp_ri(Buf *b, int reg, int32_t imm) {
    emit_rex(b, 1, 0, 0, reg>>3);
    if (imm >= -128 && imm <= 127) {
        buf_push(b, 0x83); emit_modrm(b, 3, 7, reg&7); buf_push(b, (uint8_t)(int8_t)imm);
    } else {
        buf_push(b, 0x81); emit_modrm(b, 3, 7, reg&7); buf_write32(b, (uint32_t)imm);
    }
}

/* test reg, reg */
static void emit_test_rr(Buf *b, int a, int bx) { emit_alu_rr(b, 0x85, a, bx); }

/* setcc al */
static void emit_setcc(Buf *b, uint8_t cc) {
    buf_push(b, 0x0F); buf_push(b, cc);
    emit_modrm(b, 3, 0, REG_RAX);
}

/* movzx rax, al */
static void emit_movzx_rax_al(Buf *b) {
    buf_push(b, 0x48); buf_push(b, 0x0F); buf_push(b, 0xB6);
    emit_modrm(b, 3, REG_RAX, REG_RAX);
}

/* shl/shr reg, cl */
static void emit_shift_rc(Buf *b, uint8_t opc_ext, int reg) {
    emit_rex(b, 1, 0, 0, reg>>3);
    buf_push(b, 0xD3);
    emit_modrm(b, 3, opc_ext, reg&7);
}
static void emit_shl_rc(Buf *b, int reg) { emit_shift_rc(b, 4, reg); }
static void emit_shr_rc(Buf *b, int reg) { emit_shift_rc(b, 5, reg); }  /* logical */
static void emit_sar_rc(Buf *b, int reg) { emit_shift_rc(b, 7, reg); }  /* arithmetic */

/* jmp rel32 — emits 0xE9 + 4-byte placeholder, returns offset of placeholder */
static size_t emit_jmp_rel32(Buf *b) {
    buf_push(b, 0xE9);
    size_t off = b->len;
    buf_write32(b, 0);
    return off;
}

/* jcc rel32 — emits 0x0F 0x8x + 4-byte placeholder */
static size_t emit_jcc_rel32(Buf *b, uint8_t cc) {
    buf_push(b, 0x0F); buf_push(b, cc);
    size_t off = b->len;
    buf_write32(b, 0);
    return off;
}

/* call rel32 — emits 0xE8 + reloc placeholder */
static size_t emit_call_rel32(Buf *b) {
    buf_push(b, 0xE8);
    size_t off = b->len;
    buf_write32(b, 0);
    return off;
}

/* lea rax, [rip + disp32] — for RIP-relative addresses */
static size_t emit_lea_rip(Buf *b, int reg) {
    emit_rex(b, 1, reg>>3, 0, 0);
    buf_push(b, 0x8D);
    emit_modrm(b, 0, reg&7, 5);  /* mod=00, rm=101 → RIP+disp32 */
    size_t off = b->len;
    buf_write32(b, 0);
    return off;
}

/* mov [reg], src (mem store through pointer) */
static void emit_store_ptr(Buf *b, int base, int src) {
    emit_rex(b, 1, src>>3, 0, base>>3);
    buf_push(b, 0x89);
    emit_modrm(b, 0, src&7, base&7);
}

/* mov dst, [reg] (mem load through pointer) */
static void emit_load_ptr(Buf *b, int dst, int base) {
    emit_rex(b, 1, dst>>3, 0, base>>3);
    buf_push(b, 0x8B);
    emit_modrm(b, 0, dst&7, base&7);
}

/* syscall */
static void emit_syscall(Buf *b) { buf_push(b, 0x0F); buf_push(b, 0x05); }

/* cli / sti */
static void emit_cli(Buf *b) { buf_push(b, 0xFA); }
static void emit_sti(Buf *b) { buf_push(b, 0xFB); }

/* iretq */
static void emit_iretq(Buf *b) { buf_push(b, 0x48); buf_push(b, 0xCF); }

/* hlt */
static void emit_hlt(Buf *b) { buf_push(b, 0xF4); }

/* out dx, al  (0xEE) — port in rdx, byte in al */
static void emit_out_dx_al(Buf *b) { buf_push(b, 0xEE); }

/* in al, dx  (0xEC) */
static void emit_in_al_dx(Buf *b) { buf_push(b, 0xEC); }

/* wrmsr — ecx=msr, edx:eax=value */
static void emit_wrmsr(Buf *b) { buf_push(b, 0x0F); buf_push(b, 0x30); }

/* rdmsr — ecx=msr → edx:eax */
static void emit_rdmsr(Buf *b) { buf_push(b, 0x0F); buf_push(b, 0x32); }

/* ltr ax */
static void emit_ltr(Buf *b) {
    buf_push(b, 0x0F); buf_push(b, 0x00);
    emit_modrm(b, 3, 3, REG_RAX);
}

/* lgdt [rax] */
static void emit_lgdt_mem(Buf *b, int reg) {
    buf_push(b, 0x0F); buf_push(b, 0x01);
    emit_modrm(b, 0, 2, reg&7);
}

/* lidt [rax] */
static void emit_lidt_mem(Buf *b, int reg) {
    buf_push(b, 0x0F); buf_push(b, 0x01);
    emit_modrm(b, 0, 3, reg&7);
}

/* invlpg [rax] */
static void emit_invlpg(Buf *b, int reg) {
    buf_push(b, 0x0F); buf_push(b, 0x01);
    emit_modrm(b, 0, 7, reg&7);
}

/* mov rax, crN */
static void emit_mov_from_cr(Buf *b, int crn) {
    buf_push(b, 0x0F); buf_push(b, 0x20);
    emit_modrm(b, 3, crn&7, REG_RAX);
}

/* mov crN, rax */
static void emit_mov_to_cr(Buf *b, int crn, int src) {
    emit_rex(b, 0, crn>>3, 0, src>>3);
    buf_push(b, 0x0F); buf_push(b, 0x22);
    emit_modrm(b, 3, crn&7, src&7);
}

/* rep stosq — fill */
static void emit_rep_stosq(Buf *b) {
    buf_push(b, 0x48); buf_push(b, 0xF3); buf_push(b, 0xAB);
}

/* rep movsq — copy */
static void emit_rep_movsq(Buf *b) {
    buf_push(b, 0x48); buf_push(b, 0xF3); buf_push(b, 0xA5);
}

/* xor reg, reg (zero a register, 32-bit form zeros upper 32 bits) */
static void emit_xor32_rr(Buf *b, int reg) {
    if (reg >= 8) { buf_push(b, 0x45); }
    buf_push(b, 0x31);
    emit_modrm(b, 3, reg&7, reg&7);
}

/* ── Stack frame / local variable management ───────────────────────────── */

static Local *local_find(ElfCtx *ctx, const char *name) {
    for (int i = 0; i < ctx->local_count; i++)
        if (strcmp(ctx->locals[i].name, name) == 0)
            return &ctx->locals[i];
    return NULL;
}

static Local *local_add(ElfCtx *ctx, const char *name, int size) {
    ctx->frame_size += size;
    Local *l = &ctx->locals[ctx->local_count++];
    strncpy(l->name, name, 127);
    l->rbp_off = -(int32_t)ctx->frame_size;
    l->size    = size;
    return l;
}

/* Load operand into rax. If it's a temp, load from stack. */
static void load_operand_rax(ElfCtx *ctx, IROperand op) {
    Buf *b = &ctx->text;
    switch (op.kind) {
    case IROP_TEMP: {
        char tname[32]; snprintf(tname, 32, "__t%d", op.temp_id);
        Local *l = local_find(ctx, tname);
        if (l) emit_load_rbp(b, REG_RAX, l->rbp_off);
        else   emit_xor32_rr(b, REG_RAX); /* undefined temp = 0 */
        break;
    }
    case IROP_CONST_INT:
        emit_mov_ri64(b, REG_RAX, op.int_val);
        break;
    case IROP_CONST_BOOL:
        emit_mov_ri32(b, REG_RAX, op.bool_val ? 1 : 0);
        break;
    case IROP_CONST_STR:
        /* Will be resolved via RIP-relative LEA + reloc */
        /* For now emit a placeholder — caller handles string consts */
        emit_xor32_rr(b, REG_RAX);
        break;
    case IROP_CONST_FLOAT:
        /* Float handled separately via XMM */
        break;
    case IROP_NONE:
        emit_xor32_rr(b, REG_RAX);
        break;
    default:
        break;
    }
}

/* Load operand into rdx */
static void load_operand_rdx(ElfCtx *ctx, IROperand op) {
    Buf *b = &ctx->text;
    switch (op.kind) {
    case IROP_TEMP: {
        char tname[32]; snprintf(tname, 32, "__t%d", op.temp_id);
        Local *l = local_find(ctx, tname);
        if (l) emit_load_rbp(b, REG_RDX, l->rbp_off);
        else   emit_xor32_rr(b, REG_RDX);
        break;
    }
    case IROP_CONST_INT:
        emit_mov_ri64(b, REG_RDX, op.int_val);
        break;
    case IROP_CONST_BOOL:
        emit_mov_ri32(b, REG_RDX, op.bool_val ? 1 : 0);
        break;
    default:
        emit_xor32_rr(b, REG_RDX);
        break;
    }
}

/* Store rax into dest temp's stack slot */
static void store_dest_rax(ElfCtx *ctx, IROperand dest) {
    if (dest.kind != IROP_TEMP) return;
    char tname[32]; snprintf(tname, 32, "__t%d", dest.temp_id);
    Local *l = local_find(ctx, tname);
    if (!l) l = local_add(ctx, tname, 8);
    emit_store_rbp(&ctx->text, l->rbp_off, REG_RAX);
}

/* ── Label / patch resolution ───────────────────────────────────────────── */

static Label *label_get(ElfCtx *ctx, int id) {
    for (int i = 0; i < ctx->label_count; i++)
        if (ctx->labels[i].label_id == id)
            return &ctx->labels[i];
    if (ctx->label_count >= MAX_LABELS) { fprintf(stderr, "elf: too many labels\n"); exit(1); }
    Label *l = &ctx->labels[ctx->label_count++];
    l->label_id = id;
    l->text_off = -1;
    return l;
}

static void label_define(ElfCtx *ctx, int id) {
    Label *l = label_get(ctx, id);
    l->text_off = (int32_t)ctx->text.len;
    /* Patch any forward references */
    for (int i = 0; i < ctx->patch_count; i++) {
        Patch *p = &ctx->patches[i];
        if (p->label_id == id) {
            int32_t rel = (int32_t)(l->text_off - (int32_t)(p->patch_off + 4));
            buf_patch32(&ctx->text, p->patch_off, (uint32_t)rel);
        }
    }
}

static void patch_label(ElfCtx *ctx, size_t patch_off, int label_id) {
    Label *l = label_get(ctx, label_id);
    if (l->text_off >= 0) {
        /* Already defined — patch immediately */
        int32_t rel = (int32_t)(l->text_off - (int32_t)(patch_off + 4));
        buf_patch32(&ctx->text, patch_off, (uint32_t)rel);
    } else {
        /* Forward reference — record for later */
        if (ctx->patch_count >= MAX_PATCHES) { fprintf(stderr, "elf: too many patches\n"); exit(1); }
        Patch *p = &ctx->patches[ctx->patch_count++];
        p->patch_off = patch_off;
        p->label_id  = label_id;
        p->is_rel32  = 1;
    }
}

/* ── String constant pool ───────────────────────────────────────────────── */

static const char *rodata_str_label(ElfCtx *ctx, const char *val) {
    for (int i = 0; i < ctx->str_const_count; i++)
        if (strcmp(ctx->str_consts[i].val, val) == 0)
            return ctx->str_consts[i].label;
    if (ctx->str_const_count >= 1024) return "_str_overflow";
    int idx = ctx->str_const_count++;
    ctx->str_consts[idx].val = strdup(val);
    snprintf(ctx->str_consts[idx].label, 64, "_Lstr%d", idx);
    ctx->str_consts[idx].rodata_off = ctx->rodata.len;
    /* Add symbol */
    Sym *s = sym_get_or_add(&ctx->syms, ctx->str_consts[idx].label);
    s->section  = 2; /* .rodata */
    s->value    = ctx->rodata.len;
    s->sym_type = STT_OBJECT;
    s->is_global = 0;
    /* Write string bytes to rodata */
    buf_write(&ctx->rodata, val, strlen(val) + 1);
    return ctx->str_consts[idx].label;
}

/* Emit LEA rax, [rip + sym] with a relocation */
static void emit_lea_sym(ElfCtx *ctx, int reg, const char *sym_name) {
    size_t patch_off = emit_lea_rip(&ctx->text, reg);
    reloc_add(&ctx->relocs, patch_off, sym_name, R_X86_64_PC32, -4);
}

/* Emit CALL sym with relocation */
static void emit_call_sym(ElfCtx *ctx, const char *sym_name) {
    size_t patch_off = emit_call_rel32(&ctx->text);
    reloc_add(&ctx->relocs, patch_off, sym_name, R_X86_64_PLT32, -4);
}

/* ── Function prologue / epilogue ───────────────────────────────────────── */

/* We emit a prologue with a patched frame size:
   push rbp; mov rbp, rsp; sub rsp, <frame_size>
   Frame size is patched after we know all locals. */

static size_t emit_prologue(ElfCtx *ctx) {
    Buf *b = &ctx->text;
    emit_push(b, REG_RBP);
    /* mov rbp, rsp */
    emit_rex(b, 1, 0, 0, 0);
    buf_push(b, 0x89); emit_modrm(b, 3, REG_RSP, REG_RBP);
    /* sub rsp, imm32 — placeholder */
    emit_rex(b, 1, 0, 0, 0);
    buf_push(b, 0x81); emit_modrm(b, 3, 5, REG_RSP);
    size_t frame_patch = b->len;
    buf_write32(b, 0);  /* patched later */
    return frame_patch;
}

static void patch_frame_size(ElfCtx *ctx, size_t frame_patch) {
    /* Align frame to 16 bytes */
    uint32_t fs = (uint32_t)((ctx->frame_size + 15) & ~15u);
    buf_patch32(&ctx->text, frame_patch, fs);
}

static void emit_epilogue(ElfCtx *ctx) {
    Buf *b = &ctx->text;
    /* mov rsp, rbp */
    emit_rex(b, 1, 0, 0, 0);
    buf_push(b, 0x89); emit_modrm(b, 3, REG_RBP, REG_RSP);
    emit_pop(b, REG_RBP);
    emit_ret(b);
}

/* ── IR instruction handlers ────────────────────────────────────────────── */

static void handle_func_begin(ElfCtx *ctx, IRInstr *ins);
static void handle_func_end(ElfCtx *ctx, IRInstr *ins);

/* Per-function frame patch offset */
static size_t g_frame_patch = 0;

static void handle_func_begin(ElfCtx *ctx, IRInstr *ins) {
    /* Reset per-function state */
    ctx->local_count = 0;
    ctx->frame_size  = 0;
    ctx->label_count = 0;
    ctx->patch_count = 0;
    ctx->is_main     = ins->extra_int;
    strncpy(ctx->cur_fn, ins->str_extra ? ins->str_extra : "_fn", 127);
    ctx->fn_text_start = ctx->text.len;

    /* Register function symbol */
    Sym *s = sym_get_or_add(&ctx->syms, ctx->cur_fn);
    s->section   = 1; /* .text */
    s->value     = ctx->text.len;
    s->sym_type  = STT_FUNC;
    s->is_global = 1;

    /* Allocate parameter locals */
    for (int i = 0; i < ins->param_count; i++) {
        local_add(ctx, ins->params[i].name, 8);
    }

    g_frame_patch = emit_prologue(ctx);

    /* Store incoming args from registers to stack */
    for (int i = 0; i < ins->param_count && i < SYSV_ARG_REG_COUNT; i++) {
        Local *l = local_find(ctx, ins->params[i].name);
        if (l) emit_store_rbp(&ctx->text, l->rbp_off, SYSV_ARG_REGS[i]);
    }
}

static void handle_func_end(ElfCtx *ctx, IRInstr *ins) {
    (void)ins;
    patch_frame_size(ctx, g_frame_patch);
    /* Update function symbol size */
    Sym *s = sym_find(&ctx->syms, ctx->cur_fn);
    if (s) s->st_size_dummy = ctx->text.len - ctx->fn_text_start;
}

/* Helper: map a temp or load into rax, rdx as needed for binary ops */

static void codegen_instr(ElfCtx *ctx, IRInstr *ins) {
    Buf *b = &ctx->text;

    switch (ins->op) {

    case IR_NOP:
        emit_nop(b);
        break;

    case IR_ALLOCA: {
        /* Reserve stack slot for named variable */
        const char *vname = ins->str_extra;
        if (vname && !local_find(ctx, vname))
            local_add(ctx, vname, 8);
        break;
    }

    case IR_CONST_INT:
    case IR_CONST_BOOL: {
        long val = (ins->op == IR_CONST_INT) ? ins->src1.int_val : ins->src1.bool_val;
        emit_mov_ri64(b, REG_RAX, val);
        store_dest_rax(ctx, ins->dest);
        break;
    }

    case IR_CONST_NIL:
        emit_xor32_rr(b, REG_RAX);
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_CONST_STR: {
        const char *val = ins->src1.str_val ? ins->src1.str_val : "";
        const char *lbl = rodata_str_label(ctx, val);
        emit_lea_sym(ctx, REG_RAX, lbl);
        store_dest_rax(ctx, ins->dest);
        break;
    }

    case IR_CONST_FLOAT: {
        /* Store float bits in rodata, load via movq xmm0, [rip+off] */
        double fval = ins->src1.float_val;
        char lbl[64]; snprintf(lbl, 64, "_Lflt%d", ctx->flt_const_count);
        size_t roff = ctx->rodata.len;
        buf_write(&ctx->rodata, &fval, 8);
        Sym *s = sym_get_or_add(&ctx->syms, lbl);
        s->section = 2; s->value = roff; s->sym_type = STT_OBJECT;
        ctx->flt_const_count++;
        /* movq xmm0, [rip+lbl] → store to temp as raw bits via rax */
        /* For simplicity: load as integer bits into rax, store to stack */
        uint64_t bits; memcpy(&bits, &fval, 8);
        emit_mov_ri64(b, REG_RAX, (int64_t)bits);
        store_dest_rax(ctx, ins->dest);
        break;
    }

    case IR_LOAD_VAR: {
        const char *vname = ins->str_extra;
        Local *l = local_find(ctx, vname);
        if (l) {
            emit_load_rbp(b, REG_RAX, l->rbp_off);
        } else {
            /* Global symbol */
            emit_lea_sym(ctx, REG_RAX, vname);
            emit_load_ptr(b, REG_RAX, REG_RAX);
        }
        store_dest_rax(ctx, ins->dest);
        break;
    }

    case IR_STORE_VAR: {
        load_operand_rax(ctx, ins->src1);
        const char *vname = ins->str_extra;
        Local *l = local_find(ctx, vname);
        if (l) {
            emit_store_rbp(b, l->rbp_off, REG_RAX);
        } else {
            /* Store to global */
            emit_mov_rr(b, REG_RDX, REG_RAX);
            emit_lea_sym(ctx, REG_RAX, vname);
            emit_store_ptr(b, REG_RAX, REG_RDX);
        }
        break;
    }

    case IR_ADD:
        load_operand_rax(ctx, ins->src1);
        load_operand_rdx(ctx, ins->src2);
        emit_alu_rr(b, 0x01, REG_RAX, REG_RDX);  /* add rax, rdx */
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_SUB:
        load_operand_rax(ctx, ins->src1);
        load_operand_rdx(ctx, ins->src2);
        emit_alu_rr(b, 0x29, REG_RAX, REG_RDX);  /* sub rax, rdx */
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_MUL:
        load_operand_rax(ctx, ins->src1);
        load_operand_rdx(ctx, ins->src2);
        emit_imul_rr(b, REG_RAX, REG_RDX);
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_DIV:
        load_operand_rax(ctx, ins->src1);
        load_operand_rdx(ctx, ins->src2);
        emit_push(b, REG_RDX); /* save divisor */
        emit_cqo(b);
        emit_pop(b, REG_RCX);
        emit_idiv(b, REG_RCX);
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_MOD:
        load_operand_rax(ctx, ins->src1);
        load_operand_rdx(ctx, ins->src2);
        emit_push(b, REG_RDX);
        emit_cqo(b);
        emit_pop(b, REG_RCX);
        emit_idiv(b, REG_RCX);
        emit_mov_rr(b, REG_RAX, REG_RDX); /* remainder in rdx */
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_NEG:
        load_operand_rax(ctx, ins->src1);
        emit_neg(b, REG_RAX);
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_NOT:
        load_operand_rax(ctx, ins->src1);
        emit_cmp_ri(b, REG_RAX, 0);
        emit_setcc(b, 0x94); /* sete al */
        emit_movzx_rax_al(b);
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_BITNOT:
        load_operand_rax(ctx, ins->src1);
        emit_not_r(b, REG_RAX);
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_BITAND:
        load_operand_rax(ctx, ins->src1);
        load_operand_rdx(ctx, ins->src2);
        emit_alu_rr(b, 0x21, REG_RAX, REG_RDX); /* and rax, rdx */
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_BITOR:
        load_operand_rax(ctx, ins->src1);
        load_operand_rdx(ctx, ins->src2);
        emit_alu_rr(b, 0x09, REG_RAX, REG_RDX); /* or rax, rdx */
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_BITXOR:
        load_operand_rax(ctx, ins->src1);
        load_operand_rdx(ctx, ins->src2);
        emit_alu_rr(b, 0x31, REG_RAX, REG_RDX); /* xor rax, rdx */
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_SHL:
        load_operand_rax(ctx, ins->src1);
        load_operand_rdx(ctx, ins->src2);
        emit_mov_rr(b, REG_RCX, REG_RDX);
        emit_shl_rc(b, REG_RAX);
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_SHR:
        load_operand_rax(ctx, ins->src1);
        load_operand_rdx(ctx, ins->src2);
        emit_mov_rr(b, REG_RCX, REG_RDX);
        emit_sar_rc(b, REG_RAX);
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_EQ: case IR_NEQ: case IR_LT: case IR_LE: case IR_GT: case IR_GE: {
        load_operand_rax(ctx, ins->src1);
        load_operand_rdx(ctx, ins->src2);
        emit_cmp_rr(b, REG_RAX, REG_RDX);
        uint8_t cc;
        switch (ins->op) {
        case IR_EQ:  cc = 0x94; break; /* sete */
        case IR_NEQ: cc = 0x95; break; /* setne */
        case IR_LT:  cc = 0x9C; break; /* setl */
        case IR_LE:  cc = 0x9E; break; /* setle */
        case IR_GT:  cc = 0x9F; break; /* setg */
        case IR_GE:  cc = 0x9D; break; /* setge */
        default:     cc = 0x94; break;
        }
        emit_setcc(b, cc);
        emit_movzx_rax_al(b);
        store_dest_rax(ctx, ins->dest);
        break;
    }

    case IR_LABEL:
        label_define(ctx, ins->dest.label_id);
        break;

    case IR_JUMP: {
        size_t patch = emit_jmp_rel32(b);
        patch_label(ctx, patch, ins->src1.label_id);
        break;
    }

    case IR_JUMP_IF: {
        load_operand_rax(ctx, ins->src1);
        emit_test_rr(b, REG_RAX, REG_RAX);
        size_t patch = emit_jcc_rel32(b, 0x85); /* jnz */
        patch_label(ctx, patch, ins->src2.label_id);
        break;
    }

    case IR_JUMP_UNLESS: {
        load_operand_rax(ctx, ins->src1);
        emit_test_rr(b, REG_RAX, REG_RAX);
        size_t patch = emit_jcc_rel32(b, 0x84); /* jz */
        patch_label(ctx, patch, ins->src2.label_id);
        break;
    }

    case IR_CALL: {
        /* Push args in reverse, then load into regs */
        int argc = ins->arg_count;
        /* For args beyond 6, push right-to-left */
        for (int i = argc - 1; i >= SYSV_ARG_REG_COUNT; i--) {
            load_operand_rax(ctx, ins->args[i]);
            emit_push(b, REG_RAX);
        }
        /* Load register args */
        for (int i = 0; i < argc && i < SYSV_ARG_REG_COUNT; i++) {
            if (ins->args[i].kind == IROP_CONST_STR) {
                const char *lbl = rodata_str_label(ctx, ins->args[i].str_val ? ins->args[i].str_val : "");
                emit_lea_sym(ctx, SYSV_ARG_REGS[i], lbl);
            } else {
                load_operand_rax(ctx, ins->args[i]);
                emit_mov_rr(b, SYSV_ARG_REGS[i], REG_RAX);
            }
        }
        /* Align stack */
        /* sub rsp, 8 if odd number of pushed args */
        int stack_args = argc > SYSV_ARG_REG_COUNT ? argc - SYSV_ARG_REG_COUNT : 0;
        int needs_align = stack_args % 2 != 0;
        if (needs_align) emit_sub_ri(b, REG_RSP, 8);

        const char *callee = ins->str_extra ? ins->str_extra : "_unknown";
        emit_call_sym(ctx, callee);

        /* Clean up stack args */
        if (stack_args > 0 || needs_align) {
            int clean = stack_args * 8 + (needs_align ? 8 : 0);
            emit_add_ri(b, REG_RSP, clean);
        }

        if (ins->dest.kind == IROP_TEMP)
            store_dest_rax(ctx, ins->dest);
        break;
    }

    case IR_RETURN:
        if (ins->src1.kind != IROP_NONE)
            load_operand_rax(ctx, ins->src1);
        else
            emit_xor32_rr(b, REG_RAX);
        emit_epilogue(ctx);
        break;

    case IR_ADDROF: {
        const char *vname = ins->str_extra;
        Local *l = local_find(ctx, vname);
        if (l) {
            emit_rex(b, 1, 0, 0, 0);
            buf_push(b, 0x8D); emit_modrm(b, 2, REG_RAX, REG_RBP);
            buf_write32(b, (uint32_t)l->rbp_off);
        } else {
            emit_lea_sym(ctx, REG_RAX, vname);
        }
        store_dest_rax(ctx, ins->dest);
        break;
    }

    case IR_ADDROF_FN:
        emit_lea_sym(ctx, REG_RAX, ins->str_extra ? ins->str_extra : "_fn");
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_LOAD_PTR:
    case IR_LOAD_VOLATILE:
        load_operand_rax(ctx, ins->src1);
        emit_load_ptr(b, REG_RAX, REG_RAX);
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_STORE_PTR:
    case IR_STORE_VOLATILE:
        load_operand_rax(ctx, ins->src1); /* ptr */
        emit_push(b, REG_RAX);
        load_operand_rax(ctx, ins->src2); /* val */
        emit_pop(b, REG_RDX);            /* ptr back in rdx */
        emit_store_ptr(b, REG_RDX, REG_RAX);
        break;

    case IR_CAST:
        load_operand_rax(ctx, ins->src1);
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_MEMSET: {
        /* memset(ptr, byte, count) → rdi=ptr, al=byte, rcx=count, rep stosb */
        load_operand_rax(ctx, ins->src1); emit_mov_rr(b, REG_RDI, REG_RAX);
        load_operand_rax(ctx, ins->src2); /* byte val already in rax/al */
        load_operand_rdx(ctx, ins->extra_src); emit_mov_rr(b, REG_RCX, REG_RDX);
        /* rep stosq (8-byte fill, count in bytes / 8) */
        /* For simplicity emit call to memset */
        emit_mov_rr(b, REG_RSI, REG_RAX);
        emit_call_sym(ctx, "memset");
        break;
    }

    case IR_MEMCPY: {
        load_operand_rax(ctx, ins->src1); emit_mov_rr(b, REG_RDI, REG_RAX);
        load_operand_rax(ctx, ins->src2); emit_mov_rr(b, REG_RSI, REG_RAX);
        load_operand_rax(ctx, ins->extra_src); emit_mov_rr(b, REG_RDX, REG_RAX);
        emit_call_sym(ctx, "memcpy");
        break;
    }

    case IR_CLI: emit_cli(b); break;
    case IR_STI: emit_sti(b); break;
    case IR_IRET: emit_iretq(b); break;

    case IR_OUTB:
        load_operand_rax(ctx, ins->src1); emit_mov_rr(b, REG_RDX, REG_RAX); /* port */
        load_operand_rax(ctx, ins->src2); /* val in al */
        emit_out_dx_al(b);
        break;

    case IR_INB:
        load_operand_rax(ctx, ins->src1); emit_mov_rr(b, REG_RDX, REG_RAX);
        emit_xor32_rr(b, REG_RAX);
        emit_in_al_dx(b);
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_WRMSR:
        load_operand_rax(ctx, ins->src1); emit_mov_rr(b, REG_RCX, REG_RAX); /* msr */
        load_operand_rax(ctx, ins->src2);
        /* split rax into edx:eax */
        emit_mov_rr(b, REG_RDX, REG_RAX);
        /* shr edx, 32 */
        emit_rex(b, 0, 0, 0, 0); buf_push(b, 0xC1); emit_modrm(b, 3, 5, REG_RDX); buf_push(b, 32);
        emit_wrmsr(b);
        break;

    case IR_RDMSR:
        load_operand_rax(ctx, ins->src1); emit_mov_rr(b, REG_RCX, REG_RAX);
        emit_rdmsr(b);
        /* combine edx:eax → rax */
        emit_rex(b, 1, 0, 0, REG_RDX>>3); buf_push(b, 0xC1); emit_modrm(b, 3, 4, REG_RDX&7); buf_push(b, 32);
        emit_alu_rr(b, 0x09, REG_RAX, REG_RDX);
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_READ_CR:
        emit_mov_from_cr(b, ins->extra_int);
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_WRITE_CR:
        load_operand_rax(ctx, ins->src1);
        emit_mov_to_cr(b, ins->extra_int, REG_RAX);
        break;

    case IR_LGDT: {
        /* Build descriptor on stack: limit(2) + base(8) */
        load_operand_rax(ctx, ins->src2); emit_push(b, REG_RAX); /* base */
        load_operand_rax(ctx, ins->src1);                         /* limit */
        emit_push(b, REG_RAX);
        emit_mov_rr(b, REG_RAX, REG_RSP);
        emit_lgdt_mem(b, REG_RAX);
        emit_add_ri(b, REG_RSP, 16);
        break;
    }

    case IR_LIDT: {
        load_operand_rax(ctx, ins->src2); emit_push(b, REG_RAX);
        load_operand_rax(ctx, ins->src1);
        emit_push(b, REG_RAX);
        emit_mov_rr(b, REG_RAX, REG_RSP);
        emit_lidt_mem(b, REG_RAX);
        emit_add_ri(b, REG_RSP, 16);
        break;
    }

    case IR_LTR:
        load_operand_rax(ctx, ins->src1);
        emit_ltr(b);
        break;

    case IR_INVLPG:
        load_operand_rax(ctx, ins->src1);
        emit_invlpg(b, REG_RAX);
        break;

    case IR_SAVE_REGS: {
        int dummy = ins->extra_int;
        if (dummy) emit_push(b, REG_RAX); /* dummy error code slot */
        static const int gprs[] = { REG_RAX, REG_RCX, REG_RDX, REG_RBX,
                                     REG_RSI, REG_RDI, REG_R8,  REG_R9,
                                     REG_R10, REG_R11, REG_R12, REG_R13,
                                     REG_R14, REG_R15 };
        for (int i = 0; i < 14; i++) emit_push(b, gprs[i]);
        break;
    }

    case IR_RESTORE_REGS: {
        static const int gprs[] = { REG_R15, REG_R14, REG_R13, REG_R12,
                                     REG_R11, REG_R10, REG_R9,  REG_R8,
                                     REG_RDI, REG_RSI, REG_RBX, REG_RDX,
                                     REG_RCX, REG_RAX };
        for (int i = 0; i < 14; i++) emit_pop(b, gprs[i]);
        int skip = ins->extra_int;
        if (skip) emit_add_ri(b, REG_RSP, 8);
        break;
    }

    case IR_STATIC_VAR: {
        /* Global variable — add to .data or .bss */
        const char *vname = ins->str_extra;
        if (ins->src1.kind == IROP_CONST_INT || ins->src1.kind == IROP_CONST_BOOL) {
            Sym *s = sym_get_or_add(&ctx->syms, vname);
            s->section  = 3; /* .data */
            s->value    = ctx->data.len;
            s->sym_type = STT_OBJECT;
            s->is_global = 1;
            int64_t val = ins->src1.kind == IROP_CONST_INT ? ins->src1.int_val : ins->src1.bool_val;
            buf_write64(&ctx->data, (uint64_t)val);
        } else {
            /* BSS */
            if (ctx->bss_sym_count < 512) {
                strncpy(ctx->bss_syms[ctx->bss_sym_count].name, vname, 127);
                ctx->bss_syms[ctx->bss_sym_count].size  = 8;
                ctx->bss_syms[ctx->bss_sym_count].align = 8;
                ctx->bss_sym_count++;
                ctx->bss_size += 8;
            }
        }
        break;
    }

    /* Array/OOP/interp ops: delegate to runtime calls */
    case IR_ARRAY_ALLOC:
        emit_mov_ri32(b, REG_RDI, 0);
        emit_call_sym(ctx, "hylian_array_alloc");
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_ARRAY_LEN:
        load_operand_rax(ctx, ins->src1);
        emit_load_ptr(b, REG_RAX, REG_RAX);
        store_dest_rax(ctx, ins->dest);
        break;

    case IR_ARRAY_LOAD: {
        load_operand_rax(ctx, ins->src1); emit_mov_rr(b, REG_RDI, REG_RAX);
        load_operand_rax(ctx, ins->src2); emit_mov_rr(b, REG_RSI, REG_RAX);
        emit_call_sym(ctx, "hylian_array_get");
        store_dest_rax(ctx, ins->dest);
        break;
    }

    case IR_ARRAY_STORE: {
        load_operand_rax(ctx, ins->src1); emit_mov_rr(b, REG_RDI, REG_RAX);
        load_operand_rax(ctx, ins->src2); emit_mov_rr(b, REG_RSI, REG_RAX);
        load_operand_rax(ctx, ins->extra_src); emit_mov_rr(b, REG_RDX, REG_RAX);
        emit_call_sym(ctx, "hylian_array_set");
        break;
    }

    case IR_ARRAY_PUSH: {
        load_operand_rax(ctx, ins->src1); emit_mov_rr(b, REG_RDI, REG_RAX);
        load_operand_rax(ctx, ins->src2); emit_mov_rr(b, REG_RSI, REG_RAX);
        emit_call_sym(ctx, "hylian_array_push");
        break;
    }

    case IR_ARRAY_POP: {
        load_operand_rax(ctx, ins->src1); emit_mov_rr(b, REG_RDI, REG_RAX);
        emit_call_sym(ctx, "hylian_array_pop");
        store_dest_rax(ctx, ins->dest);
        break;
    }

    case IR_ARRAY_CAP: {
        load_operand_rax(ctx, ins->src1);
        /* cap is second field after len */
        emit_rex(b, 1, REG_RAX>>3, 0, REG_RAX>>3);
        buf_push(b, 0x8B); emit_modrm(b, 1, REG_RAX&7, REG_RAX&7); buf_push(b, 8);
        store_dest_rax(ctx, ins->dest);
        break;
    }

    case IR_ARRAY_INIT: {
        emit_mov_ri32(b, REG_RDI, ins->arg_count);
        emit_call_sym(ctx, "hylian_array_alloc");
        store_dest_rax(ctx, ins->dest);
        /* Push each element */
        for (int i = 0; i < ins->arg_count; i++) {
            load_operand_rax(ctx, ins->args[i]);
            emit_push(b, REG_RAX);
            /* load arr ptr */
            load_operand_rax(ctx, ins->dest);
            emit_mov_rr(b, REG_RDI, REG_RAX);
            emit_pop(b, REG_RSI);
            emit_call_sym(ctx, "hylian_array_push");
        }
        break;
    }

    case IR_NEW: {
        const char *cls = ins->str_extra ? ins->str_extra : "_obj";
        char ctor[256]; snprintf(ctor, 256, "%s_new", cls);
        for (int i = 0; i < ins->arg_count && i < SYSV_ARG_REG_COUNT; i++) {
            load_operand_rax(ctx, ins->args[i]);
            emit_mov_rr(b, SYSV_ARG_REGS[i], REG_RAX);
        }
        emit_call_sym(ctx, ctor);
        store_dest_rax(ctx, ins->dest);
        break;
    }

    case IR_ARENA_ALLOC: {
        emit_mov_ri32(b, REG_RDI, ins->extra_int);
        emit_call_sym(ctx, "arena_alloc");
        store_dest_rax(ctx, ins->dest);
        break;
    }

    case IR_GET_FIELD: {
        load_operand_rax(ctx, ins->src1);
        emit_mov_rr(b, REG_RDI, REG_RAX);
        const char *cls   = ins->str_extra  ? ins->str_extra  : "";
        const char *field = ins->str_extra2 ? ins->str_extra2 : "";
        char getter[256]; snprintf(getter, 256, "%s_get_%s", cls, field);
        emit_call_sym(ctx, getter);
        store_dest_rax(ctx, ins->dest);
        break;
    }

    case IR_SET_FIELD: {
        load_operand_rax(ctx, ins->src1); emit_mov_rr(b, REG_RDI, REG_RAX);
        load_operand_rax(ctx, ins->src2); emit_mov_rr(b, REG_RSI, REG_RAX);
        const char *cls   = ins->str_extra  ? ins->str_extra  : "";
        const char *field = ins->str_extra2 ? ins->str_extra2 : "";
        char setter[256]; snprintf(setter, 256, "%s_set_%s", cls, field);
        emit_call_sym(ctx, setter);
        break;
    }

    case IR_MULTI_ALLOC: {
        load_operand_rax(ctx, ins->src1); emit_mov_rr(b, REG_RDI, REG_RAX);
        load_operand_rax(ctx, ins->src2); emit_mov_rr(b, REG_RSI, REG_RAX);
        emit_call_sym(ctx, "hylian_multi_alloc");
        store_dest_rax(ctx, ins->dest);
        break;
    }

    case IR_ENUM_VAL: {
        const char *en  = ins->str_extra  ? ins->str_extra  : "";
        const char *var = ins->str_extra2 ? ins->str_extra2 : "";
        char sym_name[256]; snprintf(sym_name, 256, "%s_%s", en, var);
        emit_lea_sym(ctx, REG_RAX, sym_name);
        store_dest_rax(ctx, ins->dest);
        break;
    }

    case IR_INTERP_STR: {
        /* Build via runtime call: hylian_interp_build(seg_count, ...) */
        emit_mov_ri32(b, REG_RDI, ins->extra_seg_count);
        emit_call_sym(ctx, "hylian_interp_build");
        store_dest_rax(ctx, ins->dest);
        break;
    }

    case IR_ASM_BLOCK:
        /* Inline asm: not encodable directly — emit a call to a stub
           The build system should separately assemble .hy.asm stubs.
           For now, emit a nop and warn. */
        fprintf(stderr, "warning: inline asm in ELF mode not yet supported, skipping block\n");
        emit_nop(b);
        break;

    case IR_PRINT:
    case IR_PRINTLN: {
        int is_nl = (ins->op == IR_PRINTLN);
        int arg_type = ins->extra_int;
        if (arg_type == 1 /* STR_LIT */ || arg_type == 4 /* STR_PTR */) {
            if (ins->src1.kind == IROP_CONST_STR) {
                const char *lbl = rodata_str_label(ctx, ins->src1.str_val ? ins->src1.str_val : "");
                emit_lea_sym(ctx, REG_RDI, lbl);
            } else {
                load_operand_rax(ctx, ins->src1);
                emit_mov_rr(b, REG_RDI, REG_RAX);
            }
            emit_call_sym(ctx, is_nl ? "hylian_println_str" : "hylian_print_str");
        } else if (arg_type == 2 /* INT */) {
            load_operand_rax(ctx, ins->src1);
            emit_mov_rr(b, REG_RDI, REG_RAX);
            emit_call_sym(ctx, is_nl ? "hylian_println_int" : "hylian_print_int");
        } else {
            load_operand_rax(ctx, ins->src1);
            emit_mov_rr(b, REG_RDI, REG_RAX);
            emit_call_sym(ctx, is_nl ? "hylian_println" : "hylian_print");
        }
        break;
    }

    case IR_ERR: {
        load_operand_rax(ctx, ins->src1);
        emit_mov_rr(b, REG_RDI, REG_RAX);
        emit_call_sym(ctx, "hylian_make_err");
        store_dest_rax(ctx, ins->dest);
        break;
    }

    case IR_PANIC: {
        if (ins->src1.kind == IROP_CONST_STR) {
            const char *lbl = rodata_str_label(ctx, ins->src1.str_val ? ins->src1.str_val : "");
            emit_lea_sym(ctx, REG_RDI, lbl);
        } else {
            load_operand_rax(ctx, ins->src1);
            emit_mov_rr(b, REG_RDI, REG_RAX);
        }
        emit_call_sym(ctx, "hylian_panic");
        emit_hlt(b); /* unreachable, satisfies verifiers */
        break;
    }

    case IR_FUNC_BEGIN:
        handle_func_begin(ctx, ins);
        break;

    case IR_FUNC_END:
        handle_func_end(ctx, ins);
        break;

    default:
        /* Unknown opcode — emit nop */
        emit_nop(b);
        break;
    }
}

/* ── ELF64 object file writer ────────────────────────────────────────────── */

/* Section indices */
#define SEC_NULL    0
#define SEC_TEXT    1
#define SEC_RODATA  2
#define SEC_DATA    3
#define SEC_BSS     4
#define SEC_RELA    5
#define SEC_SYMTAB  6
#define SEC_STRTAB  7
#define SEC_SHSTRTAB 8
#define SEC_COUNT   9

static int write_elf(ElfCtx *ctx, const char *outfile) {
    FILE *f = fopen(outfile, "wb");
    if (!f) { perror(outfile); return -1; }

    /* ── String tables ── */
    Buf shstrtab; buf_init(&shstrtab);
    buf_push(&shstrtab, 0); /* null */
    uint32_t sh_null_name    = strtab_add(&shstrtab, "");
    uint32_t sh_text_name    = strtab_add(&shstrtab, ".text");
    uint32_t sh_rodata_name  = strtab_add(&shstrtab, ".rodata");
    uint32_t sh_data_name    = strtab_add(&shstrtab, ".data");
    uint32_t sh_bss_name     = strtab_add(&shstrtab, ".bss");
    uint32_t sh_rela_name    = strtab_add(&shstrtab, ".rela.text");
    uint32_t sh_symtab_name  = strtab_add(&shstrtab, ".symtab");
    uint32_t sh_strtab_name  = strtab_add(&shstrtab, ".strtab");
    uint32_t sh_shstrtab_name= strtab_add(&shstrtab, ".shstrrtab");
    (void)sh_null_name;

    Buf strtab; buf_init(&strtab);
    buf_push(&strtab, 0); /* null */

    /* ── Build symbol table ── */
    /* Locals first, then globals (ELF requirement) */
    Buf symtab_buf; buf_init(&symtab_buf);

    /* STN_UNDEF */
    Elf64_Sym undef_sym = {0};
    buf_write(&symtab_buf, &undef_sym, sizeof(undef_sym));

    /* File symbol */
    Elf64_Sym file_sym = {
        .st_name  = strtab_add(&strtab, "hylian_output.o"),
        .st_info  = ELF64_ST_INFO(STB_LOCAL, STT_FILE),
        .st_other = STV_DEFAULT,
        .st_shndx = SHN_ABS,
        .st_value = 0, .st_size = 0
    };
    buf_write(&symtab_buf, &file_sym, sizeof(file_sym));

    /* Section symbols */
    uint16_t text_shndx    = SEC_TEXT;
    uint16_t rodata_shndx  = SEC_RODATA;
    uint16_t data_shndx    = SEC_DATA;
    uint16_t bss_shndx     = SEC_BSS;

    Elf64_Sym sec_syms[4] = {
        { .st_name=0, .st_info=ELF64_ST_INFO(STB_LOCAL,STT_SECTION), .st_shndx=text_shndx   },
        { .st_name=0, .st_info=ELF64_ST_INFO(STB_LOCAL,STT_SECTION), .st_shndx=rodata_shndx },
        { .st_name=0, .st_info=ELF64_ST_INFO(STB_LOCAL,STT_SECTION), .st_shndx=data_shndx   },
        { .st_name=0, .st_info=ELF64_ST_INFO(STB_LOCAL,STT_SECTION), .st_shndx=bss_shndx    },
    };
    int sec_sym_base = 2; /* index of first section symbol */
    for (int i = 0; i < 4; i++)
        buf_write(&symtab_buf, &sec_syms[i], sizeof(sec_syms[i]));

    int first_global_idx = 6; /* undef + file + 4 section syms */

    /* Local user symbols */
    int local_sym_count = first_global_idx;
    for (int i = 0; i < ctx->syms.count; i++) {
        Sym *s = &ctx->syms.syms[i];
        if (s->is_global) continue;
        uint16_t shndx = s->section == 1 ? text_shndx :
                         s->section == 2 ? rodata_shndx :
                         s->section == 3 ? data_shndx : bss_shndx;
        Elf64_Sym es = {
            .st_name  = strtab_add(&strtab, s->name),
            .st_info  = ELF64_ST_INFO(STB_LOCAL, s->sym_type),
            .st_other = STV_DEFAULT,
            .st_shndx = s->section == 0 ? SHN_UNDEF : shndx,
            .st_value = s->value,
            .st_size  = 0,
        };
        s->elf_index = local_sym_count++;
        buf_write(&symtab_buf, &es, sizeof(es));
    }

    /* Global user symbols */
    for (int i = 0; i < ctx->syms.count; i++) {
        Sym *s = &ctx->syms.syms[i];
        if (!s->is_global) continue;
        uint16_t shndx = s->section == 0 ? SHN_UNDEF :
                         s->section == 1 ? text_shndx :
                         s->section == 2 ? rodata_shndx :
                         s->section == 3 ? data_shndx : bss_shndx;
        Elf64_Sym es = {
            .st_name  = strtab_add(&strtab, s->name),
            .st_info  = ELF64_ST_INFO(s->section == 0 ? STB_GLOBAL : STB_GLOBAL, s->sym_type),
            .st_other = STV_DEFAULT,
            .st_shndx = shndx,
            .st_value = s->value,
            .st_size  = 0,
        };
        s->elf_index = local_sym_count++;
        buf_write(&symtab_buf, &es, sizeof(es));
    }

    /* Undefined external syms from relocations */
    for (int i = 0; i < ctx->relocs.count; i++) {
        const char *sname = ctx->relocs.relocs[i].sym;
        Sym *s = sym_find(&ctx->syms, sname);
        if (!s) {
            s = sym_add(&ctx->syms, sname);
            s->section   = 0; /* undef */
            s->is_global = 1;
            s->sym_type  = STT_NOTYPE;
            Elf64_Sym es = {
                .st_name  = strtab_add(&strtab, sname),
                .st_info  = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE),
                .st_other = STV_DEFAULT,
                .st_shndx = SHN_UNDEF,
                .st_value = 0, .st_size = 0,
            };
            s->elf_index = local_sym_count++;
            buf_write(&symtab_buf, &es, sizeof(es));
        }
    }

    int symtab_info = local_sym_count; /* will be updated after locals are counted */

    /* Count total locals first to set sh_info correctly */
    int total_local_count = first_global_idx;
    for (int i = 0; i < ctx->syms.count; i++)
        if (!ctx->syms.syms[i].is_global) total_local_count++;

    /* ── Build .rela.text ── */
    Buf rela_buf; buf_init(&rela_buf);
    for (int i = 0; i < ctx->relocs.count; i++) {
        Reloc *r = &ctx->relocs.relocs[i];
        Sym *s = sym_find(&ctx->syms, r->sym);
        int sym_idx = s ? s->elf_index : 0;
        Elf64_Rela rela = {
            .r_offset = r->offset,
            .r_info   = ELF64_R_INFO(sym_idx, r->type),
            .r_addend = r->addend,
        };
        buf_write(&rela_buf, &rela, sizeof(rela));
    }

    /* ── BSS symbols ── */
    size_t bss_offset = 0;
    for (int i = 0; i < ctx->bss_sym_count; i++) {
        Sym *s = sym_get_or_add(&ctx->syms, ctx->bss_syms[i].name);
        s->section   = 4; /* bss */
        s->value     = bss_offset;
        s->sym_type  = STT_OBJECT;
        s->is_global = 1;
        bss_offset  += ctx->bss_syms[i].size;
        Elf64_Sym es = {
            .st_name  = strtab_add(&strtab, ctx->bss_syms[i].name),
            .st_info  = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT),
            .st_other = STV_DEFAULT,
            .st_shndx = bss_shndx,
            .st_value = s->value,
            .st_size  = ctx->bss_syms[i].size,
        };
        s->elf_index = local_sym_count++;
        buf_write(&symtab_buf, &es, sizeof(es));
    }

    /* ── Compute section offsets ── */
    size_t ehdr_size   = sizeof(Elf64_Ehdr);
    size_t shdr_size   = sizeof(Elf64_Shdr) * SEC_COUNT;

    /* Layout: ELF header | section headers | .text | .rodata | .data | .rela.text | .symtab | .strtab | .shstrtab */
    uint64_t off = ehdr_size + shdr_size;

    /* Align each section to 16 */
#define ALIGN16(x) (((x)+15)&~(size_t)15)

    uint64_t text_off    = off; off += ALIGN16(ctx->text.len);
    uint64_t rodata_off  = off; off += ALIGN16(ctx->rodata.len);
    uint64_t data_off    = off; off += ALIGN16(ctx->data.len);
    uint64_t rela_off    = off; off += ALIGN16(rela_buf.len);
    uint64_t symtab_off  = off; off += ALIGN16(symtab_buf.len);
    uint64_t strtab_off2 = off; off += ALIGN16(strtab.len);
    uint64_t shstrtab_off= off; off += ALIGN16(shstrtab.len);

    uint64_t shoff = ehdr_size; /* section headers right after elf header */

    /* ── Write ELF header ── */
    Elf64_Ehdr ehdr = {0};
    ehdr.e_ident[0]  = ELFMAG0; ehdr.e_ident[1] = ELFMAG1;
    ehdr.e_ident[2]  = ELFMAG2; ehdr.e_ident[3] = ELFMAG3;
    ehdr.e_ident[4]  = ELFCLASS64;
    ehdr.e_ident[5]  = ELFDATA2LSB;
    ehdr.e_ident[6]  = EV_CURRENT;
    ehdr.e_type      = ET_REL;
    ehdr.e_machine   = EM_X86_64;
    ehdr.e_version   = EV_CURRENT;
    ehdr.e_shoff     = shoff;
    ehdr.e_ehsize    = sizeof(Elf64_Ehdr);
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum     = SEC_COUNT;
    ehdr.e_shstrndx  = SEC_SHSTRTAB;
    fwrite(&ehdr, 1, sizeof(ehdr), f);

    /* ── Write section headers ── */
    Elf64_Shdr shdrs[SEC_COUNT] = {0};

    /* NULL */
    /* .text */
    shdrs[SEC_TEXT].sh_name      = sh_text_name;
    shdrs[SEC_TEXT].sh_type      = SHT_PROGBITS;
    shdrs[SEC_TEXT].sh_flags     = SHF_ALLOC | SHF_EXECINSTR;
    shdrs[SEC_TEXT].sh_offset    = text_off;
    shdrs[SEC_TEXT].sh_size      = ctx->text.len;
    shdrs[SEC_TEXT].sh_addralign = 16;
    /* .rodata */
    shdrs[SEC_RODATA].sh_name      = sh_rodata_name;
    shdrs[SEC_RODATA].sh_type      = SHT_PROGBITS;
    shdrs[SEC_RODATA].sh_flags     = SHF_ALLOC;
    shdrs[SEC_RODATA].sh_offset    = rodata_off;
    shdrs[SEC_RODATA].sh_size      = ctx->rodata.len;
    shdrs[SEC_RODATA].sh_addralign = 8;
    /* .data */
    shdrs[SEC_DATA].sh_name      = sh_data_name;
    shdrs[SEC_DATA].sh_type      = SHT_PROGBITS;
    shdrs[SEC_DATA].sh_flags     = SHF_ALLOC | SHF_WRITE;
    shdrs[SEC_DATA].sh_offset    = data_off;
    shdrs[SEC_DATA].sh_size      = ctx->data.len;
    shdrs[SEC_DATA].sh_addralign = 8;
    /* .bss */
    shdrs[SEC_BSS].sh_name      = sh_bss_name;
    shdrs[SEC_BSS].sh_type      = 8; /* SHT_NOBITS */
    shdrs[SEC_BSS].sh_flags     = SHF_ALLOC | SHF_WRITE;
    shdrs[SEC_BSS].sh_offset    = data_off + ctx->data.len;
    shdrs[SEC_BSS].sh_size      = ctx->bss_size;
    shdrs[SEC_BSS].sh_addralign = 8;
    /* .rela.text */
    shdrs[SEC_RELA].sh_name      = sh_rela_name;
    shdrs[SEC_RELA].sh_type      = SHT_RELA;
    shdrs[SEC_RELA].sh_flags     = 0;
    shdrs[SEC_RELA].sh_offset    = rela_off;
    shdrs[SEC_RELA].sh_size      = rela_buf.len;
    shdrs[SEC_RELA].sh_link      = SEC_SYMTAB;
    shdrs[SEC_RELA].sh_info      = SEC_TEXT;
    shdrs[SEC_RELA].sh_addralign = 8;
    shdrs[SEC_RELA].sh_entsize   = sizeof(Elf64_Rela);
    /* .symtab */
    shdrs[SEC_SYMTAB].sh_name      = sh_symtab_name;
    shdrs[SEC_SYMTAB].sh_type      = SHT_SYMTAB;
    shdrs[SEC_SYMTAB].sh_offset    = symtab_off;
    shdrs[SEC_SYMTAB].sh_size      = symtab_buf.len;
    shdrs[SEC_SYMTAB].sh_link      = SEC_STRTAB;
    shdrs[SEC_SYMTAB].sh_info      = total_local_count;
    shdrs[SEC_SYMTAB].sh_addralign = 8;
    shdrs[SEC_SYMTAB].sh_entsize   = sizeof(Elf64_Sym);
    /* .strtab */
    shdrs[SEC_STRTAB].sh_name      = sh_strtab_name;
    shdrs[SEC_STRTAB].sh_type      = SHT_STRTAB;
    shdrs[SEC_STRTAB].sh_offset    = strtab_off2;
    shdrs[SEC_STRTAB].sh_size      = strtab.len;
    shdrs[SEC_STRTAB].sh_addralign = 1;
    /* .shstrtab */
    shdrs[SEC_SHSTRTAB].sh_name      = sh_shstrtab_name;
    shdrs[SEC_SHSTRTAB].sh_type      = SHT_STRTAB;
    shdrs[SEC_SHSTRTAB].sh_offset    = shstrtab_off;
    shdrs[SEC_SHSTRTAB].sh_size      = shstrtab.len;
    shdrs[SEC_SHSTRTAB].sh_addralign = 1;

    fwrite(shdrs, 1, sizeof(shdrs), f);

    /* ── Write section data ── */
    static const uint8_t PAD[16] = {0};
#define WRITE_SEC(buf) do { \
    if ((buf).len) fwrite((buf).data, 1, (buf).len, f); \
    size_t _p = ALIGN16((buf).len) - (buf).len; \
    if (_p) fwrite(PAD, 1, _p, f); \
} while(0)

    WRITE_SEC(ctx->text);
    WRITE_SEC(ctx->rodata);
    WRITE_SEC(ctx->data);
    WRITE_SEC(rela_buf);
    WRITE_SEC(symtab_buf);
    WRITE_SEC(strtab);
    WRITE_SEC(shstrtab);

    fclose(f);

    buf_free(&shstrtab);
    buf_free(&strtab);
    buf_free(&symtab_buf);
    buf_free(&rela_buf);
    return 0;
}

/* ── Public entry point ──────────────────────────────────────────────────── */

int codegen_elf(IRModule *mod, const char *outfile,
                const char *src_filename,
                const char *target,
                int freestanding)
{
    (void)src_filename;

    ElfCtx ctx;
    ctx_init(&ctx, target, freestanding);

    /* Walk IR */
    for (int i = 0; i < mod->instr_count; i++)
        codegen_instr(&ctx, &mod->instrs[i]);

    /* Any function that fell off without IR_FUNC_END: patch frame */
    if (ctx.cur_fn[0])
        patch_frame_size(&ctx, g_frame_patch);

    int ret = write_elf(&ctx, outfile);
    ctx_free(&ctx);
    return ret;
}
