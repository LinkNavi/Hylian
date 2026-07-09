#include "hymir.h"
#include "hyregalloc.h"
#include "hyx64.h"
#include "hyobj.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/wait.h>

/* builds a freestanding _start that computes 40 + 2 via inline asm
   referencing two Hylian-style variables through {0}/{1} placeholders,
   then exits with that value as the exit code - so the OS exit status
   itself proves the asm block executed correctly. */
int main(void) {
    MIRModule *mod = mir_module_new();
    MIRFunc *fn = mir_func_new(mod, "_start");
    fn->is_naked = 1;

    int v_a = mir_new_vreg(fn);
    int v_b = mir_new_vreg(fn);
    MIRInstr *m;

    m = mir_emit(fn, MIR_MOV); m->dest = mir_vreg(v_a, MIR_I64); m->src1 = mir_imm_int(40, MIR_I64);
    m = mir_emit(fn, MIR_MOV); m->dest = mir_vreg(v_b, MIR_I64); m->src1 = mir_imm_int(2, MIR_I64);

    /* asm text as ir_to_mir.c would produce it after rewriting {a}/{b} -> {0}/{1} */
    MIRValue *args = malloc(2 * sizeof(MIRValue));
    args[0] = mir_vreg(v_a, MIR_I64);
    args[1] = mir_vreg(v_b, MIR_I64);
    MIRInstr *asmi = mir_emit(fn, MIR_ASM_TEXT);
    asmi->asm_text = "add {0}, {1}\nmov rdi, {0}\nmov rax, 60\nsyscall\n";
    asmi->args = args;
    asmi->arg_count = 2;

    RegAllocTarget target = { .num_int_regs = X64_NUM_ALLOCATABLE_INT_REGS, .num_float_regs = 4 };
    RegAllocResult ra = regalloc_run(fn, &target);

    X64Buf code; x64_buf_init(&code);
    X64RelocList relocs; x64_reloc_list_init(&relocs);
    x64_lower_func(fn, &ra, &code, &relocs);

    ObjModule *obj = obj_module_new();
    obj_add_func(obj, "_start", &code, &relocs);

    int ok = obj_write_elf(obj, "/tmp/hygen_miniasm_test.o");
    assert(ok);

    fprintf(stderr, "--- objdump -dr ---\n");
    int rc = system("objdump -dr /tmp/hygen_miniasm_test.o");

    rc |= system("ld -o /tmp/hygen_miniasm_test_bin /tmp/hygen_miniasm_test.o 2>&1");
    fprintf(stderr, "link result: %d\n", rc);
    assert(rc == 0);

    rc = system("/tmp/hygen_miniasm_test_bin");
    int exit_code = WEXITSTATUS(rc);
    fprintf(stderr, "exit code: %d (expect 42, computed entirely inside the asm block)\n", exit_code);
    assert(exit_code == 42 && "inline asm block should compute 40+2=42 via {0}/{1} placeholders");

    obj_module_free(obj);
    x64_buf_free(&code);
    x64_reloc_list_free(&relocs);
    regalloc_result_free(&ra);
    mir_module_free(mod);

    printf("\nOK - mini-assembler proven: MIR_ASM_TEXT with {N} placeholders resolved "
           "post-regalloc, encoded, linked, executed, produced the right answer\n");
    return 0;
}
