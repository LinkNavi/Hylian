#include "typecheck.h"
#include "diag.h"
#include "ast.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *current_tc_file = "<unknown>";
static int in_unsafe = 0;
/* Both route through the shared diagnostic sink (see diag.h) so this one
   typechecker can serve the CLI and the LSP. */
/* Diagnostics anchor to a node: its line, column and width come from the
   parser's exact source span (see ast.c), so the editor can underline the
   offending expression instead of guessing. */
static void tc_emit(DiagSeverity sev, const ASTNode *n, const char *hint, const char *buf) {
  int span = (n && n->end_line == n->line && n->end_col > n->col) ? n->end_col - n->col : 0;
  diag_set_span(span);
  diag_emit(sev, current_tc_file, n ? n->line : 0, (n && n->line > 0) ? n->col : -1, hint, "%s", buf);
}

static void tc_error(const ASTNode *n, const char *fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  tc_emit(DIAG_ERROR, n, NULL, buf);
}

static void tc_warn(const ASTNode *n, const char *fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  tc_emit(DIAG_WARNING, n, NULL, buf);
}


/* Error with a second line of advice (shown as `hint:` by the CLI and after the
   message in the editor). */
static void tc_error_hint(const ASTNode *n, const char *hint, const char *fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  tc_emit(DIAG_ERROR, n, hint, buf);
}

/* ── "did you mean ...?" ─────────────────────────────────────────────────── */

typedef struct { const char *target; const char *best; int bestd; } Suggest;

static void sg_init(Suggest *s, const char *target) {
  s->target = target; s->best = NULL; s->bestd = 1000;
}

/* Levenshtein distance, case-insensitive (a wrong-case name is the most likely
   typo of all), capped so long names stay cheap. */
static int edit_distance(const char *a, const char *b) {
  int la = (int)strlen(a), lb = (int)strlen(b);
  if (la > 40 || lb > 40) return 1000;
  int prev[64], cur[64];
  for (int j = 0; j <= lb; j++) prev[j] = j;
  for (int i = 1; i <= la; i++) {
    cur[0] = i;
    for (int j = 1; j <= lb; j++) {
      char ca = a[i - 1], cb = b[j - 1];
      if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
      if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
      int sub = prev[j - 1] + (ca != cb);
      int del = prev[j] + 1, ins = cur[j - 1] + 1;
      int m = sub < del ? sub : del;
      cur[j] = m < ins ? m : ins;
    }
    memcpy(prev, cur, sizeof(int) * (size_t)(lb + 1));
  }
  return prev[lb];
}

static void sg_add(Suggest *s, const char *cand) {
  if (!cand || !cand[0] || strcmp(cand, s->target) == 0) return;
  if (cand[0] == '_' && s->target[0] != '_') return; /* private helpers */
  int d = edit_distance(s->target, cand);
  if (d < s->bestd) { s->bestd = d; s->best = cand; }
}

/* the closest candidate, if it is close enough to plausibly be a typo */
static const char *sg_result(const Suggest *s) {
  int len = (int)strlen(s->target);
  int limit = len <= 3 ? 1 : len <= 6 ? 2 : 3;
  return s->best && s->bestd <= limit ? s->best : NULL;
}

static void did_you_mean_hint(const char *best, char *buf, size_t n) {
  if (best) snprintf(buf, n, "did you mean '%s'?", best);
  else buf[0] = '\0';
}

#define MAX_SCOPE_DEPTH 32
#define MAX_SCOPE_SYMS 4096

typedef struct {
  char *name;
  Type type;
} Symbol;

static Symbol symbols[MAX_SCOPE_SYMS];
static int sym_count = 0;

static int scope_stack[MAX_SCOPE_DEPTH];
static int scope_depth = 0;

static void scope_push(void) {
  if (scope_depth < MAX_SCOPE_DEPTH)
    scope_stack[scope_depth++] = sym_count;
}

static void scope_pop(void) {
  if (scope_depth > 0)
    sym_count = scope_stack[--scope_depth];
}

static void scope_define(const char *name, Type t) {
  if (sym_count < MAX_SCOPE_SYMS) {
    symbols[sym_count].name = (char *)name;
    symbols[sym_count].type = t;
    sym_count++;
  }
}

static Type *scope_lookup(const char *name) {
  for (int i = sym_count - 1; i >= 0; i--)
    if (symbols[i].name && strcmp(symbols[i].name, name) == 0)
      return &symbols[i].type;
  return NULL;
}


typedef struct {
  char *name;
  Type return_type;
  Type param_types[16];
  int param_count;
  int is_external_decl;
} FuncInfo;

typedef struct {
  char *class_name;
  char *field_name;
  Type field_type;
} FieldEntry;

static FuncInfo funcs[4096];
static int func_count = 0;

static FieldEntry fields[4096];
static int field_count = 0;


static const char *enum_type_names[256];
static const char *tc_class_names[1024];
static int tc_class_name_count = 0;
static int enum_type_count = 0;

static const char *tc_module_names[64];
static int tc_module_count = 0;

static int tc_is_module(const char *name) {
    for (int i = 0; i < tc_module_count; i++)
        if (strcmp(tc_module_names[i], name) == 0) return 1;
    return 0;
}

/* Which module (if any) owns a given bare function name, e.g. catFunc ->
   "Cat". Populated while registering `module Foo { ... }` blocks (pass 1)
   and consulted when a bare NODE_FUNC_CALL can't be found in the normal
   function table, so a call to a module function from OUTSIDE that module
   gets a clear "use Module.func(...)" error at typecheck time instead of a
   linker "undefined reference" once codegen tries to call a symbol that was
   only ever emitted as ModuleName__func. */
static const char *mod_member_name[256];
static const char *mod_member_module[256];
static int mod_member_count = 0;

static const char *mod_owner_of(const char *name) {
    for (int i = 0; i < mod_member_count; i++)
        if (mod_member_name[i] && strcmp(mod_member_name[i], name) == 0)
            return mod_member_module[i];
    return NULL;
}

/* Set to the enclosing module's name while typechecking the bodies of that
   module's own functions (pass 2), NULL everywhere else. Lets a function
   inside `module Cat { }` call a sibling function by its bare name — the
   same unqualified-intra-module-call convention lower.c already implements
   via LowerState.current_module — without opening that bare name up to the
   rest of the program. */
static const char *tc_current_module = NULL;

