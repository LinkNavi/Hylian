; Hylian runtime — standard I/O (x86-64 NASM, Windows PE64)
; Assemble with: nasm -f win64 io_windows.asm
;
; Exports:
;   hylian_print      (char *str [rdi], int64_t len [rsi])
;   hylian_println    (char *str [rdi], int64_t len [rsi])
;   hylian_int_to_str (int64_t n [rdi], char *buf [rsi], int64_t buflen [rdx]) -> int64_t
;
; Our public interface uses System V AMD64 ABI (rdi/rsi/rdx) so callers
; compiled for that ABI work unchanged.  Each function saves those args and
; then issues Win32 calls using the Microsoft x64 ABI.
;
; Win32 calls used:
;   HANDLE GetStdHandle(DWORD nStdHandle)          — nStdHandle = -11 (stdout), -10 (stdin)
;   BOOL   WriteFile(HANDLE, LPCVOID, DWORD,
;                    LPDWORD, LPOVERLAPPED)
;   BOOL   ReadFile(HANDLE, LPVOID, DWORD,
;                   LPDWORD, LPOVERLAPPED)
;
; Microsoft x64 calling convention reminder:
;   args   : rcx, rdx, r8, r9, then stack (above 32-byte shadow)
;   caller : must allocate 32-byte shadow space before every call
;            must align rsp to 16 bytes before every call

bits 64
default rel

extern __imp_GetStdHandle
extern __imp_WriteFile
extern __imp_ReadFile

global hylian_print
global hylian_println
global hylian_int_to_str
global hylian_read_line
global hylian_str_to_int

section .data
    _newline: db 0x0a
    _minus:   db 0x2d   ; '-'

section .text

; ─────────────────────────────────────────────────────────────────────────────
; hylian_print(char *str [rdi], int64_t len [rsi])
;   Writes exactly `len` bytes from `str` to stdout.
;   No newline appended.  Does nothing if len <= 0.
;
; Stack frame layout after prologue (push rbp → rsp%16==8, then sub 48):
;   [rbp +  0] = saved rbp
;   [rbp -  8] = saved rdi (str)
;   [rbp - 16] = saved rsi (len)
;   [rbp - 24] = bytesWritten DWORD (4 bytes, padded to 8)
;   [rbp - 32..rbp-24] = (part of shadow / alignment pad)
;   [rsp +  0..+31]   = 32-byte shadow space for Win32 calls
;   [rsp + 32]        = 5th arg slot (lpOverlapped = NULL) for WriteFile
; Total: push rbp (8) + sub 48 = 56 bytes pushed → rsp aligned to 16 ✓
; ─────────────────────────────────────────────────────────────────────────────
hylian_print:
    push rbp
    mov  rbp, rsp
    ; 32 shadow + 8 bytesWritten + 8 5th-arg slot = 48, keeps 16-byte alignment
    sub  rsp, 48

    ; Save System V args
    mov  [rbp - 8],  rdi    ; str
    mov  [rbp - 16], rsi    ; len

    ; if len <= 0, nothing to do
    test rsi, rsi
    jle  .done

    ; ── GetStdHandle(-11) ──────────────────────────────────────────────────
    mov  ecx, -11                       ; STD_OUTPUT_HANDLE (sign-extended)
    call [rel __imp_GetStdHandle]       ; rax = handle

    ; ── WriteFile(handle, str, len, &bytesWritten, NULL) ──────────────────
    ; rcx = hFile
    ; rdx = lpBuffer
    ; r8  = nNumberOfBytesToWrite
    ; r9  = lpNumberOfBytesWritten
    ; [rsp+32] = lpOverlapped (NULL)
    mov  rcx, rax                       ; hFile = handle
    mov  rdx, [rbp - 8]                 ; lpBuffer = str
    mov  r8,  [rbp - 16]                ; nNumberOfBytesToWrite = len
    lea  r9,  [rbp - 24]                ; lpNumberOfBytesWritten = &bytesWritten
    mov  qword [rsp + 32], 0            ; lpOverlapped = NULL
    call [rel __imp_WriteFile]

.done:
    mov  rsp, rbp
    pop  rbp
    ret


