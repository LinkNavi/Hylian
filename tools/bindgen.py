#!/usr/bin/env python3
"""
bindgen.py — Hylian FFI binding generator
==========================================
Parses a C header file using libclang and emits a .hyi vendor interface file
that the Hylian compiler and linkle build system can consume directly.

Usage
-----
    python3 tools/bindgen.py [options] <header>

    python3 tools/bindgen.py /usr/include/SDL2/SDL.h \\
        --module vendors.sdl2 \\
        --link   libSDL2-2.0.so.0 \\
        --out    Bindings/sdl2/sdl2.hyi

Options
-------
    --module   <name>       Module name to emit (default: vendors.<stem>)
    --link     <lib>        Shared library soname — can be repeated
    --out      <path>       Output path (default: stdout)
    --include  <dir>        Extra -I flag for clang — can be repeated
    --filter   <prefix>     Only emit symbols whose name starts with prefix
                            (can be repeated; default = all symbols)
    --no-structs            Skip struct declarations
    --no-enums              Skip enum → const blocks
    --no-functions          Skip function declarations
    --opaque   <TypeName>   Treat this struct as an opaque class handle
                            (can be repeated)
    --verbose               Print skipped/unknown types to stderr
    --help, -h              Show this message

Requirements
------------
    pip install libclang

    On Debian/Ubuntu the native library is provided by:
        apt install libclang-dev
"""

import argparse
import os
import re
import subprocess
import sys
import textwrap
from pathlib import Path

# ── libclang import with friendly error ──────────────────────────────────────

try:
    import clang.cindex as clang
except ImportError:
    print(
        "error: libclang Python bindings not found.\n"
        "       Install them with:  pip install libclang\n"
        "       On Debian/Ubuntu also run:  apt install libclang-dev",
        file=sys.stderr,
    )
    sys.exit(1)


def _find_clang_resource_dir() -> str | None:
    """
    Ask the system clang where its builtin resource headers live
    (the directory that contains stddef.h, limits.h, etc.).
    Returns the include path string, or None if clang isn't on PATH.
    """
    import subprocess

    for binary in ("clang", "clang-15", "clang-14", "clang-16", "clang-17", "clang-18"):
        try:
            r = subprocess.run(
                [binary, "-print-resource-dir"],
                capture_output=True,
                text=True,
                timeout=5,
            )
            if r.returncode == 0:
                rdir = r.stdout.strip()
                inc = os.path.join(rdir, "include")
                if os.path.isdir(inc):
                    return inc
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
    return None


# ── Colour helpers ────────────────────────────────────────────────────────────


def _supports_color():
    return hasattr(sys.stderr, "isatty") and sys.stderr.isatty()


def _c(code, text):
    return f"\033[{code}m{text}\033[0m" if _supports_color() else text


def warn(msg):
    print(_c("33", f"warn:  {msg}"), file=sys.stderr)


def info(msg):
    print(_c("2", f"info:  {msg}"), file=sys.stderr)


def err(msg):
    print(_c("31", f"error: {msg}"), file=sys.stderr)


# ── C type → Hylian type mapping ─────────────────────────────────────────────
#
# libclang gives us fully spelled-out canonical C types.  We map them to the
# closest Hylian type.  Pointer-to-opaque-struct stays as the struct name
# (the class handle pattern).

# Exact canonical spelling → Hylian type
_EXACT: dict[str, str] = {
    "void": "void",
    "char": "int",  # C char used as number
    "signed char": "int",
    "unsigned char": "uint8",
    "short": "int",
    "unsigned short": "uint",
    "int": "int",
    "unsigned int": "uint",
    "long": "int",
    "unsigned long": "uint",
    "long long": "int",
    "unsigned long long": "uint64",
    "float": "float",
    "double": "float",
    "long double": "float",
    "_Bool": "bool",
    "bool": "bool",
    # Fixed-width (SDL / stdint typedefs resolve to these canonically)
    "int8_t": "int8",
    "uint8_t": "uint8",
    "int16_t": "int16",
    "uint16_t": "uint16",
    "int32_t": "int32",
    "uint32_t": "uint32",
    "int64_t": "int64",
    "uint64_t": "uint64",
    "Sint8": "int8",
    "Sint16": "int16",
    "Sint32": "int32",
    "Sint64": "int64",
    "Uint8": "uint8",
    "Uint16": "uint16",
    "Uint32": "uint32",
    "Uint64": "uint64",
    # size / ptrdiff
    "size_t": "uint64",
    "ptrdiff_t": "int64",
    "intptr_t": "int64",
    "uintptr_t": "uint64",
    # char* variants → str
    "char *": "str",
    "const char *": "str",
    "char *const": "str",
    "const char *const": "str",
    # void pointer → *void
    "void *": "*void",
    "const void *": "*void",
}