static int is_enum_type(const char *name) {
  for (int i = 0; i < enum_type_count; i++)
    if (enum_type_names[i] && strcmp(enum_type_names[i], name) == 0)
      return 1;
  return 0;
}

static FuncInfo *func_lookup(const char *name) {
  for (int i = 0; i < func_count; i++)
    if (funcs[i].name && strcmp(funcs[i].name, name) == 0)
      return &funcs[i];
  /* Try stripping known stdlib source prefixes so that e.g. str_contains
     resolves to the 'contains' entry registered from std.strings's .hyi. */
  static const char *prefixes[] = { "str_", NULL };
  for (int p = 0; prefixes[p]; p++) {
    size_t plen = strlen(prefixes[p]);
    if (strncmp(name, prefixes[p], plen) == 0) {
      const char *unprefixed = name + plen;
      for (int i = 0; i < func_count; i++)
        if (funcs[i].name && strcmp(funcs[i].name, unprefixed) == 0)
          return &funcs[i];
    }
  }
  return NULL;
}

static FieldEntry *field_lookup(const char *class_name, const char *fname) {
  for (int i = 0; i < field_count; i++)
    if (fields[i].class_name && fields[i].field_name &&
        strcmp(fields[i].class_name, class_name) == 0 &&
        strcmp(fields[i].field_name, fname) == 0)
      return &fields[i];
  return NULL;
}


/* does the class have at least one registered field (so a missing field is a real error, not
   a type we know nothing about)? */
static int class_is_known(const char *class_name) {
  for (int i = 0; i < field_count; i++)
    if (fields[i].class_name && strcmp(fields[i].class_name, class_name) == 0) return 1;
  return 0;
}

static int type_name_declared(const char *n) {
  static const char *builtin[] = { "int","int8","int16","int32","int64","uint8","uint16","uint32","uint64",
    "usize","isize","float","float32","float64","double","bool","char","str","void","Error","auto",
    "ptr","rawptr","any","array","multi","unknown","nil", NULL };
  for (int i = 0; builtin[i]; i++) if (strcmp(n, builtin[i]) == 0) return 1;
  for (int i = 0; i < tc_class_name_count; i++) if (tc_class_names[i] && strcmp(tc_class_names[i], n) == 0) return 1;
  if (is_enum_type(n)) return 1;
  for (int i = 0; i < tc_module_count; i++) if (tc_module_names[i] && strcmp(tc_module_names[i], n) == 0) return 1;
  if (class_is_known(n)) return 1;
  return 0;
}

static void suggest_type(const char *name, char *hint, size_t n) {
  Suggest sg; sg_init(&sg, name);
  for (int i = 0; i < tc_class_name_count; i++) sg_add(&sg, tc_class_names[i]);
  did_you_mean_hint(sg_result(&sg), hint, n);
}

static Type unknown_type(void) { return make_simple_type(NULL, 0); }


static const char *type_name(Type t) {
  static char buf[256];
  if (t.kind == TYPE_ARRAY) {
    if (t.elem_type_count > 0 && t.elem_types[0].name)
      snprintf(buf, sizeof(buf), "array<%s>", t.elem_types[0].name);
    else
      snprintf(buf, sizeof(buf), "array<unknown>");
    return buf;
  }
  if (t.kind == TYPE_MULTI) {
    if (t.is_any)
      return "multi<any>";
    snprintf(buf, sizeof(buf), "multi<...>");
    return buf;
  }
  if (t.name)
    return t.name;
  return "unknown";
}


static Type infer_expr(ASTNode *node);
static void infer_stmt(ASTNode *node);



/* ── externally-supplied symbols (see typecheck.h) ───────────────────────── */

static const TCExternal *g_ext = NULL;
static int               g_ext_count = 0;

void tc_set_externals(const TCExternal *ext, int count) {
  g_ext = ext;
  g_ext_count = ext ? count : 0;
}

static const TCExternal *ext_find(const char *name, TCExternalKind kind) {
  if (!g_ext || !name) return NULL;
  for (int i = 0; i < g_ext_count; i++)
    if (g_ext[i].kind == kind && g_ext[i].name &&
        strcmp(g_ext[i].name, name) == 0)
      return &g_ext[i];
  return NULL;
}

static const TCExternalField *g_ext_fields = NULL;
static int                    g_ext_field_count = 0;

void tc_set_external_fields(const TCExternalField *fields_in, int count) {
  g_ext_fields = fields_in;
  g_ext_field_count = fields_in ? count : 0;
}

static const TCExternalField *ext_field_find(const char *class_name, const char *fname) {
  if (!g_ext_fields || !class_name || !fname) return NULL;
  for (int i = 0; i < g_ext_field_count; i++)
    if (g_ext_fields[i].class_name && g_ext_fields[i].field_name &&
        strcmp(g_ext_fields[i].class_name, class_name) == 0 &&
        strcmp(g_ext_fields[i].field_name, fname) == 0)
      return &g_ext_fields[i];
  return NULL;
}

/* Resolve an external's declared type name into a Type, defaulting to a
   permissive "unknown"-ish int rather than erroring: the LSP's index carries
   type names as free text, and being wrong about a type is much less harmful
   here than falsely reporting the symbol as undefined. */
static Type ext_type(const TCExternal *e) {
  if (!e || !e->type_name || !e->type_name[0])
    return make_simple_type("int", 0);
  return make_simple_type((char *)e->type_name, 0);
}

/* Suggestion builders. They live here, after the tables they read. */

static void suggest_variable(const char *name, char *hint, size_t n) {
  Suggest s; sg_init(&s, name);
  for (int i = 0; i < sym_count; i++) sg_add(&s, symbols[i].name);
  for (int i = 0; i < g_ext_count; i++)
    if (g_ext[i].kind == TC_EXT_VAR) sg_add(&s, g_ext[i].name);
  const char *best = sg_result(&s);
  if (!best && (func_lookup(name) || ext_find(name, TC_EXT_FUNC))) {
    snprintf(hint, n, "'%s' is a function: call it as %s(...), or take its address with addrof_fn(%s)",
             name, name, name);
    return;
  }
  did_you_mean_hint(best, hint, n);
}

