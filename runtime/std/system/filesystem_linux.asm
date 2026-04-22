; Hylian runtime — filesystem operations (x86-64 NASM, Linux System V ABI)
; Assemble with: nasm -f elf64 filesystem_linux.asm
;
; Exports:
;   hylian_file_read   (char *path [rdi], int64_t path_len [rsi],
;                       char *buf  [rdx], int64_t buf_len  [rcx]) -> int64_t
;   hylian_file_write  (char *path [rdi], int64_t path_len [rsi],
;                       char *buf  [rdx], int64_t buf_len  [rcx]) -> int64_t
;   hylian_file_exists (char *path [rdi], int64_t path_len [rsi]) -> int64_t
;
; All path strings coming from Hylian are length-based and NOT guaranteed to
; be null-terminated.  Each function copies the path onto the stack and adds
; a NUL byte before handing it to any syscall that requires a C string.
;
; Syscall numbers (x86-64 Linux):
;   sys_read   = 0
;   sys_write  = 1
;   sys_open   = 2
;   sys_close  = 3
;   sys_access = 21
;
; Open flags:
;   O_RDONLY            = 0x000
;   O_WRONLY|O_CREAT|O_TRUNC = 0x241
;
; Mode for creat:
;   0644 = 0x1a4
;
; F_OK (access mode — file exists?) = 0

bits 64
default rel

global hylian_file_read
global hylian_file_write
global hylian_file_exists

; Maximum path length we will copy onto the stack.
; Linux PATH_MAX is 4096; we reserve one extra byte for the NUL terminator.
%define PATH_MAX_LEN  4096
%define PATH_BUF_SIZE 4097   ; PATH_MAX_LEN + 1

section .text

; ─────────────────────────────────────────────────────────────────────────────
; hylian_file_read(char *path    [rdi], int64_t path_len [rsi],
;                  char *buf     [rdx], int64_t buf_len  [rcx])
;     -> int64_t
;
;   Opens the file at `path` (path_len bytes, not NUL-terminated) for reading,
;   reads up to buf_len bytes into buf, closes the file, and returns the number
;   of bytes actually read.  Returns -1 on any error.
;
; Stack layout (after prologue, rsp is 16-byte aligned):
;   [rbp - PATH_BUF_SIZE]  : NUL-terminated copy of path  (4097 bytes)
;   (compiler may round allocation up to keep alignment)
;
; Callee-saved registers used: rbx, r12, r13, r14, r15
; ─────────────────────────────────────────────────────────────────────────────
hylian_file_read:
    push rbp
    mov  rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Save arguments before we clobber registers.
    mov  r12, rdi       ; r12 = path
    mov  r13, rsi       ; r13 = path_len
    mov  r14, rdx       ; r14 = buf
    mov  r15, rcx       ; r15 = buf_len

    ; Validate inputs.
    test r13, r13
    jle  .fr_error
    cmp  r13, PATH_MAX_LEN
    jg   .fr_error
    test r15, r15
    jle  .fr_error

    ; Allocate stack buffer for NUL-terminated path.
    ; Round PATH_BUF_SIZE up to the next 16-byte boundary = 4112.
    sub  rsp, 4112

    ; Copy path bytes into stack buffer.
    mov  rdi, rsp       ; destination
    mov  rsi, r12       ; source
    mov  rcx, r13       ; byte count
    rep movsb

    ; Append NUL terminator.
    mov  byte [rsp + r13], 0

    ; sys_open(path, O_RDONLY=0, 0)
    mov  rdi, rsp       ; NUL-terminated path
    xor  rsi, rsi       ; flags = O_RDONLY
    xor  rdx, rdx       ; mode (unused for O_RDONLY)
    mov  rax, 2         ; sys_open
    syscall

    test rax, rax
    js   .fr_restore_error  ; negative fd means error

    mov  rbx, rax       ; rbx = fd

    ; sys_read(fd, buf, buf_len)
    mov  rdi, rbx       ; fd
    mov  rsi, r14       ; buf
    mov  rdx, r15       ; count
    mov  rax, 0         ; sys_read
    syscall

    ; Save the read result (may be negative on error).
    mov  r15, rax       ; r15 = bytes_read (or negative error)

    ; sys_close(fd) — always close, ignore return value.
    mov  rdi, rbx
    mov  rax, 3         ; sys_close
    syscall

    ; Restore rsp past the path buffer.
    add  rsp, 4112

    ; Return bytes_read, or -1 on read error.
    test r15, r15
    js   .fr_error
    mov  rax, r15
    jmp  .fr_done