# Regex patterns matched against the canonical spelling for anything not exact
_PATTERNS: list[tuple[re.Pattern, str]] = [
    (re.compile(r"^(const\s+)?char\s*\*"), "str"),
    (re.compile(r"^(const\s+)?void\s*\*"), "*void"),
    (re.compile(r"^(const\s+)?unsigned char\s*\*"), "*void"),
]

# SDL-specific platform typedefs that clang sees as their underlying type
_SDL_TYPEDEFS: dict[str, str] = {
    "SDL_bool": "bool",
    "SDL_AudioDeviceID": "uint32",
    "SDL_JoystickID": "int32",
    "SDL_TouchID": "int64",
    "SDL_FingerID": "int64",
    "SDL_GestureID": "int64",
    "SDL_GUID": "*void",
}


def _map_type(
    cursor_type: "clang.Type",
    opaque_set: set[str],
    verbose: bool,
    original_spelling: str = "",
) -> str | None:
    """
    Map a clang.Type to a Hylian type string.
    Returns None if the type cannot be represented (caller decides what to do).
    """
    kind = cursor_type.kind

    # Pointer types — recurse on pointee
    if kind == clang.TypeKind.POINTER:
        pointee = cursor_type.get_pointee()
        ptk = pointee.kind

        # pointer-to-void
        if ptk == clang.TypeKind.VOID:
            return "*void"
        # pointer-to-char → str
        if ptk in (
            clang.TypeKind.CHAR_S,
            clang.TypeKind.CHAR_U,
            clang.TypeKind.SCHAR,
            clang.TypeKind.UCHAR,
        ):
            return "str"
        # pointer-to-function → skip
        if ptk == clang.TypeKind.FUNCTIONPROTO:
            return None
        # pointer-to-pointer → *void (good enough for FFI)
        if ptk == clang.TypeKind.POINTER:
            return "*void"

        # pointer-to-typedef (e.g. SDL_Window *, SDL_Renderer *)
        # pointee.kind == TYPEDEF means the pointee IS the typedef name
        if ptk == clang.TypeKind.TYPEDEF:
            tname = pointee.spelling.strip()
            if tname in opaque_set:
                return tname
            # The typedef might resolve to a struct — check that too
            canon_pt = pointee.get_canonical()
            if canon_pt.kind == clang.TypeKind.RECORD:
                sname = re.sub(r"^(struct|union)\s+", "", canon_pt.spelling).strip()
                if sname in opaque_set:
                    return sname
            # Not a known opaque handle — treat as *void
            return "*void"

        # pointer-to-struct (no typedef wrapper)
        if ptk == clang.TypeKind.RECORD:
            sname = re.sub(r"^(struct|union)\s+", "", pointee.spelling).strip()
            if sname in opaque_set:
                return sname
            return "*void"

        # pointer-to-anything-else → *void
        return "*void"

    # Typedef — check if it's a known opaque handle name first
    if kind == clang.TypeKind.TYPEDEF:
        canonical = cursor_type.get_canonical()
        tdef_name = cursor_type.spelling.strip()
        # If the typedef name itself is in our opaque set, use it directly
        if tdef_name in opaque_set:
            return tdef_name
        # Check SDL typedef overrides
        for k, v in _SDL_TYPEDEFS.items():
            if k in tdef_name:
                return v
        # If canonical is pointer-to-record, check if typedef name is opaque
        if canonical.kind == clang.TypeKind.POINTER:
            pt = canonical.get_pointee()
            if pt.kind == clang.TypeKind.RECORD and tdef_name in opaque_set:
                return tdef_name
        return _map_type(canonical, opaque_set, verbose, tdef_name)

    # Enum → int
    if kind == clang.TypeKind.ENUM:
        return "int"

    # Record (struct/union) — inline struct in param, use *void
    if kind == clang.TypeKind.RECORD:
        sname = re.sub(r"^(struct|union)\s+", "", cursor_type.spelling).strip()
        if sname in opaque_set:
            return sname
        return "*void"

    # Array types → *void (C arrays in params decay to pointers anyway)
    if kind in (
        clang.TypeKind.CONSTANTARRAY,
        clang.TypeKind.INCOMPLETEARRAY,
        clang.TypeKind.VARIABLEARRAY,
    ):
        return "*void"

    # Elaborated (C++ style like "struct Foo") — rare in C but handle it
    if kind == clang.TypeKind.ELABORATED:
        return _map_type(
            cursor_type.get_canonical(), opaque_set, verbose, original_spelling
        )

    # Try exact canonical spelling
    canonical_spell = cursor_type.get_canonical().spelling
    if canonical_spell in _EXACT:
        return _EXACT[canonical_spell]

    # Try original spelling
    spell = cursor_type.spelling
    if spell in _EXACT:
        return _EXACT[spell]

    # Try regex patterns
    for pat, htype in _PATTERNS:
        if pat.match(canonical_spell) or pat.match(spell):
            return htype

    # Builtin scalar kinds
    _BUILTIN_MAP = {
        clang.TypeKind.VOID: "void",
        clang.TypeKind.BOOL: "bool",
        clang.TypeKind.CHAR_U: "uint8",
        clang.TypeKind.UCHAR: "uint8",
        clang.TypeKind.CHAR_S: "int",
        clang.TypeKind.SCHAR: "int8",
        clang.TypeKind.USHORT: "uint",
        clang.TypeKind.UINT: "uint",
        clang.TypeKind.ULONG: "uint",
        clang.TypeKind.ULONGLONG: "uint64",
        clang.TypeKind.SHORT: "int",
        clang.TypeKind.INT: "int",
        clang.TypeKind.LONG: "int",
        clang.TypeKind.LONGLONG: "int",
        clang.TypeKind.FLOAT: "float",
        clang.TypeKind.DOUBLE: "float",
        clang.TypeKind.LONGDOUBLE: "float",
        clang.TypeKind.INT128: "int64",
        clang.TypeKind.UINT128: "uint64",
    }
    canonical_kind = cursor_type.get_canonical().kind
    if canonical_kind in _BUILTIN_MAP:
        return _BUILTIN_MAP[canonical_kind]

    if verbose:
        warn(
            f"unknown type '{spell}' (canonical: '{canonical_spell}') kind={kind} → skipped"
        )
    return None


