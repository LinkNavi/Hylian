#include "codegen_asm.h"
#include "ast.h"
#include "ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── NASM reserved-word safety ───────────────────────────────────────────────
   NASM treats certain bare identifiers as keywords. If a Hylian variable or
   struct-literal hidden label collides with one of these names the assembler
   will error with "symbol not defined" or a mysterious parse failure.
   nasm_safe_label() returns a pointer to a static buffer with "_hy_" prepended
   whenever the name matches a known NASM keyword; otherwise it returns the
   original pointer unchanged. */
static const char *nasm_safe_label(const char *name) {
  if (!name) return name;
  /* NASM size keywords and other commonly-colliding reserved words */
  static const char *NASM_KEYWORDS[] = {
    "byte", "word", "dword", "qword", "tword", "oword", "yword", "zword",
    "box",
    "abs", "rel", "seg", "wrt",
    "section", "segment", "global", "extern", "common",
    "bits", "use16", "use32", "use64", "default", "cpu",
    "org", "align", "alignb",
    "at", "end", "db", "dw", "dd", "dq", "dt", "do", "dy", "dz",
    "resb", "resw", "resd", "resq", "rest", "reso", "resy", "resz",
    "equ", "times", "incbin", "include",
    "struc", "endstruc", "istruc", "iend", "ptr",
    "near", "far", "short",
    "nop", "ret", "call", "jmp", "push", "pop", "add", "sub", "mul",
    "div", "mod", "and", "or", "xor", "not", "neg",
    "mov", "lea", "cmp", "test", "int",
    NULL
  };
  for (int _i = 0; NASM_KEYWORDS[_i]; _i++) {
    if (strcmp(name, NASM_KEYWORDS[_i]) == 0) {
      static char _buf[256];
      snprintf(_buf, sizeof(_buf), "_hy_%s", name);
      return _buf;
    }
  }
  return name;
}


/* ── Globals ────────────────────────────────────────────────────────────── */
static const char *current_target = "linux";
static int io_included = 0;
static const char *current_class_name = NULL;
static int current_freestanding = 0;

#define MAX_FN_PREFIXES 32
typedef struct {
  const char *src_prefix;
  const char *abi_prefix;
} FnPrefix;
static FnPrefix fn_prefixes[MAX_FN_PREFIXES];
static int fn_prefix_count = 0;

/* Exact-name rewrites for std.io / std.kernel builtins that don't share a
   common prefix with all other user-defined names.  Each entry maps the
   Hylian source name to the C ABI symbol name. */
typedef struct { const char *src; const char *abi; } ExactRename;
static const ExactRename EXACT_RENAMES[] = {
  /* std.io */
  { "int_to_str",  "hylian_int_to_str"  },
  { "str_to_int",  "hylian_str_to_int"  },
  { "read_line",   "hylian_read_line"   },
  /* std.kernel */
  { "vga_clear",     "hylian_vga_clear"     },
  { "vga_set_color", "hylian_vga_set_color" },
  { "outb",          "hylian_outb"          },
  { "inb",           "hylian_inb"           },
  { "halt",          "hylian_halt"          },
  /* std.kernel memory map */
  { "hhdm_offset",   "hylian_hhdm_offset"   },
  { "memmap_count",  "hylian_memmap_count"  },
  { "memmap_base",   "hylian_memmap_base"   },
  { "memmap_len",    "hylian_memmap_len"    },
  { "memmap_type",   "hylian_memmap_type"   },
};
static const int EXACT_RENAMES_N =
    (int)(sizeof(EXACT_RENAMES) / sizeof(EXACT_RENAMES[0]));

static const char *rewrite_call_name(const char *name) {
  static char buf[256];
  /* Check exact-name rewrites first */
  for (int i = 0; i < EXACT_RENAMES_N; i++) {
    if (strcmp(name, EXACT_RENAMES[i].src) == 0)
      return EXACT_RENAMES[i].abi;
  }
  /* Then prefix-based rewrites */
  for (int i = 0; i < fn_prefix_count; i++) {
    size_t plen = strlen(fn_prefixes[i].src_prefix);
    if (strncmp(name, fn_prefixes[i].src_prefix, plen) == 0) {
      snprintf(buf, sizeof(buf), "%s%s", fn_prefixes[i].abi_prefix,
               name + plen);
      return buf;
    }
  }
  return name;
}

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

/* True when targeting the Windows x64 ABI */
static int is_win64(void) {
  return strcmp(current_target, "windows") == 0;
}

/* True when the target requires 16-byte stack alignment before calls
   (all hosted targets — Linux, macOS, Windows). Freestanding/kernel
   targets that never call C runtime functions don't need this. */
static int needs_call_alignment(void) {
  return !current_freestanding;
}

/* Argument registers for the active ABI.
   SysV AMD64: rdi rsi rdx rcx r8 r9
   Win64:      rcx rdx r8  r9  (only 4 register args; rest on stack) */
static const char *arg_reg(int i) {
  if (is_win64()) {
    static const char *win_regs[] = {"rcx", "rdx", "r8", "r9"};
    return (i < 4) ? win_regs[i] : NULL;
  }
  static const char *sysv_regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
  return (i < 6) ? sysv_regs[i] : NULL;
}
static int max_reg_args(void) { return is_win64() ? 4 : 6; }


#define MAX_STR_CONSTS 1024
typedef struct {
  char *value;
  char *label;
} StrConst;
static StrConst str_consts[MAX_STR_CONSTS];
static int str_const_count = 0;

#define MAX_FLOAT_CONSTS 512
typedef struct {
  double value;
  char   label[32];
} FloatConst;
static FloatConst float_consts[MAX_FLOAT_CONSTS];
static int float_const_count = 0;

static const char *register_float(double value) {
  for (int i = 0; i < float_const_count; i++) {
    if (float_consts[i].value == value)
      return float_consts[i].label;
  }
  if (float_const_count >= MAX_FLOAT_CONSTS)
    return "_flt_overflow";
  FloatConst *fc = &float_consts[float_const_count++];
  fc->value = value;
  snprintf(fc->label, sizeof(fc->label), "_flt%d", float_const_count - 1);
  return fc->label;
}
static int _label_counter = 0;

static int next_label(void) { return _label_counter++; }

static char *nasm_escape_string(const char *s) {
  size_t len = strlen(s);
  char *buf = malloc(len * 6 + 16);
  char *p = buf;
  int in_quotes = 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c >= 0x20 && c != '"' && c != '\\') {
      if (!in_quotes) {
        if (p != buf)
          *p++ = ',';
        *p++ = '"';
        in_quotes = 1;
      }
      *p++ = (char)c;
    } else {
      if (in_quotes) {
        *p++ = '"';
        in_quotes = 0;
      }
      if (p != buf)
        *p++ = ',';
      p += sprintf(p, "0x%02x", c);
    }
  }
  if (in_quotes)
    *p++ = '"';
  *p = '\0';
  if (p == buf)
    strcpy(buf, "0x00");
  return buf;
}

static const char *register_string(const char *value) {
  /* value should already be unquoted */
  if (!value)
    value = "";
  for (int i = 0; i < str_const_count; i++)
    if (strcmp(str_consts[i].value, value) == 0)
      return str_consts[i].label;
  if (str_const_count >= MAX_STR_CONSTS)
    return "_str_overflow";
  char *lbl = malloc(32);
  snprintf(lbl, 32, "_str%d", str_const_count);
  str_consts[str_const_count].value = strdup(value);
  str_consts[str_const_count].label = lbl;
  str_const_count++;
  return lbl;
}

static size_t string_const_len(const char *value) {
  return value ? strlen(value) : 0;
}


#define MAX_CLASSES 512
#define MAX_FIELDS 64
typedef struct {
  char *name;
  int offset;
  int width;
  char *type_name;
} FieldInfo;
typedef struct {
  char *name;
  FieldInfo fields[MAX_FIELDS];
  int field_count;
  int size;
  int has_ctor;
} ClassInfo;
static ClassInfo class_registry[MAX_CLASSES];
static int class_count = 0;

static void class_registry_reset(void) { class_count = 0; }

static ClassInfo *class_find(const char *name) {
  if (!name)
    return NULL;
  for (int i = 0; i < class_count; i++)
    if (strcmp(class_registry[i].name, name) == 0)
      return &class_registry[i];
  return NULL;
}

static int field_byte_width(const char *type_name) {
  if (!type_name) return 8;
  if (strcmp(type_name, "int8")   == 0 || strcmp(type_name, "uint8")  == 0) return 1;
  if (strcmp(type_name, "int16")  == 0 || strcmp(type_name, "uint16") == 0) return 2;
  if (strcmp(type_name, "int32")  == 0 || strcmp(type_name, "uint32") == 0 ||
      strcmp(type_name, "float32") == 0) return 4;
  /* explicit 8-byte aliases */
  if (strcmp(type_name, "usize") == 0 || strcmp(type_name, "isize") == 0) return 8;
  if (strcmp(type_name, "int64") == 0 || strcmp(type_name, "uint64") == 0) return 8;
  /* 8-byte types: int64, uint64, int, float, float64, ptr, rawptr, str, and anything else */
  return 8;
}

/* Compute the total byte width of a struct/union field, accounting for
   fixed-size array types (e.g. uint8[56] → 1 * 56 = 56 bytes). */
static int field_total_byte_width(FieldNode *f) {
  if (f->field_type.kind == TYPE_ARRAY && f->field_type.fixed_size > 0 &&
      f->field_type.elem_type_count > 0 && f->field_type.elem_types) {
    const char *elem_name = f->field_type.elem_types[0].name;
    int elem_w = field_byte_width(elem_name);
    ClassInfo *nested = class_find(elem_name);
    if (nested) elem_w = nested->size;
    return elem_w * f->field_type.fixed_size;
  }
  const char *tname = f->field_type.name ? f->field_type.name : "int";
  int w = field_byte_width(tname);
  ClassInfo *nested = class_find(tname);
  if (nested) w = nested->size;
  return w;
}

static ClassInfo *class_register(ClassNode *cls) {
  if (class_count >= MAX_CLASSES)
    return NULL;
  ClassInfo *ci = &class_registry[class_count++];
  ci->name = cls->name;
  ci->field_count = 0;
  ci->has_ctor = cls->has_ctor;
  int is_packed = cls->is_packed;
  int is_union  = cls->is_union;
  if (is_union) {
    /* Union: all fields share offset 0; size = max field width */
    int max_w = 0;
    for (int i = 0; i < cls->field_count && i < MAX_FIELDS; i++) {
      FieldNode *f = cls->fields[i];
      const char *tname = f->field_type.name ? f->field_type.name : "int";
      int w = field_total_byte_width(f);
      ci->fields[ci->field_count].name = f->name;
      ci->fields[ci->field_count].offset = 0; /* all at 0 — union semantics */
      ci->fields[ci->field_count].width = w;
      ci->fields[ci->field_count].type_name = (char *)tname;
      ci->field_count++;
      if (w > max_w) max_w = w;
    }
    ci->size = max_w ? max_w : 8;
    if (ci->size % 8 != 0) ci->size += 8 - ci->size % 8;
  } else {
    int offset = 0;
    for (int i = 0; i < cls->field_count && i < MAX_FIELDS; i++) {
      FieldNode *f = cls->fields[i];
      const char *tname = f->field_type.name ? f->field_type.name : "int";
      int w = field_total_byte_width(f);
      ci->fields[ci->field_count].name = f->name;
      ci->fields[ci->field_count].offset = offset;
      ci->fields[ci->field_count].width = w;
      ci->fields[ci->field_count].type_name = (char *)tname;
      ci->field_count++;
      offset += w;
    }
    ci->size = offset ? offset : 8;
    /* For packed classes: use sequential byte offsets with no alignment rounding */
    if (!is_packed) {
      if (ci->size % 16 != 0)
        ci->size += 16 - ci->size % 16;
    }
  }
  return ci;
}

static int class_field_offset(ClassInfo *ci, const char *field_name) {
  for (int i = 0; i < ci->field_count; i++)
    if (strcmp(ci->fields[i].name, field_name) == 0)
      return ci->fields[i].offset;
  return -1;
}

static int class_field_width(ClassInfo *ci, const char *field_name) {
  for (int i = 0; i < ci->field_count; i++)
    if (strcmp(ci->fields[i].name, field_name) == 0)
      return ci->fields[i].width;
  return 8;
}


#define MAX_ENUMS 256
#define MAX_VARIANTS 64
typedef struct {
  char *variant_name;
  int value;
} EnumVariantInfo;
typedef struct {
  char *name;
  EnumVariantInfo variants[MAX_VARIANTS];
  int variant_count;
} EnumRegEntry;
static EnumRegEntry enum_registry[MAX_ENUMS];
static int enum_count = 0;

static void enum_registry_reset(void) { enum_count = 0; }

static EnumRegEntry *enum_find(const char *name) {
  for (int i = 0; i < enum_count; i++)
    if (strcmp(enum_registry[i].name, name) == 0)
      return &enum_registry[i];
  return NULL;
}

static void enum_register(EnumNode *en) {
  if (enum_count >= MAX_ENUMS)
    return;
  EnumRegEntry *ei = &enum_registry[enum_count++];
  ei->name = en->name;
  ei->variant_count = 0;
  for (int i = 0; i < en->variant_count && i < MAX_VARIANTS; i++) {
    ei->variants[i].variant_name = en->variants[i].name;
    ei->variants[i].value = en->variants[i].value;
    ei->variant_count++;
  }
}

