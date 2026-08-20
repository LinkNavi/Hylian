#ifndef HYOBJ_H
#define HYOBJ_H

#include "hyx64.h"
#include <stdint.h>
#include <stddef.h>

/* Format-agnostic object module: collects functions/data/relocs from
   libhyx64 output. obj_write_elf() is one backend consuming this; a PE or
   Mach-O writer later would consume the exact same struct. */

typedef struct {
    char   *name;
    size_t  text_offset; /* offset into the combined .text section */
    size_t  size;
} ObjFuncSym;

typedef struct {
    char    *name;
    size_t   offset;   /* offset into .rodata */
    size_t   len;
} ObjRodataSym;

typedef struct {
    char    *name;
    int      has_init;
    int64_t  init_val;
    int      size;
    size_t   offset;   /* offset into .data (if has_init) or .bss (if not) -
                           or into `section`'s own buffer, if section != NULL */
    char    *section;  /* NULL = default (.data/.bss) placement; else the name
                           of a custom section (see ObjExtraSection) this
                           global's bytes were written into instead. */
} ObjGlobalSym;

typedef struct {
    size_t       text_offset; /* offset into the combined .text section */
    const char  *symbol;
    X64RelocKind kind;
} ObjReloc;

typedef struct {
    uint8_t *data;
    size_t   len, cap;
} ObjBuf;

/* A PROGBITS section beyond the fixed .text/.rodata/.data/.bss set, created
   on first use by obj_add_global_bytes(). Needed for boot-protocol structs
   (e.g. Limine's ".limine_requests") that a bootloader scans for by exact
   section name before any of our code runs — an ordinary .data global isn't
   enough because the *name* of the section is what identifies the data, not
   just its bytes. */
typedef struct {
    char   *name;
    ObjBuf  data;
} ObjExtraSection;

typedef struct {
    ObjBuf text;
    ObjBuf rodata;
    ObjBuf data;
    size_t bss_size;

    ObjFuncSym   *funcs;   int func_count, func_cap;
    ObjRodataSym *strs;    int str_count, str_cap;
    ObjGlobalSym *globals; int global_count, global_cap;
    ObjReloc     *relocs;  int reloc_count, reloc_cap;

    ObjExtraSection *extra_sections; int extra_section_count, extra_section_cap;
} ObjModule;

ObjModule *obj_module_new(void);
void       obj_module_free(ObjModule *mod);

/* appends `code`'s bytes to .text, remaps `relocs`'s offsets (which are
   relative to `code` alone) into whole-module .text offsets, and records
   a global FUNC symbol at the function's start */
void obj_add_func(ObjModule *mod, const char *name, const X64Buf *code, const X64RelocList *relocs);

/* interns one MIR string constant into .rodata */
void obj_add_string(ObjModule *mod, const char *label, const char *data, size_t len);

/* declares a global var: goes to .data if has_init, .bss otherwise */
void obj_add_global(ObjModule *mod, const char *name, int has_init, int64_t init_val, int size);

/* declares a global var with arbitrary initialized byte content (not just a
   single 8-byte int) placed in a named custom section (created on first
   use) instead of .data - e.g. a Limine boot-protocol request struct, whose
   exact field bytes (magic numbers, revision, a null response pointer) must
   be laid out at compile time and found by the bootloader via section name
   before any of our code runs. */
void obj_add_global_bytes(ObjModule *mod, const char *name, const char *section,
                          const void *data, size_t size);

/* Like obj_add_global_bytes, but for a multi-byte (possibly >8 byte)
   initializer with no custom section - a compile-time-folded struct literal
   (see lower.c fold_struct_literal_to_bytes) that just needs to live in the
   ordinary .data section like any other initialized global. obj_add_global
   can't be reused for this: it only ever copies sizeof(int64_t) worth of
   bytes from a single scalar value, which would read past the end of a
   larger struct's data. */
void obj_add_global_data(ObjModule *mod, const char *name, const void *data, size_t size);

/* writes a Linux ELF64 relocatable object (.o) - functions are STB_GLOBAL,
   string/global symbols STB_LOCAL, anything referenced by a reloc that
   isn't one of the above becomes an STB_GLOBAL UNDEF symbol for the linker
   to resolve (runtime/libc functions, extern globals). */
int obj_write_elf(const ObjModule *mod, const char *path);

#endif
