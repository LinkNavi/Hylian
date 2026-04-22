; Hylian runtime — Error type (x86-64 NASM, Linux System V ABI)
; Assemble with: nasm -f elf64 errors_linux.asm
;
; Error struct layout (16 bytes):
;   offset  0:  char* message   (8 bytes)
;   offset  8:  int64_t code    (8 bytes)
;
; Exports:
;   hylian_make_error  (char *msg [rdi], int64_t len [rsi]) -> Error* [rax]
;   hylian_panic       (char *msg [rdi], int64_t len [rsi]) -> void (no return)

bits 64
default rel

global hylian_make_error
global hylian_panic

extern malloc
extern free

section .data
    _panic_prefix:     db "panic: ", 0
    _panic_prefix_len: equ 7
    _newline:          db 0x0a

section .text

; ─────────────────────────────────────────────────────────────────────────────
; hylian_make_error(char *msg [rdi], int64_t len [rsi]) -> Error* [rax]
;
;   Allocates a copy of the message string (null-terminated) and an Error
;   struct on the heap. Returns a pointer to the struct, or NULL on OOM.
;
;   Error struct:
;     [ptr +  0]  char*   message   — heap-allocated null-terminated copy
;     [ptr +  8]  int64_t code      — 0 (reserved for future use)
;
; Register usage:
;   r12 = original msg pointer
;   r13 = message length
;   r14 = pointer to heap-allocated message copy
;   r15 = pointer to allocated Error struct
; ─────────────────────────────────────────────────────────────────────────────
hylian_make_error:
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov  r12, rdi           ; r12 = msg ptr
    mov  r13, rsi           ; r13 = msg len

    ; Allocate space for a null-terminated copy of the message: len + 1
    mov  rdi, r13
    inc  rdi                ; len + 1 for null terminator
    call malloc
    test rax, rax
    jz   .oom

    mov  r14, rax           ; r14 = message copy buffer

    ; Copy the message bytes
    mov  rdi, r14
    mov  rsi, r12
    mov  rcx, r13
    rep  movsb
    mov  byte [r14 + r13], 0    ; null-terminate

    ; Allocate the Error struct: 16 bytes
    mov  rdi, 16
    call malloc
    test rax, rax
    jz   .oom_struct

    mov  r15, rax           ; r15 = Error struct pointer

    ; Fill the struct
    mov  [r15 + 0],  r14   ; message pointer
    mov  qword [r15 + 8], 0 ; code = 0

    mov  rax, r15           ; return Error*

    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    ret

.oom_struct:
    ; Free the message copy we already allocated, then return NULL
    mov  rdi, r14
    call free
.oom:
    xor  rax, rax           ; return NULL

    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    ret


; ─────────────────────────────────────────────────────────────────────────────
; hylian_panic(char *msg [rdi], int64_t len [rsi]) -> void (no return)
;
;   Writes "panic: <msg>\n" to stderr (fd=2) then exits with code 1.
;
; Register usage:
;   rbx = msg pointer
;   r12 = msg length
; ─────────────────────────────────────────────────────────────────────────────
hylian_panic:
    push rbx
    push r12

    mov  rbx, rdi           ; save msg ptr
    mov  r12, rsi           ; save msg len

    ; Write "panic: " prefix to stderr
    lea  rsi, [rel _panic_prefix]
    mov  rdx, _panic_prefix_len
    mov  rdi, 2             ; fd = stderr
    mov  rax, 1             ; sys_write
    syscall

    ; Write the message (if non-empty)
    test r12, r12
    jle  .write_newline
    mov  rsi, rbx
    mov  rdx, r12
    mov  rdi, 2
    mov  rax, 1
    syscall

.write_newline:
    ; Write trailing newline
    lea  rsi, [rel _newline]
    mov  rdx, 1
    mov  rdi, 2
    mov  rax, 1
    syscall

    ; Exit with code 1
    mov  rdi, 1
    mov  rax, 60            ; sys_exit
    syscall

    hlt                     ; unreachable — guard against kernel anomaly
