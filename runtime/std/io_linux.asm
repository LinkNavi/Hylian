; Hylian runtime — standard I/O (x86-64 NASM, Linux System V ABI)
; Assemble with: nasm -f elf64 io_linux.asm
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
global hylian_read_line
global hylian_str_to_int

section .data
    _newline: db 0x0a
    _minus:   db 0x2d   ; '-'

section .bss
    _rl_buf:      resb 4096   ; internal read buffer
    _rl_buf_pos:  resq 1      ; current read position in _rl_buf
    _rl_buf_fill: resq 1      ; number of valid bytes in _rl_buf

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


; ─────────────────────────────────────────────────────────────────────────────
; hylian_read_line
; hylian_read_line(char *buf [rdi], int64_t buflen [rsi]) -> int64_t
;   Reads characters from stdin into buf until \n, EOF, or buflen chars read.
;   Returns number of characters written (NOT including the \n).
;   Returns 0 on EOF with nothing read.
;   Uses a 4096-byte internal .bss buffer to amortize sys_read calls.
;
; Register usage during loop:
;   rbx = caller's buf pointer
;   r12 = caller's buflen
;   r13 = result_len (chars written so far)
;   r14 = _rl_buf_pos  (flushed to .bss on exit)
;   r15 = _rl_buf_fill (flushed to .bss on exit)
;
; Stack: 5 pushes (rbx,r12-r15) = 40 bytes; entry rsp is 16-aligned so after
; push rbp (1 more) = 48 bytes total → rsp%16==0. No sub rsp needed.
; ─────────────────────────────────────────────────────────────────────────────
hylian_read_line:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov  rbx, rdi               ; rbx = buf
    mov  r12, rsi               ; r12 = buflen
    xor  r13, r13               ; r13 = result_len = 0

    ; Load buffer state into registers for the hot loop
    mov  r14, [rel _rl_buf_pos]
    mov  r15, [rel _rl_buf_fill]

.rl_loop:
    ; If internal buffer is exhausted, refill from stdin
    cmp  r14, r15
    jl   .rl_have_byte

    ; sys_read(fd=0, _rl_buf, 4096)
    mov  rax, 0                 ; sys_read
    mov  rdi, 0                 ; fd = stdin
    lea  rsi, [rel _rl_buf]
    mov  rdx, 4096
    syscall

    test rax, rax
    jle  .rl_eof                ; 0 = EOF, negative = error

    xor  r14, r14               ; buf_pos = 0
    mov  r15, rax               ; buf_fill = bytes read

.rl_have_byte:
    ; Fetch next byte from internal buffer
    lea  rax, [rel _rl_buf]
    movzx ecx, byte [rax + r14]
    inc  r14                    ; buf_pos++

    ; Newline → done
    cmp  ecx, 0x0a
    je   .rl_done

    ; Store byte only if there is room in the caller's buffer
    cmp  r13, r12
    jge  .rl_loop               ; buffer full — consume byte but discard it
    mov  byte [rbx + r13], cl
    inc  r13
    jmp  .rl_loop

.rl_eof:
.rl_done:
    ; Flush buffer state back to .bss
    mov  [rel _rl_buf_pos],  r14
    mov  [rel _rl_buf_fill], r15
    mov  rax, r13               ; return result_len

    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret


; ─────────────────────────────────────────────────────────────────────────────
; hylian_str_to_int(char *str [rdi], int64_t len [rsi]) -> int64_t
;   Converts a decimal ASCII string to a signed 64-bit integer.
;   Skips leading whitespace (0x20, 0x09). Handles optional +/- sign.
;   Stops at the first non-digit character.
;   Returns 0 for empty or whitespace-only input. No overflow checking.
;
;   Hot-path optimisation: multiply-by-10 uses two cheap address-gen
;   instructions instead of imul:
;       lea rax, [rax + rax*4]   ; rax *= 5
;       add rax, rax             ; rax *= 10
;
; Register plan:
;   r12 = current pointer (advances through string)
;   r13 = end pointer     (str + len)
;   r14 = negative flag
;   rax = accumulator / return value
;
; 4 callee-saved pushes (32 bytes) from 16-aligned entry → rsp%16==0 ✓
; ─────────────────────────────────────────────────────────────────────────────
hylian_str_to_int:
    push rbx
    push r12
    push r13
    push r14

    mov  r12, rdi               ; r12 = str (current ptr)
    lea  r13, [rdi + rsi]       ; r13 = end ptr
    xor  r14, r14               ; r14 = negative flag
    xor  rax, rax               ; rax = result

    ; Skip leading whitespace
.ssti_ws:
    cmp  r12, r13
    jge  .ssti_done
    movzx ecx, byte [r12]
    cmp  ecx, 0x20              ; space
    je   .ssti_ws_next
    cmp  ecx, 0x09              ; tab
    jne  .ssti_sign
.ssti_ws_next:
    inc  r12
    jmp  .ssti_ws

    ; Check for optional sign
.ssti_sign:
    cmp  r12, r13
    jge  .ssti_done
    movzx ecx, byte [r12]
    cmp  ecx, 0x2b              ; '+'
    je   .ssti_sign_skip
    cmp  ecx, 0x2d              ; '-'
    jne  .ssti_digits
    inc  r14                    ; set negative flag
.ssti_sign_skip:
    inc  r12

    ; Main digit loop
.ssti_digits:
    cmp  r12, r13
    jge  .ssti_apply_sign
    movzx ecx, byte [r12]
    sub  ecx, 0x30              ; ecx = byte - '0'
    cmp  ecx, 9                 ; unsigned: wraps if < '0', catches > '9' too
    ja   .ssti_apply_sign
    ; result = result * 10 + digit  (no imul — two LEA/ADD instead)
    lea  rax, [rax + rax*4]     ; rax *= 5
    add  rax, rax               ; rax *= 10
    add  rax, rcx               ; rax += digit
    inc  r12
    jmp  .ssti_digits

.ssti_apply_sign:
    test r14, r14
    jz   .ssti_done
    neg  rax

.ssti_done:
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    ret
