; ── Hylian indentation rules ─────────────────────────────────────────────────
;
; Zed uses Tree-sitter to determine how much to indent the next line after the
; user presses Enter.  The two captures used here are:
;
;   @indent  – the node whose body should be indented one level deeper
;   @end     – the closing token that ends that indented region
;
; ─────────────────────────────────────────────────────────────────────────────

; Blocks: { … }
(block "}" @end) @indent

; Class bodies: class Foo { … }
(class_decl "{" "}" @end) @indent

; if / else branches
(if_stmt
  consequence: (block "}" @end)) @indent

(if_stmt
  alternative: (block "}" @end)) @indent

; while loops
(while_stmt
  body: (block "}" @end)) @indent

; for loops
(for_stmt
  body: (block "}" @end)) @indent

; for-in loops
(for_in_stmt
  body: (block "}" @end)) @indent

; Function bodies
(func_decl
  body: (block "}" @end)) @indent

; Method bodies
(method_decl
  body: (block "}" @end)) @indent

; Constructor bodies
(ctor_decl
  body: (block "}" @end)) @indent
