#include "ir.h"
#include "ir_to_mir.h"
#include <stdio.h>
#include <assert.h>

/* builds IR equivalent to:
   func main() {
       let a: int = 3;
       let b: float = 2.0;
       let c: float = a as float;   // exercises the cast fix
       return c + b;
   }
*/
int main(void) {
    IRModule *ir = ir_module_new();

    IRInstr *fb = ir_emit(ir, IR_FUNC_BEGIN);
    fb->str_extra = "main";
    fb->extra_int = 1;

    IRInstr *alloc_a = ir_emit(ir, IR_ALLOCA);
    alloc_a->str_extra = "a"; alloc_a->str_extra2 = "int";

    IRInstr *c1 = ir_emit(ir, IR_CONST_INT);
    c1->dest = irop_temp(0); c1->src1 = irop_const_int(3);

    IRInstr *store_a = ir_emit(ir, IR_STORE_VAR);
    store_a->str_extra = "a"; store_a->src1 = irop_temp(0);

    IRInstr *alloc_b = ir_emit(ir, IR_ALLOCA);
    alloc_b->str_extra = "b"; alloc_b->str_extra2 = "float";

    IRInstr *c2 = ir_emit(ir, IR_CONST_FLOAT);
    c2->dest = irop_temp(1); c2->src1 = irop_const_float(2.0);

    IRInstr *store_b = ir_emit(ir, IR_STORE_VAR);
    store_b->str_extra = "b"; store_b->src1 = irop_temp(1);

    IRInstr *load_a = ir_emit(ir, IR_LOAD_VAR);
    load_a->str_extra = "a"; load_a->dest = irop_temp(2);

    IRInstr *cast = ir_emit(ir, IR_CAST);
    cast->str_extra = "float"; cast->src1 = irop_temp(2); cast->dest = irop_temp(3);

    IRInstr *load_b = ir_emit(ir, IR_LOAD_VAR);
    load_b->str_extra = "b"; load_b->dest = irop_temp(4);

    IRInstr *add = ir_emit(ir, IR_ADD);
    add->src1 = irop_temp(3); add->src2 = irop_temp(4); add->dest = irop_temp(5);

    IRInstr *ret = ir_emit(ir, IR_RETURN);
    ret->src1 = irop_temp(5);

    ir_emit(ir, IR_FUNC_END);

    printf("=== source IR ===\n");
    ir_dump(ir, stdout);

    MIRModule *mir = lower_ir_to_mir(ir);

    printf("\n=== lowered MIR ===\n");
    mir_dump(mir, stdout);

    MIRVerifyResult vr = mir_verify(mir);
    if (!vr.ok) { fprintf(stderr, "verify failed: %s\n", vr.msg); assert(0); }

    /* the whole point: cast must NOT be a bare mov/bitcast - it's int->float */
    MIRFunc *fn = mir->funcs[0];
    int found_i2f = 0;
    for (int i = 0; i < fn->count; i++)
        if (fn->instrs[i].op == MIR_I2F) found_i2f = 1;
    assert(found_i2f && "cast lowering regressed: expected MIR_I2F for int->float cast");

    /* the add must be float add, not integer add, since operand is float */
    int found_fadd = 0;
    for (int i = 0; i < fn->count; i++)
        if (fn->instrs[i].op == MIR_FADD) found_fadd = 1;
    assert(found_fadd && "add on float operands should lower to MIR_FADD");

    mir_module_free(mir);
    ir_module_free(ir);
    printf("\nOK\n");
    return 0;
}