static int enum_variant_value(const char *ename, const char *variant) {
  EnumRegEntry *ei = enum_find(ename);
  if (!ei)
    return -1;
  for (int i = 0; i < ei->variant_count; i++)
    if (strcmp(ei->variants[i].variant_name, variant) == 0)
      return ei->variants[i].value;
  return -1;
}


#define MAX_NAMED_SLOTS 512
#define MAX_TEMP_SLOTS 4096

typedef struct {
  const char *name;
  const char *type_name;
  int rbp_offset;
} NamedSlot;
static NamedSlot named_slots[MAX_NAMED_SLOTS];
static int named_count;

/* temp_slots is indexed directly by temp_id */
static int temp_slot_offset[MAX_TEMP_SLOTS]; /* rbp_offset, -1 = unassigned */
static int max_temp_id;
/* Tracks which temps carry a float value (set during prescan). */
static int temp_is_float[MAX_TEMP_SLOTS];

static int frame_bytes;
static int interp_buf_offset; /* 0 = no pre-allocated buffer */
/* Pool of fixed 32-byte buffers for int_to_str return values.
   Without this, every int_to_str call did its own `sub rsp, 32` that was
   never reclaimed until function return — in a loop that means O(N) stack
   growth per iteration and eventual SIGSEGV. We now count int_to_str calls
   in prescan, reserve N*32 bytes once in the frame, and hand out slots
   sequentially during codegen. The emission counter is reset per-function. */
#define INT_TO_STR_BUF_BYTES 32
static int int_to_str_total_bufs;     /* count discovered in prescan        */
static int int_to_str_pool_offset;    /* rbp offset of byte 0 of first buf  */
static int int_to_str_next_buf;       /* next buffer index to hand out      */

#define MAX_STATIC_VARS 8192
typedef struct {
  char *name;
  char *type_name;
  long  int_val;
  double float_val; /* used when type is float/float32/float64 */
  int   is_float;   /* 1 if the initializer is a float constant */
  char *str_val;   /* NULL = integer value */
  int   is_const;  /* bit 0 of extra_int */
  int   array_size; /* extra_int >> 1; 0 = not a fixed array */
} StaticVarEntry;
static StaticVarEntry static_vars[MAX_STATIC_VARS];
static int static_var_count = 0;

static void static_var_registry_reset(void) { static_var_count = 0; }

static int static_var_find(const char *name) {
  for (int i = 0; i < static_var_count; i++)
    if (static_vars[i].name && strcmp(static_vars[i].name, name) == 0)
      return i;
  return -1;
}

static int get_named_slot(const char *name) {
  if (!name)
    return -1;
  const char *safe = nasm_safe_label(name);
  for (int i = 0; i < named_count; i++)
    if (named_slots[i].name && strcmp(named_slots[i].name, safe) == 0)
      return named_slots[i].rbp_offset;
  return -1;
}

static const char *get_named_slot_type(const char *name) {
  if (!name)
    return NULL;
  const char *safe = nasm_safe_label(name);
  for (int i = 0; i < named_count; i++)
    if (named_slots[i].name && strcmp(named_slots[i].name, safe) == 0)
      return named_slots[i].type_name;
  return NULL;
}

static int get_temp_slot(int temp_id) {
  if (temp_id >= 0 && temp_id < MAX_TEMP_SLOTS)
    return temp_slot_offset[temp_id];
  return -1;
}


static int is_float_type_name(const char *n) {
  if (!n) return 0;
  return strcmp(n, "float") == 0 || strcmp(n, "float32") == 0 ||
         strcmp(n, "float64") == 0;
}

static void prescan_function(const IRModule *mod, int begin_idx, int end_idx) {
  named_count = 0;
  max_temp_id = -1;
  interp_buf_offset = 0;
  int_to_str_total_bufs = 0;
  int_to_str_pool_offset = 0;
  int_to_str_next_buf = 0;
  /* Clear named_slots so that multi-slot gaps (from structs/unions that
     occupy more than one 8-byte slot) don't contain stale names from a
     previous function's prescan. */
  for (int i = 0; i < MAX_NAMED_SLOTS; i++) {
    named_slots[i].name = NULL;
    named_slots[i].type_name = NULL;
    named_slots[i].rbp_offset = 0;
  }
  for (int i = 0; i < MAX_TEMP_SLOTS; i++) {
    temp_slot_offset[i] = -1;
    temp_is_float[i] = 0;
  }

  int needs_interp = 0;

  for (int i = begin_idx; i <= end_idx && i < mod->instr_count; i++) {
    const IRInstr *ins = &mod->instrs[i];

    if (ins->op == IR_ALLOCA && ins->str_extra &&
        named_count < MAX_NAMED_SLOTS) {
      int base = named_count;
      named_slots[base].name = nasm_safe_label(ins->str_extra);
      named_slots[base].type_name = ins->str_extra2;
      int slots = 1;
      if (ins->str_extra2) {
        ClassInfo *ci = class_find(ins->str_extra2);
        if (ci && !ci->has_ctor)
          slots = (ci->size + 7) / 8;
      }
      named_slots[base].rbp_offset = (base + slots) * 8;
      named_count += slots;
    }

    if (ins->op == IR_INTERP_STR)
      needs_interp = 1;
    if ((ins->op == IR_PRINT || ins->op == IR_PRINTLN) &&
        ins->extra_int == PRINT_ARG_INTERP)
      needs_interp = 1;

    /* Reserve one persistent 32-byte buffer per int_to_str call so the
       returned char* remains valid for the rest of the function without
       leaking stack on each call. */
    if (ins->op == IR_CALL && ins->str_extra &&
        strcmp(ins->str_extra, "int_to_str") == 0)
      int_to_str_total_bufs++;

    /* Track float-typed temps */
    if (ins->dest.kind == IROP_TEMP && ins->dest.temp_id >= 0 &&
        ins->dest.temp_id < MAX_TEMP_SLOTS) {
      int tid = ins->dest.temp_id;
      if (ins->op == IR_CONST_FLOAT) {
        temp_is_float[tid] = 1;
      } else if (ins->op == IR_LOAD_VAR && ins->str_extra) {
        /* If the named slot has a float type, the loaded temp is float */
        const char *vtype = get_named_slot_type(ins->str_extra);
        if (is_float_type_name(vtype)) temp_is_float[tid] = 1;
      } else if ((ins->op == IR_ADD || ins->op == IR_SUB ||
                  ins->op == IR_MUL || ins->op == IR_DIV) &&
                 ((ins->src1.kind == IROP_TEMP && ins->src1.temp_id >= 0 &&
                   ins->src1.temp_id < MAX_TEMP_SLOTS &&
                   temp_is_float[ins->src1.temp_id]) ||
                  (ins->src2.kind == IROP_TEMP && ins->src2.temp_id >= 0 &&
                   ins->src2.temp_id < MAX_TEMP_SLOTS &&
                   temp_is_float[ins->src2.temp_id]) ||
                  ins->src1.kind == IROP_CONST_FLOAT ||
                  ins->src2.kind == IROP_CONST_FLOAT)) {
        temp_is_float[tid] = 1;
      }
    }

    /* Find max temp id */
    if (ins->dest.kind == IROP_TEMP && ins->dest.temp_id > max_temp_id)
      max_temp_id = ins->dest.temp_id;
    if (ins->src1.kind == IROP_TEMP && ins->src1.temp_id > max_temp_id)
      max_temp_id = ins->src1.temp_id;
    if (ins->src2.kind == IROP_TEMP && ins->src2.temp_id > max_temp_id)
      max_temp_id = ins->src2.temp_id;
    if (ins->extra_src.kind == IROP_TEMP &&
        ins->extra_src.temp_id > max_temp_id)
      max_temp_id = ins->extra_src.temp_id;
    for (int j = 0; j < ins->arg_count; j++)
      if (ins->args[j].kind == IROP_TEMP && ins->args[j].temp_id > max_temp_id)
        max_temp_id = ins->args[j].temp_id;
  }

  /* Assign temp slot offsets — after all named slots */
  int base_offset = named_count * 8;
  for (int t = 0; t <= max_temp_id && t < MAX_TEMP_SLOTS; t++)
    temp_slot_offset[t] = base_offset + (t + 1) * 8;

  /* Interp buffer: placed after all named+temp slots */
  int slot_bytes =
      named_count * 8 + (max_temp_id >= 0 ? (max_temp_id + 1) * 8 : 0);
  if (needs_interp) {
    interp_buf_offset = slot_bytes + 512; /* buf at [rbp - interp_buf_offset] */
    frame_bytes = interp_buf_offset;
  } else {
    frame_bytes = slot_bytes;
  }
  /* int_to_str buffer pool: N * 32-byte slots, placed after interp buffer.
     pool_offset is the rbp-relative offset of byte 0 of the FIRST buffer
     (i.e. lowest address); buffer i lives at [rbp - (pool_offset - i*32)]. */
  if (int_to_str_total_bufs > 0) {
    int_to_str_pool_offset = frame_bytes + int_to_str_total_bufs * INT_TO_STR_BUF_BYTES;
    frame_bytes = int_to_str_pool_offset;
  }
  frame_bytes = (frame_bytes + 15) & ~15;
  if (frame_bytes < 16)
    frame_bytes = 16;
}


static void load_operand_reg(const IROperand *op, const char *reg, FILE *out) {
  switch (op->kind) {
  case IROP_TEMP: {
    int off = get_temp_slot(op->temp_id);
    if (off > 0)
      fprintf(out, "    mov %s, [rbp - %d]\n", reg, off);
    else
      fprintf(out, "    xor %s, %s\n", reg, reg);
    /* Note: float temps store raw IEEE 754 bits in the GP slot; callers that
       need xmm arithmetic do movq xmm, reg themselves after this load. */
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
    if (op->bool_val)
      fprintf(out, "    mov %s, 1\n", reg);
    else
      fprintf(out, "    xor %s, %s\n", reg, reg);
    break;
  case IROP_CONST_FLOAT: {
    /* Load float constant via .data label into xmm0, then movq to GP reg */
    const char *flbl = register_float(op->float_val);
    fprintf(out, "    movsd xmm0, [rel %s]\n", flbl);
    fprintf(out, "    movq %s, xmm0\n", reg);
    break;
  }
  case IROP_NONE:
    fprintf(out, "    xor %s, %s\n", reg, reg);
    break;
  default:
    break;
  }
}

static void store_dest_rax(const IROperand *dest, FILE *out) {
  if (dest->kind == IROP_TEMP) {
    int off = get_temp_slot(dest->temp_id);
    if (off > 0)
      fprintf(out, "    mov [rbp - %d], rax\n", off);
  }
}


static void emit_interp_segments(const InterpSegment *segs, int seg_count,
                                 FILE *out) {
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
        fprintf(out, "    ; interp expr: %s (%s)\n", varname,
                vtype ? vtype : "int");
        fprintf(out, "    mov rax, [rbp - %d]\n", off);
        if (is_str) {
          fprintf(out, "    push r14\n");
            /* strlen(ptr): arg0 = ptr */
            fprintf(out, "    mov %s, rax\n", arg_reg(0));
            if (is_win64()) fprintf(out, "    sub rsp, 32\n");
            fprintf(out, "    call %s\n", sym("strlen"));
            if (is_win64()) fprintf(out, "    add rsp, 32\n");
            fprintf(out, "    mov rcx, rax\n");
            fprintf(out, "    pop r14\n");
            fprintf(out, "    mov rsi, [rbp - %d]\n", off);
          fprintf(out, "    mov rdi, r14\n");
          fprintf(out, "    rep movsb\n");
          fprintf(out, "    mov r14, rdi\n");
        } else {
          fprintf(out, "    push r14\n");
          fprintf(out, "    sub rsp, 32\n");
          /* hylian_int_to_str(val, buf, bufsz) */
          fprintf(out, "    mov %s, rax\n", arg_reg(0));
          fprintf(out, "    mov %s, rsp\n", arg_reg(1));
          fprintf(out, "    mov %s, 32\n",  arg_reg(2));
          if (is_win64()) fprintf(out, "    sub rsp, 32\n");
          fprintf(out, "    call %s\n", sym("hylian_int_to_str"));
          if (is_win64()) fprintf(out, "    add rsp, 32\n");
          fprintf(out, "    mov rcx, rax\n"); /* length */
          fprintf(out, "    mov rsi, rsp\n"); /* buf (rep movsb src) */
          fprintf(out, "    add rsp, 32\n");
          fprintf(out, "    pop r14\n");
          fprintf(out, "    mov rdi, r14\n");
          fprintf(out, "    rep movsb\n");
          fprintf(out, "    mov r14, rdi\n");
        }
      } else {
        /* Not a local — fall back to static/global variables */
        int svi = static_var_find(varname);
        if (svi >= 0) {
          const char *vtype = static_vars[svi].type_name;
          int sv_is_float = static_vars[svi].is_float || is_float_type_name(vtype);
          int is_str = vtype && strcmp(vtype, "str") == 0;
          fprintf(out, "    ; interp expr (global): %s (%s)\n", varname,
                  vtype ? vtype : "int");
          if (static_vars[svi].is_const && !sv_is_float && !is_str)
            fprintf(out, "    mov rax, %s\n", nasm_safe_label(varname));
          else if (sv_is_float) {
            fprintf(out, "    movsd xmm0, [rel %s]\n", nasm_safe_label(varname));
            fprintf(out, "    movq rax, xmm0\n");
          } else
            fprintf(out, "    mov rax, [rel %s]\n", nasm_safe_label(varname));
          if (is_str) {
            fprintf(out, "    push r14\n");
            fprintf(out, "    mov %s, rax\n", arg_reg(0));
            if (is_win64()) fprintf(out, "    sub rsp, 32\n");
            fprintf(out, "    call %s\n", sym("strlen"));
            if (is_win64()) fprintf(out, "    add rsp, 32\n");
            fprintf(out, "    mov rcx, rax\n");
            fprintf(out, "    pop r14\n");
            fprintf(out, "    mov rsi, [rel %s]\n", nasm_safe_label(varname));
            fprintf(out, "    mov rdi, r14\n");
            fprintf(out, "    rep movsb\n");
            fprintf(out, "    mov r14, rdi\n");
          } else if (sv_is_float) {
            fprintf(out, "    push r14\n");
            fprintf(out, "    sub rsp, 32\n");
            fprintf(out, "    movq xmm0, rax\n");
            fprintf(out, "    mov %s, rsp\n", arg_reg(0));
            fprintf(out, "    mov %s, 32\n",  arg_reg(1));
            if (is_win64()) fprintf(out, "    sub rsp, 32\n");
            fprintf(out, "    call %s\n", sym("hylian_float_to_str"));
            if (is_win64()) fprintf(out, "    add rsp, 32\n");
            fprintf(out, "    mov rcx, rax\n");
            fprintf(out, "    mov rsi, rsp\n");
            fprintf(out, "    add rsp, 32\n");
            fprintf(out, "    pop r14\n");
            fprintf(out, "    mov rdi, r14\n");
            fprintf(out, "    rep movsb\n");
            fprintf(out, "    mov r14, rdi\n");
          } else {
            fprintf(out, "    push r14\n");
            fprintf(out, "    sub rsp, 32\n");
            fprintf(out, "    mov %s, rax\n", arg_reg(0));
            fprintf(out, "    mov %s, rsp\n", arg_reg(1));
            fprintf(out, "    mov %s, 32\n",  arg_reg(2));
            if (is_win64()) fprintf(out, "    sub rsp, 32\n");
            fprintf(out, "    call %s\n", sym("hylian_int_to_str"));
            if (is_win64()) fprintf(out, "    add rsp, 32\n");
            fprintf(out, "    mov rcx, rax\n");
            fprintf(out, "    mov rsi, rsp\n");
            fprintf(out, "    add rsp, 32\n");
            fprintf(out, "    pop r14\n");
            fprintf(out, "    mov rdi, r14\n");
            fprintf(out, "    rep movsb\n");
            fprintf(out, "    mov r14, rdi\n");
          }
        } else {
          fprintf(out, "    ; interp expr '%s' not in scope — skipped\n",
                  varname);
        }
      }
    }
  }
  fprintf(out, "    mov byte [r14], 0\n"); /* null-terminate */
}


