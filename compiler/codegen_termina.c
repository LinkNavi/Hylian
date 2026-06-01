#include "codegen_termina.h"
#include "ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* ── Termina encoding ────────────────────────────────────────────────────────
   [31:24] opcode  [23:20] rd  [19:16] rs1  [15:12] rs2  [11:0] imm12       */

typedef enum {
    OP_ADD=0, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_ADDI,
    OP_AND, OP_OR, OP_XOR, OP_NOT, OP_SHL, OP_SHR,
    OP_LOAD, OP_STORE, OP_MOV, OP_MOVI, OP_MOVHI,
    OP_JMP, OP_JMPI, OP_JZ, OP_JNZ, OP_JL, OP_JG,
    OP_PUSH, OP_POP, OP_CALL, OP_CALLI, OP_RET,
    OP_INT, OP_HLT, OP_NOP
} TerminaOp;

/* Special registers */
#define REG_SP  13
#define REG_LR  14
#define REG_PC  15

/* Calling convention:
   r0-r5  = args / return value (r0)
   r6-r11 = callee-saved locals
   r12    = scratch
   r13    = stack pointer
   r14    = link register
   r15    = program counter                                                   */

#define MAX_TEMPS        64
#define MAX_LABELS       256
#define MAX_PATCHES      256
#define MAX_STRINGS      256
#define MAX_STATIC_VARS  512
#define MAX_FN_ADDRS     128
#define CODE_BASE        0x10000
#define DATA_BASE        0x1000
#define HEAP_BASE        0x2000
#define HEAP_SIZE        0xDFFF8  /* ~896 KB */

/* Termina syscall numbers (INT instruction immediate values) */
#define SYS_WRITE        0   /* write(fd, buf, len) */
#define SYS_DRAW         1   /* draw() - swap buffers */
#define SYS_OUTB         2   /* outb(port, val) */
#define SYS_INB          3   /* inb(port) -> r0 */
#define SYS_PSET         4   /* pset(x, y, color) */
#define SYS_LINE         5   /* line(x0, y0, x1, y1, color) */
#define SYS_RECTFILL     6   /* rectfill(x, y, w, h, color) */
#define SYS_CLS          7   /* cls(color) */
#define SYS_PRINT_STR    8   /* print_str(x, y, str, color) */
#define SYS_RECT         9   /* rect(x, y, w, h, color) */
#define SYS_CIRCLE       10  /* circle(cx, cy, r, color) */
#define SYS_CIRCLEFILL   11  /* circlefill(cx, cy, r, color) */

static uint32_t enc(uint8_t op, uint8_t rd, uint8_t rs1, uint8_t rs2, uint32_t imm) {
    return ((uint32_t)op  << 24)
         | ((uint32_t)(rd  & 0xF) << 20)
         | ((uint32_t)(rs1 & 0xF) << 16)
         | ((uint32_t)(rs2 & 0xF) << 12)
         |  (imm & 0xFFF);
}

/* ── Output buffer ───────────────────────────────────────────────────────── */

typedef struct {
    uint32_t *code;
    int       code_len;
    int       code_cap;

    uint8_t  *data;
    int       data_len;
    int       data_cap;
} Buf;

static void buf_init(Buf *b) {
    b->code_cap = 1024;
    b->code     = malloc(b->code_cap * sizeof(uint32_t));
    b->code_len = 0;
    b->data_cap = 4096;
    b->data     = malloc(b->data_cap);
    b->data_len = 0;
}

static void emit(Buf *b, uint32_t instr) {
    if (b->code_len >= b->code_cap) {
        b->code_cap *= 2;
        b->code = realloc(b->code, b->code_cap * sizeof(uint32_t));
    }
    b->code[b->code_len++] = instr;
}

static int cur_pc(Buf *b) { return b->code_len; }   /* index, not byte addr */

static void data_push_bytes(Buf *b, const uint8_t *bytes, int n) {
    while (b->data_len + n > b->data_cap) {
        b->data_cap *= 2;
        b->data = realloc(b->data, b->data_cap);
    }
    memcpy(b->data + b->data_len, bytes, n);
    b->data_len += n;
}

/* ── Register allocator ──────────────────────────────────────────────────── */

/* We map IR temps to physical registers or spill slots on the Termina stack.
   Registers r6-r11 are used as "persistent locals" within a function.
   r12 is the scratch register for computations.
   Temps that don't fit in r6-r11 are spilled to [SP + offset].            */

#define FIRST_LOCAL 6
#define LAST_LOCAL  11
#define NUM_LOCALS  (LAST_LOCAL - FIRST_LOCAL + 1)  /* 6 */
#define SCRATCH     12

typedef struct {
    int temp_id;     /* -1 = slot free */
    int spill_off;   /* byte offset from frame base (rbp-equivalent), 0=not spilled */
} RegSlot;

typedef struct {
    RegSlot  slots[NUM_LOCALS];
    int      spill_next;   /* next spill slot (grows up from frame base) */
    int      frame_size;   /* total bytes reserved for spills            */
} RegAlloc;

static void ra_init(RegAlloc *ra) {
    for (int i = 0; i < NUM_LOCALS; i++) {
        ra->slots[i].temp_id  = -1;
        ra->slots[i].spill_off = 0;
    }
    ra->spill_next = 8;    /* start at 8 (0 reserved for saved LR) */
    ra->frame_size = 0;
}

/* Returns physical register index, or -1 if spilled. */
static int ra_find(RegAlloc *ra, int temp_id) {
    for (int i = 0; i < NUM_LOCALS; i++)
        if (ra->slots[i].temp_id == temp_id)
            return FIRST_LOCAL + i;
    return -1;
}

static int ra_spill_off(RegAlloc *ra, int temp_id) {
    for (int i = 0; i < NUM_LOCALS; i++)
        if (ra->slots[i].temp_id == temp_id)
            return ra->slots[i].spill_off;
    return -1;
}

/* Assign a physical register to temp_id.  Evicts LRU if all full.
   Returns the physical register.                                           */
