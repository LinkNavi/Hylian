# Kernel Development Guide

Hylian has first-class support for bare-metal and kernel development. With the `--freestanding` compiler flag, low-level type primitives, hardware-oriented class and function modifiers, and the `kernel` standard library module, you can write a complete operating system kernel entirely in Hylian.

---

## Overview

The following features are available for kernel development:

| Feature | Purpose |
|---|---|
| `--freestanding` flag | Disables stdlib/runtime linking, emits `_start` entry point |
| `include { kernel }` | VGA output, port I/O, and CPU halt for bare-metal targets |
| `packed class` | Struct layout with no alignment padding — required for hardware descriptors |
| `naked` functions | Functions with no prologue/epilogue — required for interrupt handlers |
| `volatile` pointer access | Prevents caching of hardware memory-mapped I/O reads and writes |
| `usize` / `isize` | Pointer-width integers for addresses, offsets, and sizes |
| `uint8`–`uint64`, `int8`–`int64` | Fixed-width integer types for hardware-defined data structures |
| `static` globals | Mutable global state in the `.data` section |
| `asm { ... }` blocks | Inline NASM assembly for direct register and instruction access |
| `&`, `\|`, `^`, `~`, `<<`, `>>` | Bitwise operators — native to all integer types |
| `cast<T>(expr)` | Truncating / reinterpreting type conversion |
| `size_of(T)` | Compile-time size of a type in bytes |

---

## The `--freestanding` Flag

When compiling a kernel or any bare-metal program, pass `--freestanding` to the Hylian compiler:

```sh
hylian --freestanding kernel.hyl -o kernel.asm
```

This flag does three things:

1. **Disables all standard library and runtime linking.** No `libc`, no allocator, no startup glue is linked in. Your binary contains only the code you write plus what the `kernel` module provides.
2. **Changes the entry point from `main` to `_start`.** The linker will look for `_start` as the binary entry symbol. Your bootloader or linker script is responsible for jumping to it.
3. **Suppresses calling-convention wrappers** that assume a hosted environment (e.g. `argc`/`argv` setup).

> **Note:** Do not use `--freestanding` for normal userspace programs. It is exclusively for kernels, bootloaders, and other bare-metal targets.

---

## The `kernel` Module

The `kernel` module is a special standard library module designed for bare-metal x86-64 targets. It provides the minimal primitives needed to produce output and control the CPU without any OS underneath.

### Importing

```hylian
include { kernel }
```

### Provided Functions

#### VGA Text Output

| Function | Signature | Description |
|---|---|---|
| `vga_clear` | `void vga_clear()` | Clears the VGA text buffer (fills with spaces using the current color) |
| `vga_set_color` | `void vga_set_color(uint8 color)` | Sets the foreground/background color byte for subsequent writes |
| `vga_print` | `void vga_print(str text)` | Writes a null-terminated string to the VGA buffer at the current cursor position |
| `vga_println` | `void vga_println(str text)` | Like `vga_print`, but advances the cursor to the next line afterward |
| `vga_put_char` | `void vga_put_char(uint8 c)` | Writes a single character to the VGA buffer |

#### Port I/O

| Function | Signature | Description |
|---|---|---|
| `outb` | `void outb(uint16 port, uint8 value)` | Writes a byte to an x86 I/O port |
| `inb` | `uint8 inb(uint16 port)` | Reads a byte from an x86 I/O port |
| `outw` | `void outw(uint16 port, uint16 value)` | Writes a word to an x86 I/O port |
| `inw` | `uint16 inw(uint16 port)` | Reads a word from an x86 I/O port |
| `io_wait` | `void io_wait()` | Issues a dummy port write to introduce a small I/O delay (required by some legacy hardware) |

#### CPU Control

| Function | Signature | Description |
|---|---|---|
| `halt` | `void halt()` | Disables interrupts and halts the CPU in an infinite `hlt` loop; never returns |
| `enable_interrupts` | `void enable_interrupts()` | Executes `sti` — enables hardware interrupts |
| `disable_interrupts` | `void disable_interrupts()` | Executes `cli` — disables hardware interrupts |
| `cli` | `void cli()` | Alias for `disable_interrupts` — executes the `cli` instruction directly |
| `sti` | `void sti()` | Alias for `enable_interrupts` — executes the `sti` instruction directly |

#### Memory Utilities