# ── Struct field type mapping (uses sized types) ───────────────────────────────

_FIELD_TYPE_MAP: dict[str, str] = {
    "int8_t": "int8",
    "Sint8": "int8",
    "uint8_t": "uint8",
    "Uint8": "uint8",
    "int16_t": "int16",
    "Sint16": "int16",
    "uint16_t": "uint16",
    "Uint16": "uint16",
    "int32_t": "int32",
    "Sint32": "int32",
    "uint32_t": "uint32",
    "Uint32": "uint32",
    "int64_t": "int64",
    "Sint64": "int64",
    "uint64_t": "uint64",
    "Uint64": "uint64",
    "float": "float32",
    "double": "float",
}


def _map_field_type(
    cursor_type: "clang.Type", opaque_set: set[str], verbose: bool
) -> str:
    """
    Like _map_type but prefers sized integer names for struct fields so the
    layout exactly matches the C struct.
    """
    kind = cursor_type.kind

    if kind == clang.TypeKind.TYPEDEF:
        tname = cursor_type.spelling
        # exact sized typedef
        for k, v in _FIELD_TYPE_MAP.items():
            if tname == k or tname.endswith(k):
                return v
        return _map_field_type(cursor_type.get_canonical(), opaque_set, verbose)

    if kind == clang.TypeKind.POINTER:
        pointee = cursor_type.get_pointee()
        if pointee.kind in (
            clang.TypeKind.CHAR_S,
            clang.TypeKind.CHAR_U,
            clang.TypeKind.SCHAR,
            clang.TypeKind.UCHAR,
        ):
            return "str"
        if pointee.kind == clang.TypeKind.VOID:
            return "ptr"
        if pointee.kind == clang.TypeKind.RECORD:
            sname = re.sub(r"^(struct|union)\s+", "", pointee.spelling).strip()
            if sname:
                return sname
        return "ptr"

    if kind == clang.TypeKind.RECORD:
        sname = re.sub(r"^(struct|union)\s+", "", cursor_type.spelling).strip()
        return sname if sname else "ptr"

    if kind == clang.TypeKind.ENUM:
        return "int32"

    if kind in (clang.TypeKind.CONSTANTARRAY, clang.TypeKind.INCOMPLETEARRAY):
        return "ptr"

    spell = cursor_type.spelling
    if spell in _FIELD_TYPE_MAP:
        return _FIELD_TYPE_MAP[spell]

    # fall back to general mapper
    result = _map_type(cursor_type, opaque_set, verbose)
    if result == "int":
        return "int32"
    if result == "uint":
        return "uint32"
    return result or "ptr"


# ── Emission helpers ──────────────────────────────────────────────────────────


def _fmt_fn(name: str, params: list[tuple[str, str]], ret: str | None) -> str:
    """
    Format a single fn declaration line.
    params = list of (param_name, hylian_type)
    ret    = return type string, or None / "void" for void functions
    """
    parts = []
    for pname, ptype in params:
        parts.append(f"{pname}: {ptype}")
    sig = f"fn {name}({', '.join(parts)})"
    if ret and ret != "void":
        sig += f" -> {ret}"
    return sig


# ── Core parser ───────────────────────────────────────────────────────────────


class BindgenResult:
    def __init__(self):
        self.functions: list[str] = []  # formatted fn lines
        self.structs: list[str] = []  # formatted struct blocks
        self.enums: list[str] = []  # formatted const blocks
        self.defines: list[str] = []  # const lines from #define macros
        self.opaque_classes: set[str] = set()  # names used as opaque handles
        self.skipped: list[str] = []  # names we couldn't handle


