#define _POSIX_C_SOURCE 200809L
#include "hyobj.h"
#include <stdlib.h>
#include <string.h>

static void objbuf_init(ObjBuf *b) { b->cap = 256; b->data = malloc(b->cap); b->len = 0; }
static void objbuf_free(ObjBuf *b) { free(b->data); }
static void objbuf_write(ObjBuf *b, const void *data, size_t n) {
    if (b->len + n > b->cap) {
        while (b->len + n > b->cap) b->cap *= 2;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, data, n);
    b->len += n;
}
static void objbuf_push(ObjBuf *b, uint8_t byte) { objbuf_write(b, &byte, 1); }

ObjModule *obj_module_new(void) {
    ObjModule *m = calloc(1, sizeof(ObjModule));
    objbuf_init(&m->text);
    objbuf_init(&m->rodata);
    objbuf_init(&m->data);
    m->func_cap = 8; m->funcs = malloc(m->func_cap * sizeof(ObjFuncSym));
    m->str_cap = 8; m->strs = malloc(m->str_cap * sizeof(ObjRodataSym));
    m->global_cap = 8; m->globals = malloc(m->global_cap * sizeof(ObjGlobalSym));
    m->reloc_cap = 16; m->relocs = malloc(m->reloc_cap * sizeof(ObjReloc));
    return m;
}

void obj_module_free(ObjModule *mod) {
    objbuf_free(&mod->text);
    objbuf_free(&mod->rodata);
    objbuf_free(&mod->data);
    for (int i = 0; i < mod->func_count; i++) free(mod->funcs[i].name);
    for (int i = 0; i < mod->str_count; i++) free(mod->strs[i].name);
    for (int i = 0; i < mod->global_count; i++) {
        free(mod->globals[i].name);
        free(mod->globals[i].section);
    }
    for (int i = 0; i < mod->extra_section_count; i++) {
        free(mod->extra_sections[i].name);
        objbuf_free(&mod->extra_sections[i].data);
    }
    free(mod->funcs); free(mod->strs); free(mod->globals); free(mod->relocs);
    free(mod->extra_sections);
    free(mod);
}

void obj_add_func(ObjModule *mod, const char *name, const X64Buf *code, const X64RelocList *relocs) {
    size_t base = mod->text.len;
    objbuf_write(&mod->text, code->data, code->len);

    if (mod->func_count == mod->func_cap) {
        mod->func_cap *= 2;
        mod->funcs = realloc(mod->funcs, mod->func_cap * sizeof(ObjFuncSym));
    }
    mod->funcs[mod->func_count].name = strdup(name);
    mod->funcs[mod->func_count].text_offset = base;
    mod->funcs[mod->func_count].size = code->len;
    mod->func_count++;

    for (int i = 0; i < relocs->count; i++) {
        if (mod->reloc_count == mod->reloc_cap) {
            mod->reloc_cap *= 2;
            mod->relocs = realloc(mod->relocs, mod->reloc_cap * sizeof(ObjReloc));
        }
        mod->relocs[mod->reloc_count].text_offset = base + relocs->relocs[i].offset;
        mod->relocs[mod->reloc_count].symbol = relocs->relocs[i].symbol;
        mod->relocs[mod->reloc_count].kind = relocs->relocs[i].kind;
        mod->reloc_count++;
    }
}

void obj_add_string(ObjModule *mod, const char *label, const char *data, size_t len) {
    size_t off = mod->rodata.len;
    objbuf_write(&mod->rodata, data, len);
    objbuf_push(&mod->rodata, 0); /* NUL terminator, harmless for non-string byte blobs too */

    if (mod->str_count == mod->str_cap) {
        mod->str_cap *= 2;
        mod->strs = realloc(mod->strs, mod->str_cap * sizeof(ObjRodataSym));
    }
    mod->strs[mod->str_count].name = strdup(label);
    mod->strs[mod->str_count].offset = off;
    mod->strs[mod->str_count].len = len;
    mod->str_count++;
}

void obj_add_global(ObjModule *mod, const char *name, int has_init, int64_t init_val, int size) {
    if (mod->global_count == mod->global_cap) {
        mod->global_cap *= 2;
        mod->globals = realloc(mod->globals, mod->global_cap * sizeof(ObjGlobalSym));
    }
    ObjGlobalSym *g = &mod->globals[mod->global_count++];
    g->name = strdup(name);
    g->has_init = has_init;
    g->init_val = init_val;
    g->size = size;
    g->section = NULL;
    if (has_init) {
        g->offset = mod->data.len;
        objbuf_write(&mod->data, &init_val, size);
    } else {
        g->offset = mod->bss_size;
        mod->bss_size += size;
    }
}

void obj_add_global_data(ObjModule *mod, const char *name, const void *data, size_t size) {
    if (mod->global_count == mod->global_cap) {
        mod->global_cap *= 2;
        mod->globals = realloc(mod->globals, mod->global_cap * sizeof(ObjGlobalSym));
    }
    ObjGlobalSym *g = &mod->globals[mod->global_count++];
    g->name = strdup(name);
    g->has_init = 1;
    g->init_val = 0;
    g->size = (int)size;
    g->section = NULL;
    g->offset = mod->data.len;
    objbuf_write(&mod->data, data, size);
}

static ObjBuf *obj_get_or_add_section(ObjModule *mod, const char *name) {
    for (int i = 0; i < mod->extra_section_count; i++)
        if (strcmp(mod->extra_sections[i].name, name) == 0)
            return &mod->extra_sections[i].data;
    if (mod->extra_section_count == mod->extra_section_cap) {
        mod->extra_section_cap = mod->extra_section_cap ? mod->extra_section_cap * 2 : 4;
        mod->extra_sections = realloc(mod->extra_sections,
                                      mod->extra_section_cap * sizeof(ObjExtraSection));
    }
    ObjExtraSection *s = &mod->extra_sections[mod->extra_section_count++];
    s->name = strdup(name);
    objbuf_init(&s->data);
    return &s->data;
}

void obj_add_global_bytes(ObjModule *mod, const char *name, const char *section,
                          const void *data, size_t size) {
    if (mod->global_count == mod->global_cap) {
        mod->global_cap *= 2;
        mod->globals = realloc(mod->globals, mod->global_cap * sizeof(ObjGlobalSym));
    }
    ObjGlobalSym *g = &mod->globals[mod->global_count++];
    g->name = strdup(name);
    g->has_init = 1;
    g->init_val = 0;
    g->size = (int)size;
    g->section = strdup(section);

    ObjBuf *buf = obj_get_or_add_section(mod, section);
    g->offset = buf->len;
    objbuf_write(buf, data, size);
}
