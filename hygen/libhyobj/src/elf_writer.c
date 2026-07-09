#define _POSIX_C_SOURCE 200809L
#include "hyobj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Minimal self-contained ELF64 structs - not relying on system <elf.h> so
   this stays portable to non-Linux build hosts cross-compiling for Linux. */

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Shdr;

typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} Elf64_Rela;

#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8

#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4

#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2

#define R_X86_64_PC32 2
#define R_X86_64_PLT32 4

#define SHN_UNDEF 0

/* section indices, fixed order */
enum { SEC_NULL, SEC_TEXT, SEC_RODATA, SEC_DATA, SEC_BSS, SEC_SYMTAB, SEC_STRTAB,
       SEC_RELA_TEXT, SEC_NOTE_GNU_STACK, SEC_SHSTRTAB, SEC_COUNT };

/* ---- growable byte buffer, local to this file ---- */
typedef struct { uint8_t *d; size_t len, cap; } Buf;
static void buf_init(Buf *b) { b->cap = 256; b->d = malloc(b->cap); b->len = 0; }
static void buf_write(Buf *b, const void *p, size_t n) {
    if (b->len + n > b->cap) { while (b->len + n > b->cap) b->cap *= 2; b->d = realloc(b->d, b->cap); }
    memcpy(b->d + b->len, p, n); b->len += n;
}
static size_t buf_add_str(Buf *b, const char *s) {
    size_t off = b->len;
    buf_write(b, s, strlen(s) + 1);
    return off;
}
static void buf_free(Buf *b) { free(b->d); }

/* symbol table entry we build up before serializing */
typedef struct {
    char    *name;
    int      binding;   /* STB_LOCAL / STB_GLOBAL */
    int      type;      /* STT_* */
    int      shndx;      /* section index, or SHN_UNDEF */
    uint64_t value;
    uint64_t size;
} SymEnt;

typedef struct { SymEnt *e; int count, cap; } SymList;
static void symlist_init(SymList *s) { s->cap = 16; s->e = malloc(s->cap * sizeof(SymEnt)); s->count = 0; }
static int symlist_find(SymList *s, const char *name) {
    for (int i = 0; i < s->count; i++) if (strcmp(s->e[i].name, name) == 0) return i;
    return -1;
}
static int symlist_add(SymList *s, const char *name, int binding, int type, int shndx, uint64_t value, uint64_t size) {
    if (s->count == s->cap) { s->cap *= 2; s->e = realloc(s->e, s->cap * sizeof(SymEnt)); }
    s->e[s->count].name = strdup(name);
    s->e[s->count].binding = binding;
    s->e[s->count].type = type;
    s->e[s->count].shndx = shndx;
    s->e[s->count].value = value;
    s->e[s->count].size = size;
    return s->count++;
}

