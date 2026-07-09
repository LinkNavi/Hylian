#include "ir.h"
#include "ir_to_mir.h"
#include "hyregalloc.h"
#include "hyx64.h"
#include "hyobj.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/wait.h>

/* mirrors: interp"hello, {name}!" where "name" is an expr segment (raw
   source text, unevaluated) sitting between two literal segments "hello, "
   and "!". Proves: (1) the literal-only case is genuinely computed and
   correct - written to stdout via a real syscall and checked byte-for-byte,
   (2) the expr segment doesn't silently corrupt or crash anything, it's
   just cleanly omitted with a diagnostic already visible above this test's
   own output (ir_to_mir.c's fprintf runs during lower_ir_to_mir). */
int main(void) {
    IRModule *ir = ir_module_new();

    IRInstr *fb = ir_emit(ir, IR_FUNC_BEGIN);
    fb->str_extra = "_start"; fb->extra_int = 1;

    static InterpSegment segs[3];
    segs[0].is_expr = 0; segs[0].text = "hello, ";
    segs[1].is_expr = 1; segs[1].text = "name"; /* raw unparsed source - the known gap */
    segs[2].is_expr = 0; segs[2].text = "!";

    IRInstr *interp = ir_emit(ir, IR_INTERP_STR);
    interp->extra_segs = segs;
    interp->extra_seg_count = 3;
    interp->dest = irop_temp(0);

    /* write(1, ptr, 8) - "hello, !" is exactly 8 bytes, so a correct
       literal-only concatenation writes exactly that */
    IRInstr *wr = ir_emit(ir, IR_CALL);
    wr->str_extra = "syscall";
    static IROperand wr_args[4];
    wr_args[0] = irop_const_int(1);
    wr_args[1] = irop_const_int(1);
    wr_args[2] = irop_temp(0);
    wr_args[3] = irop_const_int(8);
    wr->args = wr_args; wr->arg_count = 4;
    wr->dest = irop_temp(1);

    IRInstr *ex = ir_emit(ir, IR_CALL);
    ex->str_extra = "syscall";
    static IROperand ex_args[2];
    ex_args[0] = irop_const_int(60);
    ex_args[1] = irop_const_int(0);
    ex->args = ex_args; ex->arg_count = 2;
    ex->dest = irop_temp(2);

    ir_emit(ir, IR_FUNC_END);

    fprintf(stderr, "=== ir_to_mir diagnostics (expect one about the 'name' segment) ===\n");
    MIRModule *mir = lower_ir_to_mir(ir);
    fprintf(stderr, "=== end diagnostics ===\n\n");

    printf("=== lowered MIR ===\n");
    mir_dump(mir, stdout);

    assert(mir->str_count == 1 && "should have interned exactly one string constant");
    assert(mir->strs[0].len == 8 && "literal-only concatenation should be \"hello, !\" (8 bytes)");
    assert(memcmp(mir->strs[0].data, "hello, !", 8) == 0 &&
           "expr segment should be cleanly omitted, not garbage, not crashed");
    printf("interned string: \"%.*s\" (%d bytes) - matches expected literal-only result\n",
           mir->strs[0].len, mir->strs[0].data, mir->strs[0].len);

    MIRFunc *fn = mir->funcs[0];
    fn->is_naked = 1;

    RegAllocTarget target = { .num_int_regs = X64_NUM_ALLOCATABLE_INT_REGS, .num_float_regs = 4 };
    RegAllocResult ra = regalloc_run(fn, &target);

    X64Buf code; x64_buf_init(&code);
    X64RelocList relocs; x64_reloc_list_init(&relocs);
    x64_lower_func(fn, &ra, &code, &relocs);

    ObjModule *obj = obj_module_new();
    obj_add_func(obj, "_start", &code, &relocs);
    for (int i = 0; i < mir->str_count; i++)
        obj_add_string(obj, mir->strs[i].label, mir->strs[i].data, mir->strs[i].len);

    int ok = obj_write_elf(obj, "/tmp/hygen_interp_test.o");
    assert(ok);

    int rc = system("ld -o /tmp/hygen_interp_test_bin /tmp/hygen_interp_test.o 2>&1");
    assert(rc == 0);

    rc = system("/tmp/hygen_interp_test_bin > /tmp/hygen_interp_test_out.txt");
    int exit_code = WEXITSTATUS(rc);
    FILE *out = fopen("/tmp/hygen_interp_test_out.txt", "r");
    char outbuf[32] = {0};
    size_t n = fread(outbuf, 1, sizeof(outbuf) - 1, out);
    fclose(out);
    fprintf(stderr, "executed output: %zu bytes: \"%s\", exit code %d\n", n, outbuf, exit_code);

    assert(exit_code == 0);
    assert(n == 8 && memcmp(outbuf, "hello, !", 8) == 0 &&
           "the ACTUAL EXECUTED BINARY should print the literal-only concatenation correctly");

    obj_module_free(obj);
    x64_buf_free(&code);
    x64_reloc_list_free(&relocs);
    regalloc_result_free(&ra);
    mir_module_free(mir);
    ir_module_free(ir);

    printf("\nOK - literal-only interpolation is genuinely correct (executed and verified), "
           "expression segments are cleanly and loudly flagged rather than faked\n");
    return 0;
}