static void emit_ir_instr(const IRInstr *ins, FILE *out) {

  switch (ins->op) {

  case IR_NOP:
    break;

  case IR_ALLOCA:
    /* Slot already assigned during prescan; just emit a zero store for safety
     */
    {
      int off = get_named_slot(ins->str_extra);
      if (off > 0)
        fprintf(out, "    mov qword [rbp - %d], 0\n", off);
    }
    break;

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

  case IR_CONST_FLOAT: {
    const char *flbl = register_float(ins->src1.float_val);
    fprintf(out, "    movsd xmm0, [rel %s]\n", flbl);
    fprintf(out, "    movq rax, xmm0\n");
    store_dest_rax(&ins->dest, out);
    break;
  }

  case IR_LOAD_VAR: {
    if (!ins->str_extra) {
      fprintf(out, "    xor rax, rax\n");
      store_dest_rax(&ins->dest, out);
      break;
    }
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
      /* check static globals */
      {
        int svi = static_var_find(ins->str_extra);
        if (svi >= 0) {
          int sv_is_float = static_vars[svi].is_float ||
                            is_float_type_name(static_vars[svi].type_name);
          if (static_vars[svi].is_const && !sv_is_float)
            fprintf(out, "    mov rax, %s\n", nasm_safe_label(ins->str_extra));
          else if (sv_is_float) {
            /* Load float static/const via movsd so the bit pattern is preserved */
            fprintf(out, "    movsd xmm0, [rel %s]\n", nasm_safe_label(ins->str_extra));
            fprintf(out, "    movq rax, xmm0\n");
          } else
            fprintf(out, "    mov rax, [rel %s]\n", nasm_safe_label(ins->str_extra));
          store_dest_rax(&ins->dest, out);
          break;
        }
      }
      fprintf(out, "    ; LOAD_VAR '%s' not found\n", ins->str_extra);
      fprintf(out, "    xor rax, rax\n");
    } else {
      int svi = static_var_find(ins->str_extra);
      if (svi >= 0) {
        int sv_is_float = static_vars[svi].is_float ||
                          is_float_type_name(static_vars[svi].type_name);
        if (static_vars[svi].is_const && !sv_is_float)
          fprintf(out, "    mov rax, %s\n", nasm_safe_label(ins->str_extra));
        else if (sv_is_float) {
          fprintf(out, "    movsd xmm0, [rel %s]\n", nasm_safe_label(ins->str_extra));
          fprintf(out, "    movq rax, xmm0\n");
        } else
          fprintf(out, "    mov rax, [rel %s]\n", nasm_safe_label(ins->str_extra));
        store_dest_rax(&ins->dest, out);
        break;
      }
      fprintf(out, "    ; LOAD_VAR '%s' not found\n", ins->str_extra);
      fprintf(out, "    xor rax, rax\n");
    }
    store_dest_rax(&ins->dest, out);
    break;
  }

  case IR_STORE_VAR: {
    if (!ins->str_extra)
      break;
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
      /* check static globals */
      {
        int svi = static_var_find(ins->str_extra);
        if (svi >= 0) {
          if (!static_vars[svi].is_const)
            fprintf(out, "    mov [rel %s], rax\n", nasm_safe_label(ins->str_extra));
          break;
        }
      }
      fprintf(out, "    ; STORE_VAR '%s' not found\n", ins->str_extra);
    } else {
      int svi = static_var_find(ins->str_extra);
      if (svi >= 0) {
        if (!static_vars[svi].is_const)
          fprintf(out, "    mov [rel %s], rax\n", nasm_safe_label(ins->str_extra));
        break;
      }
      fprintf(out, "    ; STORE_VAR '%s' not found\n", ins->str_extra);
    }
    break;
  }
  case IR_ADDROF: {
      int off = ins->str_extra ? get_named_slot(ins->str_extra) : -1;
      if (off > 0)
        fprintf(out, "    lea rax, [rbp - %d]\n", off);
      else if (ins->str_extra)
        fprintf(out, "    lea rax, [rel %s]\n", nasm_safe_label(ins->str_extra));
      else
        fprintf(out, "    xor rax, rax\n");
      store_dest_rax(&ins->dest, out);
    } break;
  case IR_ADDROF_FN:
      if (ins->str_extra)
          fprintf(out, "    lea rax, [rel %s]\n", ins->str_extra);
      else
          fprintf(out, "    xor rax, rax\n");
      store_dest_rax(&ins->dest, out);
      break;
  case IR_LOAD_PTR:
    load_operand_reg(&ins->src1, "rax", out);
    fprintf(out, "    mov rax, [rax]\n");
    store_dest_rax(&ins->dest, out);
    break;

  case IR_STORE_PTR:
    load_operand_reg(&ins->src1, "rcx", out);
    load_operand_reg(&ins->src2, "rax", out);
    if (ins->str_extra &&
        (strcmp(ins->str_extra, "uint8") == 0 || strcmp(ins->str_extra, "int8") == 0))
        fprintf(out, "    mov byte [rcx], al\n");
    else if (ins->str_extra &&
             (strcmp(ins->str_extra, "uint16") == 0 || strcmp(ins->str_extra, "int16") == 0))
        fprintf(out, "    mov word [rcx], ax\n");
    else if (ins->str_extra &&
             (strcmp(ins->str_extra, "uint32") == 0 || strcmp(ins->str_extra, "int32") == 0))
        fprintf(out, "    mov dword [rcx], eax\n");
    else
        fprintf(out, "    mov [rcx], rax\n");
    break;

  case IR_READ_CR:
    fprintf(out, "    mov rax, cr%d\n", ins->extra_int);
    store_dest_rax(&ins->dest, out);
    break;

  case IR_WRITE_CR:
    load_operand_reg(&ins->src1, "rax", out);
    fprintf(out, "    mov cr%d, rax\n", ins->extra_int);
    break;

  case IR_SAVE_REGS:
    /* Push a dummy zero error-code slot if this ISR doesn't get one from the CPU */
    if (ins->extra_int)
        fprintf(out, "    push 0\n");
    fprintf(out,
        "    push rax\n"
        "    push rbx\n"
        "    push rcx\n"
        "    push rdx\n"
        "    push rsi\n"
        "    push rdi\n"
        "    push rbp\n"
        "    push r8\n"
        "    push r9\n"
        "    push r10\n"
        "    push r11\n"
        "    push r12\n"
        "    push r13\n"
        "    push r14\n"
        "    push r15\n"
    );
    break;

  case IR_RESTORE_REGS:
    /* Discard the error-code slot before popping GPRs if requested */
    if (ins->extra_int)
        fprintf(out, "    add rsp, 8\n");
    fprintf(out,
        "    pop r15\n"
        "    pop r14\n"
        "    pop r13\n"
        "    pop r12\n"
        "    pop r11\n"
        "    pop r10\n"
        "    pop r9\n"
        "    pop r8\n"
        "    pop rbp\n"
        "    pop rdi\n"
        "    pop rsi\n"
        "    pop rdx\n"
        "    pop rcx\n"
        "    pop rbx\n"
        "    pop rax\n"
    );
    break;

  case IR_IRET:
    fprintf(out, "    iretq\n");
    break;

  case IR_OUTB:
    load_operand_reg(&ins->src1, "rdx", out);  /* port */
    load_operand_reg(&ins->src2, "rax", out);  /* value */
    fprintf(out, "    out dx, al\n");
    break;

  case IR_INB:
    load_operand_reg(&ins->src1, "rdx", out);  /* port */
    fprintf(out, "    xor rax, rax\n");
    fprintf(out, "    in al, dx\n");
    store_dest_rax(&ins->dest, out);
    break;

  case IR_LOAD_VOLATILE: ;; /* volatile read */
    load_operand_reg(&ins->src1, "rax", out);
    fprintf(out, "    mov rax, [rax]    ;; volatile\n");
    store_dest_rax(&ins->dest, out);
    break;

  case IR_STORE_VOLATILE: ;; /* volatile write */
    load_operand_reg(&ins->src1, "rcx", out);
    load_operand_reg(&ins->src2, "rax", out);
    fprintf(out, "    mov [rcx], rax    ;; volatile\n");
    break;

  case IR_STATIC_VAR:
    /* Static vars are emitted in the .data section; nothing to do here */
    break;
#define TEMP_IS_FLOAT(op) \
    ((op).kind == IROP_CONST_FLOAT || \
     ((op).kind == IROP_TEMP && (op).temp_id >= 0 && \
      (op).temp_id < MAX_TEMP_SLOTS && temp_is_float[(op).temp_id]))