def _is_from_main_header(cursor, main_file: str) -> bool:
    """
    Accept symbols from the main header file itself OR any header in the same
    directory tree.  This is necessary because umbrella headers like SDL.h or
    GL/gl.h are thin wrappers that #include all the real content from sibling
    files in the same directory (SDL_video.h, SDL_events.h, etc.).

    Symbols from unrelated system directories (/usr/include/stddef.h etc.) are
    still excluded unless --transitive is passed.
    """
    loc = cursor.location
    if not loc.file:
        return False
    abs_file = os.path.abspath(loc.file.name)
    abs_main = os.path.abspath(main_file)
    # Exact match
    if abs_file == abs_main:
        return True
    # Same directory as the header (e.g. /usr/include/SDL2/SDL_video.h)
    main_dir = os.path.dirname(abs_main)
    return (
        abs_file.startswith(main_dir + os.sep) or os.path.dirname(abs_file) == main_dir
    )


def parse_header(
    header: str,
    extra_includes: list[str],
    filters: list[str],
    opaque_names: list[str],
    skip_structs: bool,
    skip_enums: bool,
    skip_functions: bool,
    verbose: bool,
    transitive: bool = False,
) -> BindgenResult:

    index = clang.Index.create()

    args = ["-x", "c", "-std=c11"]

    # Always inject the clang builtin resource headers first so that
    # stddef.h / limits.h / stdint.h etc. are found even when the system
    # compiler is gcc and libclang's own copy isn't on the default path.
    resource_inc = _find_clang_resource_dir()
    if resource_inc:
        args += ["-I", resource_inc]

    for inc in extra_includes:
        args += ["-I", inc]

    tu = index.parse(
        header,
        args=args,
        options=clang.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD,
    )

    if tu.diagnostics:
        for diag in tu.diagnostics:
            if diag.severity >= clang.Diagnostic.Error:
                warn(f"clang: {diag.spelling}")

    opaque_set: set[str] = set(opaque_names)
    result = BindgenResult()
    result.opaque_classes = opaque_set

    # ── First pass: collect all struct names used as pointer targets ─────────
    # Strategy A: typedef struct _SDL_Window SDL_Window → the typedef name is
    #             the public handle name we should use as a class.
    # Strategy B: any FuncDecl param/return that is Foo* where Foo is a struct
    #             → Foo is an opaque handle.

    def _add_opaque_from_type(t: "clang.Type"):
        """If t is a pointer-to-struct, register the typedef name as opaque."""
        # Unwrap TYPEDEF: use typedef spelling as the public handle name
        if t.kind == clang.TypeKind.TYPEDEF:
            canonical = t.get_canonical()
            if canonical.kind == clang.TypeKind.POINTER:
                pt = canonical.get_pointee()
                if pt.kind == clang.TypeKind.RECORD:
                    tname = re.sub(r"^(struct|union)\s+", "", t.spelling).strip()
                    if tname:
                        opaque_set.add(tname)
                    return
            _add_opaque_from_type(canonical)
            return
        if t.kind == clang.TypeKind.POINTER:
            pt = t.get_pointee()
            # SDL2 pattern: pointee is a TYPEDEF (e.g. SDL_Window * → pointee is SDL_Window typedef)
            if pt.kind == clang.TypeKind.TYPEDEF:
                # Only treat as opaque if the canonical resolves to a RECORD,
                # NOT a scalar.  Uint8 *, Uint32 * etc. are integer pointers,
                # not opaque handles.
                pt_canon = pt.get_canonical()
                if pt_canon.kind == clang.TypeKind.RECORD:
                    # Only opaque if the struct has no visible fields.
                    decl = pt_canon.get_declaration()
                    has_fields = any(
                        c.kind == clang.CursorKind.FIELD_DECL
                        for c in decl.get_children()
                    )
                    if not has_fields:
                        tname = pt.spelling.strip()
                        if tname:
                            opaque_set.add(tname)
                # If canonical is scalar/enum/etc. — don't add to opaque set
                return
            if pt.kind == clang.TypeKind.RECORD:
                sname = re.sub(r"^(struct|union)\s+", "", pt.spelling).strip()
                if sname:
                    opaque_set.add(sname)

    def _collect_opaque(cursor):
        if not transitive and not _is_from_main_header(cursor, header):
            return
        if cursor.kind == clang.CursorKind.FUNCTION_DECL:
            if filters and not any(cursor.spelling.startswith(f) for f in filters):
                return
            for arg in cursor.get_arguments():
                _add_opaque_from_type(arg.type)
            _add_opaque_from_type(cursor.result_type)

    for cursor in tu.cursor.walk_preorder():
        _collect_opaque(cursor)

    # ── Pass 1b: forward-declared struct typedefs ─────────────────────────────
    # SDL2 pattern:  typedef struct SDL_Window SDL_Window;
    # These are TYPEDEF_DECL whose underlying canonical is a RECORD with NO
    # visible fields (forward declaration only) — i.e. a truly opaque handle.
    #
    # We explicitly skip:
    #   - canonicals that are ENUM  (SDL_BlendMode etc. — go to const blocks)
    #   - canonicals that are scalar  (Uint8, Sint16 etc. — map to int types)
    #   - canonicals that are RECORD with fields  (SDL_Rect, SDL_Event etc.
    #     — go to the struct emitter in pass 2)
    _SCALAR_KINDS = {
        clang.TypeKind.BOOL,
        clang.TypeKind.CHAR_U,
        clang.TypeKind.UCHAR,
        clang.TypeKind.CHAR_S,
        clang.TypeKind.SCHAR,
        clang.TypeKind.USHORT,
        clang.TypeKind.SHORT,
        clang.TypeKind.UINT,
        clang.TypeKind.INT,
        clang.TypeKind.ULONG,
        clang.TypeKind.LONG,
        clang.TypeKind.ULONGLONG,
        clang.TypeKind.LONGLONG,
        clang.TypeKind.FLOAT,
        clang.TypeKind.DOUBLE,
        clang.TypeKind.UINT128,
        clang.TypeKind.INT128,
    }
    for cursor in tu.cursor.get_children():
        if not transitive and not _is_from_main_header(cursor, header):
            continue
        if cursor.kind != clang.CursorKind.TYPEDEF_DECL:
            continue
        underlying = cursor.underlying_typedef_type
        canon = underlying.get_canonical()
        # Skip enums and scalars — they are not opaque handles
        if canon.kind == clang.TypeKind.ENUM:
            continue
        if canon.kind in _SCALAR_KINDS:
            continue
        if canon.kind != clang.TypeKind.RECORD:
            continue
        # Check that the struct is truly forward-declared (no visible fields)
        decl = canon.get_declaration()
        has_fields = any(
            c.kind == clang.CursorKind.FIELD_DECL for c in decl.get_children()
        )
        if not has_fields:
            tname = cursor.spelling.strip()
            if tname:
                opaque_set.add(tname)

    # ── Second pass: emit everything ──────────────────────────────────────────
    seen_fns: set[str] = set()
    seen_structs: set[str] = set()
    seen_enums: set[str] = set()

    for cursor in tu.cursor.get_children():
        if not transitive and not _is_from_main_header(cursor, header):
            continue

        name = cursor.spelling

        # ── Enum ──────────────────────────────────────────────────────────────
        if cursor.kind == clang.CursorKind.ENUM_DECL and not skip_enums:
            if name in seen_enums:
                continue
            if filters and not any(name.startswith(f) for f in filters):
                # still walk values for filter match
                pass
            seen_enums.add(name or f"__anon_{id(cursor)}")

            values = []
            for child in cursor.get_children():
                if child.kind == clang.CursorKind.ENUM_CONSTANT_DECL:
                    vname = child.spelling
                    if filters and not any(vname.startswith(f) for f in filters):
                        continue
                    values.append((vname, child.enum_value))

            if values:
                block_lines = [f"// enum {name}" if name else "// anonymous enum"]
                for vname, vval in values:
                    block_lines.append(f"const {vname} = {vval}")
                result.enums.append("\n".join(block_lines))
            continue

        # ── Typedef'd enum or struct (SDL pattern) ───────────────────────────
        if cursor.kind == clang.CursorKind.TYPEDEF_DECL:
            underlying = cursor.underlying_typedef_type
            canon = underlying.get_canonical()

            # ─ typedef enum { ... } SDL_BlendMode; ────────────────────────
            if underlying.kind == clang.TypeKind.ENUM and not skip_enums:
                tname = cursor.spelling
                if tname in seen_enums:
                    continue
                seen_enums.add(tname)

                values = []
                enum_cursor = underlying.get_declaration()
                for child in enum_cursor.get_children():
                    if child.kind == clang.CursorKind.ENUM_CONSTANT_DECL:
                        vname = child.spelling
                        if filters and not any(vname.startswith(f) for f in filters):
                            continue
                        values.append((vname, child.enum_value))

                if values:
                    block_lines = [f"// {tname}"]
                    for vname, vval in values:
                        block_lines.append(f"const {vname} = {vval}")
                    result.enums.append("\n".join(block_lines))
                continue

            # ─ typedef struct/union { ... } SDL_Rect / SDL_Event; ────────
            # Emit as a struct/union class if the canonical has visible fields
            # AND is not already in the opaque set (which means it has NO
            # fields — i.e. it's a true forward-declared opaque handle like
            # SDL_Window).
            if canon.kind == clang.TypeKind.RECORD and not skip_structs:
                tname = cursor.spelling
                if not tname or tname in seen_structs or tname in opaque_set:
                    continue
                if filters and not any(tname.startswith(f) for f in filters):
                    continue
                decl = canon.get_declaration()
                fields = [
                    c
                    for c in decl.get_children()
                    if c.kind == clang.CursorKind.FIELD_DECL
                ]
                if not fields:
                    continue  # no fields — opaque, handled by pass 1b
                seen_structs.add(tname)
                # Detect whether the underlying record is a union
                is_union_record = decl.kind == clang.CursorKind.UNION_DECL
                keyword = "union class" if is_union_record else "struct"
                lines_s = [f"{keyword} {tname} {{"]
                for f in fields:
                    ftype = _map_field_type(f.type, opaque_set, verbose)
                    fname2 = f.spelling
                    if fname2:
                        lines_s.append(f"    {ftype}  {fname2}")
                lines_s.append("}")
                result.structs.append("\n".join(lines_s))
            continue

        # ── Struct / Union ────────────────────────────────────────────────────
        if (
            cursor.kind in (clang.CursorKind.STRUCT_DECL, clang.CursorKind.UNION_DECL)
            and not skip_structs
        ):
            sname = name
            if not sname:
                continue  # anonymous struct/union, skip
            if sname in seen_structs:
                continue
            if sname in opaque_set:
                continue  # emitted as class instead
            if filters and not any(sname.startswith(f) for f in filters):
                continue
            seen_structs.add(sname)

            is_union_cursor = cursor.kind == clang.CursorKind.UNION_DECL
            keyword = "union class" if is_union_cursor else "struct"
            fields = []
            for child in cursor.get_children():
                if child.kind == clang.CursorKind.FIELD_DECL:
                    ftype = _map_field_type(child.type, opaque_set, verbose)
                    fname = child.spelling
                    if fname:
                        fields.append((ftype, fname))

            if fields:
                lines = [f"{keyword} {sname} {{"]
                for ftype, fname in fields:
                    lines.append(f"    {ftype}  {fname}")
                lines.append("}")
                result.structs.append("\n".join(lines))
            continue

        # ── Function ──────────────────────────────────────────────────────────
        if cursor.kind == clang.CursorKind.FUNCTION_DECL and not skip_functions:
            fname = cursor.spelling
            if not fname:
                continue
            if fname in seen_fns:
                continue
            if filters and not any(fname.startswith(f) for f in filters):
                continue
            seen_fns.add(fname)

            # map return type
            ret_htype = _map_type(cursor.result_type, opaque_set, verbose)
            if ret_htype is None:
                if verbose:
                    warn(
                        f"skipping {fname}: cannot map return type '{cursor.result_type.spelling}'"
                    )
                result.skipped.append(fname)
                continue

            # map parameters
            params = []
            skip_fn = False
            for i, arg in enumerate(cursor.get_arguments()):
                atype = _map_type(arg.type, opaque_set, verbose)
                if atype is None:
                    if verbose:
                        warn(
                            f"skipping {fname}: cannot map param '{arg.spelling}' type '{arg.type.spelling}'"
                        )
                    result.skipped.append(fname)
                    skip_fn = True
                    break
                pname = arg.spelling or f"arg{i}"
                # sanitise param name (avoid Hylian keywords)
                _KEYWORDS = {
                    "type",
                    "new",
                    "return",
                    "if",
                    "else",
                    "while",
                    "for",
                    "break",
                    "continue",
                    "module",
                    "class",
                    "enum",
                    "static",
                    "const",
                    "void",
                    "bool",
                    "int",
                    "str",
                }
                if pname in _KEYWORDS:
                    pname = pname + "_"
                params.append((pname, atype))

            if skip_fn:
                continue

            # variadic → skip (can't express in .hyi)
            if cursor.type.is_function_variadic():
                if verbose:
                    warn(f"skipping {fname}: variadic function")
                result.skipped.append(fname)
                continue

            result.functions.append(_fmt_fn(fname, params, ret_htype))

    return result


