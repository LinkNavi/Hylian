import re

with open("compiler/compiler.c", "r", encoding="utf-8") as f:
    src = f.read()

# 1. Remove all section banner comments (single-line and multi-line variants)
src = re.sub(r"/\* \u2500{3}[^\n]*\u2500+[^\n]*\n \*/\n", "", src)
src = re.sub(r"/\* \u2500{3}[^\n]*\u2500+ \*/\n\n", "\n", src)
src = re.sub(r"/\* \u2500{3}[^\n]*\u2500+ \*/\n", "", src)

# 2. Remove /* replace dots with slashes */
src = src.replace("    /* replace dots with slashes */\n", "")

# 3. Remove /* join src_dir + "/" + rel */
src = src.replace('    /* join src_dir + "/" + rel */\n', "")

# 4. Remove /* blank line */ on the empty-string continue
src = src.replace(
    "        if (*s == '\\0') continue; /* blank line */",
    "        if (*s == '\\0') continue;",
)

# 5. Remove /* module ... — ignore */
src = src.replace("        /* module ... \u2014 ignore */\n        ", "        ")

# 6. Remove /* link "libname" */
src = src.replace('        /* link "libname" */\n        ', "        ")

# 7. Remove /* skip opening quote */
src = src.replace(
    "                    q++; /* skip opening quote */", "                    q++;"
)

# 8. Remove /* Append all declarations from src into dst */
src = src.replace("    /* Append all declarations from src into dst */\n", "")

# 9. Remove /* Merge ccpinclude paths (deduped) */
src = src.replace("    /* Merge ccpinclude paths (deduped) */\n", "")

# 10. Remove /* Inside a class block → MethodNode */ comment
src = src.replace(
    "            } else if (current_class) {\n                /* Inside a class block \u2192 MethodNode */\n",
    "            } else if (current_class) {\n",
)

# 11. Remove /* Top-level → FuncNode */ comment
src = src.replace(
    "            } else {\n                /* Top-level \u2192 FuncNode */\n",
    "            } else {\n",
)

# 12. Remove /* Copy dep's std includes */
src = src.replace("        /* Copy dep's std includes */\n", "")

# 13. Remove /* Set default output filename if not specified */
src = src.replace("    /* Set default output filename if not specified */\n", "")

# 14. Remove /* Lower AST → IR */
src = src.replace("    /* Lower AST \u2192 IR */\n", "")

# 15. Remove /* Optimize IR */
src = src.replace("    /* Optimize IR */\n", "")

# 16. Remove /* Generate assembly from IR */
src = src.replace("    /* Generate assembly from IR */\n", "")

with open("compiler/compiler.c", "w", encoding="utf-8") as f:
    f.write(src)

print("compiler.c done. Lines:", src.count("\n"))
