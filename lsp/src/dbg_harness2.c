#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lsp_analysis.h"

static void test(LspProject *proj, const char *filepath, const char *src) {
    printf("=== testing ===\n%s\n---\n", src);
    lsp_project_update_file(proj, filepath, src);
    ProjectFile *pf = lsp_project_find_file(proj, filepath);
    printf("diag_count=%d\n", pf->diag_count);
    for (int i = 0; i < pf->diag_count; i++)
        printf("[%d] line=%d: %s\n", i, pf->diags[i].start_line, pf->diags[i].message);
}

int main(int argc, char **argv) {
    const char *root = argv[1];
    LspProject *proj = lsp_project_create(root);
    const char *filepath = argv[2];

    test(proj, filepath,
        "include {\n    vendors.raylib,\n}\n\nvoid main() {\n"
        "    Color bleh = Color { r: 1, g: 2, b: 3, a: 4 };\n"
        "    int q = bleh.r;\n"
        "}\n");

    test(proj, filepath,
        "include {\n    vendors.raylib,\n}\n\nvoid main() {\n"
        "    int q = r;\n"
        "}\n");

    return 0;
}