# ── #define macro scraper ───────────────────────────────────────────────────


def scrape_defines(
    header: str,
    extra_includes: list[str],
    filters: list[str],
    verbose: bool,
) -> list[str]:
    """
    Run  gcc -dM -E <header>  to get every fully-expanded #define, then
    keep only object-like macros whose value is a plain integer literal
    (decimal, hex, octal, with optional cast/suffix stripping).

    Returns a list of  "const NAME = VALUE"  strings ready to append to
    the .hyi file.
    """
    cmd = ["gcc", "-dM", "-E"]
    for inc in extra_includes:
        cmd += ["-I", inc]
    cmd.append(header)

    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    except (FileNotFoundError, subprocess.TimeoutExpired) as e:
        if verbose:
            warn(f"gcc -dM -E failed: {e}")
        return []

    if r.returncode != 0:
        if verbose:
            warn(f"gcc -dM -E exited {r.returncode}: {r.stderr.strip()[:200]}")
        return []

    # Patterns that indicate a value is a plain integer constant:
    #   42   0x1F   0777   (1u)  ((int)0x20)  1UL  etc.
    _INT_RE = re.compile(
        r"^\s*"
        r"(?:\(\s*(?:unsigned\s+)?(?:int|long|long long)\s*\)\s*)?"  # optional cast
        r"(0[xX][0-9a-fA-F]+|0[0-7]*|[1-9][0-9]*)"  # the number
        r"[uUlL]*"  # optional suffix
        r"\s*$"
    )

    seen: set[str] = set()
    results: list[str] = []

    for line in r.stdout.splitlines():
        # #define NAME value
        m = re.match(r"^#define\s+(\w+)\s+(.*)", line)
        if not m:
            continue
        name, value = m.group(1), m.group(2).strip()

        # Skip if it looks like a function-like macro (has a '('  immediately
        # after the name before any space — gcc -dM shows these as
        # "#define FOO(x) ..."  but we already strip that via the regex above;
        # double-check by seeing if value starts with '(' and contains params)
        if re.match(r"^\(\s*\w+\s*[,)]", value):
            continue

        # Apply prefix filters if given
        if filters and not any(name.startswith(f) for f in filters):
            continue

        # Skip internal / compiler macros
        if name.startswith("__") or name.startswith("_"):
            continue

        # Try to parse value as integer
        vm = _INT_RE.match(value)
        if not vm:
            # Also accept parenthesised integers like (0x20u)
            stripped = re.sub(r"[()uUlL\s]", "", value)
            vm2 = re.match(r"^(0[xX][0-9a-fA-F]+|0[0-7]*|[1-9][0-9]*)$", stripped)
            if not vm2:
                continue
            int_str = stripped
        else:
            int_str = vm.group(1)

        # Convert to a canonical decimal integer string
        try:
            if int_str.startswith(("0x", "0X")):
                val = int(int_str, 16)
            elif len(int_str) > 1 and int_str.startswith("0"):
                val = int(int_str, 8)
            else:
                val = int(int_str, 10)
        except ValueError:
            continue

        if name in seen:
            continue
        seen.add(name)

        results.append(f"const {name} = {val}")

    if verbose:
        info(
            f"scraped {len(results)} #define constants from {os.path.basename(header)}"
        )

    return results