static void suggest_function(const char *name, char *hint, size_t n) {
  Suggest s; sg_init(&s, name);
  for (int i = 0; i < func_count; i++)
    if (funcs[i].name && !strstr(funcs[i].name, "__")) sg_add(&s, funcs[i].name);
  for (int i = 0; i < g_ext_count; i++)
    if (g_ext[i].kind == TC_EXT_FUNC && !strstr(g_ext[i].name, "__")) sg_add(&s, g_ext[i].name);
  const char *best = sg_result(&s);
  if (!best && scope_lookup(name)) {
    snprintf(hint, n, "'%s' is a variable, not a function", name);
    return;
  }
  did_you_mean_hint(best, hint, n);
}

static void suggest_field(const char *cls, const char *name, char *hint, size_t n) {
  Suggest s; sg_init(&s, name);
  for (int i = 0; i < field_count; i++)
    if (fields[i].class_name && strcmp(fields[i].class_name, cls) == 0) sg_add(&s, fields[i].field_name);
  for (int i = 0; i < g_ext_field_count; i++)
    if (g_ext_fields[i].class_name && strcmp(g_ext_fields[i].class_name, cls) == 0)
      sg_add(&s, g_ext_fields[i].field_name);
  did_you_mean_hint(sg_result(&s), hint, n);
}

static void suggest_method(const char *cls, const char *name, char *hint, size_t n) {
  Suggest s; sg_init(&s, name);
  size_t pl = strlen(cls);
  for (int i = 0; i < func_count; i++) {
    const char *fnm = funcs[i].name;
    if (fnm && strncmp(fnm, cls, pl) == 0 && fnm[pl] == '_' && fnm[pl + 1] == '_') sg_add(&s, fnm + pl + 2);
  }
  for (int i = 0; i < g_ext_count; i++) {
    const char *fnm = g_ext[i].name;
    if (g_ext[i].kind == TC_EXT_FUNC && fnm && strncmp(fnm, cls, pl) == 0 && fnm[pl] == '_' && fnm[pl + 1] == '_')
      sg_add(&s, fnm + pl + 2);
  }
  for (int i = 0; i < field_count; i++)  /* a field called like a method */
    if (fields[i].class_name && strcmp(fields[i].class_name, cls) == 0) sg_add(&s, fields[i].field_name);
  did_you_mean_hint(sg_result(&s), hint, n);
}

