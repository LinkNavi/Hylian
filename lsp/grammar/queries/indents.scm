; ── Indent begin (opening braces / block starters) ──────────────────────────

[
  (block)
  (class_body)
  (switch_stmt)
  (unsafe_block)
  (enum_decl)
] @indent.begin

; ── Indent end (closing braces dedent) ──────────────────────────────────────

[
  "}"
] @indent.end

; ── Branch (else / case / default stay at same level as opener) ─────────────

[
  "else"
  "case"
  "default"
] @indent.branch