# ── .hyi emitter ─────────────────────────────────────────────────────────────


def emit_hyi(
    result: BindgenResult,
    module_name: str,
    link_libs: list[str],
    header: str,
    out_path: str | None,
    extra_defines: list[str] | None = None,
) -> str:
    lines = []

    # Header comment
    lines.append(f"// Hylian vendor interface — {module_name}")
    lines.append(f"// Auto-generated by bindgen.py from: {os.path.basename(header)}")
    lines.append(f"// Do not edit manually — re-run bindgen.py to regenerate.")
    lines.append("")

    # Module declaration
    lines.append(f"module {module_name}")
    lines.append("")

    # Link directives
    for lib in link_libs:
        lines.append(f'link "{lib}"')
    if link_libs:
        lines.append("")

    # Opaque class declarations (just the names, used as handles)
    # Filter out:
    #   - names starting with "const " (e.g. "const SDL_Rect") — these are
    #     artefacts of const-pointer params and are duplicates of the non-const
    #     version; they are not valid Hylian identifiers.
    #   - names containing spaces (any other qualifier weirdness)
    #   - C built-in / libc names that are not useful as Hylian types
    _SKIP_CLASSES = {"FILE", "size_t", "wchar_t", "__locale_struct"}
    clean_classes = {
        c
        for c in result.opaque_classes
        if not c.startswith("const") and " " not in c and c not in _SKIP_CLASSES
    }
    if clean_classes:
        lines.append(
            "// ── Opaque handle types ────────────────────────────────────────────────"
        )
        lines.append(
            "// These are pointer-sized opaque handles (passed by pointer in C)."
        )
        lines.append("// Methods can be added manually after generation.")
        for cname in sorted(clean_classes):
            lines.append(f"class {cname} {{}}")
        lines.append("")

    # Enums → const blocks  +  scraped #define macros
    all_defines = list(extra_defines or [])
    # Collect names already emitted from enums so we don't duplicate
    enum_names: set[str] = set()
    for block in result.enums:
        for bl in block.splitlines():
            m = re.match(r"^const (\w+)", bl)
            if m:
                enum_names.add(m.group(1))
    # Deduplicate defines against enum names
    deduped_defines = [
        d
        for d in all_defines
        if re.match(r"^const (\w+)", d)
        and re.match(r"^const (\w+)", d).group(1) not in enum_names
    ]

    has_consts = result.enums or deduped_defines
    if has_consts:
        lines.append(
            "// ── Constants (from enums + #define macros) ───────────────────────────"
        )
        for block in result.enums:
            lines.append(block)
            lines.append("")
        if deduped_defines:
            lines.append("// #define constants")
            for d in deduped_defines:
                lines.append(d)
            lines.append("")

    # Struct declarations
    if result.structs:
        lines.append(
            "// ── Structs ──────────────────────────────────────────────────────────────────"
        )
        for block in result.structs:
            lines.append(block)
            lines.append("")

    # Free functions
    if result.functions:
        lines.append(
            "// ── Functions ────────────────────────────────────────────────────────────────"
        )
        for fn_line in result.functions:
            lines.append(fn_line)
        lines.append("")

    # Skipped summary
    if result.skipped:
        lines.append(
            f"// ── Skipped ({len(result.skipped)} symbols — variadic or unmappable types) ──"
        )
        for s in result.skipped:
            lines.append(f"// {s}")
        lines.append("")

    text = "\n".join(lines)

    if out_path:
        os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
        with open(out_path, "w") as f:
            f.write(text)
        print(f"\033[32mwrote\033[0m  {out_path}", file=sys.stderr)
        # print summary
        define_count = len(extra_defines) if extra_defines else 0
        print(
            f"       {len(result.opaque_classes)} opaque classes, "
            f"{len(result.structs)} structs, "
            f"{len(result.enums)} enum blocks, "
            f"{define_count} #defines, "
            f"{len(result.functions)} functions, "
            f"{len(result.skipped)} skipped",
            file=sys.stderr,
        )
    else:
        print(text)

    return text


