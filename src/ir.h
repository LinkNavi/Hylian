#ifndef IR_H
#define IR_H

#include <stdio.h>
#include "ast.h"  /* ClassNode, EnumNode, ProgramNode, InterpSegment, TypeKind */

/* ─── Opcodes ────────────────────────────────────────────────────────────────── */

typedef enum {
    /* Constants — load immediate into dest temp */
    IR_CONST_INT = 0, /* dest = src1(int_val)   */
    IR_CONST_STR,     /* dest = src1(str_val)   */
    IR_CONST_BOOL,    /* dest = src1(bool_val)  */
    IR_CONST_NIL,     /* dest = 0               */

    /* Named-variable access */
    IR_LOAD_VAR,      /* dest = var[str_extra]  */
    IR_STORE_VAR,     /* var[str_extra] = src1  */

    /* Arithmetic */
    IR_ADD,           /* dest = src1 + src2 */
    IR_SUB,           /* dest = src1 - src2 */
    IR_MUL,           /* dest = src1 * src2 */
    IR_DIV,           /* dest = src1 / src2 */
    IR_MOD,           /* dest = src1 % src2 */
    IR_NEG,           /* dest = -src1       */
    IR_NOT,           /* dest = !src1       */

    /* Comparisons */
    IR_EQ,
    IR_NEQ,
    IR_LT,
    IR_LE,
    IR_GT,
    IR_GE,

    /* Control flow */
    IR_LABEL,         /* .L{dest.label_id}:                          */
    IR_JUMP,          /* jmp .L{src1.label_id}                       */
    IR_JUMP_IF,       /* if src1 != 0: jmp .L{src2.label_id}         */
    IR_JUMP_UNLESS,   /* if src1 == 0: jmp .L{src2.label_id}         */

    /* Function calls (str_extra = callee name) */
    IR_CALL,          /* dest = str_extra(args...)                   */
    IR_RETURN,        /* return src1 (IROP_NONE = void)              */

    /* OOP */
    IR_NEW,           /* dest = new str_extra(args...)               */
    IR_GET_FIELD,     /* dest = src1.field: str_extra=class str_extra2=field */
    IR_SET_FIELD,     /* src1.field = src2: str_extra=class str_extra2=field */

    /* Array intrinsics */
    IR_ARRAY_ALLOC,   /* dest = empty array (str_extra = elem type)  */
    IR_ARRAY_INIT,    /* dest = [args[0], args[1], ...] literal      */
    IR_ARRAY_PUSH,    /* array.push: src1=arr_ptr src2=val str_extra=var_name */
    IR_ARRAY_POP,     /* dest = array.pop(): src1=arr_ptr            */
    IR_ARRAY_LEN,     /* dest = src1.len                             */
    IR_ARRAY_CAP,     /* dest = src1.cap                             */
    IR_ARRAY_LOAD,    /* dest = src1[src2]                           */
    IR_ARRAY_STORE,   /* src1[src2] = extra_src                      */

    /* Tagged union */
    IR_MULTI_ALLOC,   /* dest = multi(tag=src1, val=src2)            */

    /* Enum constant */
    IR_ENUM_VAL,      /* dest = Enum.Var: str_extra=enum str_extra2=var */

    /* Interpolated string (segments in extra_segs) */
    IR_INTERP_STR,    /* dest = built interp string                  */

    /* Inline assembly */
    IR_ASM_BLOCK,     /* raw asm text in str_extra                   */

    /* Print/println intrinsics (arg type in extra_int) */
    IR_PRINT,         /* print(src1)   */
    IR_PRINTLN,       /* println(src1) */

    /* Error/panic intrinsics */
    IR_ERR,           /* dest = Err(src1) — src1 may be CONST_STR or TEMP */
    IR_PANIC,         /* panic(src1)                                 */

    /* Function structure */
    IR_FUNC_BEGIN,    /* str_extra=name extra_int=is_main params/param_count set */
    IR_FUNC_END,

    /* Local variable declaration */
    IR_ALLOCA,        /* str_extra=var_name str_extra2=type_name extra_int=TypeKind */

    IR_NOP,           /* no-operation (placeholder after DCE)        */

    IR_OPCODE_COUNT
} IROpcode;