#define OPERANDS_ARE_FLOAT(i) (TEMP_IS_FLOAT((i)->src1) || TEMP_IS_FLOAT((i)->src2))

  case IR_ADD:
    if (OPERANDS_ARE_FLOAT(ins)) {
      load_operand_reg(&ins->src1, "rax", out);
      fprintf(out, "    movq xmm0, rax\n");
      load_operand_reg(&ins->src2, "rax", out);
      fprintf(out, "    movq xmm1, rax\n");
      fprintf(out, "    addsd xmm0, xmm1\n");
      fprintf(out, "    movq rax, xmm0\n");
    } else {
      load_operand_reg(&ins->src1, "rax", out);
      fprintf(out, "    push rax\n");
      load_operand_reg(&ins->src2, "rax", out);
      fprintf(out, "    pop rcx\n");
      fprintf(out, "    add rax, rcx\n");
    }
    store_dest_rax(&ins->dest, out);
    break;

  case IR_SUB:
    if (OPERANDS_ARE_FLOAT(ins)) {
      load_operand_reg(&ins->src1, "rax", out);
      fprintf(out, "    movq xmm0, rax\n");
      load_operand_reg(&ins->src2, "rax", out);
      fprintf(out, "    movq xmm1, rax\n");
      fprintf(out, "    subsd xmm0, xmm1\n");
      fprintf(out, "    movq rax, xmm0\n");
    } else {
      load_operand_reg(&ins->src1, "rcx", out);
      load_operand_reg(&ins->src2, "rax", out);
      fprintf(out, "    sub rcx, rax\n");
      fprintf(out, "    mov rax, rcx\n");
    }
    store_dest_rax(&ins->dest, out);
    break;

  case IR_MUL:
    if (OPERANDS_ARE_FLOAT(ins)) {
      load_operand_reg(&ins->src1, "rax", out);
      fprintf(out, "    movq xmm0, rax\n");
      load_operand_reg(&ins->src2, "rax", out);
      fprintf(out, "    movq xmm1, rax\n");
      fprintf(out, "    mulsd xmm0, xmm1\n");
      fprintf(out, "    movq rax, xmm0\n");
    } else {
      load_operand_reg(&ins->src1, "rax", out);
      fprintf(out, "    push rax\n");
      load_operand_reg(&ins->src2, "rax", out);
      fprintf(out, "    pop rcx\n");
      fprintf(out, "    imul rax, rcx\n");
    }
    store_dest_rax(&ins->dest, out);
    break;

  case IR_DIV:
    if (OPERANDS_ARE_FLOAT(ins)) {
      load_operand_reg(&ins->src1, "rax", out);
      fprintf(out, "    movq xmm0, rax\n");
      load_operand_reg(&ins->src2, "rax", out);
      fprintf(out, "    movq xmm1, rax\n");
      fprintf(out, "    divsd xmm0, xmm1\n");
      fprintf(out, "    movq rax, xmm0\n");
    } else {
      load_operand_reg(&ins->src1, "rax", out);
      fprintf(out, "    push rax\n");
      load_operand_reg(&ins->src2, "rax", out);
      fprintf(out, "    mov rcx, rax\n");
      fprintf(out, "    pop rax\n");
      fprintf(out, "    cqo\n");
      fprintf(out, "    idiv rcx\n");
    }
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

  case IR_EQ:
  case IR_NEQ:
  case IR_LT:
  case IR_LE:
  case IR_GT:
  case IR_GE: {
    load_operand_reg(&ins->src1, "rax", out);
    fprintf(out, "    push rax\n");
    load_operand_reg(&ins->src2, "rax", out);
    fprintf(out, "    pop rcx\n");
    fprintf(out, "    cmp rcx, rax\n");
    const char *setcc = ins->op == IR_EQ    ? "sete"
                        : ins->op == IR_NEQ ? "setne"
                        : ins->op == IR_LT  ? "setl"
                        : ins->op == IR_LE  ? "setle"
                        : ins->op == IR_GT  ? "setg"
                                            : "setge";
    fprintf(out, "    %s al\n", setcc);
    fprintf(out, "    movzx rax, al\n");
    store_dest_rax(&ins->dest, out);
    break;
  }

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

  case IR_CALL: {
    /* Special case: int_to_str(n) -> str
       The C ABI function hylian_int_to_str(n, buf, buflen) writes into a
       caller-supplied buffer and returns the length. We hand it one slot
       from the per-function buffer pool reserved in the prologue, then
       return the buffer's address as a stable null-terminated str. The
       slot lives until function exit (reclaimed by `mov rsp, rbp`).
       Previously every call did its own `sub rsp, 32` that was never
       reclaimed until function return — a loop calling int_to_str N times
       would leak 32*N bytes of stack and eventually crash. */
    if (ins->str_extra && strcmp(ins->str_extra, "int_to_str") == 0) {
      int buf_idx = int_to_str_next_buf++;
      /* Buffer i lives at [rbp - (pool_offset - i*32)]; lowest address is
         the FIRST buffer (offset == pool_offset), highest is the last.
         Use lea to get a stable pointer. */
      int buf_off = int_to_str_pool_offset - buf_idx * INT_TO_STR_BUF_BYTES;
      load_operand_reg(&ins->args[0], "rax", out);
      fprintf(out, "    lea r11, [rbp - %d]\n", buf_off);       /* r11 = buf */
      fprintf(out, "    mov %s, rax\n", arg_reg(0));            /* arg0 = n  */
      fprintf(out, "    mov %s, r11\n", arg_reg(1));            /* arg1 = buf */
      fprintf(out, "    mov %s, 31\n",  arg_reg(2));            /* arg2 = len */
      if (is_win64()) fprintf(out, "    sub rsp, 32\n");
      fprintf(out, "    call %s\n", sym("hylian_int_to_str"));
      if (is_win64()) fprintf(out, "    add rsp, 32\n");
      /* rax = length written; null-terminate at buf[rax] */
      fprintf(out, "    lea r11, [rbp - %d]\n", buf_off);
      fprintf(out, "    mov byte [r11 + rax], 0\n");
      fprintf(out, "    mov rax, r11\n");                       /* result = buf ptr */
      store_dest_rax(&ins->dest, out);
      break;
    }

    int nreg = max_reg_args();
    int nargs = ins->arg_count;
    int reg_args = nargs < nreg ? nargs : nreg;
    int stack_args = nargs > nreg ? nargs - nreg : 0;

    /* Win64: 32-byte shadow space + stack args must be pushed before
       aligning; SysV: no shadow space, stack args pushed right-to-left. */
    if (is_win64()) {
      /* Push stack args right-to-left (beyond the 4 register args) */
      for (int i = nargs - 1; i >= nreg; i--) {
        load_operand_reg(&ins->args[i], "rax", out);
        fprintf(out, "    push rax\n");
      }
      /* Ensure 16-byte alignment: account for the 8-byte return address that
         `call` will push, the shadow space (32), and any stack args we pushed. */
      int pushed = stack_args * 8;   /* bytes we just pushed for stack args */
      /* After `call` pushes ret addr: total extra = pushed + 32 shadow + 8 ret
         must be 0 mod 16.  We need (pushed + 32 + 8 + pad) % 16 == 0. */
      int pad = (16 - ((pushed + 40) % 16)) % 16;
      if (pad) fprintf(out, "    sub rsp, %d  ; align\n", pad);
      /* Allocate shadow space */
      fprintf(out, "    sub rsp, 32  ; shadow space\n");
      /* Load register args */
      for (int i = reg_args - 1; i >= 0; i--) {
        load_operand_reg(&ins->args[i], "rax", out);
        fprintf(out, "    push rax\n");
      }
      for (int i = 0; i < reg_args; i++)
        fprintf(out, "    pop %s\n", arg_reg(i));
      const char *cname = ins->str_extra ? rewrite_call_name(ins->str_extra) : "unknown";
      fprintf(out, "    call %s\n", sym(cname));
      /* Tear down shadow space + alignment pad + stack args */
      int cleanup = 32 + pad + stack_args * 8;
      fprintf(out, "    add rsp, %d\n", cleanup);
    } else {
      /* SysV AMD64:
         1. Compute alignment pad based on total bytes that will be on the
            stack when `call` executes (stack_args * 8 + pad + 8 ret addr).
         2. Emit the pad FIRST so the register-arg pushes sit on top of it.
         3. Push register args in order 0..reg_args-1.
         4. Push stack args right-to-left (above the reg-arg pushes).
         5. Pop register args back into their ABI registers.
         6. call; then clean up stack_args + pad. */
      int pad = 0;
      if (needs_call_alignment()) {
        /* The System V ABI requires RSP to be 16-byte aligned at the point
           of a `call` instruction (before the call pushes the return address).
           frame_RSP is 0 mod 16 (push rbp + 16-byte-aligned frame_bytes).
           At call time only `pad` and `stack_args` remain on the stack
           (reg args are pushed then popped before the call), so:
             (stack_args * 8 + pad) % 16 == 0  */
        pad = (16 - ((stack_args * 8) % 16)) % 16;
        if (pad) fprintf(out, "    sub rsp, %d  ; align\n", pad);
      }
      /* Push integer register args in order (arg0 first, deepest on stack).
         Float args are loaded directly into xmm registers instead. */
      int xmm_idx = 0;
      for (int i = 0; i < reg_args; i++) {
        if (TEMP_IS_FLOAT(ins->args[i])) continue;
        load_operand_reg(&ins->args[i], "rax", out);
        fprintf(out, "    push rax\n");
      }
      /* Load float register args into xmm0..xmm7 in argument order.
         Hylian floats are 64-bit doubles internally, but C functions declared
         with `float` parameters expect a 32-bit single in xmm.  We always
         truncate double→float32 via cvtsd2ss so the bit pattern in xmm
         matches what the C callee will read with `movss`. */
      for (int i = 0; i < reg_args; i++) {
        if (!TEMP_IS_FLOAT(ins->args[i])) continue;
        load_operand_reg(&ins->args[i], "rax", out);
        fprintf(out, "    movq xmm%d, rax\n", xmm_idx);
        fprintf(out, "    cvtsd2ss xmm%d, xmm%d\n", xmm_idx, xmm_idx);
        xmm_idx++;
      }
      /* Push stack args right-to-left */
      for (int i = nargs - 1; i >= nreg; i--) {
        load_operand_reg(&ins->args[i], "rax", out);
        fprintf(out, "    push rax\n");
      }
      /* Pop integer register args into their SysV registers (rdi, rsi, rdx, …).
         They were pushed in integer-only order (floats skipped), so we pop them
         into sequential integer arg registers 0, 1, 2, … — NOT the absolute
         argument position, because float args consume an xmm slot, not an
         integer register slot. */
      {
        /* Build the ordered list of integer register indices for each pushed arg.
           Integer args occupy integer reg slots 0,1,2,… sequentially;
           float args do NOT consume an integer reg slot. */
        int pop_targets[6];
        int pt_count = 0;
        int int_slot = 0;
        for (int i = 0; i < reg_args; i++) {
          if (!TEMP_IS_FLOAT(ins->args[i]))
            pop_targets[pt_count++] = int_slot++;
          else
            int_slot = int_slot; /* float: no integer slot consumed */
        }
        /* Args were pushed in order 0..reg_args-1 (ints only), so they come
           off the stack in reverse: pop into highest slot first. */
        for (int i = pt_count - 1; i >= 0; i--)
          fprintf(out, "    pop %s\n", arg_reg(pop_targets[i]));
      }
      const char *cname = ins->str_extra ? rewrite_call_name(ins->str_extra) : "unknown";
      fprintf(out, "    call %s\n", sym(cname));
      if (stack_args > 0 || pad)
        fprintf(out, "    add rsp, %d\n", stack_args * 8 + pad);
    }
    /* If the callee returns bool (C _Bool), only `al` is set; upper bytes of
       rax may contain garbage from the callee's frame.  extra_int is stamped
       by lower.c via tc_func_return_is_bool() when this is the case. */
    if (ins->extra_int)
      fprintf(out, "    movzx rax, al\n");
    store_dest_rax(&ins->dest, out);
    break;
  }

  case IR_RETURN: {
    if (ins->src1.kind != IROP_NONE)
      load_operand_reg(&ins->src1, "rax", out);
    else
      fprintf(out, "    xor rax, rax\n");
    fprintf(out, "    mov rsp, rbp\n");
    if (is_win64()) {
      /* Restore non-volatiles saved in the prologue (pushed in reverse order) */
      fprintf(out, "    pop rsi\n");
      fprintf(out, "    pop rdi\n");
    }
    fprintf(out, "    pop rbp\n");
    fprintf(out, "    ret\n");
    break;
  }

  case IR_NEW: {
    ClassInfo *ci = ins->str_extra ? class_find(ins->str_extra) : NULL;
    if (!ci) {
      fprintf(out, "    ; new: class '%s' not registered\n",
              ins->str_extra ? ins->str_extra : "?");
      fprintf(out, "    xor rax, rax\n");
      store_dest_rax(&ins->dest, out);
      break;
    }
    fprintf(out, "    ; new %s (size=%d)\n", ci->name, ci->size);
    /* malloc(size) */
    fprintf(out, "    mov %s, %d\n", arg_reg(0), ci->size);
    if (is_win64()) fprintf(out, "    sub rsp, 32\n");
    fprintf(out, "    call %s\n", sym("malloc"));
    if (is_win64()) fprintf(out, "    add rsp, 32\n");
    /* zero the allocation with rep stosb */
    fprintf(out, "    push rax\n");
    fprintf(out, "    mov rdi, rax\n");
    fprintf(out, "    xor al, al\n");
    fprintf(out, "    mov rcx, %d\n", ci->size);
    fprintf(out, "    rep stosb\n");
    fprintf(out, "    pop rax\n");
    if (ci->has_ctor) {
      char ctor_name[256];
      snprintf(ctor_name, sizeof(ctor_name), "%s__ctor", ci->name);
      int max_ctor_args = max_reg_args() - 1; /* first reg is self */
      int nargs = ins->arg_count > max_ctor_args ? max_ctor_args : ins->arg_count;
      fprintf(out, "    push rax\n");
      for (int i = 0; i < nargs; i++) {
        load_operand_reg(&ins->args[i], "rax", out);
        fprintf(out, "    push rax\n");
      }
      /* self goes in arg_reg(0), ctor args in arg_reg(1..n) */
      for (int i = nargs - 1; i >= 0; i--)
        fprintf(out, "    pop %s\n", arg_reg(i + 1));
      fprintf(out, "    pop %s\n", arg_reg(0)); /* self */
      fprintf(out, "    push %s\n", arg_reg(0)); /* save self for return */
      if (is_win64()) fprintf(out, "    sub rsp, 32\n");
      fprintf(out, "    call %s\n", ctor_name);
      if (is_win64()) fprintf(out, "    add rsp, 32\n");
      fprintf(out, "    pop rax\n");
    }
    store_dest_rax(&ins->dest, out);
    break;
  }

  case IR_ARENA_ALLOC: {
    /* dest = arena_alloc(&__arena__, sizeof(ClassName))
     * src1 = arena pointer (from IR_ADDROF __arena__)
     * str_extra = class name
     * args / arg_count = constructor arguments (forwarded after alloc)
     */
    ClassInfo *ci_arena = ins->str_extra ? class_find(ins->str_extra) : NULL;
    if (!ci_arena) {
      fprintf(out, "    ; arena_alloc: class '%s' not registered\n",
              ins->str_extra ? ins->str_extra : "?");
      fprintf(out, "    xor rax, rax\n");
      store_dest_rax(&ins->dest, out);
      break;
    }
    fprintf(out, "    ; arena new %s (size=%d)\n", ci_arena->name, ci_arena->size);
    /* arena_alloc(arena_ptr, size) */
    load_operand_reg(&ins->src1, arg_reg(0), out);
    fprintf(out, "    mov %s, %d\n", arg_reg(1), ci_arena->size);
    if (is_win64()) fprintf(out, "    sub rsp, 32\n");
    fprintf(out, "    call %s\n", sym("arena_alloc"));
    if (is_win64()) fprintf(out, "    add rsp, 32\n");
    /* zero the allocation with rep stosb */
    fprintf(out, "    push rax\n");
    fprintf(out, "    mov rdi, rax\n");
    fprintf(out, "    xor al, al\n");
    fprintf(out, "    mov rcx, %d\n", ci_arena->size);
    fprintf(out, "    rep stosb\n");
    fprintf(out, "    pop rax\n");
    /* call constructor if present */
    if (ci_arena->has_ctor) {
      char ctor_name_a[256];
      snprintf(ctor_name_a, sizeof(ctor_name_a), "%s__ctor", ci_arena->name);
      int max_ctor_args_a = max_reg_args() - 1;
      int nargs_a = ins->arg_count > max_ctor_args_a ? max_ctor_args_a : ins->arg_count;
      fprintf(out, "    push rax\n");
      for (int i = 0; i < nargs_a; i++) {
        load_operand_reg(&ins->args[i], "rax", out);
        fprintf(out, "    push rax\n");
      }
      for (int i = nargs_a - 1; i >= 0; i--)
        fprintf(out, "    pop %s\n", arg_reg(i + 1));
      fprintf(out, "    pop %s\n", arg_reg(0)); /* self */
      fprintf(out, "    push %s\n", arg_reg(0)); /* save self */
      if (is_win64()) fprintf(out, "    sub rsp, 32\n");
      fprintf(out, "    call %s\n", ctor_name_a);
      if (is_win64()) fprintf(out, "    add rsp, 32\n");
      fprintf(out, "    pop rax\n");
    }
    store_dest_rax(&ins->dest, out);
    break;
  }

  case IR_GET_FIELD: {
    const char *cname2 = ins->str_extra;
    const char *fname = ins->str_extra2;
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
    int fw = class_field_width(ci2, fname);
    if (foff < 0) {
      fprintf(out, "    ; GET_FIELD: field '%s' not in '%s'\n", fname,
              ci2->name);
      fprintf(out, "    xor rax, rax\n");
    } else {
      load_operand_reg(&ins->src1, "rax", out);
      if (fw == 1)      fprintf(out, "    movsx rax, byte [rax + %d]\n", foff);
      else if (fw == 2) fprintf(out, "    movsx rax, word [rax + %d]\n", foff);
      else if (fw == 4) fprintf(out, "    movsxd rax, dword [rax + %d]\n", foff);
      else              fprintf(out, "    mov rax, [rax + %d]\n", foff);
    }
    store_dest_rax(&ins->dest, out);
    break;
  }

  case IR_SET_FIELD: {
    const char *cname3 = ins->str_extra;
    const char *fname2 = ins->str_extra2;
    ClassInfo *ci3 = cname3 ? class_find(cname3) : NULL;
    if (!ci3 || !fname2) {
      fprintf(out, "    ; SET_FIELD: class/field not found\n");
      break;
    }
    int foff2 = class_field_offset(ci3, fname2);
    int fw2 = class_field_width(ci3, fname2);
    if (foff2 < 0) {
      fprintf(out, "    ; SET_FIELD: field '%s' not in '%s'\n", fname2,
              ci3->name);
      break;
    }
    load_operand_reg(&ins->src2, "rax", out); /* value */
    fprintf(out, "    push rax\n");
    load_operand_reg(&ins->src1, "rcx", out); /* object pointer */
    fprintf(out, "    pop rax\n");
    if (fw2 == 1)      fprintf(out, "    mov byte [rcx + %d], al\n",   foff2);
    else if (fw2 == 2) fprintf(out, "    mov word [rcx + %d], ax\n",   foff2);
    else if (fw2 == 4) fprintf(out, "    mov dword [rcx + %d], eax\n", foff2);
    else               fprintf(out, "    mov [rcx + %d], rax\n",        foff2);
    break;
  }

  case IR_ARRAY_ALLOC: {
    fprintf(out, "    ; array<%s> alloc (8 slots)\n",
            ins->str_extra ? ins->str_extra : "?");
    fprintf(out, "    mov %s, %d\n", arg_reg(0), (8 + 2) * 8);
    if (is_win64()) fprintf(out, "    sub rsp, 32\n");
    fprintf(out, "    call %s\n", sym("malloc"));
    if (is_win64()) fprintf(out, "    add rsp, 32\n");
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
    int count = ins->arg_count;
    int capacity = count < 8 ? 8 : count;
    fprintf(out, "    ; array literal (%d elements)\n", count);
    fprintf(out, "    mov %s, %d\n", arg_reg(0), (capacity + 2) * 8);
    if (is_win64()) fprintf(out, "    sub rsp, 32\n");
    fprintf(out, "    call %s\n", sym("malloc"));
    if (is_win64()) fprintf(out, "    add rsp, 32\n");
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
      fprintf(out, "    ; array.push: var '%s' not found\n",
              ins->str_extra ? ins->str_extra : "?");
      fprintf(out, "    xor rax, rax\n");
      store_dest_rax(&ins->dest, out);
      break;
    }
    int lbl_fast = next_label();
    int lbl_dbl = next_label();
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
    /* realloc(ptr, new_size): new capacity is in rax */
    fprintf(out, "    push rax\n");
    if (is_win64()) {
      fprintf(out, "    mov rdx, rax\n");
      fprintf(out, "    shl rdx, 3\n");
      fprintf(out, "    add rdx, 16\n");
      fprintf(out, "    mov rcx, r11\n");
      fprintf(out, "    sub rsp, 32\n");
      fprintf(out, "    call %s\n", sym("realloc"));
      fprintf(out, "    add rsp, 32\n");
    } else {
      fprintf(out, "    mov rsi, rax\n");
      fprintf(out, "    shl rsi, 3\n");
      fprintf(out, "    add rsi, 16\n");
      fprintf(out, "    mov rdi, r11\n");
      fprintf(out, "    call %s\n", sym("realloc"));
    }
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
    load_operand_reg(&ins->src1, "rax", out);
    fprintf(out, "    mov r11, rax\n");
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
    if (ins->extra_int & 1) {
        /* Flat static array layout: no [len][cap] header, stride = elem width */
        int elem_w = field_byte_width(ins->str_extra);
        fprintf(out, "    imul rcx, rcx, %d\n", elem_w);
        fprintf(out, "    add rcx, r11\n");
        if (elem_w == 1)
            fprintf(out, "    movzx rax, byte [rcx]\n");
        else if (elem_w == 2)
            fprintf(out, "    movzx rax, word [rcx]\n");
        else if (elem_w == 4)
            fprintf(out, "    movsxd rax, dword [rcx]\n");
        else
            fprintf(out, "    mov rax, [rcx]\n");
    } else {
        /* Heap array layout: [len:8][cap:8][e0:8][e1:8]... stride=8 */
        fprintf(out, "    imul rcx, rcx, 8\n");
        fprintf(out, "    add rcx, 16\n");
        fprintf(out, "    add rcx, r11\n");
        fprintf(out, "    mov rax, [rcx]\n");
    }
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
    if (ins->extra_int & 1) {
        /* Flat static array: stride = elem width, no [len][cap] header */
        int elem_w = field_byte_width(ins->str_extra);
        fprintf(out, "    imul rcx, rcx, %d\n", elem_w);
        fprintf(out, "    add rcx, r11\n");
        fprintf(out, "    pop r11\n");
        fprintf(out, "    pop rdx\n");
        if (elem_w == 1)
            fprintf(out, "    mov byte [rcx], dl\n");
        else if (elem_w == 2)
            fprintf(out, "    mov word [rcx], dx\n");
        else if (elem_w == 4)
            fprintf(out, "    mov dword [rcx], edx\n");
        else
            fprintf(out, "    mov [rcx], rdx\n");
    } else {
        /* Heap array: stride=8, skip [len][cap] header */
        fprintf(out, "    imul rcx, rcx, 8\n");
        fprintf(out, "    add rcx, 16\n");
        fprintf(out, "    add rcx, r11\n");
        fprintf(out, "    pop r11\n");
        fprintf(out, "    pop rdx\n");
        fprintf(out, "    mov [rcx], rdx\n");
    }
    break;
  }

  case IR_MULTI_ALLOC:
    load_operand_reg(&ins->src2, "rax", out);
    fprintf(out, "    push rax\n");
    load_operand_reg(&ins->src1, "rax", out);
    fprintf(out, "    push rax\n");
    fprintf(out, "    mov %s, 16\n", arg_reg(0));
    if (is_win64()) fprintf(out, "    sub rsp, 32\n");
    fprintf(out, "    call %s\n", sym("malloc"));
    if (is_win64()) fprintf(out, "    add rsp, 32\n");
    fprintf(out, "    pop rcx\n"); /* tag */
    fprintf(out, "    mov qword [rax + 0], rcx\n");
    fprintf(out, "    pop rcx\n"); /* value */
    fprintf(out, "    mov [rax + 8], rcx\n");
    store_dest_rax(&ins->dest, out);
    break;

  case IR_ENUM_VAL: {
    int val = -1;
    if (ins->str_extra && ins->str_extra2)
      val = enum_variant_value(ins->str_extra, ins->str_extra2);
    if (val >= 0)
      fprintf(out, "    mov rax, %d\n", val);
    else {
      fprintf(out, "    ; unknown enum %s.%s\n",
              ins->str_extra ? ins->str_extra : "?",
              ins->str_extra2 ? ins->str_extra2 : "?");
      fprintf(out, "    xor rax, rax\n");
    }
    store_dest_rax(&ins->dest, out);
    break;
  }

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
    fprintf(out, "    mov rax, r13\n"); /* result = buf ptr */
    fprintf(out, "    sub r14, r13\n");
    fprintf(out, "    mov r15, r14\n"); /* r15 = length */
    /* Restore r13 and r14 from saved stack; r15 stays as length */
    fprintf(out, "    mov r13, [rsp + 16]\n");
    fprintf(out, "    mov r14, [rsp + 8]\n");
    fprintf(out, "    add rsp, 24\n");
    store_dest_rax(&ins->dest, out);
    break;
  }

 case IR_ASM_BLOCK: {
    if (!ins->str_extra) break;
    const char *src = ins->str_extra;
    char buf[8192]; char *dst = buf;
    while (*src && dst < buf + sizeof(buf) - 64) {
        if (src[0] == '{') {
            const char *end = src + 1;
            while (*end && *end != '}') end++;
            char varname[128];
            int len = (int)(end - (src + 1));
            if (len > 0 && len < 127) {
                for (int i=0;i<len;i++) varname[i]=src[1+i];
                varname[len] = '\0';
                int off = get_named_slot(varname);
                if (off > 0)
                    dst += sprintf(dst, "rbp - %d", off);
                else
                    dst += sprintf(dst, "0 /* unknown: %s */", varname);
            }
            src = *end ? end + 1 : end;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    fprintf(out, "%s\n", buf);
    break;
}

  case IR_PRINT:
  case IR_PRINTLN: {
    /* a0/a1: first two arg registers for the active ABI */
    const char *a0 = arg_reg(0); /* ptr arg to hylian_print(ptr, len) */
    const char *a1 = arg_reg(1); /* len arg */
    switch (ins->extra_int) {
    case PRINT_ARG_STR_LIT: {
      const char *sv = ins->src1.str_val ? ins->src1.str_val : "";
      const char *lbl = register_string(sv);
      size_t len = string_const_len(sv);
      fprintf(out, "    lea %s, [rel %s]\n", a0, lbl);
      fprintf(out, "    mov %s, %zu\n", a1, len);
      break;
    }
    case PRINT_ARG_INT:
      load_operand_reg(&ins->src1, "rax", out);
      fprintf(out, "    sub rsp, 32\n");
      /* hylian_int_to_str(val, buf, bufsz) */
      fprintf(out, "    mov %s, rax\n", arg_reg(0));
      fprintf(out, "    mov %s, rsp\n", arg_reg(1));
      fprintf(out, "    mov %s, 32\n",  arg_reg(2));
      if (is_win64()) fprintf(out, "    sub rsp, 32\n");
      fprintf(out, "    call %s\n", sym("hylian_int_to_str"));
      if (is_win64()) fprintf(out, "    add rsp, 32\n");
      /* now rax = length; ptr is still at rsp (before the 32-byte reservation) */
      fprintf(out, "    mov %s, rax\n", a1);   /* length */
      fprintf(out, "    mov %s, rsp\n", a0);   /* buf ptr (still below rsp) */
      break;
    case PRINT_ARG_STR_PTR:
      load_operand_reg(&ins->src1, "rax", out);
      fprintf(out, "    push rax\n");
      fprintf(out, "    mov %s, rax\n", arg_reg(0));
      if (is_win64()) fprintf(out, "    sub rsp, 32\n");
      fprintf(out, "    call %s\n", sym("strlen"));
      if (is_win64()) fprintf(out, "    add rsp, 32\n");
      fprintf(out, "    mov %s, rax\n", a1);   /* length */
      fprintf(out, "    pop %s\n",    a0);     /* ptr */
      break;
    case PRINT_ARG_FLOAT:
      load_operand_reg(&ins->src1, "rax", out);
      fprintf(out, "    movq xmm0, rax\n");
      fprintf(out, "    sub rsp, 48\n");
      /* hylian_float_to_str(xmm0, buf, bufsz) — xmm0 already set */
      fprintf(out, "    mov %s, rsp\n", arg_reg(0));
      fprintf(out, "    mov %s, 48\n",  arg_reg(1));
      if (is_win64()) fprintf(out, "    sub rsp, 32\n");
      fprintf(out, "    call %s\n", sym("hylian_float_to_str"));
      if (is_win64()) fprintf(out, "    add rsp, 32\n");
      fprintf(out, "    mov %s, rax\n", a1);   /* length */
      fprintf(out, "    mov %s, rsp\n", a0);   /* buf ptr */
      break;
    case PRINT_ARG_INTERP:
      if (ins->extra_segs && ins->extra_seg_count > 0 &&
          interp_buf_offset > 0) {
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
        fprintf(out, "    push rax\n");
        fprintf(out, "    mov %s, rax\n", arg_reg(0));
        if (is_win64()) fprintf(out, "    sub rsp, 32\n");
        fprintf(out, "    call %s\n", sym("strlen"));
        if (is_win64()) fprintf(out, "    add rsp, 32\n");
        fprintf(out, "    mov %s, rax\n", a1);
        fprintf(out, "    pop %s\n",    a0);
      } else if (ins->src1.kind != IROP_NONE) {
        load_operand_reg(&ins->src1, "rax", out);
        fprintf(out, "    push rax\n");
        fprintf(out, "    mov %s, rax\n", arg_reg(0));
        if (is_win64()) fprintf(out, "    sub rsp, 32\n");
        fprintf(out, "    call %s\n", sym("strlen"));
        if (is_win64()) fprintf(out, "    add rsp, 32\n");
        fprintf(out, "    mov %s, rax\n", a1);
        fprintf(out, "    pop %s\n",    a0);
      } else {
        const char *empty = register_string("");
        fprintf(out, "    lea %s, [rel %s]\n", a0, empty);
        fprintf(out, "    mov %s, 0\n", a1);
      }
      break;
    default: {
      const char *empty = register_string("");
      fprintf(out, "    lea %s, [rel %s]\n", a0, empty);
      fprintf(out, "    mov %s, 0\n", a1);
      break;
    }
    }
    const char *print_fn =
        (ins->op == IR_PRINT) ? "hylian_print" : "hylian_println";
    if (is_win64()) fprintf(out, "    sub rsp, 32\n");
    fprintf(out, "    call %s\n", sym(print_fn));
    if (is_win64()) fprintf(out, "    add rsp, 32\n");
    if (ins->extra_int == PRINT_ARG_INT)
      fprintf(out, "    add rsp, 32\n");
    else if (ins->extra_int == PRINT_ARG_FLOAT)
      fprintf(out, "    add rsp, 48\n");
    break;
  }

  case IR_ERR: {
    if (ins->src1.kind == IROP_CONST_STR) {
      const char *sv2 = ins->src1.str_val ? ins->src1.str_val : "error";
      const char *lbl = register_string(sv2);
      size_t len = string_const_len(sv2);
      fprintf(out, "    lea %s, [rel %s]\n", arg_reg(0), lbl);
      fprintf(out, "    mov %s, %zu\n", arg_reg(1), len);
    } else {
      load_operand_reg(&ins->src1, "rax", out);
      fprintf(out, "    mov %s, rax\n", arg_reg(0));
      fprintf(out, "    mov %s, 0\n", arg_reg(1));
    }
    if (is_win64()) fprintf(out, "    sub rsp, 32\n");
    fprintf(out, "    call %s\n", sym("hylian_make_error"));
    if (is_win64()) fprintf(out, "    add rsp, 32\n");
    store_dest_rax(&ins->dest, out);
    break;
  }

  case IR_PANIC: {
    if (ins->src1.kind == IROP_CONST_STR) {
      const char *sv3 = ins->src1.str_val ? ins->src1.str_val : "panic";
      const char *lbl = register_string(sv3);
      size_t len = string_const_len(sv3);
      fprintf(out, "    lea %s, [rel %s]\n", arg_reg(0), lbl);
      fprintf(out, "    mov %s, %zu\n", arg_reg(1), len);
    } else {
      load_operand_reg(&ins->src1, "rax", out);
      fprintf(out, "    mov %s, rax\n", arg_reg(0));
      fprintf(out, "    mov %s, 0\n", arg_reg(1));
    }
    if (is_win64()) fprintf(out, "    sub rsp, 32\n");
    fprintf(out, "    call %s\n", sym("hylian_panic"));
    if (is_win64()) fprintf(out, "    add rsp, 32\n");
    break;
  }

  case IR_FUNC_BEGIN:
  case IR_FUNC_END:
    break; /* handled by emit_ir_function */

  case IR_BITAND:
    load_operand_reg(&ins->src1, "rax", out);
    fprintf(out, "    push rax\n");
    load_operand_reg(&ins->src2, "rax", out);
    fprintf(out, "    pop rcx\n");
    fprintf(out, "    and rax, rcx\n");
    store_dest_rax(&ins->dest, out);
    break;

  case IR_BITOR:
    load_operand_reg(&ins->src1, "rax", out);
    fprintf(out, "    push rax\n");
    load_operand_reg(&ins->src2, "rax", out);
    fprintf(out, "    pop rcx\n");
    fprintf(out, "    or rax, rcx\n");
    store_dest_rax(&ins->dest, out);
    break;

  case IR_BITXOR:
    load_operand_reg(&ins->src1, "rax", out);
    fprintf(out, "    push rax\n");
    load_operand_reg(&ins->src2, "rax", out);
    fprintf(out, "    pop rcx\n");
    fprintf(out, "    xor rax, rcx\n");
    store_dest_rax(&ins->dest, out);
    break;

  case IR_BITNOT:
    load_operand_reg(&ins->src1, "rax", out);
    fprintf(out, "    not rax\n");
    store_dest_rax(&ins->dest, out);
    break;

  case IR_SHL:
    load_operand_reg(&ins->src2, "rcx", out);
    load_operand_reg(&ins->src1, "rax", out);
    fprintf(out, "    shl rax, cl\n");
    store_dest_rax(&ins->dest, out);
    break;

  case IR_SHR:
    load_operand_reg(&ins->src2, "rcx", out);
    load_operand_reg(&ins->src1, "rax", out);
    fprintf(out, "    shr rax, cl\n");
    store_dest_rax(&ins->dest, out);
    break;

  case IR_CAST:
    load_operand_reg(&ins->src1, "rax", out);
    /* Truncate / convert for target type */
    if (ins->str_extra) {
      if (strcmp(ins->str_extra, "uint8") == 0 || strcmp(ins->str_extra, "int8") == 0)
        fprintf(out, "    and rax, 0xFF\n");
      else if (strcmp(ins->str_extra, "uint16") == 0 || strcmp(ins->str_extra, "int16") == 0)
        fprintf(out, "    and rax, 0xFFFF\n");
      else if (strcmp(ins->str_extra, "uint32") == 0 || strcmp(ins->str_extra, "int32") == 0)
        fprintf(out, "    mov eax, eax\n");
      else if (strcmp(ins->str_extra, "float") == 0 ||
               strcmp(ins->str_extra, "float32") == 0 ||
               strcmp(ins->str_extra, "float64") == 0) {
        /* int → float: numeric conversion via SSE2 */
        fprintf(out, "    cvtsi2sd xmm0, rax\n");
        fprintf(out, "    movq rax, xmm0\n");
      }
      /* Removed automatic float→int conversion to avoid SSE instructions in kernel code.
         Casting from pointer/int to int/uint64 is a no-op (value already in rax).
         Explicit float→int conversion should be handled separately if needed. */
      /* other pointer/handle types: no conversion needed */
    }
    store_dest_rax(&ins->dest, out);
    break;

  case IR_MEMSET:
    /* memset(rdi=ptr, al=byte, rcx=count) using rep stosb */
    load_operand_reg(&ins->src1, "rdi", out);
    load_operand_reg(&ins->src2, "rax", out);
    load_operand_reg(&ins->extra_src, "rcx", out);
    fprintf(out, "    push rdi\n");
    fprintf(out, "    rep stosb\n");
    fprintf(out, "    pop rax\n");
    store_dest_rax(&ins->dest, out);
    break;

  case IR_MEMCPY:
    /* memcpy(rdi=dst, rsi=src, rcx=count) using rep movsb */
    load_operand_reg(&ins->src1, "rdi", out);
    load_operand_reg(&ins->src2, "rsi", out);
    load_operand_reg(&ins->extra_src, "rcx", out);
    fprintf(out, "    push rdi\n");
    fprintf(out, "    rep movsb\n");
    fprintf(out, "    pop rax\n");
    store_dest_rax(&ins->dest, out);
    break;

  case IR_CLI:
    fprintf(out, "    cli\n");
    break;

  case IR_STI:
    fprintf(out, "    sti\n");
    break;

  case IR_LGDT:
    /* lgdt [base, limit] — build descriptor on stack */
    load_operand_reg(&ins->src1, "rax", out);  /* base */
    load_operand_reg(&ins->src2, "cx", out);   /* limit */
    fprintf(out, "    sub  rsp, 16\n");
    fprintf(out, "    mov  [rsp + 2], rax\n");
    fprintf(out, "    mov  [rsp], cx\n");
    fprintf(out, "    lgdt [rsp]\n");
    fprintf(out, "    add  rsp, 16\n");
    break;

  case IR_LIDT:
    /* lidt [base, limit] — build descriptor on stack */
    load_operand_reg(&ins->src1, "rax", out);  /* base */
    load_operand_reg(&ins->src2, "cx", out);   /* limit */
    fprintf(out, "    sub  rsp, 16\n");
    fprintf(out, "    mov  [rsp + 2], rax\n");
    fprintf(out, "    mov  [rsp], cx\n");
    fprintf(out, "    lidt [rsp]\n");
    fprintf(out, "    add  rsp, 16\n");
    break;

  case IR_LTR:
    /* ltr selector */
    load_operand_reg(&ins->src1, "ax", out);
    fprintf(out, "    ltr  ax\n");
    break;

  case IR_INVLPG:
    /* invlpg [vaddr] */
    load_operand_reg(&ins->src1, "rax", out);
    fprintf(out, "    invlpg [rax]\n");
    break;

  case IR_WRMSR:
    /* wrmsr msr, val — rcx=msr, edx:eax=val */
    load_operand_reg(&ins->src1, "rcx", out);  /* msr */
    load_operand_reg(&ins->src2, "rax", out);  /* val */
    fprintf(out, "    mov  rdx, rax\n");
    fprintf(out, "    shr  rdx, 32\n");
    fprintf(out, "    and  eax, 0xFFFFFFFF\n");
    fprintf(out, "    wrmsr\n");
    break;

  case IR_RDMSR:
    /* dest = rdmsr(msr) — rcx=msr, result in edx:eax */
    load_operand_reg(&ins->src1, "rcx", out);  /* msr */
    fprintf(out, "    rdmsr\n");
    fprintf(out, "    shl  rdx, 32\n");
    fprintf(out, "    or   rax, rdx\n");
    store_dest_rax(&ins->dest, out);
    break;

  default:
    fprintf(out, "    ; unhandled IR op %d\n", (int)ins->op);
    break;
  }
}