static Type infer_expr(ASTNode *node) {
  if (!node)
    return unknown_type();

  Type result = unknown_type();

  switch (node->type) {

  case NODE_LITERAL: {
    LiteralNode *ln = (LiteralNode *)node;
    switch (ln->lit_type) {
    case LIT_INT:
      result = make_simple_type("int", 0);
      break;
    case LIT_STRING:
      result = make_simple_type("str", 0);
      break;
    case LIT_BOOL:
      result = make_simple_type("bool", 0);
      break;
    case LIT_NIL:
      result = make_simple_type("void", 0);
      break;
    case LIT_FLOAT:
      result = make_simple_type("float", 0);
      break;
    }
    break;
  }

  case NODE_IDENTIFIER: {
    IdentifierNode *id = (IdentifierNode *)node;
    Type *t = scope_lookup(id->name);
    if (t) {
      result = *t;
    } else {
      const TCExternal *ev = ext_find(id->name, TC_EXT_VAR);
      const TCExternal *em = ev ? NULL : ext_find(id->name, TC_EXT_MODULE);
      if (ev) {
        result = ext_type(ev);
      } else if (em) {
        /* a module name used as a namespace prefix, e.g. `Pmm.init()` */
        result = make_simple_type((char *)em->name, 0);
      } else {
        char hint[256];
        suggest_variable(id->name, hint, sizeof(hint));
        tc_error_hint(node, hint, "undefined variable '%s'", id->name);
      }
    }
    break;
  }

  case NODE_BINARY_OP: {
    BinaryOpNode *bn = (BinaryOpNode *)node;
    Type left = infer_expr(bn->left);
    Type right = infer_expr(bn->right);
    (void)right;
    const char *op = bn->op;
    if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
        strcmp(op, "<") == 0 || strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 ||
        strcmp(op, ">=") == 0 || strcmp(op, "&&") == 0 ||
        strcmp(op, "||") == 0) {
      result = make_simple_type("bool", 0);
    } else if (strcmp(op, "cast") == 0) {
      /* cast<T>(expr): type comes from the right-side string literal (type name) */
      if (bn->right && bn->right->type == NODE_LITERAL) {
        LiteralNode *ln = (LiteralNode *)bn->right;
        const char *tname = ln->value ? ln->value : "void";
        /* strip quotes if present */
        size_t tlen = strlen(tname);
        if (tlen >= 2 && tname[0] == '"' && tname[tlen-1] == '"') {
          char *uq = malloc(tlen - 1);
          memcpy(uq, tname + 1, tlen - 2);
          uq[tlen - 2] = '\0';
          result = make_simple_type(uq, 0);
          free(uq);
        } else {
          result = make_simple_type((char *)tname, 0);
        }
      } else {
        result = left;
      }
    } else if (strcmp(op, "&") == 0 || strcmp(op, "|") == 0 ||
               strcmp(op, "^") == 0 || strcmp(op, "<<") == 0 ||
               strcmp(op, ">>") == 0) {
      /* bitwise ops return the left operand type (numeric) */
      result = left;
    } else {
      /* Warn on arithmetic applied directly to a boolean */
      if (left.name && strcmp(left.name, "bool") == 0 &&
          (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 ||
           strcmp(op, "*") == 0 || strcmp(op, "/") == 0 ||
           strcmp(op, "%") == 0)) {
        tc_warn(node, "arithmetic on boolean value with operator '%s'", op);
      }
      result = left;
    }

    break;
  }

  case NODE_UNARY_OP: {
    UnaryOpNode *un = (UnaryOpNode *)node;
    Type operand = infer_expr(un->operand);
    if (un->op && strcmp(un->op, "!") == 0)
      result = make_simple_type("bool", 0);
    else if (un->op && strcmp(un->op, "addrof") == 0) {
      if (!un->operand || un->operand->type != NODE_IDENTIFIER)
        tc_error(node, "'&' can only take the address of a variable");
      result = make_ref_type(infer_expr(un->operand));
    }
    else if (un->op && strcmp(un->op, "deref") == 0) {
      if (operand.kind == TYPE_SIMPLE && operand.name &&
          (!strcmp(operand.name, "float") || !strcmp(operand.name, "float32") ||
           !strcmp(operand.name, "float64") || !strcmp(operand.name, "double") || !strcmp(operand.name, "bool")))
        tc_error(node, "cannot dereference a value of type '%s'", operand.name);
      if (operand.kind == TYPE_RAWPTR && !in_unsafe && !un->is_volatile)
        tc_error(node, "cannot deref raw pointer outside unsafe block");
      result = (operand.elem_type_count > 0) ? operand.elem_types[0]
                                             : unknown_type();
    } else
      result = operand;
    break;
  }

  case NODE_NEW: {
    NewNode *nn = (NewNode *)node;
    /* Constructor arguments were never type-checked here, so any non-trivial
       expression passed to `new X(...)` (e.g. a string concat) never got its
       resolved_type populated. lower.c's codegen relies on resolved_type to
       decide things like "is this a string concat or numeric add" — without
       it, `new Cmd("ls " + path)` silently miscompiled the concat to plain
       pointer addition (see the str-concat fix in lower.c) and crashed at
       runtime. */
    for (int i = 0; i < nn->arg_count; i++)
      infer_expr(nn->args[i]);
    result = make_simple_type(nn->class_name, 0);
    break;
  }

  case NODE_FUNC_CALL: {
    FuncCallNode *fc = (FuncCallNode *)node;
    if (fc->name && strcmp(fc->name, "__addrof_fn__") == 0) {
      /* The operand is a function symbol, not a variable reference. */
      result = make_simple_type("usize", 0);
      break;
    }

    /* Infer all args first — except __size_of__, whose sole argument is a TYPE
       NAME rather than an expression. Treating it as one reported "undefined
       variable 'Header'" for perfectly valid code. */
    if (!(fc->name && strcmp(fc->name, "__size_of__") == 0)) {
      for (int i = 0; i < fc->arg_count; i++)
        infer_expr(fc->args[i]);
    }
    /* Special built-ins that are not registered in the func table */
    if (fc->name && strcmp(fc->name, "hylian_array_zeros") == 0) {
      /* the parser's rewrite of `T name[N];` - the declared type carries the array type */
      result = unknown_type();
    } else if (fc->name && strcmp(fc->name, "__interp") == 0) {
      /* the lexer's rewrite of a "...{{expr}}..." string - see lexer.l */
      result = make_simple_type("str", 0);
    } else if (fc->name && strcmp(fc->name, "Err") == 0) {

      result = make_simple_type("Error", 0);
    } else if (fc->name && strcmp(fc->name, "panic") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "print") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "len") == 0) {
      result = make_simple_type("int", 0);
    } else if (fc->name && strcmp(fc->name, "push") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "pop") == 0) {
      result = unknown_type();
    } else if (fc->name && strcmp(fc->name, "exit") == 0) {

      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "cli") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "print") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "println") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "hlt") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "cls") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "draw") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "pset") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "line") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "rect") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "rectfill") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "circle") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "circlefill") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "print_str") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "sti") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "lgdt") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "lidt") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "ltr") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "invlpg") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "wrmsr") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "rdmsr") == 0) {
      result = make_simple_type("uint64", 0);
    } else if (fc->name && strcmp(fc->name, "read_cr") == 0) {
      result = make_simple_type("uint64", 0);
    } else if (fc->name && strcmp(fc->name, "write_cr") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "save_regs") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "restore_regs") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "iret") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "sysret") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "outb") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "inb") == 0) {
      result = make_simple_type("int", 0);
    } else if (fc->name && strcmp(fc->name, "outw") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "inw") == 0) {
      result = make_simple_type("int", 0);
    } else if (fc->name && strcmp(fc->name, "io_wait") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "enable_interrupts") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "disable_interrupts") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "halt") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "vga_clear") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "vga_set_color") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "vga_print") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "vga_println") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "vga_put_char") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "memset") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "memcpy") == 0) {
      result = make_simple_type("void", 0);
    } else if (fc->name && strcmp(fc->name, "__size_of__") == 0) {
      result = make_simple_type("usize", 0);
    } else if (fc->name && strcmp(fc->name, "size_of") == 0) {
      result = make_simple_type("usize", 0);
    } else if (fc->name && strcmp(fc->name, "__size_of__") == 0) {
      /* The argument is a TYPE NAME, not an expression. Inferring it as one
         made `__size_of__(Header)` report "undefined variable 'Header'" while
         still compiling to the right number. */
      result = make_simple_type("int", 0);
    } else if (fc->name && strcmp(fc->name, "syscall") == 0) {
      result = make_simple_type("int", 0);
    } else if (fc->name && !func_lookup(fc->name) && !ext_find(fc->name, TC_EXT_FUNC) &&
               scope_lookup(fc->name)) {
      /* `fp(args)` where fp is a variable: an indirect call through a
         function address (see addrof_fn). The result is int-sized. */
      fc->is_indirect = 1;
      result = make_simple_type("int", 0);
    } else if (fc->name) {
      FuncInfo *fi = func_lookup(fc->name);
      const TCExternal *ef = fi ? NULL : ext_find(fc->name, TC_EXT_FUNC);
      if (fi) {
        result = fi->return_type;
      } else if (ef) {
        result = ext_type(ef);
      } else {
        const char *owner = mod_owner_of(fc->name);
        if (owner && tc_current_module && strcmp(tc_current_module, owner) == 0) {
          /* Bare call to a sibling function from inside the same module —
             resolve via the mangled entry registered for the module. */
          char mangled[256];
          snprintf(mangled, sizeof(mangled), "%s__%s", owner, fc->name);
          FuncInfo *mfi = func_lookup(mangled);
          result = mfi ? mfi->return_type : unknown_type();
        } else if (owner) {
          tc_error(node,
                    "call to undefined function '%s' - '%s' is defined inside module '%s'; call it as %s.%s(...)",
                    fc->name, fc->name, owner, owner, fc->name);
        } else {
          char hint[256];
          suggest_function(fc->name, hint, sizeof(hint));
          tc_error_hint(node, hint, "call to undefined function '%s'", fc->name);
        }
      }
    }
    break;
  }

  case NODE_METHOD_CALL: {
    MethodCallNode *mc = (MethodCallNode *)node;
    /* Don't infer object type for module calls — check module registry first */
    if (mc->object && mc->object->type == NODE_IDENTIFIER) {
      const char *obj_name = ((IdentifierNode *)mc->object)->name;
      if (tc_is_module(obj_name)) {
        for (int i = 0; i < mc->arg_count; i++)
          infer_expr(mc->args[i]);
        char mangled[256];
        snprintf(mangled, sizeof(mangled), "%s__%s", obj_name, mc->method);
        FuncInfo *fi = func_lookup(mangled);
        if (fi) {
          result = fi->return_type;
        } else {
          tc_error(node, "no function '%s' in module '%s'", mc->method, obj_name);
        }
        node->resolved_type = result;
        return result;
      }
    }
    Type obj_type = infer_expr(mc->object);
    /* Infer all args */
    for (int i = 0; i < mc->arg_count; i++)
      infer_expr(mc->args[i]);
    /* Special: .message() on Error -> str */
    if (obj_type.name && strcmp(obj_type.name, "Error") == 0 && mc->method &&
        strcmp(mc->method, "message") == 0) {
      result = make_simple_type("str", 0);
    } else if (obj_type.name && strcmp(obj_type.name, "Error") == 0 && mc->method &&
               strcmp(mc->method, "code") == 0) {
      result = make_simple_type("int", 0);
    } else if (obj_type.name && mc->method) {
      /* Look for mangled name ClassName__method */
      char mangled[256];
      snprintf(mangled, sizeof(mangled), "%s__%s", obj_type.name, mc->method);
      FuncInfo *fi = func_lookup(mangled);
      if (fi) {
        result = fi->return_type;
      } else {
        /* UFCS fallback: `x.method(args)` where `method` isn't a real
           ClassName method (e.g. builtin types like str have no class of
           their own) — resolve it as sugar for the free function
           `method(x, args)` if one exists with matching arity. This is what
           makes `aString.starts_with("bl")` work the same as the free
           function `starts_with(aString, "bl")`. */
        FuncInfo *ufcs = func_lookup(mc->method);
        const TCExternal *em = ext_find(mangled, TC_EXT_FUNC);
        const TCExternal *eu = em ? NULL : ext_find(mc->method, TC_EXT_FUNC);
        if (em) {
          /* a method on a type the frontend didn't parse (stdlib class) */
          result = ext_type(em);
        } else if (ufcs && ufcs->param_count == mc->arg_count + 1) {
          result = ufcs->return_type;
          mc->is_ufcs = 1;
        } else if (eu) {
          result = ext_type(eu);
          mc->is_ufcs = 1;
        } else {
          char hint[256] = "";
          if (obj_type.name) suggest_method(obj_type.name, mc->method, hint, sizeof(hint));
          tc_error_hint(node, hint, "no method '%s' on type '%s'", mc->method,
                        obj_type.name ? obj_type.name : "unknown");
        }
      }
    } else if (mc->method && obj_type.kind == TYPE_ARRAY) {
      if (strcmp(mc->method, "push") == 0 || strcmp(mc->method, "pop") == 0 ||
          strcmp(mc->method, "len") == 0 || strcmp(mc->method, "cap") == 0) {
        result = make_simple_type("int", 0);
      } else {
        tc_error(node, "no method '%s' on type '%s'", mc->method,
                 type_name(obj_type));
      }
    } else if (mc->method && !obj_type.name) {
      tc_error(node, "no method '%s' on type 'unknown'", mc->method);
    }
    break;
  }

  case NODE_MEMBER_ACCESS: {
    MemberAccessNode *ma = (MemberAccessNode *)node;
    /* EnumName.Variant → int */
    if (ma->object && ma->object->type == NODE_IDENTIFIER) {
      const char *id = ((IdentifierNode *)ma->object)->name;
      if (is_enum_type(id)) {
        result = make_simple_type("int", 0);
        node->resolved_type = result;
        break;
      }
    }
    Type obj_type = infer_expr(ma->object);
    if (obj_type.kind == TYPE_ARRAY) {
      if (ma->member &&
          (strcmp(ma->member, "len") == 0 || strcmp(ma->member, "cap") == 0)) {
        result = make_simple_type("int", 0);
      }
    } else if (obj_type.kind == TYPE_MULTI) {
      if (ma->member && strcmp(ma->member, "tag") == 0)
        result = make_simple_type("int", 0);
      /* "value" stays unknown */
    } else if (obj_type.name) {
      /* Special array name check (stored as TYPE_SIMPLE "array") */
      if (strcmp(obj_type.name, "array") == 0) {
        if (ma->member && (strcmp(ma->member, "len") == 0 ||
                           strcmp(ma->member, "cap") == 0)) {
          result = make_simple_type("int", 0);
        }
      } else if (strcmp(obj_type.name, "multi") == 0) {
        if (ma->member && strcmp(ma->member, "tag") == 0)
          result = make_simple_type("int", 0);
      } else {
        FieldEntry *fe = field_lookup(obj_type.name, ma->member);
        if (fe) {
          result = fe->field_type;
        } else {
          const TCExternalField *ef = ext_field_find(obj_type.name, ma->member);
          if (ef) {
            result = make_simple_type(
                (char *)(ef->type_name && ef->type_name[0] ? ef->type_name : "int"), 0);
          } else {
            char hint[256];
            suggest_field(obj_type.name, ma->member, hint, sizeof(hint));
            tc_error_hint(node, hint, "no field '%s' on type '%s'", ma->member, obj_type.name);
          }
        }
      }
    }
    break;
  }

  case NODE_ARRAY_LITERAL: {
    ArrayLiteralNode *al = (ArrayLiteralNode *)node;
    Type elem = unknown_type();
    for (int i = 0; i < al->elem_count; i++) {
      Type et = infer_expr(al->elements[i]);
      if (i == 0)
        elem = et;
    }
    /* Build TYPE_ARRAY with elem type */
    result.kind = TYPE_ARRAY;
    result.nullable = 0;
    result.name = "array";
    result.elem_types = malloc(sizeof(Type));
    if (result.elem_types) {
      result.elem_types[0] = elem;
      result.elem_type_count = 1;
    } else {
      result.elem_type_count = 0;
    }
    result.is_any = 0;
    result.fixed_size = 0;
    break;
  }

  case NODE_INDEX: {
    IndexNode *in_node = (IndexNode *)node;
    Type obj_type = infer_expr(in_node->object);
    infer_expr(in_node->index);
    if (obj_type.kind == TYPE_ARRAY && obj_type.elem_type_count > 0)
      result = obj_type.elem_types[0];
    else if (obj_type.kind == TYPE_SIMPLE && obj_type.name && strcmp(obj_type.name, "str") == 0)
      result = make_simple_type("uint8", 0); /* s[i] is the byte at i */
    break;
  }

  case NODE_INTERP_STRING: {
    result = make_simple_type("str", 0);
    break;
  }

  case NODE_ASSIGN: {
    AssignNode *as = (AssignNode *)node;
    result = infer_expr(as->value);
    break;
  }

  case NODE_COMPOUND_ASSIGN: {
    CompoundAssignNode *ca = (CompoundAssignNode *)node;
    result = infer_expr(ca->value);
    break;
  }

  case NODE_TERNARY: {
    /* c ? a : b - the value has the type of whichever arm has a real type
       (a bare `nil` arm has no name of its own, so prefer the other one). */
    TernaryNode *tn = (TernaryNode *)node;
    infer_expr(tn->cond);
    Type tt = infer_expr(tn->then_expr);
    Type et = infer_expr(tn->else_expr);
    result = (tt.name || tt.kind != TYPE_SIMPLE) ? tt : et;
    break;
  }

  case NODE_STRUCT_LITERAL: {
    StructLiteralNode *sl = (StructLiteralNode *)node;
    for (int i = 0; i < sl->field_count; i++)
      infer_expr(sl->field_values[i]);
    result = make_simple_type(sl->class_name, 0);
    break;
  }

  default:
    break;
  }

  node->resolved_type = result;
  return result;
}