static int ra_assign(RegAlloc *ra, int temp_id) {
    /* Already assigned? */
    int existing = ra_find(ra, temp_id);
    if (existing >= 0) return existing;

    /* Find a free slot */
    for (int i = 0; i < NUM_LOCALS; i++) {
        if (ra->slots[i].temp_id == -1) {
            ra->slots[i].temp_id  = temp_id;
            ra->slots[i].spill_off = 0;
            return FIRST_LOCAL + i;
        }
    }

    /* All occupied — evict slot 0 (simple FIFO) and spill it */
    /* Rotate: move slot 0 to a spill and shift everything down */
    if (ra->slots[0].spill_off == 0) {
        ra->slots[0].spill_off = ra->spill_next;
        ra->spill_next += 8;
        if (ra->spill_next > ra->frame_size)
            ra->frame_size = ra->spill_next;
    }
    /* We never reload the evicted temp in this simple allocator:
       the evicted temp is now "spill-only". */
    ra->slots[0].temp_id   = temp_id;
    ra->slots[0].spill_off = 0;
    return FIRST_LOCAL;
}

/* ── Label / patch system ────────────────────────────────────────────────── */

typedef struct {
    int code_idx;   /* index in code[] of the instruction to patch */
    int label_id;   /* IR label id to resolve                       */
    int is_cond;    /* 1 = conditional jump (JZ/JNZ/JL/JG), 0 = JMPI */
    uint8_t cond_op;
    uint8_t cond_rs1;
} Patch;

typedef struct {
    int label_id;
    int code_idx;   /* -1 = not yet seen */
} LabelAddr;

static Patch     patches[MAX_PATCHES];
static int       patch_count = 0;
static LabelAddr label_addrs[MAX_LABELS];
static int       label_cap = 0;

static void labels_reset(void) {
    patch_count = 0;
    label_cap   = 0;
}

static void label_define(int label_id, int code_idx) {
    for (int i = 0; i < label_cap; i++) {
        if (label_addrs[i].label_id == label_id) {
            label_addrs[i].code_idx = code_idx;
            return;
        }
    }
    if (label_cap < MAX_LABELS) {
        label_addrs[label_cap].label_id = label_id;
        label_addrs[label_cap].code_idx = code_idx;
        label_cap++;
    }
}

static int label_lookup(int label_id) {
    for (int i = 0; i < label_cap; i++)
        if (label_addrs[i].label_id == label_id)
            return label_addrs[i].code_idx;
    return -1;
}

static void patch_add(Buf *b, int code_idx, int label_id, int is_cond,
                      uint8_t cond_op, uint8_t cond_rs1) {
    if (patch_count < MAX_PATCHES) {
        patches[patch_count].code_idx  = code_idx;
        patches[patch_count].label_id  = label_id;
        patches[patch_count].is_cond   = is_cond;
        patches[patch_count].cond_op   = cond_op;
        patches[patch_count].cond_rs1  = cond_rs1;
        patch_count++;
    }
}

static void apply_patches(Buf *b) {
    for (int i = 0; i < patch_count; i++) {
        Patch *p = &patches[i];
        int target = label_lookup(p->label_id);
        if (target < 0) {
            fprintf(stderr, "codegen_termina: unresolved label %d\n", p->label_id);
            continue;
        }
        /* Compute PC-relative offset in bytes.
           At runtime when the instruction executes, PC already points PAST it
           (fetch advances by 4).  So offset = (target - (code_idx+1)) * 4.
           We store offset+4 in the imm12 field (the VM subtracts 4 from JMPI). */
        int offset = (target - (p->code_idx + 1)) * 4 + 4;
        uint32_t imm12 = (uint32_t)(offset) & 0xFFF;
        if (p->is_cond) {
            b->code[p->code_idx] = enc(p->cond_op, 0, p->cond_rs1, 0, imm12);
        } else {
            b->code[p->code_idx] = enc(OP_JMPI, 0, 0, 0, imm12);
        }
    }
}

/* ── String constant pool ────────────────────────────────────────────────── */

typedef struct {
    char *value;
    int   data_off;  /* byte offset in data section */
} StrConst;

static StrConst str_consts[MAX_STRINGS];
static int      str_const_count = 0;

static void str_pool_reset(void) { str_const_count = 0; }

/* ── Static/global storage ───────────────────────────────────────────────── */

typedef struct {
    char *name;
    int   data_off;  /* byte offset in data section */
    int   size;      /* bytes reserved */
} StaticVar;

static StaticVar static_vars[MAX_STATIC_VARS];
static int       static_var_count = 0;

static void static_vars_reset(void) { static_var_count = 0; }

static int data_align(Buf *b, int align) {
    int pad = (align - (b->data_len % align)) % align;
    if (pad > 0) {
        uint8_t zeros[8] = {0};
        data_push_bytes(b, zeros, pad);
    }
    return b->data_len;
}

static int static_var_find(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < static_var_count; i++)
        if (strcmp(static_vars[i].name, name) == 0)
            return i;
    return -1;
}

static int static_var_offset(const char *name) {
    int idx = static_var_find(name);
    return idx >= 0 ? static_vars[idx].data_off : -1;
}

static void static_vars_collect(Buf *b, const IRModule *mod) {
    static_vars_reset();

    for (int i = 0; i < mod->instr_count; i++) {
        const IRInstr *ins = &mod->instrs[i];
        if (ins->op != IR_STATIC_VAR || !ins->str_extra)
            continue;

        if (static_var_find(ins->str_extra) >= 0)
            continue;

        if (static_var_count >= MAX_STATIC_VARS) {
            fprintf(stderr, "codegen_termina: too many static variables\n");
            break;
        }

        data_align(b, 8);

        int slots = ins->extra_int > 0 ? ins->extra_int : 1;
        int size  = slots * 8;
        int off   = b->data_len;

        uint8_t *zeros = calloc(1, size);
        data_push_bytes(b, zeros, size);
        free(zeros);

        if (ins->src1.kind == IROP_CONST_INT ||
            ins->src1.kind == IROP_CONST_BOOL) {
            int64_t init = ins->src1.kind == IROP_CONST_BOOL
                         ? ins->src1.bool_val
                         : ins->src1.int_val;
            memcpy(b->data + off, &init, 8);
        } else if (ins->src1.kind == IROP_CONST_FLOAT) {
            union { double d; int64_t i; } u;
            u.d = ins->src1.float_val;
            memcpy(b->data + off, &u.i, 8);
        }

        static_vars[static_var_count].name     = strdup(ins->str_extra);
        static_vars[static_var_count].data_off = off;
        static_vars[static_var_count].size     = size;
        static_var_count++;
    }
}

