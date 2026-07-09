#ifndef HYMIR_H
#define HYMIR_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* ---- Types ---- */
typedef enum {
    MIR_I8, MIR_I16, MIR_I32, MIR_I64,
    MIR_U8, MIR_U16, MIR_U32, MIR_U64,
    MIR_F32, MIR_F64,
    MIR_PTR,
} MIRType;

static inline int mir_type_size(MIRType t) {
    switch (t) {
        case MIR_I8: case MIR_U8:  return 1;
        case MIR_I16: case MIR_U16: return 2;
        case MIR_I32: case MIR_U32: return 4;
        case MIR_F32: return 4;
        case MIR_I64: case MIR_U64: case MIR_F64: case MIR_PTR: return 8;
    }
    return 8;
}

static inline int mir_type_is_float(MIRType t) { return t == MIR_F32 || t == MIR_F64; }
static inline int mir_type_is_unsigned(MIRType t) {
    return t == MIR_U8 || t == MIR_U16 || t == MIR_U32 || t == MIR_U64;
}

/* ---- Values (pre-regalloc: everything is a virtual reg or constant) ---- */
typedef enum {
    MIRV_VREG,     /* virtual register, assigned by regalloc later */
    MIRV_IMM_INT,
    MIRV_IMM_FLOAT,
    MIRV_LABEL,    /* block/label id */
    MIRV_GLOBAL,   /* symbol name (string interned elsewhere) */
    MIRV_LOCAL,    /* per-call-frame local slot id - see mir_local() */
} MIRValueKind;

typedef struct {
    MIRValueKind kind;
    MIRType      type;
    union {
        int      vreg;
        int64_t  imm_int;
        double   imm_float;
        int      label_id;
        const char *global_name;
        int      local_id;
    };
} MIRValue;

/* ---- Opcodes ---- */
typedef enum {
    MIR_MOV,
    MIR_ADD, MIR_SUB, MIR_MUL,
    MIR_SDIV, MIR_UDIV, MIR_SMOD, MIR_UMOD,
    MIR_NEG, MIR_NOT,
    MIR_AND, MIR_OR, MIR_XOR,
    MIR_SHL, MIR_SHR_A, MIR_SHR_L,   /* arithmetic vs logical right shift */

    MIR_FADD, MIR_FSUB, MIR_FMUL, MIR_FDIV, MIR_FNEG,

    MIR_CMP_EQ, MIR_CMP_NE,
    MIR_CMP_LT, MIR_CMP_LE, MIR_CMP_GT, MIR_CMP_GE,       /* signed/int */
    MIR_UCMP_LT, MIR_UCMP_LE, MIR_UCMP_GT, MIR_UCMP_GE,   /* unsigned */
    MIR_FCMP_LT, MIR_FCMP_LE, MIR_FCMP_GT, MIR_FCMP_GE, MIR_FCMP_EQ, MIR_FCMP_NE,

    /* explicit conversions - this is what replaces the old IR_CAST no-op */
    MIR_SEXT,   /* sign-extend src (smaller int) to dest type */
    MIR_ZEXT,   /* zero-extend src (smaller int) to dest type */
    MIR_TRUNC,  /* truncate src to dest (smaller) type */
    MIR_I2F,    /* int -> float */
    MIR_F2I,    /* float -> int (truncating) */
    MIR_F2F,    /* f32 <-> f64 */
    MIR_BITCAST,/* reinterpret bits, same width, e.g. ptr<->u64 */

    MIR_LOAD,   /* dest = *src, width = dest.type. extra_int=1 -> volatile */
    MIR_STORE,  /* *dst_addr = src. extra_int=1 -> volatile */
    MIR_LEA_LOCAL,  /* dest = addr of stack slot N */
    MIR_LEA_GLOBAL, /* dest = addr of global (src1 carries the name) */
    MIR_ALLOCA_LOCAL, /* dest = ptr to a fresh N-byte stack slot; extra_int = size */

    MIR_LABEL,
    MIR_JMP,
    MIR_JMP_IF,     /* src1 != 0 -> label */
    MIR_JMP_UNLESS,

    MIR_CALL,       /* args attached separately; dest = return value or NONE */
    MIR_RET,

    /* raw / privileged escape hatch - kept 1:1 with old IR intent */
    MIR_ASM_RAW,    /* opaque byte-emit callback registered by frontend, see hymir_raw_fn */
    MIR_CLI, MIR_STI, MIR_IRET,
    MIR_LGDT, MIR_LIDT, MIR_LTR, MIR_INVLPG,
    MIR_WRMSR, MIR_RDMSR,
    MIR_READ_CR, MIR_WRITE_CR,
    MIR_OUTB, MIR_INB,
    MIR_SAVE_REGS, MIR_RESTORE_REGS,

    MIR_MEMSET, MIR_MEMCPY,  /* self-contained, no libc call */
    MIR_SYSCALL,             /* dest = syscall(nr=args[0], args[1..arg_count-1]) - raw
                                 kernel call, Linux syscall ABI (arg4 in r10, not rcx).
                                 nr is a runtime value (loaded into rax), not extra_int,
                                 since Hylian's stdlib already treats syscall numbers as
                                 ordinary function parameters (_sc3/_sc5 wrappers). */

    MIR_ASM_TEXT,            /* inline asm block. asm_text holds real x86 text with
                                 {0},{1},... placeholders instead of the user's {varname} -
                                 the frontend (ir_to_mir.c) does that rewrite so MIR itself
                                 stays frontend-blind (it never sees Hylian variable names).
                                 args[N] is the operand {N} refers to, and is a normal MIR
                                 use - regalloc sees it exactly like a call arg, so it can't
                                 reallocate that vreg's register out from under the asm
                                 block. libhyx64's mini-assembler resolves {N} to a real
                                 register name (or loads a spill into scratch) and encodes
                                 the result for real, post-regalloc, in lower_x64.c. */

    MIR_NOP,
} MIROp;

