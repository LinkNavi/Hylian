; ── Hylian code outline ───────────────────────────────────────────────────────
;
; These patterns populate Zed's outline panel (the symbol tree shown in the
; breadcrumb bar and the outline view).  Each @item capture marks the full
; node that should appear as an entry; @name captures the text shown in the
; panel; @context gives extra surrounding context (e.g. the class name when
; showing a method).
;
; ─────────────────────────────────────────────────────────────────────────────

; ── Top-level functions ───────────────────────────────────────────────────────

(func_decl
  name: (identifier) @name) @item

; ── Classes ───────────────────────────────────────────────────────────────────

(class_decl
  name: (identifier) @name) @item

; ── Methods (shown as "ClassName :: methodName") ─────────────────────────────

(class_decl
  name: (identifier) @context
  (class_body
    (class_member
      (method_decl
        name: (identifier) @name)))) @item

; ── Constructors (shown as "ClassName :: ClassName(…)") ──────────────────────

(class_decl
  name: (identifier) @context
  (class_body
    (class_member
      (ctor_decl
        name: (identifier) @name)))) @item

; ── Public fields ─────────────────────────────────────────────────────────────

(class_decl
  name: (identifier) @context
  (class_body
    (class_member
      (field_decl
        "public"
        name: (identifier) @name)))) @item