static int str_pool_get(Buf *b, const char *value) {
    if (!value) value = "";
    for (int i = 0; i < str_const_count; i++)
        if (strcmp(str_consts[i].value, value) == 0)
            return str_consts[i].data_off;
    if (str_const_count >= MAX_STRINGS) return 0;
    int off = b->data_len;
    int len = strlen(value) + 1;
    data_push_bytes(b, (const uint8_t *)value, len);
    str_consts[str_const_count].value    = strdup(value);
    str_consts[str_const_count].data_off = off;
    str_const_count++;
    return off;
}

/* ── Function address table (for CALL patching) ──────────────────────────── */

typedef struct {
    char *name;
    int   code_idx;
} FnAddr;

static FnAddr fn_addrs[MAX_FN_ADDRS];
static int    fn_addr_count = 0;

typedef struct {
    int  code_idx;
    char name[256];
} CallPatch;

static CallPatch call_patches[MAX_PATCHES];
static int       call_patch_count = 0;

static void fn_table_reset(void) {
    fn_addr_count    = 0;
    call_patch_count = 0;
}

static void fn_define(const char *name, int code_idx) {
    for (int i = 0; i < fn_addr_count; i++)
        if (strcmp(fn_addrs[i].name, name) == 0) {
            fn_addrs[i].code_idx = code_idx;
            return;
        }
    if (fn_addr_count < MAX_FN_ADDRS) {
        fn_addrs[fn_addr_count].name     = strdup(name);
        fn_addrs[fn_addr_count].code_idx = code_idx;
        fn_addr_count++;
    }
}

static int fn_lookup(const char *name) {
    for (int i = 0; i < fn_addr_count; i++)
        if (strcmp(fn_addrs[i].name, name) == 0)
            return fn_addrs[i].code_idx;
    return -1;
}

static void call_patch_add(int code_idx, const char *name) {
    if (call_patch_count < MAX_PATCHES) {
        call_patches[call_patch_count].code_idx = code_idx;
        strncpy(call_patches[call_patch_count].name, name, 255);
        call_patch_count++;
    }
}

static void apply_call_patches(Buf *b) {
    for (int i = 0; i < call_patch_count; i++) {
        CallPatch *p = &call_patches[i];
        int target = fn_lookup(p->name);
        if (target < 0) {
            fprintf(stderr, "codegen_termina: unresolved function '%s'\n", p->name);
            continue;
        }
        int offset = (target - (p->code_idx + 1)) * 4 + 4;
        
        /* Check if offset fits in 12-bit signed immediate (-2048 to 2047) */
        if (offset >= -2048 && offset <= 2047) {
            /* Direct call - offset fits in immediate */
            b->code[p->code_idx]     = enc(OP_CALLI, 0, 0, 0, (uint32_t)offset & 0xFFF);
            b->code[p->code_idx + 1] = enc(OP_NOP, 0, 0, 0, 0);
            b->code[p->code_idx + 2] = enc(OP_NOP, 0, 0, 0, 0);
            b->code[p->code_idx + 3] = enc(OP_NOP, 0, 0, 0, 0);
        } else {
            /* Indirect call - offset too large, load absolute address into r12 and CALL r12 
               Use: MOVI r12, 0; MOVHI r12, high12; ADDI r12, r12, low12; CALL r12
               This avoids sign-extension issues with MOVHI's OR behavior */
            uint32_t target_addr = CODE_BASE + (uint32_t)target * 4;
            uint32_t low12 = target_addr & 0xFFF;
            uint32_t high12 = (target_addr >> 12) & 0xFFF;
            
            /* ADDI uses signed 12-bit immediate. If low12 >= 0x800 (2048), it will be negative.
               To handle this, increment high12 and use (low12 - 0x1000) as the ADDI immediate. */
            int32_t addi_imm;
            if (low12 >= 0x800) {
                high12 = (high12 + 1) & 0xFFF;
                addi_imm = (int32_t)low12 - 0x1000;
            } else {
                addi_imm = (int32_t)low12;
            }
            
            /* MOVI r12, 0 - clear r12 */
            b->code[p->code_idx]     = enc(OP_MOVI, 12, 0, 0, 0);
            /* MOVHI r12, high12 - set high bits via OR (0 | high12<<12) */
            b->code[p->code_idx + 1] = enc(OP_MOVHI, 12, 12, 0, high12);
            /* ADDI r12, r12, low12 - add low bits */
            b->code[p->code_idx + 2] = enc(OP_ADDI, 12, 12, 0, (uint32_t)addi_imm & 0xFFF);
            /* CALL r12 */
            b->code[p->code_idx + 3] = enc(OP_CALL, 0, 12, 0, 0);
        }
    }
}

/* ── Emit helpers ────────────────────────────────────────────────────────── */

/* Load a 24-bit immediate into rd using MOVI + MOVHI if needed. */
static void emit_imm(Buf *b, uint8_t rd, long val) {
    int32_t v = (int32_t)val;
    if (v >= -2048 && v <= 2047) {
        emit(b, enc(OP_MOVI, rd, 0, 0, (uint32_t)v & 0xFFF));
    } else {
        emit(b, enc(OP_MOVI,  rd, 0, 0, (uint32_t)(v & 0xFFF)));
        emit(b, enc(OP_MOVHI, rd, rd, 0, (uint32_t)((v >> 12) & 0xFFF)));
    }
}

/* Load a data-section absolute address (DATA_BASE + off) into rd. */
static void emit_data_addr(Buf *b, uint8_t rd, int off) {
    emit_imm(b, rd, DATA_BASE + off);
}

/* ── Per-function state ───────────────────────────────────────────────────── */

typedef struct {
    RegAlloc ra;
    int      has_frame;   /* 1 if we emitted a frame prologue */
    int      frame_size;  /* bytes reserved on stack          */
    /* Map from IR temp_id to spill slot offset (for temps not in registers) */
    int      temp_spill[MAX_TEMPS];
    int      next_spill;
} FnState;

static FnState fs;

static void fs_init(void) {
    ra_init(&fs.ra);
    fs.has_frame  = 0;
    fs.frame_size = 8;   /* at minimum, we save LR */
    fs.next_spill = 8;
    memset(fs.temp_spill, -1, sizeof(fs.temp_spill));
}

/* Get or assign a physical register for a temp.
   Returns register index (0-15).
   For temps beyond what the allocator tracks, uses SCRATCH and spills. */