/* Returns 1 if the last non-NOP instruction before end_idx is IR_RETURN */
static int ends_with_return(const IRModule *mod, int begin_idx, int end_idx) {
  for (int i = end_idx - 1; i > begin_idx; i--) {
    if (mod->instrs[i].op == IR_NOP)
      continue;
    return mod->instrs[i].op == IR_RETURN;
  }
  return 0;
}

static void emit_ir_function(const IRModule *mod, int begin_idx, FILE *out) {
  const IRInstr *begin = &mod->instrs[begin_idx];

  int end_idx = begin_idx + 1;
  int depth = 1;
  while (end_idx < mod->instr_count && depth > 0) {
    if (mod->instrs[end_idx].op == IR_FUNC_BEGIN)
      depth++;
    if (mod->instrs[end_idx].op == IR_FUNC_END)
      depth--;
    end_idx++;
  }
  end_idx--; /* points at IR_FUNC_END */

  /* Pre-scan to allocate slots and compute frame size */
  prescan_function(mod, begin_idx, end_idx);

  const char *saved_class = current_class_name;
  /* Detect if this is a method: params[0] == "self" */
  const char *new_class = NULL;
  if (begin->param_count > 0 && begin->params[0].name &&
      strcmp(begin->params[0].name, "self") == 0 &&
      begin->params[0].type_name) {
    new_class = begin->params[0].type_name;
  }
  current_class_name = new_class;

  const char *fn_name = begin->str_extra ? begin->str_extra : "unknown";
  int is_main   = (begin->extra_int & 1);
  int is_naked  = (begin->extra_int >> 1) & 1;
  if (is_main)
    fprintf(out, "%s:\n", current_freestanding ? "_start" : "main");
  else
    fprintf(out, "%s:\n", fn_name);

  if (is_naked) {
    /* Naked function: emit body instructions only — no prologue or epilogue */
    for (int i = begin_idx + 1; i < end_idx; i++) {
      const IRInstr *ins = &mod->instrs[i];
      if (ins->op == IR_ALLOCA) continue; /* skip slot setup */
      emit_ir_instr(ins, out);
    }
    fprintf(out, "\n");
    current_class_name = saved_class;
    return;
  }

  fprintf(out, "    push rbp\n");
  fprintf(out, "    mov rbp, rsp\n");
  /* Win64: save non-volatile registers that our codegen uses as scratch.
     rdi and rsi are volatile on SysV but NON-volatile on Win64. */
  if (is_win64()) {
    fprintf(out, "    push rdi\n");
    fprintf(out, "    push rsi\n");
  }
  fprintf(out, "    sub rsp, %d\n", frame_bytes);

  /* Spill parameters from arg registers to their named slots */
  int nparams = begin->param_count > max_reg_args() ? max_reg_args() : begin->param_count;
  for (int pi = 0; pi < nparams; pi++) {
    const char *pname = begin->params[pi].name;
    if (!pname)
      continue;
    int off = get_named_slot(pname);
    if (off > 0)
      fprintf(out, "    mov [rbp - %d], %s\n", off, arg_reg(pi));
  }

  for (int i = begin_idx + 1; i < end_idx; i++) {
    const IRInstr *ins = &mod->instrs[i];
    /* Skip ALLOCA instructions that were already handled as parameter spills */
    if (ins->op == IR_ALLOCA) {
      /* Just emit the zero-init (already done in emit_ir_instr) */
      /* Check if this is a parameter; if so, skip zeroing (value already set)
       */
      int is_param = 0;
      for (int pi2 = 0; pi2 < nparams; pi2++) {
        if (begin->params[pi2].name && ins->str_extra &&
            strcmp(begin->params[pi2].name, ins->str_extra) == 0) {
          is_param = 1;
          break;
        }
      }
      if (is_param)
        continue; /* param already spilled above */
    }
    emit_ir_instr(ins, out);
  }

  /* Implicit return (only if the function doesn't already end with IR_RETURN)
   */
  if (!ends_with_return(mod, begin_idx, end_idx)) {
    fprintf(out, "    xor rax, rax\n");
    fprintf(out, "    mov rsp, rbp\n");
    if (is_win64()) {
      fprintf(out, "    pop rsi\n");
      fprintf(out, "    pop rdi\n");
    }
    fprintf(out, "    pop rbp\n");
    fprintf(out, "    ret\n");
  }
  fprintf(out, "\n");
  current_class_name = saved_class;
}


