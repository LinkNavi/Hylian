; ── Hylian indentation rules ─────────────────────────────────────────────────

; Blocks: { … }
(block "}" @end) @indent

; Class bodies: class Foo { … }
(class_decl "{" "}" @end) @indent

; if / else branches
(if_stmt
  then: (block "}" @end)) @indent

(if_stmt
  else: (block "}" @end)) @indent

; while loops
(while_stmt
  (block "}" @end)) @indent

; for loops
(for_stmt
  (block "}" @end)) @indent

; for-in loops
(for_in_stmt
  (block "}" @end)) @indent

; Function bodies
(func_decl
  (block "}" @end)) @indent

; Method bodies
(method_decl
  (block "}" @end)) @indent

; Constructor bodies
(ctor_decl
  (block "}" @end)) @indent
