; ── Keywords ────────────────────────────────────────────────────────────────

[
  "if"
  "else"
  "while"
  "for"
  "in"
  "return"
  "break"
  "continue"
  "defer"
  "switch"
  "case"
  "default"
] @keyword

[
  "unsafe"
  "volatile"
] @keyword.unsafe

[
  "class"
  "union"
  "public"
  "private"
  "enum"
] @keyword.type

[
  "new"
] @keyword.operator

[
  "cast"
  "as"
  "size_of"
  "addrof_fn"
  "adrof_fn"
] @keyword.operator


[
  "include"
  "ccpinclude"
] @keyword.import

[
  (static)
  (const)
] @keyword.modifier

; ── Types ───────────────────────────────────────────────────────────────────

[
  "int"
  "str"
  "bool"
  "void"
  "Error"
  "array"
  "multi"
  "any"
  "usize"
  "isize"
] @type.builtin

(class_decl
  name: (identifier) @type)

(union_class_decl
  name: (identifier) @type)

(enum_decl
  name: (identifier) @type)

(new_expr
  class: (identifier) @type)

(struct_literal_expr
  class: (identifier) @type)

(var_decl_stmt
  type: (type
    (identifier) @type))

(param
  type: (type
    (identifier) @type))

(field_decl
  type: (type
    (identifier) @type))

(method_decl
  return_type: (type
    (identifier) @type))

(func_decl
  return_type: (type
    (identifier) @type))

(union_field_decl
  type: (type
    (identifier) @type))

; ── Enum variants ────────────────────────────────────────────────────────────

(enum_variant
  name: (identifier) @constant)

; ── Functions and methods ────────────────────────────────────────────────────

(func_decl
  name: (identifier) @function)

(method_decl
  name: (identifier) @function.method)

(ctor_decl
  name: (identifier) @constructor)

(call_expr
  function: (identifier) @function.call)

(method_call_expr
  method: (identifier) @function.method.call)

; ── Built-in functions ───────────────────────────────────────────────────────

((call_expr
  function: (identifier) @function.builtin)
 (#match? @function.builtin "^(print|panic|Err|len|push|pop|exit)$"))

; ── Variables ────────────────────────────────────────────────────────────────

(param
  name: (identifier) @variable.parameter)

(var_decl_stmt
  name: (identifier) @variable)

(static_var_stmt
  name: (identifier) @variable)

(const_var_stmt
  name: (identifier) @variable)

(static_array_stmt
  name: (identifier) @variable)

(declare_assign_stmt

  name: (identifier) @variable)

(for_in_stmt
  variable: (identifier) @variable)

; ── Fields ───────────────────────────────────────────────────────────────────

(field_decl
  name: (identifier) @property)

(union_field_decl
  name: (identifier) @property)

(member_expr
  member: (identifier) @property)

(member_assign_stmt
  member: (identifier) @property)

; ── Operators ────────────────────────────────────────────────────────────────

[
  "+"  "-"  "*"  "/"  "%"
  "+=" "-=" "*=" "/=" "%="
  "="  ":="
  "==" "!=" "<"  ">"  "<=" ">="
  "&&" "||" "!"
  "++" "--"
  "|"  "&"  "^"  "~"
  "<<"  ">>"
  "?"
] @operator

; ── Punctuation ──────────────────────────────────────────────────────────────

[ "{" "}" ] @punctuation.bracket
[ "(" ")" ] @punctuation.bracket
[ "[" "]" ] @punctuation.bracket
[ "<" ">" ] @punctuation.bracket

[ "." "," ";" ":" ] @punctuation.delimiter

; ── Literals ─────────────────────────────────────────────────────────────────

(string_literal)  @string
(interp_string)   @string.special
(integer_literal) @number
(float_literal)   @number.float

[ "true" "false" ] @boolean

(nil_literal) @constant.builtin

; `null` is treated as nil
"null" @constant.builtin

; ── Comments ─────────────────────────────────────────────────────────────────

(line_comment) @comment

; ── Modules / include paths ───────────────────────────────────────────────────

(module_path
  (identifier) @namespace)

; ── ASM blocks ───────────────────────────────────────────────────────────────

(asm_block) @embedded
(asm_content) @string.special.symbol