static uint8_t temp_reg(Buf *b, int temp_id, int write_mode) {
    if (temp_id < 0) return SCRATCH;
    int r = ra_find(&fs.ra, temp_id);
    if (r >= 0) return (uint8_t)r;
    /* Assign */
    r = ra_assign(&fs.ra, temp_id);
    return (uint8_t)r;
}

/* Load operand into a physical register, return which register holds it. */
static uint8_t load_operand(Buf *b, const IROperand *op) {
    switch (op->kind) {
    case IROP_TEMP:
        return temp_reg(b, op->temp_id, 0);

    case IROP_CONST_INT:
        emit_imm(b, SCRATCH, op->int_val);
        return SCRATCH;

    case IROP_CONST_BOOL:
        emit_imm(b, SCRATCH, op->bool_val ? 1 : 0);
        return SCRATCH;

    case IROP_CONST_STR:
        /* Put string in data section, load address */
        /* String pool is pre-built; we just get the offset */
        emit_imm(b, SCRATCH, 0); /* placeholder — real addr needs pool built first */
        return SCRATCH;

    case IROP_NONE:
        emit(b, enc(OP_MOVI, SCRATCH, 0, 0, 0));
        return SCRATCH;

    case IROP_CONST_FLOAT:
        /* Store float bits as integer */
        {
            union { double d; long l; } u; u.d = op->float_val;
            emit_imm(b, SCRATCH, u.l);
        }
        return SCRATCH;

    default:
        emit(b, enc(OP_MOVI, SCRATCH, 0, 0, 0));
        return SCRATCH;
    }
}

/* Store SCRATCH (or src_reg) into dest temp's register. */
static void store_dest(Buf *b, const IROperand *dest, uint8_t src_reg) {
    if (dest->kind != IROP_TEMP) return;
    uint8_t dr = temp_reg(b, dest->temp_id, 1);
    if (dr != src_reg)
        emit(b, enc(OP_MOV, dr, src_reg, 0, 0));
}

/* ── Binary / comparison op helper ──────────────────────────────────────── */

static void emit_binop(Buf *b, uint8_t term_op,
                       const IROperand *dest,
                       const IROperand *src1, const IROperand *src2) {
    uint8_t l = load_operand(b, src1);
    /* If l is SCRATCH, move to a temp register to avoid clobbering */
    if (l == SCRATCH) {
        emit(b, enc(OP_MOV, 11, SCRATCH, 0, 0));
        l = 11;
    }
    uint8_t r = load_operand(b, src2);
    uint8_t dr = (dest->kind == IROP_TEMP) ? temp_reg(b, dest->temp_id, 1) : (uint8_t)SCRATCH;
    emit(b, enc(term_op, dr, l, r, 0));
}

/* ── Comparison → boolean (sets dest to 0 or 1) ─────────────────────────── */

static void emit_cmp(Buf *b, IROpcode ir_op,
                     const IROperand *dest,
                     const IROperand *src1, const IROperand *src2) {
    /* Strategy: compute src1 - src2 into scratch, then use conditional
       branches to set a result register.                                   */
    uint8_t l = load_operand(b, src1);
    if (l == SCRATCH) { emit(b, enc(OP_MOV, 10, SCRATCH, 0, 0)); l = 10; }
    uint8_t r = load_operand(b, src2);
    if (r == SCRATCH) { emit(b, enc(OP_MOV, 11, SCRATCH, 0, 0)); r = 11; }
    uint8_t dr = (dest->kind == IROP_TEMP) ? temp_reg(b, dest->temp_id, 1) : (uint8_t)SCRATCH;

    /* diff = l - r */
    emit(b, enc(OP_SUB, SCRATCH, l, r, 0));

    /* Set dr = 1, then conditionally set to 0 if condition fails.
       (Alternatively: set dr=0, then branch to set=1 path.)
       We use: dr=0; if(condition holds on SCRATCH) dr=1 */
    emit(b, enc(OP_MOVI, dr, 0, 0, 0));  /* dr = 0 */

    /* Jump over the "set to 1" instruction if condition does NOT hold */
    /* The condition is on SCRATCH (= l - r) */
    uint8_t skip_op;
    switch (ir_op) {
    case IR_EQ:  skip_op = OP_JNZ; break;  /* skip if diff != 0  */
    case IR_NEQ: skip_op = OP_JZ;  break;  /* skip if diff == 0  */
    case IR_LT:  skip_op = OP_JG;  break;  /* skip if diff >= 0  (i.e. not <) */
    case IR_LE:  skip_op = OP_JG;  break;  /* skip if diff > 0   */
    case IR_GT:  skip_op = OP_JL;  break;  /* skip if diff <= 0  */
    case IR_GE:  skip_op = OP_JL;  break;  /* skip if diff < 0   */
    default:     skip_op = OP_JNZ; break;
    }

    /* Special case for LE / GE: also need to check zero */
    if (ir_op == IR_LE || ir_op == IR_GE) {
        /* For LE (l<=r): true when diff<=0, i.e. diff<0 OR diff==0
           emit: if diff>0 skip; else dr=1
           (OP_JG skips the set when diff>0 — correct for LE) */
    }

    /* skip instruction is at cur_pc; target is cur_pc+2 (over MOVI dr,1) */
    int skip_idx = cur_pc(b);
    emit(b, 0); /* placeholder */
    emit(b, enc(OP_MOVI, dr, 0, 0, 1));  /* dr = 1 (condition holds) */
    /* Patch skip: offset to jump over the MOVI = 1 instruction */
    int off = 4;  /* skip 1 instruction = 4 bytes => offset+4 = 8 */
    b->code[skip_idx] = enc(skip_op, 0, SCRATCH, 0, (uint32_t)(off+4) & 0xFFF);
}

/* ── Emit a syscall (INT 0 = write) ─────────────────────────────────────── */

static void emit_syscall(Buf *b, int num) {
    emit_imm(b, SCRATCH, num);
    emit(b, enc(OP_INT, 0, 0, 0, (uint32_t)num & 0xFFF));
}

/* ── Function prologue / epilogue ────────────────────────────────────────── */

static void emit_prologue(Buf *b) {
    /* PUSH LR so we can make nested calls */
    emit(b, enc(OP_PUSH, 0, REG_LR, 0, 0));
    fs.has_frame  = 1;
    fs.frame_size = 8;  /* will grow as spills are added */
}

