#include "hymir.h"
#include "hyregalloc.h"
#include "hyx64.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

/* Test 1: func ret42() { return 42; } - exact byte check.
   Expected (no locals, no spills, frame_size=0):
     55                push rbp
     48 89 e5          mov rbp, rsp
     48 b8 2a00000000000000  movabs rax, 42
     c9                leave
     c3                ret
*/
static void test_trivial(void) {
    MIRModule *mod = mir_module_new();
    MIRFunc *fn = mir_func_new(mod, "ret42");
    MIRInstr *m = mir_emit(fn, MIR_RET);
    m->src1 = mir_imm_int(42, MIR_I64);

    RegAllocTarget target = { .num_int_regs = X64_NUM_ALLOCATABLE_INT_REGS, .num_float_regs = 4 };
    RegAllocResult ra = regalloc_run(fn, &target);

    X64Buf code; x64_buf_init(&code);
    X64RelocList relocs; x64_reloc_list_init(&relocs);
    x64_lower_func(fn, &ra, &code, &relocs);

    uint8_t expected[] = {
        0x55,
        0x48, 0x89, 0xE5,
        0x48, 0xB8, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xC9,
        0xC3,
    };
    if (code.len != sizeof(expected)) {
        fprintf(stderr, "got:      "); for (size_t i = 0; i < code.len; i++) fprintf(stderr, "%02X ", code.data[i]); fprintf(stderr, "\n");
        fprintf(stderr, "expected: "); for (size_t i = 0; i < sizeof(expected); i++) fprintf(stderr, "%02X ", expected[i]); fprintf(stderr, "\n");
    }
    assert(code.len == sizeof(expected));
    assert(memcmp(code.data, expected, sizeof(expected)) == 0 && "byte mismatch in trivial ret42()");

    x64_buf_free(&code);
    x64_reloc_list_free(&relocs);
    regalloc_result_free(&ra);
    mir_module_free(mod);
    printf("test_trivial: OK\n");
}

/* Test 2: the loop-sum function from test_regalloc.c, lowered end to end,
   disassembled with objdump so a human (or future CI) can eyeball it. */
static void test_loop(void) {
    MIRModule *mod = mir_module_new();
    MIRFunc *fn = mir_func_new(mod, "loop_sum");

    int v_sum = mir_new_vreg(fn);
    int v_i   = mir_new_vreg(fn);
    int v_t   = mir_new_vreg(fn);
    MIRInstr *m;

    m = mir_emit(fn, MIR_MOV); m->dest = mir_vreg(v_sum, MIR_I64); m->src1 = mir_imm_int(0, MIR_I64);
    m = mir_emit(fn, MIR_MOV); m->dest = mir_vreg(v_i, MIR_I64);   m->src1 = mir_imm_int(0, MIR_I64);
    m = mir_emit(fn, MIR_LABEL); m->dest = mir_label(0);
    m = mir_emit(fn, MIR_CMP_LT); m->dest = mir_vreg(v_t, MIR_I64);
    m->src1 = mir_vreg(v_i, MIR_I64); m->src2 = mir_imm_int(10, MIR_I64);
    m = mir_emit(fn, MIR_JMP_UNLESS); m->src1 = mir_vreg(v_t, MIR_I64); m->dest = mir_label(1);
    m = mir_emit(fn, MIR_ADD); m->dest = mir_vreg(v_sum, MIR_I64);
    m->src1 = mir_vreg(v_sum, MIR_I64); m->src2 = mir_vreg(v_i, MIR_I64);
    m = mir_emit(fn, MIR_ADD); m->dest = mir_vreg(v_i, MIR_I64);
    m->src1 = mir_vreg(v_i, MIR_I64); m->src2 = mir_imm_int(1, MIR_I64);
    m = mir_emit(fn, MIR_JMP); m->dest = mir_label(0);
    m = mir_emit(fn, MIR_LABEL); m->dest = mir_label(1);
    m = mir_emit(fn, MIR_RET); m->src1 = mir_vreg(v_sum, MIR_I64);

    MIRVerifyResult vr = mir_verify(mod);
    if (!vr.ok) { fprintf(stderr, "verify failed: %s\n", vr.msg); assert(0); }

    RegAllocTarget target = { .num_int_regs = X64_NUM_ALLOCATABLE_INT_REGS, .num_float_regs = 4 };
    RegAllocResult ra = regalloc_run(fn, &target);

    X64Buf code; x64_buf_init(&code);
    X64RelocList relocs; x64_reloc_list_init(&relocs);
    x64_lower_func(fn, &ra, &code, &relocs);

    FILE *f = fopen("/tmp/loop_sum.bin", "wb");
    fwrite(code.data, 1, code.len, f);
    fclose(f);

    printf("test_loop: %zu bytes emitted, %d spill slots, disassembly:\n", code.len, ra.num_int_spill_slots);
    int rc = system("objdump -D -b binary -m i386:x86-64 -M intel /tmp/loop_sum.bin | tail -n +8");
    (void)rc;

    assert(code.len > 0);
    x64_buf_free(&code);
    x64_reloc_list_free(&relocs);
    regalloc_result_free(&ra);
    mir_module_free(mod);
    printf("test_loop: OK\n");
}

int main(void) {
    test_trivial();
    test_loop();
    printf("\nALL OK\n");
    return 0;
}