static void infer_stmt(ASTNode *node) {
  if (!node)
    return;

  switch (node->type) {

  case NODE_VAR_DECL: {
    VarDeclNode *vd = (VarDeclNode *)node;
    Type init_type = unknown_type();
    if (vd->initializer)
      init_type = infer_expr(vd->initializer);
    Type decl_type = vd->var_type;
    /* Warn when 'auto' type cannot be resolved from the initializer */
    if (decl_type.name && strcmp(decl_type.name, "auto") == 0 &&
        (!init_type.name || strcmp(init_type.name, "unknown") == 0) &&
        init_type.kind == TYPE_SIMPLE && !init_type.name) {
      tc_warn(node, "cannot infer type for '%s' from initializer", vd->var_name);
    }
    /* a type name nobody declared (`Smal s;`) */
    if (decl_type.kind == TYPE_SIMPLE && decl_type.name && !type_name_declared(decl_type.name)) {
      char hint[256] = "";
      suggest_type(decl_type.name, hint, sizeof hint);
      tc_error_hint(node, hint[0] ? hint : NULL, "unknown type '%s'", decl_type.name);
    }
    scope_define(vd->var_name, decl_type);
    node->resolved_type = decl_type;
    break;
  }

  case NODE_ASSIGN: {
    AssignNode *as = (AssignNode *)node;
    infer_expr(as->value);
    break;
  }

  case NODE_COMPOUND_ASSIGN: {
    CompoundAssignNode *ca = (CompoundAssignNode *)node;
    infer_expr(ca->value);
    break;
  }

  case NODE_RETURN: {
    ReturnNode *ret = (ReturnNode *)node;
    if (ret->value)
      infer_expr(ret->value);
    break;
  }

  case NODE_DEFER: {
    DeferNode *dn = (DeferNode *)node;
    scope_push();
    for (int i = 0; i < dn->body_count; i++)
      infer_stmt(dn->body[i]);
    scope_pop();
    break;
  }

  case NODE_UNSAFE: {
    UnsafeBlockNode *ub = (UnsafeBlockNode *)node;
    int prev_unsafe = in_unsafe;
    in_unsafe = 1;
    scope_push();
    for (int i = 0; i < ub->body_count; i++)
      infer_stmt(ub->body[i]);
    scope_pop();
    in_unsafe = prev_unsafe;
    break;
  }

  case NODE_IF: {
    IfNode *nd = (IfNode *)node;
    infer_expr(nd->condition);
    scope_push();
    for (int i = 0; i < nd->then_count; i++)
      infer_stmt(nd->then_body[i]);
    scope_pop();
    if (nd->else_body && nd->else_count > 0) {
      scope_push();
      for (int i = 0; i < nd->else_count; i++)
        infer_stmt(nd->else_body[i]);
      scope_pop();
    }
    break;
  }

  case NODE_WHILE: {
    WhileNode *nd = (WhileNode *)node;
    infer_expr(nd->condition);
    scope_push();
    for (int i = 0; i < nd->body_count; i++)
      infer_stmt(nd->body[i]);
    scope_pop();
    break;
  }

  case NODE_FOR: {
    ForNode *nd = (ForNode *)node;
    scope_push();
    if (nd->init)
      infer_stmt(nd->init);
    if (nd->condition)
      infer_expr(nd->condition);
    if (nd->post)
      infer_stmt(nd->post);
    for (int i = 0; i < nd->body_count; i++)
      infer_stmt(nd->body[i]);
    scope_pop();
    break;
  }

  case NODE_FOR_IN: {
    ForInNode *fi = (ForInNode *)node;
    Type coll_type = infer_expr(fi->collection);
    Type elem_type = unknown_type();
    if (coll_type.kind == TYPE_ARRAY && coll_type.elem_type_count > 0)
      elem_type = coll_type.elem_types[0];
    scope_push();
    scope_define(fi->var_name, elem_type);
    for (int i = 0; i < fi->body_count; i++)
      infer_stmt(fi->body[i]);
    scope_pop();
    break;
  }

  case NODE_FUNC_CALL:
    infer_expr(node);
    break;

  case NODE_METHOD_CALL:
    infer_expr(node);
    break;

  case NODE_INDEX_ASSIGN: {
    IndexAssignNode *ia = (IndexAssignNode *)node;
    infer_expr(ia->object);
    infer_expr(ia->index);
    infer_expr(ia->value);
    break;
  }

  case NODE_MEMBER_ASSIGN: {
    MemberAssignNode *ma = (MemberAssignNode *)node;
    Type ot = unknown_type();
    if (ma->object)
      ot = infer_expr(ma->object);
    infer_expr(ma->value);
    /* assigning to a field that doesn't exist used to warn at lowering and silently drop the store */
    if (ma->object && ot.kind == TYPE_SIMPLE && ot.name && ma->member &&
        strcmp(ot.name, "array") != 0 && strcmp(ot.name, "multi") != 0 &&
        strcmp(ot.name, "unknown") != 0 && strcmp(ot.name, "auto") != 0 &&
        class_is_known(ot.name) && !field_lookup(ot.name, ma->member) &&
        !ext_field_find(ot.name, ma->member)) {
      char hint[256];
      suggest_field(ot.name, ma->member, hint, sizeof(hint));
      tc_error_hint(node, hint, "no field '%s' on type '%s'", ma->member, ot.name);
    }
    break;
  }

  case NODE_SWITCH: {
    SwitchNode *sw = (SwitchNode *)node;
    infer_expr(sw->subject);
    for (int i = 0; i < sw->case_count; i++) {
      SwitchCaseNode *arm = sw->cases[i];
      if (!arm)
        continue;
      if (!arm->is_default && arm->value)
        infer_expr(arm->value);
      scope_push();
      for (int b = 0; b < arm->body_count; b++)
        infer_stmt(arm->body[b]);
      scope_pop();
    }
    break;
  }

  case NODE_STATIC_VAR: {
    /* A `static` declaration inside a function body: register the name in the
     * current scope so subsequent references don't produce "undefined variable"
     * errors.  The lowering pass will hoist it to .data with a mangled label. */
    StaticVarNode *sv = (StaticVarNode *)node;
    if (sv->initializer)
      infer_expr(sv->initializer);
    scope_define(sv->var_name, sv->var_type);
    node->resolved_type = sv->var_type;
    break;
  }

  default:
    /* Try as expression */
    infer_expr(node);
    break;
  }
}