/* ─── Operand ────────────────────────────────────────────────────────────────── */

typedef enum {
    IROP_NONE = 0,
    IROP_TEMP,        /* virtual register: .temp_id */
    IROP_CONST_INT,   /* integer constant: .int_val */
    IROP_CONST_STR,   /* string constant (unquoted): .str_val */
    IROP_CONST_BOOL,  /* boolean: .bool_val */
    IROP_LABEL_ID,    /* label reference: .label_id */
} IROperandKind;

typedef struct {
    IROperandKind kind;
    union {
        int   temp_id;   /* IROP_TEMP      */
        long  int_val;   /* IROP_CONST_INT */
        char *str_val;   /* IROP_CONST_STR */
        int   bool_val;  /* IROP_CONST_BOOL*/
        int   label_id;  /* IROP_LABEL_ID  */
    };
} IROperand;

/* ─── Function parameter (carried in IR_FUNC_BEGIN) ─────────────────────────── */

typedef struct {
    char *name;
    char *type_name;  /* NULL for non-simple or unknown */
    int   type_kind;  /* TypeKind from ast.h             */
} IRParam;

/* ─── Print argument category (stored in extra_int on IR_PRINT/IR_PRINTLN) ───── */
#define PRINT_ARG_STR_LIT  1   /* src1 = IROP_CONST_STR                  */
#define PRINT_ARG_INT      2   /* src1 = temp holding integer             */
#define PRINT_ARG_INTERP   3   /* follows an IR_INTERP_STR for same dest  */
#define PRINT_ARG_STR_PTR  4   /* src1 = temp holding char* pointer       */

/* ─── Instruction ────────────────────────────────────────────────────────────── */

typedef struct {
    IROpcode   op;

    /* Primary three-address fields */
    IROperand  dest;          /* result / label declaration              */
    IROperand  src1;          /* first source                            */
    IROperand  src2;          /* second source                           */
    IROperand  extra_src;     /* third operand (ARRAY_STORE, SET_FIELD)  */

    /* String annotations */
    char      *str_extra;     /* callee name, var name, class, asm body  */
    char      *str_extra2;    /* field name, enum variant                */
    int        extra_int;     /* is_main flag, TypeKind, PRINT_ARG_* … */

    /* Variable-arity argument list (CALL, NEW, ARRAY_INIT) */
    IROperand *args;
    int        arg_count;

    /* Function parameters (IR_FUNC_BEGIN only) */
    IRParam   *params;
    int        param_count;

    /* Interpolated-string segments (IR_INTERP_STR only) */
    InterpSegment *extra_segs;
    int            extra_seg_count;
} IRInstr;

/* ─── Module ─────────────────────────────────────────────────────────────────── */

typedef struct {
    IRInstr *instrs;
    int      instr_count;
    int      instr_cap;

    /* AST metadata preserved for the codegen's class / enum registries */
    ClassNode **classes;
    int         class_count;
    EnumNode  **enums;
    int         enum_count;

    /* Std-include list needed by the codegen to select runtime modules */
    char      **includes;
    int         include_count;
} IRModule;

/* ─── API ────────────────────────────────────────────────────────────────────── */

IRModule  *ir_module_new(void);
void       ir_module_free(IRModule *mod);

/* Append a zeroed instruction of the given opcode and return a pointer to it. */
IRInstr   *ir_emit(IRModule *mod, IROpcode op);

/* Operand constructors */
IROperand  irop_none(void);
IROperand  irop_temp(int id);
IROperand  irop_const_int(long val);
IROperand  irop_const_str(const char *val);
IROperand  irop_const_bool(int val);
IROperand  irop_label(int id);

/* Dump / debug */
const char *ir_opcode_name(IROpcode op);
void        ir_dump(const IRModule *mod, FILE *out);

#endif /* IR_H */
