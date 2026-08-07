#include "codegen_hygen.h"
#include "ir_to_mir.h"
#include "hymir.h"
#include "hyregalloc.h"
#include "hyx64.h"
#include "hyobj.h"

#include <stdio.h>
#include <string.h>

/* NOTE: `target` and `freestanding` are accepted for interface parity with
   the old codegen_elf() signature. libhyx64/libhyobj currently only emit
   linux x64 ELF; wire additional targets/backends here as they're added. */
int codegen_hygen(IRModule *mod, const char *outfile,
                   const char *src_filename, const char *target,
                   int freestanding)
{
    (void)src_filename;
    (void)freestanding;

    if (target && strcmp(target, "linux") != 0) {
        fprintf(stderr, "hylian: hygen backend does not yet support target '%s' (only 'linux' is implemented)\n", target);
        return 1;
    }

    MIRModule *mir = lower_ir_to_mir(mod);
    if (!mir) {
        fprintf(stderr, "hylian: hygen: IR -> MIR lowering failed\n");
        return 1;
    }

    MIRVerifyResult vr = mir_verify(mir);
    if (!vr.ok) {
        fprintf(stderr, "hylian: hygen: MIR verification failed: %s\n", vr.msg);
        mir_module_free(mir);
        return 1;
    }

    ObjModule *obj = obj_module_new();
    if (!obj) {
        fprintf(stderr, "hylian: hygen: failed to allocate object module\n");
        mir_module_free(mir);
        return 1;
    }

    RegAllocTarget ratarget = {
        .num_int_regs = X64_NUM_ALLOCATABLE_INT_REGS,
        .num_float_regs = 4,
        .num_callee_saved_int_regs = X64_NUM_CALLEE_SAVED_INT_REGS,
        .num_callee_saved_float_regs = X64_NUM_CALLEE_SAVED_FLOAT_REGS,
    };

    int ok = 1;
    for (int i = 0; i < mir->func_count; i++) {
        MIRFunc *fn = mir->funcs[i];

        RegAllocResult ra = regalloc_run(fn, &ratarget);

        X64Buf code;
        x64_buf_init(&code);
        X64RelocList relocs;
        x64_reloc_list_init(&relocs);

        x64_lower_func(fn, &ra, &code, &relocs);

        obj_add_func(obj, fn->name, &code, &relocs);

        x64_buf_free(&code);
        x64_reloc_list_free(&relocs);
        regalloc_result_free(&ra);
    }

    for (int i = 0; i < mir->str_count; i++) {
        MIRStringConst *s = &mir->strs[i];
        obj_add_string(obj, s->label, s->data, (size_t)s->len);
    }

    for (int i = 0; i < mir->global_count; i++) {
        MIRGlobalVar *g = &mir->globals[i];
        if (g->section) {
            /* Custom-section global (e.g. a Limine boot-protocol request):
               always written as explicit bytes, whether from a real
               compile-time-folded initializer (init_bytes) or a value-less
               declaration (falls back to init_val, typically 0). */
            if (g->init_bytes) {
                obj_add_global_bytes(obj, g->name, g->section, g->init_bytes, (size_t)g->size);
            } else {
                obj_add_global_bytes(obj, g->name, g->section, &g->init_val, (size_t)g->size);
            }
        } else {
            obj_add_global(obj, g->name, g->has_init, g->init_val, g->size);
        }
    }

    if (!obj_write_elf(obj, outfile)) {
        fprintf(stderr, "hylian: hygen: failed to write ELF object '%s'\n", outfile);
        ok = 0;
    }

    obj_module_free(obj);
    mir_module_free(mir);
    return ok ? 0 : 1;
}