; ─────────────────────────────────────────────────────────────────────────────
; hylian_println(char *str [rdi], int64_t len [rsi])
;   Writes `len` bytes from `str` to stdout, then appends a newline.
;
; Stack frame layout (push rbp → rsp%16==8, then sub 64):
;   [rbp -  8] = saved rdi (str)
;   [rbp - 16] = saved rsi (len)
;   [rbp - 24] = saved handle (from GetStdHandle)
;   [rbp - 32] = bytesWritten DWORD slot
;   [rsp +  0..+31] = 32-byte shadow
;   [rsp + 32]      = 5th arg slot for WriteFile
; push rbp (8) + sub 64 = 72 bytes → rsp % 16 == 8 ... let's check:
;   before push: rsp % 16 == 0  (at function entry, aligned per ABI)
;   after  push: rsp % 16 == 8
;   after  sub 56: (8 + 56) % 16 = 64 % 16 = 0  → aligned ✓
; So sub 56, not 64.
; ─────────────────────────────────────────────────────────────────────────────
hylian_println:
    push rbp
    mov  rbp, rsp
    ; 8 (str) + 8 (len) + 8 (handle) + 8 (bytesWritten) + 32 (shadow) = 56
    ; push rbp = 8 bytes, sub 56 → total 64 pushed → rsp aligned ✓
    sub  rsp, 56

    ; Save System V args
    mov  [rbp -  8], rdi    ; str
    mov  [rbp - 16], rsi    ; len

    ; ── GetStdHandle(-11) — get once, reuse for both writes ───────────────
    mov  ecx, -11
    call [rel __imp_GetStdHandle]
    mov  [rbp - 24], rax    ; save handle

    ; ── Write the string (skip if len <= 0) ───────────────────────────────
    mov  rax, [rbp - 16]
    test rax, rax
    jle  .write_newline

    mov  rcx, [rbp - 24]                ; hFile
    mov  rdx, [rbp -  8]                ; lpBuffer = str
    mov  r8,  [rbp - 16]                ; nNumberOfBytesToWrite = len
    lea  r9,  [rbp - 32]                ; lpNumberOfBytesWritten = &bytesWritten
    mov  qword [rsp + 32], 0            ; lpOverlapped = NULL
    call [rel __imp_WriteFile]

.write_newline:
    ; ── Write newline ─────────────────────────────────────────────────────
    mov  rcx, [rbp - 24]                ; hFile
    lea  rdx, [rel _newline]            ; lpBuffer = &newline
    mov  r8,  1                         ; nNumberOfBytesToWrite = 1
    lea  r9,  [rbp - 32]                ; lpNumberOfBytesWritten
    mov  qword [rsp + 32], 0            ; lpOverlapped = NULL
    call [rel __imp_WriteFile]

    mov  rsp, rbp
    pop  rbp
    ret


; ─────────────────────────────────────────────────────────────────────────────
; hylian_int_to_str(int64_t n [rdi], char *buf [rsi], int64_t buflen [rdx])
;     -> int64_t  (number of bytes written, in rax)
;
; Pure register arithmetic — no syscalls or Win32 calls — so this is
; identical to the Linux version.
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
    mov  rax, r12
    xor  rdx, rdx
    mov  rcx, 10
    div  rcx            ; rax = quotient, rdx = remainder
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
    cmp  rbx, r14
    jg   .buf_too_small

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
; hylian_read_line(char *buf [rdi], int64_t buflen [rsi]) -> int64_t
;   Reads a line from stdin into buf, stripping \r\n (Windows line endings).
;   Returns number of characters written (not including the newline).
;   Returns 0 on EOF/error.
;   Uses a 4096-byte internal buffer in .bss to amortize ReadFile calls.
;
; Stack frame (entry rsp is 16-byte aligned per ABI):
;   push rbp          → rsp%16 == 8
;   push rbx          → rsp%16 == 0
;   push r12          → rsp%16 == 8
;   push r13          → rsp%16 == 0
;   push r14          → rsp%16 == 8
;   push r15          → rsp%16 == 0
;   sub  rsp, 48      → rsp%16 == 0  ✓
;     [rsp+ 0..31] = 32-byte shadow space for Win32 calls
;     [rsp+32]     = 5th arg slot (lpOverlapped = NULL)
;     [rsp+40]     = bytesRead DWORD slot (r9 points here for ReadFile)
; ─────────────────────────────────────────────────────────────────────────────
hylian_read_line:
    push rbp
    mov  rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub  rsp, 48

    mov  rbx, rdi               ; rbx = buf
    mov  r12, rsi               ; r12 = buflen
    xor  r13, r13               ; r13 = result_len = 0

    ; Load buffer state into registers for the hot loop
    mov  r14, [rel _rl_buf_pos]     ; r14 = buf_pos
    mov  r15, [rel _rl_buf_fill]    ; r15 = buf_fill

