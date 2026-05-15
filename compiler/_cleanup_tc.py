import re

with open("compiler/typecheck.c", "r", encoding="utf-8") as f:
    src = f.read()

# 1. Remove multi-line section banner comments: /* ─── Name ──...──\n */
src = re.sub(r"/\* \u2500{3}[^\n]*\u2500+[^\n]*\n \*/\n", "", src)
# Also handle single-line banners: /* ─── Name ──...── */
src = re.sub(r"/\* \u2500{3}[^\n]*\u2500+ \*/\n", "", src)

# 2. Remove /* bumped */ and /* bumped: ... */ inline annotations
src = re.sub(r"  /\* bumped(?:[^*]|\*(?!/))*\*/", "", src)

# 3. Remove /* Reset state */
src = src.replace("  /* Reset state */\n", "")

# 4. Remove /* Register fields */
src = src.replace("      /* Register fields */\n", "")

# 5. Remove /* Infer constructor body */ and /* Infer each method body */
src = src.replace("      /* Infer constructor body */\n", "")
src = src.replace("      /* Infer each method body */\n", "")

# 6. Strip the ── decoration from EnumName.Variant comment
src = src.replace(
    "    /* \u2500\u2500 EnumName.Variant \u2192 int \u2500\u2500 */",
    "    /* EnumName.Variant \u2192 int */",
)

# 7. Strip the ── decoration from Module.CONST comment
src = src.replace(
    "    /* \u2500\u2500 Module.CONST \u2192 resolve silently \u2500\u2500 */",
    "    /* Module.CONST \u2192 resolve silently */",
)

# 8. Remove /* Built-in array methods */
src = src.replace("      /* Built-in array methods */\n", "")

with open("compiler/typecheck.c", "w", encoding="utf-8") as f:
    f.write(src)

print("typecheck.c done. Lines:", src.count("\n"))