int obj_write_elf(const ObjModule *mod, const char *path) {
    SymList syms;
    symlist_init(&syms);
    symlist_add(&syms, "", 0, 0, 0, 0, 0); /* index 0: reserved null symbol */

    /* locals first (ELF requires all STB_LOCAL symbols before any STB_GLOBAL
       in .symtab, and sh_info on .symtab records how many local entries there are) */
    for (int i = 0; i < mod->str_count; i++)
        symlist_add(&syms, mod->strs[i].name, STB_LOCAL, STT_OBJECT, SEC_RODATA,
                    mod->strs[i].offset, mod->strs[i].len);
    for (int i = 0; i < mod->global_count; i++) {
        const ObjGlobalSym *g = &mod->globals[i];
        symlist_add(&syms, g->name, STB_LOCAL, STT_OBJECT, g->has_init ? SEC_DATA : SEC_BSS,
                    g->offset, g->size);
    }
    int first_global = syms.count;

    for (int i = 0; i < mod->func_count; i++)
        symlist_add(&syms, mod->funcs[i].name, STB_GLOBAL, STT_FUNC, SEC_TEXT,
                    mod->funcs[i].text_offset, mod->funcs[i].size);

    /* any relocation target not already known becomes an external UNDEF symbol */
    for (int i = 0; i < mod->reloc_count; i++) {
        if (symlist_find(&syms, mod->relocs[i].symbol) < 0)
            symlist_add(&syms, mod->relocs[i].symbol, STB_GLOBAL, STT_NOTYPE, SHN_UNDEF, 0, 0);
    }

    /* .strtab for symbol names */
    Buf strtab; buf_init(&strtab);
    buf_write(&strtab, "", 1); /* index 0 must be the empty string */
    int *name_off = malloc(syms.count * sizeof(int));
    for (int i = 0; i < syms.count; i++)
        name_off[i] = (int)(i == 0 ? 0 : buf_add_str(&strtab, syms.e[i].name));

    /* .rela.text */
    Buf relas; buf_init(&relas);
    for (int i = 0; i < mod->reloc_count; i++) {
        int sym_idx = symlist_find(&syms, mod->relocs[i].symbol);
        Elf64_Rela r;
        r.r_offset = mod->relocs[i].text_offset;
        uint32_t rtype = mod->relocs[i].kind == X64_RELOC_REL32 ? R_X86_64_PLT32 : R_X86_64_PC32;
        r.r_info = ((uint64_t)sym_idx << 32) | rtype;
        r.r_addend = -4; /* field is the last 4 bytes of the instruction - see docs/mir.md */
        buf_write(&relas, &r, sizeof(r));
    }

    /* .symtab */
    Buf symtab; buf_init(&symtab);
    for (int i = 0; i < syms.count; i++) {
        Elf64_Sym s = {0};
        s.st_name = name_off[i];
        s.st_info = (uint8_t)((syms.e[i].binding << 4) | syms.e[i].type);
        s.st_shndx = (uint16_t)syms.e[i].shndx;
        s.st_value = syms.e[i].value;
        s.st_size = syms.e[i].size;
        buf_write(&symtab, &s, sizeof(s));
    }

    /* .shstrtab for section names */
    Buf shstrtab; buf_init(&shstrtab);
    buf_write(&shstrtab, "", 1);
    size_t off_text = buf_add_str(&shstrtab, ".text");
    size_t off_rodata = buf_add_str(&shstrtab, ".rodata");
    size_t off_data = buf_add_str(&shstrtab, ".data");
    size_t off_bss = buf_add_str(&shstrtab, ".bss");
    size_t off_symtab = buf_add_str(&shstrtab, ".symtab");
    size_t off_strtab = buf_add_str(&shstrtab, ".strtab");
    size_t off_rela_text = buf_add_str(&shstrtab, ".rela.text");
    size_t off_note_stack = buf_add_str(&shstrtab, ".note.GNU-stack");
    size_t off_shstrtab = buf_add_str(&shstrtab, ".shstrtab");

    /* ---- lay out the file: header, then each section's raw bytes back to
       back (8-byte aligned), then the section header table ---- */
    size_t file_off = sizeof(Elf64_Ehdr);
    size_t off_text_data = file_off; file_off += mod->text.len;
    while (file_off % 8) file_off++;
    size_t off_rodata_data = file_off; file_off += mod->rodata.len;
    while (file_off % 8) file_off++;
    size_t off_data_data = file_off; file_off += mod->data.len;
    while (file_off % 8) file_off++;
    /* .bss occupies no file space (SHT_NOBITS) */
    size_t off_symtab_data = file_off; file_off += symtab.len;
    while (file_off % 8) file_off++;
    size_t off_strtab_data = file_off; file_off += strtab.len;
    while (file_off % 8) file_off++;
    size_t off_rela_data = file_off; file_off += relas.len;
    while (file_off % 8) file_off++;
    size_t off_shstrtab_data = file_off; file_off += shstrtab.len;
    while (file_off % 8) file_off++;
    size_t off_shdrs = file_off;

    Elf64_Ehdr eh = {0};
    eh.e_ident[0] = 0x7f; eh.e_ident[1] = 'E'; eh.e_ident[2] = 'L'; eh.e_ident[3] = 'F';
    eh.e_ident[4] = 2; /* ELFCLASS64 */
    eh.e_ident[5] = 1; /* ELFDATA2LSB */
    eh.e_ident[6] = 1; /* EV_CURRENT */
    eh.e_type = 1;     /* ET_REL */
    eh.e_machine = 62; /* EM_X86_64 */
    eh.e_version = 1;
    eh.e_shoff = off_shdrs;
    eh.e_ehsize = sizeof(Elf64_Ehdr);
    eh.e_shentsize = sizeof(Elf64_Shdr);
    eh.e_shnum = SEC_COUNT;
    eh.e_shstrndx = SEC_SHSTRTAB;

    Elf64_Shdr sh[SEC_COUNT] = {0};
    sh[SEC_TEXT] = (Elf64_Shdr){ .sh_name = (uint32_t)off_text, .sh_type = SHT_PROGBITS,
        .sh_flags = SHF_ALLOC | SHF_EXECINSTR, .sh_offset = off_text_data, .sh_size = mod->text.len,
        .sh_addralign = 16 };
    sh[SEC_RODATA] = (Elf64_Shdr){ .sh_name = (uint32_t)off_rodata, .sh_type = SHT_PROGBITS,
        .sh_flags = SHF_ALLOC, .sh_offset = off_rodata_data, .sh_size = mod->rodata.len, .sh_addralign = 8 };
    sh[SEC_DATA] = (Elf64_Shdr){ .sh_name = (uint32_t)off_data, .sh_type = SHT_PROGBITS,
        .sh_flags = SHF_ALLOC | SHF_WRITE, .sh_offset = off_data_data, .sh_size = mod->data.len, .sh_addralign = 8 };
    sh[SEC_BSS] = (Elf64_Shdr){ .sh_name = (uint32_t)off_bss, .sh_type = SHT_NOBITS,
        .sh_flags = SHF_ALLOC | SHF_WRITE, .sh_offset = off_data_data, .sh_size = mod->bss_size, .sh_addralign = 8 };
    sh[SEC_SYMTAB] = (Elf64_Shdr){ .sh_name = (uint32_t)off_symtab, .sh_type = SHT_SYMTAB,
        .sh_offset = off_symtab_data, .sh_size = symtab.len, .sh_link = SEC_STRTAB,
        .sh_info = (uint32_t)first_global, .sh_addralign = 8, .sh_entsize = sizeof(Elf64_Sym) };
    sh[SEC_STRTAB] = (Elf64_Shdr){ .sh_name = (uint32_t)off_strtab, .sh_type = SHT_STRTAB,
        .sh_offset = off_strtab_data, .sh_size = strtab.len, .sh_addralign = 1 };
    sh[SEC_RELA_TEXT] = (Elf64_Shdr){ .sh_name = (uint32_t)off_rela_text, .sh_type = SHT_RELA,
        .sh_offset = off_rela_data, .sh_size = relas.len, .sh_link = SEC_SYMTAB, .sh_info = SEC_TEXT,
        .sh_addralign = 8, .sh_entsize = sizeof(Elf64_Rela) };
    sh[SEC_NOTE_GNU_STACK] = (Elf64_Shdr){ .sh_name = (uint32_t)off_note_stack, .sh_type = SHT_PROGBITS,
        .sh_flags = 0, .sh_offset = off_shstrtab_data, .sh_size = 0, .sh_addralign = 1 };
    sh[SEC_SHSTRTAB] = (Elf64_Shdr){ .sh_name = (uint32_t)off_shstrtab, .sh_type = SHT_STRTAB,
        .sh_offset = off_shstrtab_data, .sh_size = shstrtab.len, .sh_addralign = 1 };

    FILE *f = fopen(path, "wb");
    if (!f) {
        buf_free(&strtab); buf_free(&relas); buf_free(&symtab); buf_free(&shstrtab);
        free(name_off);
        for (int i = 0; i < syms.count; i++) free(syms.e[i].name);
        free(syms.e);
        return 0;
    }

    fwrite(&eh, sizeof(eh), 1, f);
    long pos = ftell(f);
    #define PAD_TO(target) do { while (pos < (long)(target)) { fputc(0, f); pos++; } } while (0)
    fwrite(mod->text.data, 1, mod->text.len, f); pos += mod->text.len; PAD_TO(off_rodata_data);
    fwrite(mod->rodata.data, 1, mod->rodata.len, f); pos += mod->rodata.len; PAD_TO(off_data_data);
    fwrite(mod->data.data, 1, mod->data.len, f); pos += mod->data.len; PAD_TO(off_symtab_data);
    fwrite(symtab.d, 1, symtab.len, f); pos += symtab.len; PAD_TO(off_strtab_data);
    fwrite(strtab.d, 1, strtab.len, f); pos += strtab.len; PAD_TO(off_rela_data);
    fwrite(relas.d, 1, relas.len, f); pos += relas.len; PAD_TO(off_shstrtab_data);
    fwrite(shstrtab.d, 1, shstrtab.len, f); pos += shstrtab.len; PAD_TO(off_shdrs);
    fwrite(sh, sizeof(sh), 1, f);
    #undef PAD_TO
    fclose(f);

    buf_free(&strtab); buf_free(&relas); buf_free(&symtab); buf_free(&shstrtab);
    free(name_off);
    for (int i = 0; i < syms.count; i++) free(syms.e[i].name);
    free(syms.e);
    return 1;
}