| Function | Signature | Description |
|---|---|---|
| `memset` | `void memset(usize dest, uint8 value, usize count)` | Fills `count` bytes at address `dest` with `value` |
| `memcpy` | `void memcpy(usize dest, usize src, usize count)` | Copies `count` bytes from address `src` to address `dest` |

---

## Bitwise Operations

All six bitwise operators — `&`, `|`, `^`, `~`, `<<`, `>>` — are native to every integer type in Hylian. They are indispensable for kernel work: reading and writing hardware registers, constructing bitmasks, and manipulating CPU control values.

```hylian
// Read CR0 and inspect control bits
int cr0 = 0x80000001;
int pe = cr0 & 1;               // check Protection Enable bit (bit 0)
int pg = (cr0 >> 31) & 1;       // check Paging bit (bit 31)

// Set the Write Protect bit (bit 16) without disturbing other bits
cr0 = cr0 | (1 << 16);

// Clear a bit: mask off the Task Switched flag (bit 3)
cr0 = cr0 & ~(1 << 3);

// Toggle the Alignment Mask bit (bit 18)
cr0 = cr0 ^ (1 << 18);
```

Bitwise operators sit **below** arithmetic in precedence and **above** logical operators (`&&`, `||`). Always parenthesise mixed expressions to be explicit about intent.

### MMIO flag example

```hylian
// Poll the Local APIC ICR — wait until the delivery status bit (bit 12) clears
usize icr_low = 0xFEE00300;
while ((volatile *icr_low & (1 << 12)) != 0) {
    // spin
}

// Send an INIT IPI: set delivery mode 0b101 in bits 10:8, set level (bit 14)
uint32 icr_val = (0x5 << 8) | (1 << 14);
volatile *icr_low = icr_val;
```

---

## `cast<T>()` for MMIO and Pointer Arithmetic

`cast<T>(expr)` converts between integer types and pointer types. In kernel code it is most often used to turn a raw integer address into a typed pointer, or to truncate a wide register value into a narrower storage type.

```hylian
// Write to PCI config space via a memory-mapped base address
usize pci_base  = 0xE0000000;
*uint32 reg     = cast<*uint32>(pci_base);
volatile *reg   = 0xDEADBEEF;
```

```hylian
// Extract individual bytes from a 32-bit value
uint32 val = 0xDEADBEEF;
uint8  lo  = cast<uint8>(val & 0xFF);           // 0xEF
uint8  hi  = cast<uint8>((val >> 24) & 0xFF);   // 0xDE
```

```hylian
// Truncation: cast<uint8>(300) yields 44  (300 & 0xFF)
uint8 truncated = cast<uint8>(300);
```