static void infer_function(ASTNode **params, int param_count, ASTNode **body,
                           int body_count, Type return_type,
                           const char *class_name) {
  if (body_count > 0) {    /* declarations from .hyi stubs are trusted */
    if (return_type.kind == TYPE_SIMPLE && return_type.name && !type_name_declared(return_type.name) &&
        param_count >= 0 && body[0])
      tc_error(body[0], "unknown return type '%s'", return_type.name);
    for (int i = 0; i < param_count; i++) {
      VarDeclNode *pv = (VarDeclNode *)params[i];
      if (pv && pv->base.type == NODE_VAR_DECL && pv->var_type.kind == TYPE_SIMPLE && pv->var_type.name &&
          !type_name_declared(pv->var_type.name))
        tc_error(params[i], "unknown type '%s' for parameter '%s'", pv->var_type.name, pv->var_name);
    }
  }
  scope_push();
  if (class_name) {
    scope_define("self", make_simple_type((char *)class_name, 0));
    /* Inside a method, the class's own fields are in scope under their bare
       names — `x` means `self.x`. Without defining them here the typechecker
       reported "undefined variable 'x'" for perfectly valid implicit-receiver
       code, printing an error for a program that then compiled and ran fine.
       Parameters and locals are defined after / later and shadow these, which
       matches how lower.c resolves the same names. */
    for (int i = 0; i < field_count; i++) {
      if (!fields[i].class_name || !fields[i].field_name) continue;
      if (strcmp(fields[i].class_name, class_name) != 0) continue;
      scope_define(fields[i].field_name, fields[i].field_type);
    }
  }
  for (int i = 0; i < param_count; i++) {
    if (!params[i])
      continue;
    VarDeclNode *p = (VarDeclNode *)params[i];
    scope_define(p->var_name, p->var_type);
  }
  for (int i = 0; i < body_count; i++)
    infer_stmt(body[i]);
  scope_pop();
}