static void emit_epilogue(Buf *b) {
    emit(b, enc(OP_POP, REG_LR, 0, 0, 0));
    emit(b, enc(OP_RET, 0, 0, 0, 0));
}

/* ── Single IR instruction → Termina ────────────────────────────────────── */

static void emit_ir_instr(Buf *b, const IRInstr *ins) {
    switch (ins->op) {

    case IR_NOP:
    case IR_ALLOCA:
    case IR_STATIC_VAR:
    case IR_FUNC_BEGIN:
    case IR_FUNC_END:
        break;

    case IR_LABEL:
        label_define(ins->dest.label_id, cur_pc(b));
        break;

    case IR_CONST_INT:
    case IR_CONST_BOOL: {
        uint8_t dr = temp_reg(b, ins->dest.temp_id, 1);
        long val = (ins->op == IR_CONST_INT) ? ins->src1.int_val : ins->src1.bool_val;
        emit_imm(b, dr, val);
        break;
    }

    case IR_CONST_NIL: {
        uint8_t dr = temp_reg(b, ins->dest.temp_id, 1);
        emit(b, enc(OP_MOVI, dr, 0, 0, 0));
        break;
    }

    case IR_CONST_STR: {
        /* Already in string pool; emit load of data address */
        uint8_t dr = temp_reg(b, ins->dest.temp_id, 1);
        int off = str_pool_get(b, ins->src1.str_val ? ins->src1.str_val : "");
        emit_imm(b, dr, DATA_BASE + off);
        break;
    }

    case IR_CONST_FLOAT: {
        uint8_t dr = temp_reg(b, ins->dest.temp_id, 1);
        union { double d; long l; } u; u.d = ins->src1.float_val;
        emit_imm(b, dr, u.l);
        break;
    }

    case IR_LOAD_VAR: {
        uint8_t dr = temp_reg(b, ins->dest.temp_id, 1);

        int off = ins->str_extra ? static_var_offset(ins->str_extra) : -1;
        if (off >= 0) {
            emit_data_addr(b, SCRATCH, off);
            emit(b, enc(OP_LOAD, dr, SCRATCH, 0, 0));
        } else {
            /* Local variables are still register-only in the simple backend. */
            emit(b, enc(OP_MOV, dr, dr, 0, 0));  /* no-op mov to itself */
        }
        break;
    }

    case IR_STORE_VAR: {
        uint8_t sr = load_operand(b, &ins->src1);

        int off = ins->str_extra ? static_var_offset(ins->str_extra) : -1;
        if (off >= 0) {
            if (sr == SCRATCH) {
                emit(b, enc(OP_MOV, 11, SCRATCH, 0, 0));
                sr = 11;
            }
            emit_data_addr(b, SCRATCH, off);
            emit(b, enc(OP_STORE, 0, SCRATCH, sr, 0));
        } else {
            /* Local variable stores are register-only in the simple backend. */
            (void)sr;
        }
        break;
    }

    case IR_CAST: {
        uint8_t sr = load_operand(b, &ins->src1);
        uint8_t dr = temp_reg(b, ins->dest.temp_id, 1);
        if (dr != sr) emit(b, enc(OP_MOV, dr, sr, 0, 0));
        break;
    }

    case IR_ADD: emit_binop(b, OP_ADD, &ins->dest, &ins->src1, &ins->src2); break;
    case IR_SUB: emit_binop(b, OP_SUB, &ins->dest, &ins->src1, &ins->src2); break;
    case IR_MUL: emit_binop(b, OP_MUL, &ins->dest, &ins->src1, &ins->src2); break;
    case IR_DIV: emit_binop(b, OP_DIV, &ins->dest, &ins->src1, &ins->src2); break;
    case IR_MOD: emit_binop(b, OP_MOD, &ins->dest, &ins->src1, &ins->src2); break;

    case IR_BITAND: emit_binop(b, OP_AND, &ins->dest, &ins->src1, &ins->src2); break;
    case IR_BITOR:  emit_binop(b, OP_OR,  &ins->dest, &ins->src1, &ins->src2); break;
    case IR_BITXOR: emit_binop(b, OP_XOR, &ins->dest, &ins->src1, &ins->src2); break;
    case IR_SHL:    emit_binop(b, OP_SHL, &ins->dest, &ins->src1, &ins->src2); break;
    case IR_SHR:    emit_binop(b, OP_SHR, &ins->dest, &ins->src1, &ins->src2); break;

    case IR_BITNOT: {
        uint8_t sr = load_operand(b, &ins->src1);
        uint8_t dr = temp_reg(b, ins->dest.temp_id, 1);
        emit(b, enc(OP_NOT, dr, sr, 0, 0));
        break;
    }

    case IR_NEG: {
        /* neg r = 0 - r */
        uint8_t sr = load_operand(b, &ins->src1);
        uint8_t dr = temp_reg(b, ins->dest.temp_id, 1);
        emit(b, enc(OP_MOVI, SCRATCH, 0, 0, 0));
        emit(b, enc(OP_SUB,  dr, SCRATCH, sr, 0));
        break;
    }

    case IR_NOT: {
        /* logical not: result = (src == 0) ? 1 : 0 */
        uint8_t sr = load_operand(b, &ins->src1);
        uint8_t dr = temp_reg(b, ins->dest.temp_id, 1);
        int skip_idx = cur_pc(b);
        emit(b, 0); /* placeholder JNZ */
        emit(b, enc(OP_MOVI, dr, 0, 0, 1));  /* if zero: dr=1 */
        int end_idx = cur_pc(b);
        emit(b, 0); /* JMPI over else */
        emit(b, enc(OP_MOVI, dr, 0, 0, 0));  /* if nonzero: dr=0 */
        /* patch: JNZ sr, skip over dr=1 to dr=0 */
        b->code[skip_idx] = enc(OP_JNZ, 0, sr, 0, (uint32_t)(4+4) & 0xFFF);
        /* patch: JMPI over dr=0 */
        b->code[end_idx]  = enc(OP_JMPI, 0, 0, 0, (uint32_t)(4+4) & 0xFFF);
        break;
    }

    case IR_EQ:
    case IR_NEQ:
    case IR_LT:
    case IR_LE:
    case IR_GT:
    case IR_GE:
        emit_cmp(b, ins->op, &ins->dest, &ins->src1, &ins->src2);
        break;

    case IR_JUMP: {
        int lbl = ins->src1.label_id;
        int target = label_lookup(lbl);
        if (target >= 0) {
            int off = (target - (cur_pc(b) + 1)) * 4 + 4;
            emit(b, enc(OP_JMPI, 0, 0, 0, (uint32_t)off & 0xFFF));
        } else {
            patch_add(b, cur_pc(b), lbl, 0, 0, 0);
            emit(b, 0);
        }
        break;
    }

    case IR_JUMP_IF:
    case IR_JUMP_UNLESS: {
        uint8_t cr = load_operand(b, &ins->src1);
        int lbl    = ins->src2.label_id;
        uint8_t jop = (ins->op == IR_JUMP_IF) ? OP_JNZ : OP_JZ;
        int target = label_lookup(lbl);
        if (target >= 0) {
            int off = (target - (cur_pc(b) + 1)) * 4 + 4;
            emit(b, enc(jop, 0, cr, 0, (uint32_t)off & 0xFFF));
        } else {
            patch_add(b, cur_pc(b), lbl, 1, jop, cr);
            emit(b, 0);
        }
        break;
    }

    case IR_CALL: {
        if (!ins->str_extra) break;
        const char *name = ins->str_extra;

        /* ── Termina graphics/system intrinsics ──────────────────────── */
        struct { const char *fn; int sys; int nargs; } intrinsics[] = {
            {"cls",       SYS_CLS,       1},
            {"draw",      SYS_DRAW,      0},
            {"pset",      SYS_PSET,      3},
            {"line",      SYS_LINE,      5},
            {"rect",      SYS_RECT,      5},
            {"rectfill",  SYS_RECTFILL,  5},
            {"circle",    SYS_CIRCLE,    4},
            {"circlefill",SYS_CIRCLEFILL,4},
            {"print_str", SYS_PRINT_STR, 4},
            {"outb",      SYS_OUTB,      2},
            {"inb",       SYS_INB,       1},
            {NULL, 0, 0},
        };
        
        int handled = 0;
        for (int k = 0; intrinsics[k].fn; k++) {
            if (strcmp(name, intrinsics[k].fn) != 0) continue;
            
            /* Load args into r0-r4 */
            int nargs = ins->arg_count < intrinsics[k].nargs
                      ? ins->arg_count : intrinsics[k].nargs;
            for (int a = 0; a < nargs; a++) {
                uint8_t ar = load_operand(b, &ins->args[a]);
                if (ar != (uint8_t)a)
                    emit(b, enc(OP_MOV, (uint8_t)a, ar, 0, 0));
            }
            
            /* Emit INT with syscall number */
            emit(b, enc(OP_INT, 0, 0, 0, (uint32_t)intrinsics[k].sys));
            
            /* Zero dest register if present */
            if (ins->dest.kind == IROP_TEMP) {
                uint8_t dr = temp_reg(b, ins->dest.temp_id, 1);
                emit(b, enc(OP_MOVI, dr, 0, 0, 0));
            }
            handled = 1;
            break;
        }
        if (handled) break;

        /* ── hlt intrinsic ───────────────────────────────────────────── */
        if (strcmp(name, "hlt") == 0) {
            emit(b, enc(OP_HLT, 0, 0, 0, 0));
            break;
        }

        /* Push args into r0-r5 */
        int nargs = ins->arg_count > 6 ? 6 : ins->arg_count;
        for (int i = 0; i < nargs; i++) {
            uint8_t ar = load_operand(b, &ins->args[i]);
            if (ar != (uint8_t)i)
                emit(b, enc(OP_MOV, (uint8_t)i, ar, 0, 0));
        }

        /* Reserve 4 instructions for call (MOVI+MOVHI+ADDI+CALL for indirect, or CALLI+3 NOPs for direct) */
        call_patch_add(cur_pc(b), ins->str_extra);
        emit(b, 0);  /* Will be patched with CALLI or MOVI r12, 0 */
        emit(b, 0);  /* Will be patched with NOP or MOVHI r12, high12 */
        emit(b, 0);  /* Will be patched with NOP or ADDI r12, r12, low12 */
        emit(b, 0);  /* Will be patched with NOP or CALL r12 */

        /* Return value is in r0; move to dest */
        if (ins->dest.kind == IROP_TEMP) {
            uint8_t dr = temp_reg(b, ins->dest.temp_id, 1);
            if (dr != 0)
                emit(b, enc(OP_MOV, dr, 0, 0, 0));
        }
        break;
    }

    case IR_RETURN: {
        if (ins->src1.kind != IROP_NONE) {
            uint8_t sr = load_operand(b, &ins->src1);
            if (sr != 0)
                emit(b, enc(OP_MOV, 0, sr, 0, 0));
        } else {
            emit(b, enc(OP_MOVI, 0, 0, 0, 0));
        }
        emit_epilogue(b);
        break;
    }

    case IR_PRINT:
    case IR_PRINTLN: {
        /* INT 0 = write(fd, buf, len)
           r0 = 1 (stdout), r1 = ptr, r2 = len                              */
        switch (ins->extra_int) {
        case 1: /* PRINT_ARG_STR_LIT */ {
            const char *sv = ins->src1.str_val ? ins->src1.str_val : "";
            if (ins->op == IR_PRINTLN) {
                /* append newline */
                char *tmp = malloc(strlen(sv) + 2);
                strcpy(tmp, sv); strcat(tmp, "\n");
                int off = str_pool_get(b, tmp);
                free(tmp);
                emit_imm(b, 0, 1);
                emit_imm(b, 1, DATA_BASE + off);
                emit_imm(b, 2, strlen(sv) + 1);
            } else {
                int off = str_pool_get(b, sv);
                emit_imm(b, 0, 1);
                emit_imm(b, 1, DATA_BASE + off);
                emit_imm(b, 2, strlen(sv));
            }
            emit(b, enc(OP_INT, 0, 0, 0, 0));
            break;
        }
        case 4: /* PRINT_ARG_STR_PTR */ {
            uint8_t sr = load_operand(b, &ins->src1);
            emit_imm(b, 0, 1);
            if (sr != 1) emit(b, enc(OP_MOV, 1, sr, 0, 0));
            /* length: we'd need strlen here; for now emit a large fixed len
               and rely on null termination in a smarter VM.
               A real impl would call a strlen helper. */
            emit_imm(b, 2, 256);
            emit(b, enc(OP_INT, 0, 0, 0, 0));
            break;
        }
        case 2: /* PRINT_ARG_INT */ {
            /* We'd need an int_to_str helper; for simplicity, emit as-is
               and let the OS layer handle it. Real impl calls a stdlib fn. */
            uint8_t sr = load_operand(b, &ins->src1);
            emit_imm(b, 0, 1);
            if (sr != 1) emit(b, enc(OP_MOV, 1, sr, 0, 0));
            emit_imm(b, 2, 8);
            emit(b, enc(OP_INT, 0, 0, 0, 0));
            break;
        }
        default:
            break;
        }
        break;
    }



    case IR_NEW:
    case IR_ARRAY_ALLOC:
    case IR_ARRAY_INIT:
    case IR_ARENA_ALLOC: {
        /* Stub: zero dest register.  A full impl would call a heap allocator. */
        if (ins->dest.kind == IROP_TEMP) {
            uint8_t dr = temp_reg(b, ins->dest.temp_id, 1);
            emit(b, enc(OP_MOVI, dr, 0, 0, 0));
        }
        break;
    }

    case IR_GET_FIELD:
    case IR_ARRAY_LOAD: {
        /* ptr + offset load */
        uint8_t pr = load_operand(b, &ins->src1);
        if (pr == SCRATCH) { emit(b, enc(OP_MOV, 10, SCRATCH, 0, 0)); pr = 10; }
        uint8_t ir2 = load_operand(b, &ins->src2);
        uint8_t dr = temp_reg(b, ins->dest.temp_id, 1);
        /* addr = ptr + idx (for arrays stride=8, for fields idx=byte_offset) */
        emit(b, enc(OP_ADD, dr, pr, ir2, 0));
        emit(b, enc(OP_LOAD, dr, dr, 0, 0));
        break;
    }

    case IR_SET_FIELD:
    case IR_ARRAY_STORE: {
        uint8_t pr = load_operand(b, &ins->src1);
        if (pr == SCRATCH) { emit(b, enc(OP_MOV, 10, SCRATCH, 0, 0)); pr = 10; }
        uint8_t ir2 = load_operand(b, &ins->src2);
        uint8_t vr  = load_operand(b, &ins->extra_src);
        emit(b, enc(OP_ADD, SCRATCH, pr, ir2, 0));
        emit(b, enc(OP_STORE, 0, SCRATCH, vr, 0));
        break;
    }

    case IR_LOAD_PTR: {
        uint8_t pr = load_operand(b, &ins->src1);
        uint8_t dr = temp_reg(b, ins->dest.temp_id, 1);
        emit(b, enc(OP_LOAD, dr, pr, 0, 0));
        break;
    }

    case IR_STORE_PTR: {
        uint8_t pr = load_operand(b, &ins->src1);
        uint8_t vr = load_operand(b, &ins->src2);
        emit(b, enc(OP_STORE, 0, pr, vr, 0));
        break;
    }

    case IR_ADDROF:
    case IR_ADDROF_FN: {
        /* Function addresses are VM code addresses; static/global addresses
           are VM data addresses. */
        uint8_t dr = temp_reg(b, ins->dest.temp_id, 1);
        if (ins->str_extra) {
            int fn_idx = fn_lookup(ins->str_extra);
            if (fn_idx >= 0 || ins->op == IR_ADDROF_FN) {
                emit_imm(b, dr, fn_idx >= 0 ? CODE_BASE + fn_idx * 4 : 0);
            } else {
                int off = static_var_offset(ins->str_extra);
                if (off >= 0) {
                    emit_imm(b, dr, DATA_BASE + off);
                } else {
                    off = str_pool_get(b, "");  /* placeholder */
                    emit_imm(b, dr, DATA_BASE + off);
                }
            }
        } else {
            emit(b, enc(OP_MOVI, dr, 0, 0, 0));
        }
        break;
    }

    case IR_OUTB: {
        /* outb(port, val) — Termina doesn't have an outb instruction in base ISA
           but we can model it as INT 2 */
        uint8_t port = load_operand(b, &ins->src1);
        uint8_t val  = load_operand(b, &ins->src2);
        if (port != 0) emit(b, enc(OP_MOV, 0, port, 0, 0));
        if (val  != 1) emit(b, enc(OP_MOV, 1, val,  0, 0));
        emit(b, enc(OP_INT, 0, 0, 0, 2));
        break;
    }

    case IR_INB: {
        uint8_t port = load_operand(b, &ins->src1);
        if (port != 0) emit(b, enc(OP_MOV, 0, port, 0, 0));
        emit(b, enc(OP_INT, 0, 0, 0, 3));
        if (ins->dest.kind == IROP_TEMP) {
            uint8_t dr = temp_reg(b, ins->dest.temp_id, 1);
            if (dr != 0) emit(b, enc(OP_MOV, dr, 0, 0, 0));
        }
        break;
    }

    case IR_CLI: emit(b, enc(OP_NOP, 0, 0, 0, 0)); break;  /* no-op on Termina */
    case IR_STI: emit(b, enc(OP_NOP, 0, 0, 0, 0)); break;

    case IR_MEMSET: {
        /* Simple loop: for(i=0; i<count; i++) mem[ptr+i] = byte */
        uint8_t ptr   = load_operand(b, &ins->src1);
        uint8_t bval  = load_operand(b, &ins->src2);
        uint8_t count = load_operand(b, &ins->extra_src);
        if (ptr   == SCRATCH) { emit(b, enc(OP_MOV, 8, SCRATCH, 0, 0)); ptr   = 8; }
        if (bval  == SCRATCH) { emit(b, enc(OP_MOV, 9, SCRATCH, 0, 0)); bval  = 9; }
        if (count == SCRATCH) { emit(b, enc(OP_MOV,10, SCRATCH, 0, 0)); count = 10; }
        /* i = 0 in r11 */
        emit(b, enc(OP_MOVI, 11, 0, 0, 0));
        int loop_top = cur_pc(b);
        /* if i >= count, exit */
        emit(b, enc(OP_SUB, SCRATCH, 11, count, 0));
        int exit_idx = cur_pc(b);
        emit(b, 0);  /* JG SCRATCH → exit */
        /* mem[ptr + i] = bval */
        emit(b, enc(OP_ADD, SCRATCH, ptr, 11, 0));
        emit(b, enc(OP_STORE, 0, SCRATCH, bval, 0));
        /* i++ */
        emit(b, enc(OP_ADDI, 11, 11, 0, 1));
        /* jump back to loop top */
        int back_off = (loop_top - (cur_pc(b) + 1)) * 4 + 4;
        emit(b, enc(OP_JMPI, 0, 0, 0, (uint32_t)back_off & 0xFFF));
        int exit_pc = cur_pc(b);
        int exit_off = (exit_pc - (exit_idx + 1)) * 4 + 4;
        b->code[exit_idx] = enc(OP_JG, 0, SCRATCH, 0, (uint32_t)exit_off & 0xFFF);
        break;
    }

    case IR_ASM_BLOCK:
        /* Raw asm blocks are not supported in the Termina backend */
        fprintf(stderr, "codegen_termina: asm{} blocks not supported, skipping\n");
        break;

    default:
        /* Silently skip unhandled ops */
        break;
    }
}