.rl_loop:
    cmp  r14, r15
    jl   .rl_have_byte

    ; ── Buffer empty — refill via ReadFile ────────────────────────────────
    ; Get stdin handle (cached in .bss, call GetStdHandle once)
    mov  rax, [rel _stdin_handle]
    test rax, rax
    jnz  .rl_do_read
    mov  ecx, -10                       ; STD_INPUT_HANDLE
    call [rel __imp_GetStdHandle]
    mov  [rel _stdin_handle], rax

.rl_do_read:
    ; ReadFile(handle, _rl_buf, 4096, &bytesRead, NULL)
    mov  rcx, rax                       ; hFile
    lea  rdx, [rel _rl_buf]             ; lpBuffer
    mov  r8d, 4096                      ; nNumberOfBytesToRead
    lea  r9,  [rsp + 40]                ; lpNumberOfBytesRead → slot at [rsp+40]
    mov  qword [rsp + 32], 0            ; lpOverlapped = NULL
    call [rel __imp_ReadFile]

    mov  eax, dword [rsp + 40]          ; eax = bytes actually read
    test eax, eax
    jz   .rl_eof                        ; 0 bytes = EOF or error

    xor  r14, r14                       ; buf_pos = 0
    mov  r15, rax                       ; buf_fill = bytes_read

.rl_have_byte:
    ; Load next byte from internal buffer
    lea  rax, [rel _rl_buf]
    movzx ecx, byte [rax + r14]
    inc  r14                            ; buf_pos++

    ; Newline → done
    cmp  ecx, 0x0a
    je   .rl_done

    ; Carriage return → skip (Windows \r\n)
    cmp  ecx, 0x0d
    je   .rl_loop

    ; Store byte if there is room in caller's buffer
    cmp  r13, r12
    jge  .rl_loop                       ; buffer full — consume but discard
    mov  byte [rbx + r13], cl
    inc  r13
    jmp  .rl_loop

.rl_eof:
.rl_done:
    ; Flush buffer state back to .bss
    mov  [rel _rl_buf_pos],  r14
    mov  [rel _rl_buf_fill], r15
    mov  rax, r13                       ; return result_len

    add  rsp, 48
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret


; ─────────────────────────────────────────────────────────────────────────────
; hylian_str_to_int(char *str [rdi], int64_t len [rsi]) -> int64_t
;   Parses a decimal integer string. Skips leading whitespace, handles +/-.
;   Stops at first non-digit. Returns parsed value in rax.
;   Uses lea+add for multiply-by-10 (avoids imul on the hot path).
;
; Pure arithmetic — no Win32 calls needed.
; 4 pushes (32 bytes) from 16-byte-aligned entry → rsp%16 == 0 after pushes,
; no sub rsp needed.
; ─────────────────────────────────────────────────────────────────────────────
hylian_str_to_int:
    push rbx
    push r12
    push r13
    push r14

    mov  r12, rdi           ; r12 = str pointer (current)
    lea  r13, [rdi + rsi]   ; r13 = end pointer (str + len)
    xor  r14, r14           ; r14 = negative flag
    xor  rax, rax           ; rax = result

    ; Skip leading whitespace (space=0x20, tab=0x09)
.ssti_ws:
    cmp  r12, r13
    jge  .ssti_done
    movzx ecx, byte [r12]
    cmp  ecx, 0x20
    je   .ssti_ws_next
    cmp  ecx, 0x09
    jne  .ssti_sign
.ssti_ws_next:
    inc  r12
    jmp  .ssti_ws

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

.ssti_digits:
    cmp  r12, r13
    jge  .ssti_apply_sign
    movzx ecx, byte [r12]
    sub  ecx, 0x30              ; ecx = digit (or garbage if not a digit)
    cmp  ecx, 9
    ja   .ssti_apply_sign       ; unsigned compare catches < '0' too
    ; result = result * 10 + digit
    ; rax*10 using two cheap LEA/ADD instead of imul:
    lea  rax, [rax + rax*4]     ; rax = rax * 5
    add  rax, rax               ; rax = rax * 10
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


; ─────────────────────────────────────────────────────────────────────────────
section .bss
    _rl_buf:        resb 4096   ; internal read buffer
    _rl_buf_pos:    resq 1      ; current read position in _rl_buf
    _rl_buf_fill:   resq 1      ; number of valid bytes in _rl_buf
    _stdin_handle:  resq 1      ; cached stdin HANDLE (0 = not yet obtained)