static void register_func(const char *name, Type return_type, ASTNode **params,
                          int param_count, int is_external_decl) {
  if (func_count >= 256)
    return;
  FuncInfo *fi = &funcs[func_count++];
  fi->name = (char *)name;
  fi->return_type = return_type;
  fi->param_count = param_count < 16 ? param_count : 16;
  fi->is_external_decl = is_external_decl;
  for (int i = 0; i < fi->param_count; i++) {
    if (params && params[i]) {
      VarDeclNode *p = (VarDeclNode *)params[i];
      fi->param_types[i] = p->var_type;
    } else {
      fi->param_types[i] = unknown_type();
    }
  }
}

int tc_func_return_is_bool(const char *name) {
  if (!name) return 0;
  FuncInfo *fi = func_lookup(name);
  if (!fi) return 0;
  return fi->return_type.kind == TYPE_SIMPLE &&
         fi->return_type.name &&
         strcmp(fi->return_type.name, "bool") == 0;
}

/* Return-type flags for a call's IR_CALL.extra_int: 1 = bool, 2 = float
   (64-bit), 4 = float32, (1..6)<<3 = narrow integer (see below). Lets ir_to_mir type the call's result register. */
int tc_func_return_flags(const char *name) {
  if (!name) return 0;
  FuncInfo *fi = func_lookup(name);
  if (!fi || fi->return_type.kind != TYPE_SIMPLE || !fi->return_type.name) return 0;
  const char *n = fi->return_type.name;
  if (strcmp(n, "bool") == 0) return 1;
  if (strcmp(n, "float") == 0 || strcmp(n, "double") == 0 || strcmp(n, "float64") == 0) return 2;
  if (strcmp(n, "float32") == 0) return 4;
  /* narrow integers: (kind << 3), kind 1..6 = int8 uint8 int16 uint16 int32 uint32.
     A C function returning `int` leaves the upper half of rax undefined, so the
     caller has to sign/zero-extend it. */
  static const char *narrow[] = {"int8", "uint8", "int16", "uint16", "int32", "uint32"};
  for (int k = 0; k < 6; k++)
    if (strcmp(n, narrow[k]) == 0) return (k + 1) << 3;
  return 0;
}

/* Kind of the i-th declared parameter of `name`, for call-site conversion:
   0 = unknown / no such parameter, 1 = integer-like, 2 = float (64-bit),
   4 = float32. */
int tc_func_param_kind(const char *name, int i) {
  if (!name || i < 0) return 0;
  FuncInfo *fi = func_lookup(name);
  if (!fi || i >= fi->param_count) return 0;
  const Type *t = &fi->param_types[i];
  if (t->kind != TYPE_SIMPLE || !t->name) return 0;
  const char *n = t->name;
  if (strcmp(n, "float") == 0 || strcmp(n, "double") == 0 || strcmp(n, "float64") == 0) return 2;
  if (strcmp(n, "float32") == 0) return 4;
  if (strcmp(n, "unknown") == 0 || strcmp(n, "auto") == 0) return 0;
  return 1;
}

int tc_func_is_external_decl(const char *name) {
  if (!name) return 0;
  FuncInfo *fi = func_lookup(name);
  if (!fi) return 0;
  return fi->is_external_decl;
}

