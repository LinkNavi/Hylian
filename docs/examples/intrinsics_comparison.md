# X86-64 Intrinsics: Before and After

This document shows how the new intrinsics simplify common kernel operations by eliminating verbose inline assembly.

## Loading the GDT (Global Descriptor Table)

### Before (Manual Assembly)
```hylian
void flush(uint64 base, uint64 limit) {
    asm {
        sub  rsp, 16
        mov  rax, [rbp - 8]
        mov  rcx, [rbp - 16]
        mov  [rsp + 2], rax
        mov  [rsp],     cx
        lgdt [rsp]
        add  rsp, 16
        push 0x08
        lea  rax, [rel .cs_flush]
        push rax
        retfq
    .cs_flush:
        mov  ax, 0x10
        mov  ds, ax
        mov  es, ax
        mov  fs, ax
        mov  gs, ax
        mov  ss, ax
    }
}
```

### After (Using Intrinsic)
```hylian
void flush(uint64 base, uint64 limit) {
    lgdt(base, limit);
    
    // Still need assembly for segment register reload
    asm {
        push 0x08
        lea  rax, [rel .cs_flush]
        push rax
        retfq
    .cs_flush:
        mov  ax, 0x10
        mov  ds, ax
        mov  es, ax
        mov  fs, ax
        mov  gs, ax
        mov  ss, ax
    }
}
```

**Result:** Eliminated 7 lines of assembly, improved readability.

---

## Loading the IDT (Interrupt Descriptor Table)

### Before (Manual Assembly)
```hylian
void load() {
    asm {
        sub  rsp, 16
        lea  rax, [rel _idt]
        mov  [rsp + 2], rax
        mov  word [rsp], 4095
        lidt [rsp]
        add  rsp, 16
    }
}
```

### After (Using Intrinsic)
```hylian
void load() {
    uint64 idt_base = cast<uint64>(&_idt);
    uint16 idt_limit = 4095;
    lidt(idt_base, idt_limit);
}
```

**Result:** Eliminated all assembly, completely type-safe.

---

## TLB Invalidation

### Before (Manual Assembly)
```hylian
void invalidate_page(uint64 vaddr) {
    asm {
        mov rax, [rbp - 8]
        invlpg [rax]
    }
}
```

### After (Using Intrinsic)
```hylian
void invalidate_page(uint64 vaddr) {
    invlpg(vaddr);
}
```

**Result:** Single line, no assembly needed.

---

## MSR Operations (Model-Specific Registers)

### Before (Manual Assembly)
```hylian
void wrmsr(uint64 msr, uint64 val) {
    asm {
        mov rcx, [rbp - 8]
        mov rax, [rbp - 16]
        mov rdx, rax
        shr rdx, 32
        and eax, 0xFFFFFFFF
        wrmsr
    }
}

uint64 rdmsr(uint64 msr) {
    uint64 result = 0;
    asm {
        mov  rcx, [rbp - 8]
        rdmsr
        shl  rdx, 32
        or   rax, rdx
        mov  [rbp - 16], rax
    }
    return result;
}
```

### After (Using Intrinsics)
```hylian
// These are now built-in!
// Just call them directly:
wrmsr(MSR_EFER, efer_value);
uint64 efer = rdmsr(MSR_EFER);
```

**Result:** Eliminated 20+ lines of error-prone assembly per call site.

---

## Real-World Example: SYSCALL Setup

### Before
```hylian
public void init() {
    _syscall_stack_top = cast<uint64>(&_syscall_stack) + 512 * 8;

    // Read current EFER and OR in SCE (bit 0)
    uint64 efer = 0;
    asm {
        mov  rcx, 0xC0000080
        rdmsr
        shl  rdx, 32
        or   rax, rdx
        mov  [rbp - 16], rax
    }
    efer = efer | cast<uint64>(0x1);
    
    asm {
        mov rcx, 0xC0000080
        mov rax, [rbp - 16]
        mov rdx, rax
        shr rdx, 32
        and eax, 0xFFFFFFFF
        wrmsr
    }
    
    uint64 star = (cast<uint64>(0x0008) << 32) | (cast<uint64>(0x0013) << 48);
    asm {
        mov rcx, 0xC0000081
        mov rax, [rbp - 24]
        mov rdx, rax
        shr rdx, 32
        and eax, 0xFFFFFFFF
        wrmsr
    }
    
    // ... more MSR writes
}
```

### After
```hylian
public void init() {
    _syscall_stack_top = cast<uint64>(&_syscall_stack) + 512 * 8;

    // Read current EFER and OR in SCE (bit 0)
    uint64 efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | cast<uint64>(0x1));
    
    uint64 star = (cast<uint64>(0x0008) << 32) | (cast<uint64>(0x0013) << 48);
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_LSTAR, addrof_fn(Syscall__entry));
    wrmsr(MSR_SFMASK, cast<uint64>(0x200));
}
```

**Result:** ~30 lines → ~6 lines. Much clearer intent.

---

## Loading Task Register

### Before (Manual Assembly)
```hylian
void load_tss(uint16 selector) {
    asm {
        mov ax, [rbp - 8]
        ltr ax
    }
}
```

### After (Using Intrinsic)
```hylian
void load_tss(uint16 selector) {
    ltr(selector);
}
```

**Result:** One line, no assembly.

---

## Summary

The new intrinsics provide:

1. **Safety:** Type-checked arguments, no risk of register misallocation
2. **Clarity:** Intent is obvious at the call site
3. **Maintainability:** Less assembly to audit and debug
4. **Compiler optimization:** The compiler can better schedule instructions around intrinsics

### When to Still Use Assembly

Keep using inline `asm {}` blocks for:
- Complex control flow (like context switching)
- Operations requiring specific instruction sequences
- Performance-critical sections where instruction order matters
- Segment register manipulation (not yet available as intrinsics)

The intrinsics shine for simple, frequently-used x86-64 instructions that would otherwise require repetitive assembly boilerplate.