/* ── Function codegen ────────────────────────────────────────────────────── */

static void codegen_function(Buf *b, const IRModule *mod, int begin_idx) {
    const IRInstr *begin = &mod->instrs[begin_idx];
    const char *fn_name = begin->str_extra ? begin->str_extra : "unknown";
    int is_main = begin->extra_int & 1;

    /* Find end */
    int end_idx = begin_idx + 1;
    int depth = 1;
    while (end_idx < mod->instr_count && depth > 0) {
        if (mod->instrs[end_idx].op == IR_FUNC_BEGIN) depth++;
        if (mod->instrs[end_idx].op == IR_FUNC_END)   depth--;
        end_idx++;
    }
    end_idx--;  /* points at IR_FUNC_END */

    /* Register function address */
    fn_define(fn_name, cur_pc(b));
    if (is_main) fn_define("main", cur_pc(b));

    /* Per-function allocator reset */
    fs_init();
    labels_reset();

    /* Spill param registers into their assigned temps (r0-r5 → temp 0-N) */
    emit_prologue(b);

    /* Emit body */
    for (int i = begin_idx + 1; i < end_idx; i++) {
        const IRInstr *ins = &mod->instrs[i];
        if (ins->op == IR_ALLOCA) continue;  /* handled by reg allocator */
        emit_ir_instr(b, ins);
    }

    /* Implicit return if function doesn't end with one */
    const IRInstr *last = &mod->instrs[end_idx - 1];
    if (last->op != IR_RETURN) {
        emit(b, enc(OP_MOVI, 0, 0, 0, 0));
        emit_epilogue(b);
    }

    /* Resolve intra-function labels */
    apply_patches(b);
}