See the [Syntax Reference](syntax.md#castTexpr) for the full `cast<T>` rules including widening and sign-extension behaviour.

---

## Complete Example: A Minimal x86-64 Kernel

The following is a realistic, working kernel entry file. It demonstrates all of the major kernel-programming features together.

```hylian
include { kernel }

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------

// A tick counter incremented by the timer interrupt handler.
// Lives in .data; visible across the whole translation unit.
static int ticks = 0;

// The base address of the VGA text buffer (physical address 0xB8000).
static usize vga_base = 0xB8000;

// CPU feature flags we care about in CR0
static int CR0_PE = 0x00000001;   // bit  0 — Protection Enable
static int CR0_WP = 0x00010000;   // bit 16 — Write Protect
static int CR0_PG = 0x80000000;   // bit 31 — Paging

// ---------------------------------------------------------------------------
// Hardware data structures
// ---------------------------------------------------------------------------

// A single 8-byte GDT entry as the CPU expects it in memory.
// packed ensures there is absolutely no padding between fields.
packed class GdtEntry {
    public uint16 limit_low;
    public uint16 base_low;
    public uint8  base_mid;
    public uint8  access;
    public uint8  granularity;
    public uint8  base_high;

    GdtEntry(uint32 base, uint32 limit, uint8 access, uint8 gran) {
        limit_low   = cast<uint16>(limit & 0xFFFF);
        base_low    = cast<uint16>(base  & 0xFFFF);
        base_mid    = cast<uint8>((base  >> 16) & 0xFF);
        this.access = access;
        granularity = cast<uint8>(((limit >> 16) & 0x0F) | (gran & 0xF0));
        base_high   = cast<uint8>((base  >> 24) & 0xFF);
    }
}


// ---------------------------------------------------------------------------
// Volatile memory-mapped I/O
// ---------------------------------------------------------------------------

// Write a character+attribute byte pair directly into the VGA buffer.
// volatile prevents the compiler from caching or eliminating these writes,
// which is critical for memory-mapped hardware registers.
void vga_write_cell(usize offset, uint8 ch, uint8 color) {
    usize addr = vga_base + offset * 2;
    volatile *addr       = ch;
    volatile *(addr + 1) = color;
}

// Read back a character cell from the VGA buffer.
uint8 vga_read_char(usize offset) {
    usize addr = vga_base + offset * 2;
    return volatile *addr;
}

// ---------------------------------------------------------------------------
// Interrupt handler stub
// ---------------------------------------------------------------------------

// naked prevents the compiler from emitting a prologue or epilogue.
// The handler saves and restores all registers manually, then returns
// with iretq as the CPU expects for interrupt service routines.
naked void timer_isr() {
    asm {
        push rax
        push rbx
        push rcx
        push rdx
        push rsi
        push rdi
        push r8
        push r9
        push r10
        push r11
    }

    // Increment the global tick counter.
    ticks = ticks + 1;

    // Send End-of-Interrupt to the master PIC (port 0x20, command 0x20).
    outb(0x20, 0x20);

    asm {
        pop r11
        pop r10
        pop r9
        pop r8
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        pop rax
        iretq
    }
}

// ---------------------------------------------------------------------------
// Kernel entry point
// ---------------------------------------------------------------------------

void main() {
    // Clear the screen and set white-on-blue as the default color.
    // 0x1F = (background blue 0x1 << 4) | foreground white 0xF
    vga_clear();
    vga_set_color(0x1F);

    vga_println("Hylian Kernel v0.1");
    vga_println("------------------");
    vga_println("System initialized.");

    // Write a cell directly via memory-mapped I/O to demonstrate volatile access.
    // 0x2A = '*', 0x0A = bright green on black
    vga_write_cell(80, 0x2A, 0x0A);

    // Demonstrate bitwise ops: check whether paging is already enabled in CR0.
    // (In a real kernel you'd read CR0 via inline asm; this shows the operators.)
    int cr0 = 0x80000011;
    if ((cr0 & CR0_PG) != 0) {
        vga_println("Paging is ON");
    }
    // Enable write-protect in our local copy
    cr0 = cr0 | CR0_WP;

    // Enable hardware interrupts now that the kernel is ready.
    enable_interrupts();

    // Halt — spin forever waiting for interrupts.
    halt();
}
```

### Key Points

- **`static int ticks = 0;`** — mutable global variable in `.data`. Accessible from any function in the file.
- **`packed class GdtEntry`** — the `packed` modifier ensures the eight fields occupy exactly 8 bytes with no compiler-inserted padding, matching the layout the CPU's GDTR register expects.
- **`volatile *addr = ch;`** — write through a `volatile` pointer. The compiler may not batch, reorder, or eliminate this store.
- **`naked void timer_isr()`** — no generated prologue/epilogue. The handler controls the stack entirely via inline assembly and must end with `iretq`.
- **`usize vga_base = 0xB8000;`** — a pointer-width unsigned integer used as a raw address.
- **`cr0 & CR0_PG`** — bitwise AND to test a single flag bit without disturbing other bits.
- **`cr0 | CR0_WP`** — bitwise OR to set a flag bit without disturbing other bits.
- **`cast<uint8>(...)`** — truncate a 32-bit computed value to 8 bits for storage in a `uint8` field.

---

## Building and Linking

A freestanding kernel binary requires three steps: compiling Hylian to assembly, assembling to an object file, and linking with a custom linker script.

### 1. Compile Hylian to NASM Assembly

```sh
hylian --freestanding kernel.hyl -o kernel.asm
```

### 2. Assemble with NASM

```sh
nasm -f elf64 kernel.asm -o kernel.o
```

### 3. Link with `ld`

Do **not** use `gcc` or `clang` to link a freestanding kernel — they will inject C runtime startup code. Use `ld` directly with a linker script.

A minimal linker script (`kernel.ld`):

```ld
ENTRY(_start)

SECTIONS {
    . = 0x100000;

    .text   : { *(.text)   }
    .rodata : { *(.rodata) }
    .data   : { *(.data)   }
    .bss    : { *(.bss)    }
}
```

Link command:

```sh
ld -T kernel.ld -o kernel.elf kernel.o
```

### 4. Create a Bootable Image (optional)

To produce a raw binary suitable for a bootloader like GRUB or Limine:

```sh
objcopy -O binary kernel.elf kernel.bin
```

### Full Build Script

```sh
#!/bin/sh
set -e
hylian --freestanding kernel.hyl -o kernel.asm
nasm -f elf64 kernel.asm -o kernel.o
ld -T kernel.ld -o kernel.elf kernel.o
echo "Build succeeded: kernel.elf"
```

---

## Limine Bootloader Support

Hylian has built-in support for the [Limine bootloader](https://github.com/limine-bootloader/limine) via the `--target limine` compiler flag. This is the recommended way to boot a higher-half Hylian kernel.

### What `--target limine` does

- Emits a `_start` entry point that the Limine protocol expects.
- Outputs a `.limine_requests` ELF section containing the Limine base-revision magic numbers that tell the bootloader your kernel speaks the Limine protocol.
- Switches the default linker script to `runtime/platform/limine.ld`, which places the kernel in the higher half at `0xFFFFFFFF80000000 + 1 MiB`.

### Build commands

```sh
# 1. Compile Hylian to NASM assembly targeting Limine
hylian kernel.hy --target limine -o kernel.asm

# 2. Assemble with NASM
nasm -f elf64 kernel.asm -o kernel.o

# 3. Link against the Limine runtime support object
ld -T runtime/platform/limine.ld kernel.o runtime/platform/limine.o -o kernel.elf
```

### Runtime support

`runtime/platform/limine.c` is compiled alongside your kernel and handles:

- **The Limine protocol handshake** — fills in the request structures and validates the bootloader response.
- **VGA text output** — implements the `vga_*` family of functions provided by `std.kernel`.
- **Port I/O helpers** — `outb`, `inb`, `outw`, `inw`, `io_wait`.
- **CPU control stubs** — `halt`, `cli`, `sti`, `enable_interrupts`, `disable_interrupts`.
- **Memory utilities** — `memset`, `memcpy`.

You do not need to write or modify this file yourself; it is part of the Hylian runtime distribution.

### Higher-half addressing

The kernel is linked at `0xFFFFFFFF80000000 + 0x100000` (1 MiB into the higher half). All symbol addresses — including `static` globals and function pointers — will carry higher-half virtual addresses. Physical memory below 1 MiB (e.g. the VGA buffer at `0xB8000`) is still accessible through its physical address until you remap it; Limine sets up an identity map for low memory before jumping to `_start`.

```hylian
// Higher-half kernel base — useful for manual address translation
static usize KERNEL_BASE = 0xFFFFFFFF80100000;

// VGA buffer is still at its physical address under Limine's identity map
static usize vga_buf = 0xB8000;
```

---

## VGA Color Reference

The `vga_set_color` function and any direct VGA writes use a single byte encoding both the foreground and background colors. The high nibble is the background; the low nibble is the foreground.

```
color byte = (background << 4) | foreground
```

For example, white text on a blue background: `(0x1 << 4) | 0xF = 0x1F`.

### Color Values

| Value | Color |
|---|---|
| `0x0` | Black |
| `0x1` | Blue |
| `0x2` | Green |
| `0x3` | Cyan |
| `0x4` | Red |
| `0x5` | Magenta |
| `0x6` | Brown |
| `0x7` | Light Grey |
| `0x8` | Dark Grey |
| `0x9` | Light Blue |
| `0xA` | Light Green |
| `0xB` | Light Cyan |
| `0xC` | Light Red |
| `0xD` | Light Magenta |
| `0xE` | Yellow |
| `0xF` | White |

### Common Color Byte Combinations

| Byte | Description |
|---|---|
| `0x07` | Light grey on black (classic terminal default) |
| `0x0F` | Bright white on black |
| `0x1F` | Bright white on blue (classic BIOS screen) |
| `0x0A` | Bright green on black (classic green-screen) |
| `0x4F` | Bright white on red (panic / error screen) |
| `0x2F` | Bright white on green |
| `0x70` | Black on light grey (highlighted/selected item) |

---

## See Also

- [Syntax Reference](syntax.md) — full language syntax including `packed`, `naked`, `volatile`, `static`, and fixed-size types
- [Modules & Standard Library](modules.md) — how `include` and modules work
- [Inline Assembly](syntax.md#inline-assembly) — embedding raw NASM in Hylian functions