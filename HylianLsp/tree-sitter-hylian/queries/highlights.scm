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
  "match"
  "unsafe"
] @keyword

[
  "class"
  "public"
  "private"
] @keyword.type

[
  "new"
] @keyword.operator

[
  "include"
  "ccpinclude"
] @keyword.import

[
  "const"
  "static"
  "extern"
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
] @type.builtin

(class_decl
  name: (identifier) @type)

(new_expr
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

(declare_assign_stmt
  name: (identifier) @variable)

(for_in_stmt
  variable: (identifier) @variable)

; ── Fields ───────────────────────────────────────────────────────────────────

(field_decl
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
  "|"  "&"
  "?"
] @operator

; ── Punctuation ──────────────────────────────────────────────────────────────

[ "{" "}" ] @punctuation.bracket
[ "(" ")" ] @punctuation.bracket
[ "[" "]" ] @punctuation.bracket
[ "<" ">" ] @punctuation.bracket

[ "." "," ";" ] @punctuation.delimiter

; ── Literals ─────────────────────────────────────────────────────────────────

(string_literal)  @string
(interp_string)   @string.special
(integer_literal) @number
(float_literal)   @number.float

[ "true" "false" ] @boolean

(nil_literal) @constant.builtin

; ── Comments ─────────────────────────────────────────────────────────────────

(line_comment) @comment

; ── Modules / include paths ───────────────────────────────────────────────────

(module_path
  (identifier) @namespace)

; ── ASM blocks ───────────────────────────────────────────────────────────────

(asm_block) @embedded
(asm_content) @string.special.symbol