# ── CLI ───────────────────────────────────────────────────────────────────────


def main():
    p = argparse.ArgumentParser(
        prog="bindgen.py",
        description="Generate Hylian .hyi FFI bindings from a C header using libclang.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=textwrap.dedent("""\
            Examples:
              # SDL2 — everything
              python3 tools/bindgen.py /usr/include/SDL2/SDL.h \\
                  --module vendors.sdl2 --link libSDL2-2.0.so.0 \\
                  --out Bindings/sdl2/sdl2.hyi

              # SDL2 — only window + renderer functions
              python3 tools/bindgen.py /usr/include/SDL2/SDL.h \\
                  --module vendors.sdl2 --link libSDL2-2.0.so.0 \\
                  --filter SDL_Create --filter SDL_Destroy --filter SDL_Render \\
                  --out Bindings/sdl2/sdl2.hyi

              # OpenGL
              python3 tools/bindgen.py /usr/include/GL/gl.h \\
                  --module vendors.gl --link libGL.so.1 \\
                  --out Bindings/gl/gl.hyi

              # libpng with extra include dir
              python3 tools/bindgen.py /usr/include/png.h \\
                  --module vendors.png --link libpng16.so.16 \\
                  --include /usr/include \\
                  --out Bindings/png/png.hyi
        """),
    )
    p.add_argument("header", help="Path to the C header file to parse")
    p.add_argument("--module", "-m", help="Module name (default: vendors.<stem>)")
    p.add_argument(
        "--link",
        "-l",
        action="append",
        default=[],
        help="Shared library soname (can be repeated)",
    )
    p.add_argument("--out", "-o", help="Output .hyi path (default: stdout)")
    p.add_argument(
        "--include",
        "-I",
        action="append",
        default=[],
        dest="includes",
        help="Extra include directory for clang",
    )
    p.add_argument(
        "--filter",
        "-f",
        action="append",
        default=[],
        dest="filters",
        help="Only emit symbols starting with this prefix",
    )
    p.add_argument(
        "--opaque",
        action="append",
        default=[],
        dest="opaque",
        help="Force-treat this struct name as opaque class",
    )
    p.add_argument("--no-structs", action="store_true", help="Skip struct declarations")
    p.add_argument("--no-enums", action="store_true", help="Skip enum declarations")
    p.add_argument(
        "--no-functions", action="store_true", help="Skip function declarations"
    )
    p.add_argument(
        "--transitive",
        action="store_true",
        help="Also emit symbols from #included headers (default: main header only)",
    )
    p.add_argument(
        "--with-defines",
        action="store_true",
        dest="with_defines",
        help="Also scrape integer #define macros via gcc -dM -E and emit as consts",
    )
    p.add_argument(
        "--verbose", "-v", action="store_true", help="Log skipped/unknown types"
    )

    args = p.parse_args()

    header = args.header
    if not os.path.exists(header):
        err(f"header not found: {header}")
        sys.exit(1)

    # derive module name from header stem if not given
    module_name = args.module
    if not module_name:
        stem = Path(header).stem.lower()
        module_name = f"vendors.{stem}"
        info(f"--module not given, using '{module_name}'")

    info(f"parsing {header}")

    result = parse_header(
        header=header,
        extra_includes=args.includes,
        filters=args.filters,
        opaque_names=args.opaque,
        skip_structs=args.no_structs,
        skip_enums=args.no_enums,
        skip_functions=args.no_functions,
        verbose=args.verbose,
        transitive=args.transitive,
    )

    extra_defines = None
    if args.with_defines:
        info("scraping #define macros...")
        extra_defines = scrape_defines(
            header=header,
            extra_includes=args.includes,
            filters=args.filters,
            verbose=args.verbose,
        )

    emit_hyi(
        result=result,
        module_name=module_name,
        link_libs=args.link,
        header=header,
        out_path=args.out,
        extra_defines=extra_defines,
    )


if __name__ == "__main__":
    main()