typedef struct {
    MIROp     op;
    MIRValue  dest;
    MIRValue  src1;
    MIRValue  src2;

    /* MIR_CALL */
    MIRValue *args;
    int       arg_count;
    const char *callee; /* NULL if indirect (src1 holds fn ptr) */

    /* MIR_READ_CR / MIR_WRITE_CR / MIR_LOAD/STORE width overrides etc. */
    int       extra_int;

    /* MIR_ASM_RAW: raw bytes to splice in directly, already-encoded machine code.
       No text, no reassembly - frontend is responsible for producing correct
       bytes (e.g. for fixed short privileged sequences it hand-assembles once). */
    const uint8_t *raw_bytes;
    int            raw_len;

    /* MIR_ASM_TEXT: x86 asm source text with {0},{1},... placeholders -
       see the MIR_ASM_TEXT opcode comment above. */
    const char    *asm_text;
} MIRInstr;

typedef struct {
    char     *name;
    MIRInstr *instrs;
    int       count;
    int       cap;
    int       vreg_count;   /* next fresh vreg id to allocate */
    int       local_count;  /* next fresh local slot id to allocate - see mir_new_local() */
    int       param_count;  /* how many of the first local_count slots are incoming params,
                                in declaration order - backend's prologue uses this to know
                                how many SysV arg registers to spill into local slots */
    int       is_naked;     /* skip prologue/epilogue, e.g. syscall wrappers/ISR */
} MIRFunc;

typedef struct {
    char *label;
    char *data;
    int   len;
} MIRStringConst;

typedef struct {
    char   *name;
    int     has_init;
    int64_t init_val;
    int     size;
} MIRGlobalVar;

typedef struct {
    MIRFunc **funcs;
    int       func_count;
    int       func_cap;

    MIRStringConst *strs;
    int             str_count;
    int             str_cap;

    MIRGlobalVar *globals;
    int           global_count;
    int           global_cap;
} MIRModule;

/* ---- construction API ---- */
MIRModule *mir_module_new(void);
void       mir_module_free(MIRModule *mod);

MIRFunc   *mir_func_new(MIRModule *mod, const char *name);
MIRInstr  *mir_emit(MIRFunc *fn, MIROp op);
int        mir_new_vreg(MIRFunc *fn);
int        mir_new_local(MIRFunc *fn); /* allocates a fresh per-call-frame local slot id */

MIRValue mir_vreg(int id, MIRType type);
MIRValue mir_imm_int(int64_t v, MIRType type);
MIRValue mir_imm_float(double v, MIRType type);
MIRValue mir_label(int id);
MIRValue mir_global(const char *name, MIRType type);
MIRValue mir_local(int id, MIRType type);
MIRValue mir_none(void);

/* interns a string constant into the module, returns a stable label name
   (owned by the module) usable with mir_global()/MIR_LEA_GLOBAL */
const char *mir_module_intern_string(MIRModule *mod, const char *data, int len);

/* declares a global variable (data or bss depending on has_init) */
void mir_module_add_global(MIRModule *mod, const char *name, int has_init, int64_t init_val, int size);

const char *mir_op_name(MIROp op);
void mir_dump(const MIRModule *mod, FILE *out);

/* shared by every frontend adapter - builds a MIR_CALL instr */
MIRInstr *mir_build_call(MIRFunc *fn, const char *callee, MIRValue *args, int arg_count, MIRValue dest);

/* ---- verification ---- */
typedef struct {
    int ok;
    char msg[256]; /* first error found, empty if ok */
} MIRVerifyResult;

/* Catches: vreg used before defined, missing terminator (ret/jmp/jmp_if at
   block end), float op applied to non-float vreg or vice versa. Cheap
   sanity pass for a new frontend adapter - run before handing MIR to
   regalloc so bad lowering fails loud and early. */
MIRVerifyResult mir_verify(const MIRModule *mod);

#endif /* HYMIR_H */