void codegen_ir(IRModule *mod, FILE *out, const char *src_filename,
                const char *target, int freestanding) {
  current_target = target ? target : "linux";
  current_freestanding = freestanding || (target && strcmp(target, "limine") == 0);
  str_const_count = 0;
  float_const_count = 0;
  _label_counter = 0;
  fn_prefix_count = 0;
  io_included = 0;
  static_var_registry_reset();

  /* Collect IR_STATIC_VAR entries before any function emission */
  for (int i = 0; i < mod->instr_count; i++) {
    const IRInstr *ins = &mod->instrs[i];
    if (ins->op == IR_STATIC_VAR && ins->str_extra && static_var_count < MAX_STATIC_VARS) {
      StaticVarEntry *sv = &static_vars[static_var_count++];
      sv->name      = ins->str_extra;
      sv->type_name = ins->str_extra2;
      sv->str_val   = NULL;
      sv->int_val   = 0;
      sv->float_val = 0.0;
      sv->is_float  = 0;
      if (ins->src1.kind == IROP_CONST_INT)
        sv->int_val = ins->src1.int_val;
      else if (ins->src1.kind == IROP_CONST_FLOAT) {
        sv->float_val = ins->src1.float_val;
        sv->is_float  = 1;
      } else if (ins->src1.kind == IROP_CONST_STR)
        sv->str_val = ins->src1.str_val;
      sv->is_const   = ins->extra_int & 1;
      sv->array_size = ins->extra_int >> 1;
    }
  }

  /* Scan includes to set up fn_prefix rewrites and io flag */
  typedef struct {
    const char *path;
    const char *src;
    const char *abi;
    int io;
  } EarlyMod;
  static const EarlyMod EARLY[] = {
      {"std.io", NULL, NULL, 1},
      {"std.errors", NULL, NULL, 0},
      {"std.strings", "str_", "hylian_", 0},
      {"std.system.filesystem", NULL, NULL, 0},
      {"std.system.env", NULL, NULL, 0},
      {"std.kernel", NULL, NULL, 1},
  };
  static const int EARLY_N = (int)(sizeof(EARLY) / sizeof(EARLY[0]));
  for (int i = 0; i < mod->include_count; i++) {
    for (int m = 0; m < EARLY_N; m++) {
      if (strcmp(mod->includes[i], EARLY[m].path) == 0) {
        if (EARLY[m].io)
          io_included = 1;
        if (EARLY[m].src && fn_prefix_count < MAX_FN_PREFIXES) {
          fn_prefixes[fn_prefix_count].src_prefix = EARLY[m].src;
          fn_prefixes[fn_prefix_count].abi_prefix = EARLY[m].abi;
          fn_prefix_count++;
        }
      }
    }
  }

  class_registry_reset();
  enum_registry_reset();
  for (int i = 0; i < mod->class_count; i++)
    class_register(mod->classes[i]);
  for (int i = 0; i < mod->enum_count; i++)
    enum_register(mod->enums[i]);

  char *text_buf = NULL;
  size_t text_size = 0;
  FILE *text_out = open_memstream(&text_buf, &text_size);
  if (!text_out) {
    fprintf(stderr, "codegen_ir: open_memstream failed\n");
    return;
  }

  int found_main = 0;
  for (int i = 0; i < mod->instr_count; i++) {
    const IRInstr *ins = &mod->instrs[i];
    if (ins->op == IR_FUNC_BEGIN) {
      if (ins->extra_int & 1)
        found_main = 1; /* is_main */
      emit_ir_function(mod, i, text_out);
      /* Skip to FUNC_END */
      int depth = 1;
      while (++i < mod->instr_count) {
        if (mod->instrs[i].op == IR_FUNC_BEGIN)
          depth++;
        if (mod->instrs[i].op == IR_FUNC_END)
          depth--;
        if (depth == 0)
          break;
      }
    }
  }

  /* Synthesise a minimal entry if none found */
  if (!found_main) {
    named_count = 0;
    max_temp_id = -1;
    interp_buf_offset = 0;
    frame_bytes = 16;
    const char *entry_lbl = (freestanding || strcmp(current_target, "limine") == 0) ? "_start" : "main";
    fprintf(text_out, "%s:\n", entry_lbl);
    fprintf(text_out, "    push rbp\n");
    fprintf(text_out, "    mov rbp, rsp\n");
    fprintf(text_out, "    sub rsp, 16\n");
    fprintf(text_out, "    xor rax, rax\n");
    fprintf(text_out, "    mov rsp, rbp\n");
    fprintf(text_out, "    pop rbp\n");
    fprintf(text_out, "    ret\n\n");
  }
  fclose(text_out);

  typedef struct {
    const char *path;
    const char *obj;
    const char *stem;
    const char *externs;
    const char *link_libs;
    const char *fn_prefix;
    int io;
  } StdMod;
  static const StdMod STD[] = {
      {"std.io", "io", "io", "hylian_print hylian_println hylian_int_to_str hylian_float_to_str",
       NULL, NULL, 1},
      {"std.errors", "errors", "errors",
       "hylian_make_error hylian_panic Error_message Error_code", NULL, NULL,
       0},
      {"std.strings", "strings", "strings",
       "hylian_length hylian_is_empty hylian_contains hylian_starts_with "
       "hylian_ends_with hylian_index_of hylian_slice hylian_trim "
       "hylian_trim_start hylian_trim_end hylian_to_upper hylian_to_lower "
       "hylian_replace hylian_split hylian_join hylian_to_int hylian_to_float "
       "hylian_from_int hylian_equals hylian_concat hylian_char_at",
       NULL, "str_", 0},
      {"std.mem", "mem", "mem",
       "arena_init arena_alloc arena_free arena_new arena_delete malloc free realloc",
       NULL, NULL, 0},
      {"std.system.filesystem", "fs", "system/filesystem",
       "hylian_file_read hylian_file_write hylian_file_exists hylian_file_size "
       "hylian_mkdir",
       NULL, NULL, 0},
      {"std.system.env", "env", "system/env", "hylian_getenv hylian_exit", NULL,
       NULL, 0},
      {"std.kernel",
       NULL, /* obj chosen dynamically below */
       NULL, /* stem chosen dynamically below */
       "hylian_vga_clear hylian_vga_set_color hylian_outb hylian_inb "
       "hylian_halt hylian_print hylian_println hylian_int_to_str "
       "hylian_hhdm_offset hylian_memmap_count hylian_memmap_base "
       "hylian_memmap_len hylian_memmap_type",
       NULL, NULL, 1},
  };
  static const int STD_N = (int)(sizeof(STD) / sizeof(STD[0]));
  int mod_needed[sizeof(STD) / sizeof(STD[0])];
  memset(mod_needed, 0, sizeof(mod_needed));
  for (int i = 0; i < mod->include_count; i++)
    for (int m = 0; m < STD_N; m++)
      if (strcmp(mod->includes[i], STD[m].path) == 0)
        mod_needed[m] = 1;

  /* For std.kernel, pick the right platform object/stem based on target */
  const char *kernel_obj  = (strcmp(current_target, "limine") == 0) ? "limine"  : "kernel";
  const char *kernel_stem = (strcmp(current_target, "limine") == 0) ? "platform/limine" : "platform/kernel";

  const char *nasm_fmt = strcmp(current_target, "macos") == 0     ? "macho64"
                         : strcmp(current_target, "windows") == 0 ? "win64"
                                                                  : "elf64";
  const char *link_flags =
      strcmp(current_target, "windows") == 0 ? "" : " -no-pie";
  const char *bin_ext = strcmp(current_target, "windows") == 0 ? ".exe" : "";
  int is_limine = (strcmp(current_target, "limine") == 0);

  fprintf(out, "; Generated by Hylian compiler (IR backend)\n");
  if (src_filename)
    fprintf(out, "; Source: %s\n", src_filename);
  fprintf(out, "bits 64\ndefault rel\n\n");
  fprintf(out, "; Target: %s\n", current_target);
  fprintf(out, "; Assemble: nasm -f %s <file>.asm -o <file>.o\n", nasm_fmt);
  if (is_limine) {
    fprintf(out, "; Link:     ld -T runtime/platform/limine.ld <file>.o runtime/platform/limine.o -o kernel.elf\n");
  } else if (freestanding) {
    fprintf(out, "; Link:     ld -T kernel.ld <file>.o -o kernel.elf\n");
    fprintf(out, ";\n");
    fprintf(out, "; Minimal linker script (kernel.ld):\n");
    fprintf(out, ";   ENTRY(_start)\n");
    fprintf(out, ";   SECTIONS {\n");
    fprintf(out, ";     . = 0x100000;\n");
    fprintf(out, ";     .text : { *(.text) }\n");
    fprintf(out, ";     .data : { *(.data) }\n");
    fprintf(out, ";     .bss  : { *(.bss) }\n");
    fprintf(out, ";   }\n");
  } else if (!is_limine) {
    /* nothing extra for hosted targets */
  }
  for (int m = 0; m < STD_N; m++) {
    if (!mod_needed[m])
      continue;
    const char *stem = (strcmp(STD[m].path, "std.kernel") == 0) ? kernel_stem : STD[m].stem;
    const char *obj  = (strcmp(STD[m].path, "std.kernel") == 0) ? kernel_obj  : STD[m].obj;
    fprintf(out, ";          # prefer pre-built: runtime/std/%s.o\n", stem);
    fprintf(out, ";          # fallback:         gcc -O2 -c runtime/std/%s.c -o %s.o\n",
            stem, obj);
  }
  /* vendor_link_flags: space-separated -l:libname.so flags to append to link comment */
  char vendor_link_flags[2048] = "";
  /* vendor_extern_names: collected symbol names to extern-declare */
  char *vendor_extern_names[1024];
  int   vendor_extern_count = 0;

  for (int i = 0; i < mod->include_count; i++) {
    const char *inc = mod->includes[i];

    /* link:<libname> — emitted by compiler.c when it parses a .hyi link directive */
    if (strncmp(inc, "link:", 5) == 0) {
      const char *libname = inc + 5;
      char flag[256];
      snprintf(flag, sizeof(flag), " -l:%s", libname);
      if (!strstr(vendor_link_flags, flag))
        strncat(vendor_link_flags, flag, sizeof(vendor_link_flags) - strlen(vendor_link_flags) - 1);
    }

    /* pkg:<name> — emitted by compiler.c when it parses a .hyi pkg directive.
       Emit a $(pkg-config --libs <name>) shell substitution in the link comment
       so the hint is portable across platforms. */
    if (strncmp(inc, "pkg:", 4) == 0) {
      const char *pkg_name = inc + 4;
      char flag[512];
      snprintf(flag, sizeof(flag), " $(pkg-config --libs %s)", pkg_name);
      if (!strstr(vendor_link_flags, flag))
        strncat(vendor_link_flags, flag, sizeof(vendor_link_flags) - strlen(vendor_link_flags) - 1);
    }
  }

  if (!freestanding && !is_limine) {
    fprintf(out, "; Link:     gcc <file>.o");
    for (int m = 0; m < STD_N; m++)
      if (mod_needed[m]) {
        const char *obj = (strcmp(STD[m].path, "std.kernel") == 0) ? kernel_obj : STD[m].obj;
        fprintf(out, " %s.o", obj);
      }
    for (int m = 0; m < STD_N; m++)
      if (mod_needed[m] && STD[m].link_libs)
        fprintf(out, " %s", STD[m].link_libs);
    if (vendor_link_flags[0])
      fprintf(out, "%s", vendor_link_flags);
    fprintf(out, " -o <program>%s%s\n\n", bin_ext, link_flags);
  } else {
    fprintf(out, "\n");
  }

  /* Walk the merged AST classes/funcs that came from .hyi files.
     We identify vendor symbols as functions/methods whose bodies are empty
     (body_count == 0) AND whose names are not already covered by stdlib externs.
     We re-use mod->classes and the IR func list is not directly available here,
     so we emit externs for every class method (ClassName_methodName) from
     vendor-origin classes — detected by having no IR_FUNC_BEGIN for them. */

  /* Build a quick set of IR function names that DO have bodies */
  char *ir_funcs[1024];
  int   ir_func_count = 0;
  for (int i = 0; i < mod->instr_count; i++) {
    if (mod->instrs[i].op == IR_FUNC_BEGIN && mod->instrs[i].str_extra) {
      if (ir_func_count < 1024)
        ir_funcs[ir_func_count++] = mod->instrs[i].str_extra;
    }
  }

  /* Helper: check if a name has a body in the IR */
  #define HAS_IR_BODY(name) ({ \
    int _found = 0; \
    for (int _k = 0; _k < ir_func_count; _k++) \
      if (strcmp(ir_funcs[_k], (name)) == 0) { _found = 1; break; } \
    _found; \
  })

  /* Helper: check if already in a stdlib extern list */
  #define IN_STD_EXTERNS(name) ({ \
    int _found = 0; \
    for (int _sm = 0; _sm < STD_N && !_found; _sm++) { \
      if (!mod_needed[_sm] || !STD[_sm].externs) continue; \
      char _eb[512]; \
      strncpy(_eb, STD[_sm].externs, sizeof(_eb)-1); _eb[sizeof(_eb)-1]='\0'; \
      char *_tok = strtok(_eb, " "); \
      while (_tok) { if (strcmp(_tok,(name))==0){_found=1;break;} _tok=strtok(NULL," ");} \
    } \
    _found; \
  })

  /* Helper: add a vendor extern (deduped) */
  #define ADD_VENDOR_EXTERN(name) do { \
    int _dup = 0; \
    for (int _vi = 0; _vi < vendor_extern_count; _vi++) \
      if (strcmp(vendor_extern_names[_vi], (name)) == 0) { _dup = 1; break; } \
    if (!_dup && vendor_extern_count < 1024) \
      vendor_extern_names[vendor_extern_count++] = strdup(name); \
  } while(0)

  /* Scan classes: methods with no IR body are vendor externs */
  for (int ci = 0; ci < mod->class_count; ci++) {
    ClassNode *cn = mod->classes[ci];
    if (!cn) continue;
    for (int mi = 0; mi < cn->method_count; mi++) {
      MethodNode *mn = cn->methods[mi];
      if (!mn || !mn->name) continue;
      char mangled[256];
      /* Lower uses ClassName_methodName */
      snprintf(mangled, sizeof(mangled), "%s_%s", cn->name, mn->name);
      if (!HAS_IR_BODY(mangled) && !IN_STD_EXTERNS(mangled)) {
        ADD_VENDOR_EXTERN(mangled);
      }
    }
    /* Constructor */
    if (cn->has_ctor) {
      char ctor[256];
      snprintf(ctor, sizeof(ctor), "%s__ctor", cn->name);
      if (!HAS_IR_BODY(ctor) && !IN_STD_EXTERNS(ctor)) {
        ADD_VENDOR_EXTERN(ctor);
      }
    }
  }

  /* Scan top-level IR module funcs: any func declared but with no IR body
     and not in stdlib is a vendor extern (came from a .hyi FuncNode). */
  /* We can detect these by walking the IRModule includes for vendors.* and
     cross-referencing the class registry — but the cleanest approach is to
     check every IR_CALL target that has no matching IR_FUNC_BEGIN. */
  for (int i = 0; i < mod->instr_count; i++) {
    if (mod->instrs[i].op == IR_CALL && mod->instrs[i].str_extra) {
      const char *callee = mod->instrs[i].str_extra;
      if (!HAS_IR_BODY(callee) && !IN_STD_EXTERNS(callee)) {
        /* Not a stdlib rewrite target either */
        const char *rewritten = rewrite_call_name(callee);
        if (strcmp(rewritten, callee) == 0) {
          /* No prefix rewrite — it's a raw vendor extern call */
          ADD_VENDOR_EXTERN(callee);
        }
      }
    }
  }

  #undef HAS_IR_BODY
  #undef IN_STD_EXTERNS
  #undef ADD_VENDOR_EXTERN

  /* externs */
  /* In freestanding mode, malloc/realloc/strlen are provided by kernel.c's
     bump allocator and inline strlen — still need to declare them as externs
     so the assembler resolves the references at link time. */
  fprintf(out, "extern malloc\nextern realloc\nextern strlen\n");
  /* Arena functions are always needed since codegen unconditionally emits them */
  fprintf(out, "extern arena_init\nextern arena_alloc\nextern arena_free\n");
  /* Suppress duplicate arena externs that vendor-scan may have added via IR_CALL */
  for (int vi = 0; vi < vendor_extern_count; vi++) {
    if (strcmp(vendor_extern_names[vi], "arena_init") == 0 ||
        strcmp(vendor_extern_names[vi], "arena_alloc") == 0 ||
        strcmp(vendor_extern_names[vi], "arena_free") == 0) {
      free(vendor_extern_names[vi]);
      /* Shift remaining entries down */
      for (int vj = vi; vj < vendor_extern_count - 1; vj++)
        vendor_extern_names[vj] = vendor_extern_names[vj + 1];
      vendor_extern_count--;
      vi--;
    }
  }
  for (int m = 0; m < STD_N; m++) {
    if (!mod_needed[m] || !STD[m].externs)
      continue;
    char ebuf[512];
    strncpy(ebuf, STD[m].externs, sizeof(ebuf) - 1);
    ebuf[sizeof(ebuf) - 1] = '\0';
    char *tok = strtok(ebuf, " ");
    while (tok) {
      fprintf(out, "extern %s\n", sym(tok));
      tok = strtok(NULL, " ");
    }
  }
  for (int vi = 0; vi < vendor_extern_count; vi++) {
    fprintf(out, "extern %s\n", vendor_extern_names[vi]);
    free(vendor_extern_names[vi]);
  }
  fprintf(out, "\n");

  /* For Limine target: emit the base-revision request in .limine_requests */
  if (is_limine) {
    fprintf(out, "section .limine_requests\nalign 8\n");
    fprintf(out, "_limine_base_revision:\n");
    fprintf(out, "    dq 0xf9562b2d5c95a6c8\n");
    fprintf(out, "    dq 0x6a7b384944536bdc\n");
    fprintf(out, "    dq 3\n\n");
  }

  /* .data section */
  fprintf(out, "section .data\n");
  for (int i = 0; i < str_const_count; i++) {
    char *escaped = nasm_escape_string(str_consts[i].value);
    size_t len = strlen(str_consts[i].value);
    fprintf(out, "    %s: db %s, 0\n", str_consts[i].label, escaped);
    fprintf(out, "    %s_len: equ %zu\n", str_consts[i].label, len);
    free(escaped);
  }
  /* Float constant pool — each entry is a 64-bit IEEE 754 double, emitted
     as its raw bit pattern via a union so NASM sees an exact dq value. */
  for (int i = 0; i < float_const_count; i++) {
    union { double d; unsigned long long u; } fc;
    fc.d = float_consts[i].value;
    fprintf(out, "    %s: dq 0x%016llx  ; %g\n",
            float_consts[i].label, fc.u, float_consts[i].value);
  }
  /* Emit static global variables.
     For class-typed statics, allocate the full struct size as zeroed bytes.
     For primitive types, emit the correctly-sized reservation so adjacent
     variables pack tightly and addressing with `+offset` stays in bounds. */
  for (int i = 0; i < static_var_count; i++) {
    StaticVarEntry *sv = &static_vars[i];
    /* Fixed-size arrays */
    if (sv->array_size > 0) {
      int w = field_byte_width(sv->type_name);
      const char *dir = (w==1)?"db":(w==2)?"dw":(w==4)?"dd":"dq";
      fprintf(out, "    %s: times %d %s 0\n", sv->name, sv->array_size, dir);
      continue;
    }
    const char *svname = nasm_safe_label(sv->name);
    if (sv->str_val) {
      int is_str_type = sv->type_name && strcmp(sv->type_name, "str") == 0;
      /* Strip surrounding double-quotes if present — the AST stores string
         literal values as `"Alice"` (with quotes), but nasm_escape_string
         expects the raw content without them. */
      const char *raw = sv->str_val;
      char *unquoted = NULL;
      size_t rlen = strlen(raw);
      if (rlen >= 2 && raw[0] == '"' && raw[rlen - 1] == '"') {
        unquoted = malloc(rlen - 1);
        memcpy(unquoted, raw + 1, rlen - 2);
        unquoted[rlen - 2] = '\0';
        raw = unquoted;
      }
      char *escaped = nasm_escape_string(raw);
      free(unquoted);
      if (is_str_type) {
        /* str statics must be a pointer-sized slot holding the address of the
           string bytes.  Emit the bytes under a private label, then emit the
           variable itself as a dq pointer so that `mov rax, [rel name]` loads
           a valid char* rather than the raw character bytes. */
        fprintf(out, "    __strdata_%s: db %s, 0\n", svname, escaped);
        fprintf(out, "    %s: dq __strdata_%s\n", svname, svname);
      } else {
        /* Non-str type with a string initialiser (unusual, keep old behaviour) */
        fprintf(out, "    %s: db %s, 0\n", svname, escaped);
      }
      free(escaped);
      continue;
    }
    /* Const float: emit as a dq bit-pattern so it can be loaded with movsd */
    if (sv->is_const && (sv->is_float || is_float_type_name(sv->type_name))) {
      union { double d; unsigned long long u; } cv;
      cv.d = sv->is_float ? sv->float_val : (double)sv->int_val;
      fprintf(out, "    %s: dq 0x%016llx  ; %g\n", svname, cv.u, cv.d);
      continue;
    }
    /* Const integer: emit as EQU symbolic constant */
    if (sv->is_const) {
      fprintf(out, "    %s: equ %ld\n", svname, sv->int_val);
      continue;
    }
    /* Class-typed static: allocate full struct size, zeroed. */
    ClassInfo *ci_sv = sv->type_name ? class_find(sv->type_name) : NULL;
    if (ci_sv) {
      fprintf(out, "    %s: times %d db 0\n", svname, ci_sv->size);
      continue;
    }
    /* Float statics: emit dq with the IEEE 754 bit pattern of the initializer */
    if (is_float_type_name(sv->type_name)) {
      union { double d; unsigned long long u; } fv;
      fv.d = sv->is_float ? sv->float_val : (double)sv->int_val;
      if (strcmp(sv->type_name, "float32") == 0) {
        union { float f; unsigned int u; } f32;
        f32.f = (float)fv.d;
        fprintf(out, "    %s: dd 0x%08x  ; %g\n", svname, f32.u, (double)f32.f);
      } else {
        fprintf(out, "    %s: dq 0x%016llx  ; %g\n", svname, fv.u, fv.d);
      }
      continue;
    }
    /* Primitive types: pick directive matching the type width. */
    int w = field_byte_width(sv->type_name);
    long v = sv->int_val;
    if (w == 1)
      fprintf(out, "    %s: db %ld\n", svname, v & 0xFF);
    else if (w == 2)
      fprintf(out, "    %s: dw %ld\n", svname, v & 0xFFFF);
    else if (w == 4)
      fprintf(out, "    %s: dd %ld\n", svname, v & 0xFFFFFFFFL);
    else
      fprintf(out, "    %s: dq %ld\n", svname, v);
  }
  fprintf(out, "\n");

  const char *global_entry = (freestanding || is_limine) ? "_start" : "main";
  fprintf(out, "section .text\n    global %s\n\n", global_entry);
  fwrite(text_buf, 1, text_size, out);
  free(text_buf);
}
