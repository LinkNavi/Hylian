; Hylian runtime — standard I/O (x86-64 NASM, Linux System V ABI)
; Assemble with: nasm -f elf64 io.asm
;
; Exports:
;   hylian_print      (char *str [rdi], int64_t len [rsi])
;   hylian_println    (char *str [rdi], int64_t len [rsi])
;   hylian_int_to_str (int64_t n [rdi], char *buf [rsi], int64_t buflen [rdx]) -> int64_t

bits 64
default rel

global hylian_print
global hylian_println
global hylian_int_to_str

section .data
    _newline: db 0x0a
    _minus:   db 0x2d   ; '-'

section .text

; ─────────────────────────────────────────────────────────────────────────────
; hylian_print(char *str [rdi], int64_t len [rsi])
;   Writes exactly `len` bytes from `str` to stdout (fd=1).
;   No newline appended.  Does nothing if len <= 0.
; ─────────────────────────────────────────────────────────────────────────────
hylian_print:
    test rsi, rsi
    jle  .done
    ; sys_write(fd=1, buf=str, count=len)
    mov  rdx, rsi       ; count
    mov  rsi, rdi       ; buf
    mov  rdi, 1         ; fd = stdout
    mov  rax, 1         ; syscall: sys_write
    syscall
.done:
    ret

; ─────────────────────────────────────────────────────────────────────────────
; hylian_println(char *str [rdi], int64_t len [rsi])
;   Writes `len` bytes from `str` to stdout, then appends a newline.
; ─────────────────────────────────────────────────────────────────────────────
hylian_println:
    push rbx
    push r12

    mov  rbx, rdi       ; save str pointer
    mov  r12, rsi       ; save length

    ; Write the string (if non-empty)
    test r12, r12
    jle  .write_newline

    mov  rdx, r12       ; count
    mov  rsi, rbx       ; buf
    mov  rdi, 1         ; fd = stdout
    mov  rax, 1         ; sys_write
    syscall

.write_newline:
    ; Write the newline character
    lea  rsi, [rel _newline]
    mov  rdx, 1         ; count = 1
    mov  rdi, 1         ; fd = stdout
    mov  rax, 1         ; sys_write
    syscall

    pop  r12
    pop  rbx
    ret

; ─────────────────────────────────────────────────────────────────────────────
; hylian_int_to_str(int64_t n [rdi], char *buf [rsi], int64_t buflen [rdx])
;     -> int64_t  (number of bytes written, in rax)
;
; Converts a signed 64-bit integer to its decimal ASCII representation and
; writes it into `buf`.  Returns the number of characters written, or 0 if
; `buflen` is too small to hold the result (minimum needed: 21 bytes for
; -9223372036854775808).
;
; Algorithm:
;   1. Handle sign: if n < 0, write '-' and negate n.
;   2. Extract digits right-to-left into a small temporary buffer on the stack.
;   3. Reverse into the caller-supplied buf.
; ─────────────────────────────────────────────────────────────────────────────
hylian_int_to_str:
    ; rdi = n
    ; rsi = buf
    ; rdx = buflen
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov  r12, rdi       ; r12 = n
    mov  r13, rsi       ; r13 = buf
    mov  r14, rdx       ; r14 = buflen

    ; Need at least 1 byte
    test r14, r14
    jle  .return_zero

    ; Temporary digit buffer on the stack (21 bytes is enough)
    sub  rsp, 24

    ; r15 = pointer into temp buffer (we fill from the end)
    lea  r15, [rsp + 23]

    xor  rbx, rbx       ; rbx = digit count
    xor  r9,  r9        ; r9  = negative flag

    ; Check for zero
    test r12, r12
    jne  .check_sign
    ; n == 0: write '0'
    mov  byte [r15], 0x30   ; '0'
    dec  r15
    inc  rbx
    jmp  .copy_to_buf

.check_sign:
    jge  .digit_loop
    ; Negative number
    inc  r9             ; set negative flag
    ; Negate: handle INT64_MIN specially
    mov  rax, 0x8000000000000000
    cmp  r12, rax
    je   .handle_min
    neg  r12
    jmp  .digit_loop

.handle_min:
    ; INT64_MIN = -9223372036854775808
    ; We can't negate it, so hard-code the last digit and divide by 10
    ; Last digit of 9223372036854775808 is 8
    mov  byte [r15], 0x38   ; '8'
    dec  r15
    inc  rbx
    mov  rax, 922337203685477580   ; 9223372036854775808 / 10
    mov  r12, rax
    jmp  .digit_loop

.digit_loop:
    test r12, r12
    jz   .digit_done
    ; rax = r12, rdx:rax / 10
    mov  rax, r12
    xor  rdx, rdx
    mov  rcx, 10
    div  rcx            ; rax = quotient, rdx = remainder
    ; digit = rdx + '0'
    add  dl, 0x30
    mov  [r15], dl
    dec  r15
    inc  rbx
    mov  r12, rax
    jmp  .digit_loop

.digit_done:
    ; Prepend minus sign if negative
    test r9, r9
    jz   .copy_to_buf
    mov  byte [r15], 0x2d   ; '-'
    dec  r15
    inc  rbx

.copy_to_buf:
    ; r15+1 points to the start of the digit string, rbx = total length
    ; Check that rbx <= r14 (buflen)
    cmp  rbx, r14
    jg   .buf_too_small

    ; Copy rbx bytes from r15+1 into r13
    lea  rsi, [r15 + 1]
    mov  rdi, r13
    mov  rcx, rbx
    rep movsb

    mov  rax, rbx       ; return length
    add  rsp, 24
    jmp  .return

.buf_too_small:
    add  rsp, 24
.return_zero:
    xor  rax, rax

.return:
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    ret