/* ── Top-level entry point ───────────────────────────────────────────────── */

void codegen_termina(IRModule *mod, FILE *out, const char *src_filename) {
    Buf b;
    buf_init(&b);
    fn_table_reset();
    str_pool_reset();
    static_vars_collect(&b, mod);

    /* Pre-pass: collect all string constants into the data section */
    for (int i = 0; i < mod->instr_count; i++) {
        const IRInstr *ins = &mod->instrs[i];
        if (ins->op == IR_CONST_STR && ins->src1.str_val)
            str_pool_get(&b, ins->src1.str_val);
        if ((ins->op == IR_PRINT || ins->op == IR_PRINTLN) &&
            ins->extra_int == 1 && ins->src1.str_val) {
            if (ins->op == IR_PRINTLN) {
                char *tmp = malloc(strlen(ins->src1.str_val) + 2);
                strcpy(tmp, ins->src1.str_val);
                strcat(tmp, "\n");
                str_pool_get(&b, tmp);
                free(tmp);
            } else {
                str_pool_get(&b, ins->src1.str_val);
            }
        }
    }

    /* Emit entry point that calls main */
    int entry_call_idx = cur_pc(&b);
    emit(&b, 0);  /* CALLI main — patched below */
    emit(&b, enc(OP_HLT, 0, 0, 0, 0));

    /* Emit runtime stub functions (no-op implementations) */
    /* arena_init(arena_ptr) - no-op stub, just returns */
    fn_define("arena_init", cur_pc(&b));
    emit(&b, enc(OP_RET, 0, 0, 0, 0));

    /* arena_free(arena_ptr) - no-op stub, just returns */
    fn_define("arena_free", cur_pc(&b));
    emit(&b, enc(OP_RET, 0, 0, 0, 0));

    /* Compile all functions */
    for (int i = 0; i < mod->instr_count; i++) {
        if (mod->instrs[i].op == IR_FUNC_BEGIN) {
            codegen_function(&b, mod, i);
            /* Skip to FUNC_END */
            int depth = 1;
            while (++i < mod->instr_count) {
                if (mod->instrs[i].op == IR_FUNC_BEGIN) depth++;
                if (mod->instrs[i].op == IR_FUNC_END)   depth--;
                if (depth == 0) break;
            }
        }
    }

    /* Patch the entry CALLI main */
    int main_idx = fn_lookup("main");
    if (main_idx >= 0) {
        int off = (main_idx - (entry_call_idx + 1)) * 4 + 4;
        b.code[entry_call_idx] = enc(OP_CALLI, 0, 0, 0, (uint32_t)off & 0xFFF);
    } else {
        fprintf(stderr, "codegen_termina: no main() found\n");
        b.code[entry_call_idx] = enc(OP_NOP, 0, 0, 0, 0);
    }

    /* Patch all cross-function CALL instructions */
    apply_call_patches(&b);

    /* Write binary:
       Header (16 bytes):
         [0..3]  magic   "TERM"
         [4..7]  data section size
         [8..11] data section offset (always 16 = right after header)
         [12..15] code section size in instructions
       Data section (b.data_len bytes)
       Code section (b.code_len * 4 bytes)
    */
    uint32_t magic      = 0x4D524554;  /* "TERM" little-endian */
    uint32_t data_size  = (uint32_t)b.data_len;
    uint32_t data_off   = 16;
    uint32_t code_count = (uint32_t)b.code_len;

    fwrite(&magic,      4, 1, out);
    fwrite(&data_size,  4, 1, out);
    fwrite(&data_off,   4, 1, out);
    fwrite(&code_count, 4, 1, out);
    fwrite(b.data, 1, b.data_len, out);
    fwrite(b.code, 4, b.code_len, out);

    if (src_filename)
        fprintf(stderr, "codegen_termina: %s → %d instructions, %d bytes data\n",
                src_filename, b.code_len, b.data_len);

    free(b.code);
    free(b.data);
}