static void register_field(const char *class_name, const char *fname,
                           Type ftype) {
  if (field_count >= 1024)
    return;
  fields[field_count].class_name = (char *)class_name;
  fields[field_count].field_name = (char *)fname;
  fields[field_count].field_type = ftype;
  field_count++;
}


static int is_known_primitive(const char *name) {
  if (!name) return 0;
  static const char *known[] = {
    "int", "str", "bool", "void", "float", "Error", "auto",
    "usize", "isize",
    "int8",  "int16",  "int32",  "int64",
    "uint8", "uint16", "uint32", "uint64",
    "float32", "float64",
    NULL
  };
  for (int i = 0; known[i]; i++)
    if (strcmp(name, known[i]) == 0) return 1;
  return 0;
}

void typecheck(ProgramNode *program, const char *filename) {
  current_tc_file = filename ? filename : "<unknown>";

  sym_count = 0;
  scope_depth = 0;
  func_count = 0;
  field_count = 0;
  enum_type_count = 0;
  tc_module_count = 0;

  /* Pass 1: register all top-level functions, classes, and enums */
  for (int i = 0; i < program->decl_count; i++) {
    ASTNode *d = program->declarations[i];
    if (!d)
      continue;

    if (d->type == NODE_STATIC_VAR) {
      StaticVarNode *sv = (StaticVarNode *)d;
      scope_define(sv->var_name, sv->var_type);
      continue;
    }

    if (d->type == NODE_CLASS && tc_class_name_count < 1024) {
      const char *cname = ((ClassNode *)d)->name;
      if (!d->from_include)
        for (int k = 0; k < tc_class_name_count; k++)
          if (tc_class_names[k] && cname && strcmp(tc_class_names[k], cname) == 0) {
            tc_error(d, "class '%s' is defined more than once", cname);
            break;
          }
      tc_class_names[tc_class_name_count++] = cname;
    }
    if (d->type == NODE_ENUM) {
      EnumNode *en = (EnumNode *)d;
      if (enum_type_count < 64)
        enum_type_names[enum_type_count++] = en->name;
    }

    if (d->type == NODE_FUNC) {
      FuncNode *fn = (FuncNode *)d;
      register_func(fn->name, fn->return_type, fn->params, fn->param_count,
                    d->from_include && fn->body_count == 0);
    }

    if (d->type == NODE_MODULE) {
      ModuleNode *mn = (ModuleNode *)d;
      if (tc_module_count < 64)
          tc_module_names[tc_module_count++] = mn->name;
      for (int fi = 0; fi < mn->func_count; fi++) {
          FuncNode *fn = mn->funcs[fi];
          if (!fn) continue;
          char mangled[256];
          snprintf(mangled, sizeof(mangled), "%s__%s", mn->name, fn->name);
          char *mangled_copy = strdup(mangled);
          register_func(mangled_copy, fn->return_type, fn->params, fn->param_count,
                        d->from_include && fn->body_count == 0);
          /* Deliberately NOT registered under the plain name here anymore —
             that used to make e.g. `catFunc(...)` pass typecheck from
             *anywhere* in the program, even though lower.c only ever emits
             a callable symbol named `Cat__catFunc`. Bare calls from outside
             the module now get a real error (see mod_owner_of below); bare
             calls from a sibling function inside the same module still
             resolve, via tc_current_module + the mangled entry above. */
          if (mod_member_count < 256) {
              mod_member_name[mod_member_count]   = fn->name;
              mod_member_module[mod_member_count] = mn->name;
              mod_member_count++;
          }
      }
    }

    if (d->type == NODE_CLASS) {
      ClassNode *cn = (ClassNode *)d;
      for (int f = 0; f < cn->field_count; f++) {
        if (!cn->fields[f])
          continue;
        FieldNode *fld = cn->fields[f];
        register_field(cn->name, fld->name, fld->field_type);
      }
      for (int m = 0; m < cn->method_count; m++) {
        if (!cn->methods[m])
          continue;
        MethodNode *mn = cn->methods[m];
        char mangled[256];
        snprintf(mangled, sizeof(mangled), "%s__%s", cn->name, mn->name);
        /* We need a stable heap copy of the mangled name */
        char *mangled_copy = strdup(mangled);
        register_func(mangled_copy, mn->return_type, mn->params,
                      mn->param_count, 0);
      }
      if (cn->has_ctor) {
        char ctor_mangled[256];
        snprintf(ctor_mangled, sizeof(ctor_mangled), "%s__ctor", cn->name);
        char *ctor_copy = strdup(ctor_mangled);
        Type self_type = make_simple_type(cn->name, 0);
        register_func(ctor_copy, self_type, cn->ctor_params,
                      cn->ctor_param_count, 0);
      }
    }
  }

  /* Pass 2: infer function and class bodies */
  for (int i = 0; i < program->decl_count; i++) {
    ASTNode *d = program->declarations[i];
    if (!d)
      continue;

    if (d->type == NODE_STATIC_VAR) {
      StaticVarNode *sv = (StaticVarNode *)d;
      if (sv->initializer)
        infer_expr(sv->initializer);
      continue;
    }

    if (d->type == NODE_FUNC) {
      FuncNode *fn = (FuncNode *)d;
      infer_function(fn->params, fn->param_count, fn->body, fn->body_count,
                     fn->return_type, NULL);
    }

    if (d->type == NODE_CLASS) {
      ClassNode *cn = (ClassNode *)d;
      if (cn->has_ctor)
        infer_function(cn->ctor_params, cn->ctor_param_count, cn->ctor_body,
                       cn->ctor_body_count, make_simple_type(cn->name, 0),
                       cn->name);
      for (int m = 0; m < cn->method_count; m++) {
        if (!cn->methods[m])
          continue;
        MethodNode *mn = cn->methods[m];
        infer_function(mn->params, mn->param_count, mn->body, mn->body_count,
                       mn->return_type, cn->name);
      }
    }

    if (d->type == NODE_MODULE) {
      ModuleNode *mn = (ModuleNode *)d;
      tc_current_module = mn->name;
      for (int fi = 0; fi < mn->func_count; fi++) {
        FuncNode *fn = mn->funcs[fi];
        if (!fn) continue;
        infer_function(fn->params, fn->param_count, fn->body, fn->body_count,
                       fn->return_type, NULL);
      }
      tc_current_module = NULL;
    }
  }
}
