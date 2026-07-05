typedef enum { TY_I8, TY_I16, TY_I32, TY_I64, TY_U8, TY_U16, TY_U32, TY_U64, TY_F32, TY_F64, TY_PTR } MIRType;

typedef enum { LOC_REG, LOC_STACK, LOC_IMM } MIRLocKind;

typedef struct {
    MIRLocKind kind;
    MIRType    type;
    union { int reg; int stack_off; int64_t imm; };
} MIRValue;
