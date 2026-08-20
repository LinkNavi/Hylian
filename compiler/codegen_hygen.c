#include "codegen_hygen.h"
#include "ir_to_mir.h"
#include "hymir.h"
#include "hyregalloc.h"
#include "hyx64.h"
#include "hyobj.h"

#include <stdio.h>
#include <string.h>

/* NOTE: `target` is accepted for interface parity with the old codegen_elf()
   signature. libhyx64/libhyobj always emit a plain x86-64 ELF64 relocatable
   object - nothing downstream of this point reads `target` at all, so
   "linux" and "limine" produce byte-identical .o files; the actual
   difference between a hosted Linux binary and a Limine kernel is entirely
   in how the .o gets LINKED (hosted gcc+libc vs `ld -T limine.ld
   -nostdlib`, see Zora's HylianBuilder), not in codegen. "macos"/"windows"
   are rejected because those need real PE/Mach-O writers that don't exist
   yet - a different object *format*, unlike limine.

   `freestanding` does matter here, just narrowly: it renames the emitted
   symbol for the user's `main` to `_start` (see the func-emission loop
   below) since a bootloader has no crt0 to call `main` for it. */
int codegen_hygen(IRModule *mod, const char *outfile,
                   const char *src_filename, const char *target,
                   int freestanding)
{
    (void)src_filename;

    if (target && strcmp(target, "linux") != 0 && strcmp(target, "limine") != 0) {
        fprintf(stderr, "hylian: hygen backend does not yet support target '%s' (only 'linux'/'limine' emit ELF64 today)\n", target);
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

        /* A bootloader (Limine included) jumps straight to a symbol named
           `_start` - there is no crt0 in a --freestanding build to call
           `main` for us. Rename just the emitted symbol for the user's
           `main`; nothing else in this object references it by name (no
           generated code ever CALLs main - it's the entry point, not a
           callee), so this is a pure relabeling with no relocation fallout. */
        const char *sym_name = fn->name;
        if (freestanding && strcmp(fn->name, "main") == 0) sym_name = "_start";

        obj_add_func(obj, sym_name, &code, &relocs);

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
        } else if (g->init_bytes) {
            /* Compile-time-folded struct literal with no @section - real
               multi-field byte content in the ordinary .data section.
               obj_add_global would only copy 8 bytes from a scalar here,
               overreading past a larger struct's actual data. */
            obj_add_global_data(obj, g->name, g->init_bytes, (size_t)g->size);
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
