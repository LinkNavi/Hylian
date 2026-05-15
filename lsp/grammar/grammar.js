/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: "hylian",

  extras: ($) => [/\s/, $.line_comment],

  word: ($) => $.identifier,

  conflicts: ($) => [
    [$.func_decl, $.method_decl],
    [$.type, $.identifier_expr],
    [$.tuple_type, $.paren_expr],
    [$.tuple_expr, $.paren_expr],
  ],

  rules: {
    source_file: ($) =>
      repeat(
        choice(
          $.include_stmt,
          $.ccpinclude_stmt,
          $.class_decl,
          $.func_decl,
          $.static_var_stmt,
          $.const_var_stmt,
          $.static_array_stmt,
          $.enum_decl,
        ),
      ),

    // ── Comments ────────────────────────────────────────────────────────────
    line_comment: (_) => token(seq("//", /.*/)),

    // ── Includes ────────────────────────────────────────────────────────────
    include_stmt: ($) =>
      seq("include", "{", commaSep($.module_path), optional(","), "}"),

    module_path: ($) => seq($.identifier, repeat(seq(".", $.identifier))),

    ccpinclude_stmt: ($) => seq("ccpinclude", $.string_literal),

    // ── Enum declarations ────────────────────────────────────────────────────
    enum_decl: ($) =>
      seq(
        optional("public"),
        "enum",
        field("name", $.identifier),
        "{",
        commaSep($.enum_variant),
        optional(","),
        "}",
      ),

    enum_variant: ($) =>
      seq(
        field("name", $.identifier),
        optional(seq("=", field("value", $.integer_literal))),
      ),

    // ── Types ───────────────────────────────────────────────────────────────
    type: ($) =>
      prec.right(
        seq(
          choice(
            $.primitive_type,
            $.array_type,
            $.multi_type,
            $.tuple_type,
            $.rawptr_type,
            $.ref_type,
            $.identifier,
          ),
          optional("?"),
        ),
      ),

    rawptr_type: ($) => seq("*", $.type),
    ref_type: ($) => seq("&", $.type),

    tuple_type: ($) =>
      seq(
        "(",
        seq($.type, optional("?"), repeat1(seq(",", $.type, optional("?")))),
        ")",
      ),

    primitive_type: (_) =>
      choice(
        "int",
        "str",
        "bool",
        "void",
        "float",
        "float32",
        "float64",
        "Error",
        "usize",
        "isize",
        "int8",
        "int16",
        "int32",
        "int64",
        "uint8",
        "uint16",
        "uint32",
        "uint64",
      ),

    array_type: ($) =>
      seq("array", "<", $.type, optional(seq(",", $.integer_literal)), ">"),

    multi_type: ($) =>
      seq(
        "multi",
        "<",
        choice(
          seq("any", optional(seq(",", $.integer_literal))),
          seq(
            $.type,
            repeat(seq("|", $.type)),
            optional(seq(",", $.integer_literal)),
          ),
        ),
        ">",
      ),

    // ── Class declarations ───────────────────────────────────────────────────
    class_decl: ($) =>
      seq(
        optional("packed"),
        optional("public"),
        "class",
        field("name", $.identifier),
        "{",
        optional($.class_body),
        "}",
      ),

    class_body: ($) => repeat1($.class_member),

    class_member: ($) => choice($.ctor_decl, $.field_decl, $.method_decl),

    ctor_decl: ($) =>
      seq(
        field("name", $.identifier),
        "(",
        optional($.param_list),
        ")",
        $.block,
      ),

    field_decl: ($) =>
      seq(
        optional(choice("public", "private")),
        field("type", $.type),
        field("name", $.identifier),
        ";",
      ),

    method_decl: ($) =>
      seq(
        optional("naked"),
        field("return_type", $.type),
        optional("?"),
        field("name", $.identifier),
        "(",
        optional($.param_list),
        ")",
        $.block,
      ),

    // ── Function declarations ────────────────────────────────────────────────
    func_decl: ($) =>
      seq(
        optional("naked"),
        field("return_type", $.type),
        optional("?"),
        field("name", $.identifier),
        "(",
        optional($.param_list),
        ")",
        $.block,
      ),

    // ── Parameters ──────────────────────────────────────────────────────────
    param_list: ($) => commaSep1($.param),

    param: ($) => seq(field("type", $.type), field("name", $.identifier)),

    // ── Block / statements ──────────────────────────────────────────────────
    block: ($) => seq("{", repeat($.statement), "}"),

    statement: ($) =>
      choice(
        $.var_decl_stmt,
        $.declare_assign_stmt,
        $.assign_stmt,
        $.compound_assign_stmt,
        $.member_assign_stmt,
        $.index_assign_stmt,
        $.return_stmt,
        $.if_stmt,
        $.while_stmt,
        $.for_stmt,
        $.for_in_stmt,
        $.switch_stmt,
        $.break_stmt,
        $.continue_stmt,
        $.defer_stmt,
        $.asm_block,
        $.unsafe_block,
        $.volatile_write_stmt,
        $.volatile_read_stmt,
        $.deref_write_stmt,
        $.static_var_stmt /* function-local statics */,
        $.const_var_stmt,
        $.static_array_stmt,
        $.expr_stmt,
      ),

    var_decl_stmt: ($) =>
      seq(
        field("type", $.type),
        optional("?"),
        field("name", $.identifier),
        optional(seq("=", field("value", $.expression))),
        ";",
      ),

    declare_assign_stmt: ($) =>
      seq(field("name", $.identifier), ":=", field("value", $.expression), ";"),

    assign_stmt: ($) =>
      seq(field("name", $.identifier), "=", field("value", $.expression), ";"),

    compound_assign_stmt: ($) =>
      seq(
        field("name", $.identifier),
        field("op", choice("+=", "-=", "*=", "/=", "%=")),
        field("value", $.expression),
        ";",
      ),

    member_assign_stmt: ($) =>
      seq(
        field("object", $.expression),
        ".",
        field("member", $.identifier),
        "=",
        field("value", $.expression),
        ";",
      ),

    index_assign_stmt: ($) =>
      seq(
        field("object", $.expression),
        "[",
        field("index", $.expression),
        "]",
        "=",
        field("value", $.expression),
        ";",
      ),

    return_stmt: ($) =>
      seq(
        "return",
        optional(
          choice(
            seq($.expression, repeat1(seq(",", $.expression))),
            $.expression,
          ),
        ),
        ";",
      ),

    if_stmt: ($) =>
      seq(
        "if",
        "(",
        field("condition", $.expression),
        ")",
        field("then", $.block),
        optional(seq("else", field("else", choice($.block, $.if_stmt)))),
      ),

    while_stmt: ($) =>
      seq("while", "(", field("condition", $.expression), ")", $.block),

    for_stmt: ($) =>
      seq(
        "for",
        "(",
        optional($.for_init),
        ";",
        optional($.expression),
        ";",
        optional($.for_init),
        ")",
        $.block,
      ),

    for_init: ($) =>
      choice(
        seq($.type, $.identifier, "=", $.expression),
        seq($.type, $.identifier),
        seq($.identifier, "=", $.expression),
      ),

    for_in_stmt: ($) =>
      seq(
        "for",
        "(",
        optional("&"),
        field("variable", $.identifier),
        "in",
        field("collection", $.expression),
        ")",
        $.block,
      ),

    // ── Switch ───────────────────────────────────────────────────────────────
    switch_stmt: ($) =>
      seq(
        "switch",
        "(",
        field("subject", $.expression),
        ")",
        "{",
        repeat($.switch_arm),
        "}",
      ),

    switch_arm: ($) =>
      choice(
        seq("case", field("value", $.expression), ":", $.block),
        seq("default", ":", $.block),
      ),

    // ── Unsafe block ─────────────────────────────────────────────────────────
    unsafe_block: ($) => seq("unsafe", "{", repeat($.statement), "}"),

    // ── Volatile ─────────────────────────────────────────────────────────────
    volatile_write_stmt: ($) =>
      seq(
        "volatile",
        "*",
        field("ptr", $.expression),
        "=",
        field("value", $.expression),
        ";",
      ),

    volatile_read_stmt: ($) =>
      seq("volatile", "*", field("ptr", $.expression), ";"),

    // Plain pointer write: *ptr = val;
    deref_write_stmt: ($) =>
      seq(
        "*",
        field("ptr", $.expression),
        "=",
        field("value", $.expression),
        ";",
      ),

    // ── Modifier keywords / Static & const globals ─────────────────────────
    static: (_) => "static",
    const: (_) => "const",

    static_var_stmt: ($) =>
      seq(
        optional("public"),
        $.static,
        field("type", $.type),
        field("name", $.identifier),
        optional(seq("=", field("value", $.expression))),
        ";",
      ),

    const_var_stmt: ($) =>
      seq(
        optional("public"),
        $.const,
        field("type", $.type),
        field("name", $.identifier),
        "=",
        field("value", $.expression),
        ";",
      ),

    static_array_stmt: ($) =>
      seq(
        optional("public"),
        $.static,
        field("type", $.type),
        field("name", $.identifier),
        "[",
        field("size", $.integer_literal),
        "]",
        ";",
      ),

    asm_block: ($) => seq("asm{", field("body", $.asm_content), "}"),

    asm_content: (_) => token(repeat(/[^}]/)),

    break_stmt: (_) => seq("break", ";"),
    continue_stmt: (_) => seq("continue", ";"),
    defer_stmt: ($) => seq("defer", $.expression, ";"),

    expr_stmt: ($) => seq($.expression, ";"),

    // ── Expressions ──────────────────────────────────────────────────────────
    expression: ($) =>
      choice(
        $.binary_expr,
        $.unary_expr,
        $.postfix_expr,
        $.call_expr,
        $.method_call_expr,
        $.member_expr,
        $.index_expr,
        $.new_expr,
        $.array_literal,
        $.interp_string,
        $.string_literal,
        $.float_literal,
        $.integer_literal,
        $.bool_literal,
        $.nil_literal,
        $.identifier_expr,
        $.paren_expr,
        $.tuple_expr,
        $.volatile_read_expr,
        $.cast_expr,
        $.as_cast_expr,
        $.size_of_expr,
        $.struct_literal_expr,
        $.addrof_fn_expr,
        $.addrof_expr,
        $.deref_expr,
      ),

    paren_expr: ($) => seq("(", $.expression, ")"),

    tuple_expr: ($) =>
      seq("(", $.expression, repeat1(seq(",", $.expression)), ")"),

    binary_expr: ($) =>
      choice(
        prec.left(1, seq($.expression, "||", $.expression)),
        prec.left(2, seq($.expression, "&&", $.expression)),
        prec.left(3, seq($.expression, choice("==", "!="), $.expression)),
        prec.left(
          4,
          seq($.expression, choice("<", ">", "<=", ">="), $.expression),
        ),
        prec.left(4, seq($.expression, "|", $.expression)),
        prec.left(4, seq($.expression, "^", $.expression)),
        prec.left(4, seq($.expression, "&", $.expression)),
        prec.left(5, seq($.expression, choice("<<", ">>"), $.expression)),
        prec.left(5, seq($.expression, choice("+", "-"), $.expression)),
        prec.left(6, seq($.expression, choice("*", "/", "%"), $.expression)),
      ),

    unary_expr: ($) =>
      prec(
        7,
        seq(
          field("op", choice("!", "-", "++", "--", "~")),
          field("operand", $.expression),
        ),
      ),

    volatile_read_expr: ($) =>
      prec(7, seq("volatile", "*", field("ptr", $.expression))),

    cast_expr: ($) =>
      prec(
        9,
        seq(
          "cast",
          "<",
          field("target_type", $.type),
          ">",
          "(",
          field("expr", $.expression),
          ")",
        ),
      ),

    as_cast_expr: ($) =>
      prec.left(
        8,
        seq(field("expr", $.expression), "as", field("target_type", $.type)),
      ),

    size_of_expr: ($) =>
      prec(9, seq("size_of", "(", field("type_name", $.identifier), ")")),

    addrof_fn_expr: ($) =>
      prec(
        9,
        seq(
          choice("addrof_fn", "adrof_fn"),
          "(",
          field("function", $.identifier),
          ")",
        ),
      ),

    addrof_expr: ($) => prec(7, seq("&", field("operand", $.expression))),

    deref_expr: ($) => prec(7, seq("*", field("operand", $.expression))),

    postfix_expr: ($) =>
      prec(
        8,
        seq(field("operand", $.expression), field("op", choice("++", "--"))),
      ),

    call_expr: ($) =>
      prec(
        9,
        seq(
          field("function", $.identifier),
          "(",
          field("args", optional($.arg_list)),
          ")",
        ),
      ),

    method_call_expr: ($) =>
      prec(
        9,
        seq(
          field("object", $.expression),
          ".",
          field("method", $.identifier),
          "(",
          field("args", optional($.arg_list)),
          ")",
        ),
      ),

    member_expr: ($) =>
      prec(
        9,
        seq(field("object", $.expression), ".", field("member", $.identifier)),
      ),

    index_expr: ($) =>
      prec(
        9,
        seq(
          field("object", $.expression),
          "[",
          field("index", $.expression),
          "]",
        ),
      ),

    new_expr: ($) =>
      seq("new", field("class", $.identifier), "(", optional($.arg_list), ")"),

    struct_literal_expr: ($) =>
      prec(
        9,
        seq(
          field("class", $.identifier),
          "{",
          optional($.field_init_list),
          "}",
        ),
      ),

    field_init_list: ($) =>
      commaSep1(
        seq(field("name", $.identifier), ":", field("value", $.expression)),
      ),

    arg_list: ($) => commaSep1($.expression),

    array_literal: ($) => seq("[", optional($.arg_list), "]"),

    interp_string: (_) =>
      token(
        seq(
          '"',
          repeat(choice(/[^"\\{]/, /\\[\s\S]/, seq("{{", /[^}]*/, "}}"))),
          '"',
        ),
      ),

    string_literal: (_) =>
      token(seq('"', repeat(choice(/[^"\\]/, /\\[\s\S]/)), '"')),

    float_literal: (_) => token(seq(/[0-9]+/, ".", /[0-9]+/)),

    integer_literal: (_) =>
      token(choice(/0[xX][0-9a-fA-F][0-9a-fA-F_]*/, /[0-9][0-9_]*/)),

    bool_literal: (_) => choice("true", "false"),

    nil_literal: (_) => choice("nil", "null"),

    identifier_expr: ($) => $.identifier,

    identifier: (_) => token(/[a-zA-Z_][a-zA-Z0-9_]*/),
  },
});

function commaSep1(rule) {
  return seq(rule, repeat(seq(",", rule)));
}

function commaSep(rule) {
  return optional(commaSep1(rule));
}
