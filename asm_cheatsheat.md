# x86-64 NASM Cheatsheet

## Syscalls (Linux x86-64)
| rax | name | rdi | rsi | rdx |
|-----|------|-----|-----|-----|
| 0 | sys_read | fd | buf ptr | len |
| 1 | sys_write | fd | buf ptr | len |
| 3 | sys_close | fd | - | - |
| 9 | sys_mmap | addr | len | prot |
| 10 | sys_mprotect | addr | len | prot |
| 11 | sys_munmap | addr | len | - |
| 60 | sys_exit | code | - | - |

```nasm
; example: write to stdout
mov rax, 1      ; sys_write
mov rdi, 1      ; fd = stdout
mov rsi, ptr    ; string pointer
mov rdx, len    ; length
syscall
```

---

## Calling Convention (System V AMD64)
Arguments go in registers in this order:

| arg | register |
|-----|----------|
| 1st | rdi |
| 2nd | rsi |
| 3rd | rdx |
| 4th | rcx |
| 5th | r8 |
| 6th | r9 |
| 7th+ | stack |

Return value → `rax`

```nasm
; defining a function with 2 params
_add:           ; rdi = a, rsi = b
    mov rax, rdi
    add rax, rsi
    ret         ; result in rax

; calling it
mov rdi, 10
mov rsi, 20
call _add       ; rax = 30
```

---

## Registers
| 64-bit | 32-bit | 16-bit | 8-bit | notes |
|--------|--------|--------|-------|-------|
| rax | eax | ax | al | return value, syscall number |
| rbx | ebx | bx | bl | callee-saved |
| rcx | ecx | cx | cl | 4th arg, loop counter |
| rdx | edx | dx | dl | 3rd arg |
| rsi | esi | si | sil | 2nd arg |
| rdi | edi | di | dil | 1st arg |
| rsp | esp | sp | spl | stack pointer (don't clobber) |
| rbp | ebp | bp | bpl | base pointer (callee-saved) |
| r8-r15 | r8d-r15d | r8w-r15w | r8b-r15b | extra regs |

**Callee-saved** (you must preserve if you use them): `rbx, rbp, r12-r15`  
**Caller-saved** (free to clobber): `rax, rcx, rdx, rsi, rdi, r8-r11`

---

## Data Definitions
```nasm
section .data
    ; initialized data
    mystr   db "hello", 0x0A, 0     ; string + newline + null
    mylen   equ $ - mystr           ; length of mystr
    mybyte  db 42                   ; single byte
    myword  dw 1000                 ; 2 bytes
    mydword dd 100000               ; 4 bytes
    myqword dq 123456789            ; 8 bytes
    myarr   dq 10, 20, 30, 40       ; array of 4 qwords

section .bss
    ; uninitialized (zeroed at startup, no space in binary)
    buf     resb 64                 ; reserve 64 bytes
    count   resq 1                  ; reserve 1 qword (8 bytes)
    arr     resb 8 * 32             ; array of 32 qwords
```

---

## Constants
```nasm
; equ = assembler symbol, emits no bytes
MAX     equ 32
STATUS_EMPTY  equ 0
STATUS_ACTIVE equ 1
STATUS_DONE   equ 2
```

---

## Memory Access
```nasm
mov rax, [addr]         ; load 8 bytes from addr
mov [addr], rax         ; store 8 bytes to addr
mov byte [addr], 1      ; store 1 byte
mov word [addr], 1      ; store 2 bytes
mov dword [addr], 1     ; store 4 bytes
mov qword [addr], 1     ; store 8 bytes

; pointer arithmetic
mov rax, [arr + rbx*8]  ; arr[rbx]
lea rax, [arr + rbx*8]  ; &arr[rbx] (address only, no dereference)
```

---

## Common Instructions
```nasm
; arithmetic
add rax, rbx        ; rax += rbx
sub rax, rbx        ; rax -= rbx
imul rax, rbx       ; rax *= rbx (signed)
idiv rbx            ; rdx:rax / rbx → rax=quotient, rdx=remainder
inc rax             ; rax++
dec rax             ; rax--

; bitwise
and rax, rbx
or  rax, rbx
xor rax, rax        ; fast zero: rax = 0
shl rax, 3          ; rax <<= 3 (multiply by 8)
shr rax, 1          ; rax >>= 1 (divide by 2)

; comparison + jumps
cmp rax, rbx        ; sets flags, doesn't store result
je  .label          ; jump if equal
jne .label          ; jump if not equal
jl  .label          ; jump if less (signed)
jg  .label          ; jump if greater (signed)
jle .label          ; jump if less or equal
jge .label          ; jump if greater or equal
jmp .label          ; unconditional jump

; stack
push rax            ; rsp -= 8, [rsp] = rax
pop  rax            ; rax = [rsp], rsp += 8

; string ops (used with rep prefix)
rep movsb           ; memcpy: copy rcx bytes from [rsi] to [rdi]
rep stosb           ; memset: fill rcx bytes at [rdi] with al
```

---

## Stack Frame (if you need local variables)
```nasm
_myfunc:
    push rbp
    mov rbp, rsp
    sub rsp, 32         ; allocate 32 bytes of locals

    ; access locals via [rbp - N]
    mov qword [rbp - 8], rdi    ; save first arg as local

    ; cleanup
    mov rsp, rbp
    pop rbp
    ret
```

---

## Sections
| section | purpose |
|---------|---------|
| `.text` | executable code |
| `.data` | initialized data (in binary) |
| `.bss` | uninitialized data (zeroed, not in binary) |
| `.rodata` | read-only data (string literals) |
