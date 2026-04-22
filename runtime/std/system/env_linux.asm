; Hylian runtime — environment & process operations (x86-64 NASM, Linux System V ABI)
; Assemble with: nasm -f elf64 env_linux.asm
;
; Exports:
;   hylian_getenv (char *name [rdi], int64_t name_len [rsi],
;                  char *buf  [rdx], int64_t buf_len  [rcx]) -> int64_t
;   hylian_exit   (int64_t code [rdi]) -> void  [does not return]
;
; hylian_getenv scans the process environment by walking the `environ` pointer
; array (declared extern — provided by the C runtime or the dynamic linker).
; Each entry has the form "KEY=VALUE\0".  For each entry we compare the first
; name_len bytes with `name`; if they match AND the byte immediately after is
; '=', we copy the VALUE portion into buf and return its length.
; Returns -1 if the variable is not found or any argument is invalid.
;
; hylian_exit issues sys_exit (syscall 60) and never returns.
;
; Syscall numbers (x86-64 Linux):
;   sys_exit = 60

bits 64
default rel

global hylian_getenv
global hylian_exit

extern environ          ; char **environ  — array of "KEY=VALUE" C strings,
                        ; NULL-terminated, supplied by the C runtime / ld.so

section .text

; ─────────────────────────────────────────────────────────────────────────────
; hylian_getenv(char *name    [rdi], int64_t name_len [rsi],
;               char *buf     [rdx], int64_t buf_len  [rcx])
;     -> int64_t
;
;   Searches the environment for the variable whose name matches the first
;   name_len bytes of `name`.  If found, copies the value (everything after
;   the '=' separator) into buf (at most buf_len bytes) and returns the number
;   of bytes copied.  Returns -1 if not found or on invalid arguments.
;
; Algorithm:
;   ptr = environ        ; ptr is char **
;   while (*ptr != NULL):
;       entry = *ptr     ; char *  pointing to "KEY=VALUE\0"
;       if memcmp(entry, name, name_len) == 0 AND entry[name_len] == '=':
;           value = entry + name_len + 1
;           copy min(strlen(value), buf_len) bytes into buf
;           return length copied
;       ptr++
;   return -1
;
; Register allocation:
;   rbx = name   (caller's name pointer)
;   r12 = name_len
;   r13 = buf    (caller's output buffer)
;   r14 = buf_len
;   r15 = current environ slot (char **)
;
; 6 pushes (rbp + rbx + r12-r15) = 48 bytes; entry rsp is 16-aligned,
; so rsp after pushes is still 16-aligned (48 mod 16 == 0). ✓
; ─────────────────────────────────────────────────────────────────────────────
hylian_getenv:
    push rbp
    mov  rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; ── Validate arguments ────────────────────────────────────────────────
    test rsi, rsi           ; name_len <= 0 ?
    jle  .ge_notfound
    test rcx, rcx           ; buf_len <= 0 ?
    jle  .ge_notfound
    test rdi, rdi           ; null name pointer?
    jz   .ge_notfound
    test rdx, rdx           ; null buf pointer?
    jz   .ge_notfound

    ; ── Save arguments ────────────────────────────────────────────────────
    mov  rbx, rdi           ; rbx = name
    mov  r12, rsi           ; r12 = name_len
    mov  r13, rdx           ; r13 = buf
    mov  r14, rcx           ; r14 = buf_len

    ; ── Load the base of the environ array ───────────────────────────────
    mov  r15, [rel environ] ; r15 = char **  (first slot)

.ge_outer_loop:
    ; Load the current entry pointer.
    mov  rdi, [r15]         ; rdi = char *entry  (or NULL at end of array)
    test rdi, rdi
    jz   .ge_notfound       ; end of environ — variable not found

    ; ── Compare first name_len bytes of entry with name ──────────────────
    ; We need memcmp(entry, name, name_len).
    ; Implemented inline with rep cmpsb; we must preserve rdi/rsi afterwards
    ; only if we need them, so save entry pointer in a callee-saved slot first.
    ; We'll use rax as a scratch copy of the entry pointer.
    mov  rax, rdi           ; rax = entry (save for separator check / value copy)

    mov  rsi, rbx           ; rsi = name
    ; rdi already = entry
    mov  rcx, r12           ; rcx = name_len
    repe cmpsb              ; advances rdi and rsi by rcx bytes

    jne  .ge_next           ; mismatch — try next entry

    ; ── Check that entry[name_len] == '=' ────────────────────────────────
    ; After the loop rdi points to entry + name_len (because repe cmpsb
    ; advanced it).  That is exactly where '=' should be.
    cmp  byte [rdi], 0x3d   ; '='
    jne  .ge_next

    ; ── Found the variable — copy the value into buf ──────────────────────
    ; Value starts at entry + name_len + 1  (i.e. rdi + 1 after the check).
    lea  rsi, [rdi + 1]     ; rsi = pointer to value string

    ; Copy at most buf_len bytes, stopping at NUL or buf_len.
    mov  rdi, r13           ; rdi = buf (destination)
    xor  rcx, rcx           ; rcx = bytes copied so far

.ge_copy_loop:
    cmp  rcx, r14           ; copied >= buf_len?
    jge  .ge_copy_done
    movzx eax, byte [rsi + rcx]
    test eax, eax           ; NUL terminator?
    jz   .ge_copy_done
    mov  byte [rdi + rcx], al
    inc  rcx
    jmp  .ge_copy_loop

.ge_copy_done:
    mov  rax, rcx           ; return bytes copied
    jmp  .ge_done

.ge_next:
    add  r15, 8             ; advance to next char ** slot (64-bit pointer)
    jmp  .ge_outer_loop

.ge_notfound:
    mov  rax, -1

.ge_done:
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret


; ─────────────────────────────────────────────────────────────────────────────
; hylian_exit(int64_t code [rdi]) -> void  [does not return]
;
;   Terminates the process immediately via sys_exit (syscall 60).
;   The exit code is passed directly through rdi as required by the ABI.
;   No prologue/epilogue is needed — this function never returns.
; ─────────────────────────────────────────────────────────────────────────────
hylian_exit:
    mov  rax, 60            ; sys_exit
    syscall
    ; sys_exit does not return; the line below is unreachable but
    ; acts as a safety net in case of a kernel anomaly.
    hlt