.fr_restore_error:
    add  rsp, 4112
.fr_error:
    mov  rax, -1

.fr_done:
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret


; ─────────────────────────────────────────────────────────────────────────────
; hylian_file_write(char *path    [rdi], int64_t path_len [rsi],
;                   char *buf     [rdx], int64_t buf_len  [rcx])
;     -> int64_t
;
;   Opens (or creates/truncates) the file at `path` for writing, writes
;   buf_len bytes from buf, closes the file, and returns the number of bytes
;   actually written.  Returns -1 on any error.
;
;   Open flags: O_WRONLY | O_CREAT | O_TRUNC = 0x241
;   Mode:       0644 = 0x1a4
; ─────────────────────────────────────────────────────────────────────────────
hylian_file_write:
    push rbp
    mov  rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Save arguments.
    mov  r12, rdi       ; r12 = path
    mov  r13, rsi       ; r13 = path_len
    mov  r14, rdx       ; r14 = buf
    mov  r15, rcx       ; r15 = buf_len

    ; Validate inputs.
    test r13, r13
    jle  .fw_error
    cmp  r13, PATH_MAX_LEN
    jg   .fw_error
    test r15, r15
    jle  .fw_error

    ; Allocate stack buffer for NUL-terminated path.
    sub  rsp, 4112

    ; Copy path bytes and NUL-terminate.
    mov  rdi, rsp
    mov  rsi, r12
    mov  rcx, r13
    rep movsb
    mov  byte [rsp + r13], 0

    ; sys_open(path, O_WRONLY|O_CREAT|O_TRUNC, 0644)
    mov  rdi, rsp
    mov  rsi, 0x241     ; O_WRONLY | O_CREAT | O_TRUNC
    mov  rdx, 0x1a4     ; mode 0644
    mov  rax, 2         ; sys_open
    syscall

    test rax, rax
    js   .fw_restore_error

    mov  rbx, rax       ; rbx = fd

    ; sys_write(fd, buf, buf_len)
    mov  rdi, rbx
    mov  rsi, r14
    mov  rdx, r15
    mov  rax, 1         ; sys_write
    syscall

    ; Save write result.
    mov  r15, rax

    ; sys_close(fd)
    mov  rdi, rbx
    mov  rax, 3         ; sys_close
    syscall

    ; Restore rsp.
    add  rsp, 4112

    ; Return bytes written, or -1 on error.
    test r15, r15
    js   .fw_error
    mov  rax, r15
    jmp  .fw_done

.fw_restore_error:
    add  rsp, 4112
.fw_error:
    mov  rax, -1

.fw_done:
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret


; ─────────────────────────────────────────────────────────────────────────────
; hylian_file_exists(char *path [rdi], int64_t path_len [rsi]) -> int64_t
;
;   Returns 1 if the file at `path` exists and is accessible, 0 otherwise.
;   Uses sys_access (syscall 21) with mode F_OK (0).
; ─────────────────────────────────────────────────────────────────────────────
hylian_file_exists:
    push rbp
    mov  rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov  r12, rdi       ; r12 = path
    mov  r13, rsi       ; r13 = path_len

    ; Validate.
    test r13, r13
    jle  .fe_notfound
    cmp  r13, PATH_MAX_LEN
    jg   .fe_notfound

    ; Allocate stack buffer.
    sub  rsp, 4112

    ; Copy path and NUL-terminate.
    mov  rdi, rsp
    mov  rsi, r12
    mov  rcx, r13
    rep movsb
    mov  byte [rsp + r13], 0

    ; sys_access(path, F_OK=0)
    mov  rdi, rsp
    xor  rsi, rsi       ; mode = F_OK
    mov  rax, 21        ; sys_access
    syscall

    ; sys_access returns 0 on success (file exists), negative on failure.
    add  rsp, 4112

    test rax, rax
    jnz  .fe_notfound

    ; File exists.
    mov  rax, 1
    jmp  .fe_done

.fe_notfound:
    xor  rax, rax

.fe_done:
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret
