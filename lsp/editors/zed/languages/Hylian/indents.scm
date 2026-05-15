; Blocks: { … }
(block "}" @end) @indent

; Class bodies
(class_decl "{" "}" @end) @indent

; if / else
(if_stmt
  then: (block "}" @end)) @indent

(if_stmt
  else: (block "}" @end)) @indent

; while
(while_stmt
  (block "}" @end)) @indent

; for
(for_stmt
  (block "}" @end)) @indent

; for-in
(for_in_stmt
  (block "}" @end)) @indent

; switch
(switch_stmt "{" "}" @end) @indent

; unsafe
(unsafe_block "{" "}" @end) @indent

; func / method / ctor
(func_decl
  (block "}" @end)) @indent

(method_decl
  (block "}" @end)) @indent

(ctor_decl
  (block "}" @end)) @indent
