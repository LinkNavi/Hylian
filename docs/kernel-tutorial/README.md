# Writing a Kernel in Hylian

This is a from-scratch, hand-holding walkthrough of writing an x86-64 kernel
in Hylian. It assumes you already know the Hylian language (see the
[Language Reference](../language/README.md) if not) but have **never written
a kernel before**. Every concept — segmentation, interrupts, physical memory,
paging, syscalls — is explained from first principles before any code shows
up.

This tutorial is a companion to, not a replacement for, the terser
[Kernel Development Guide](../language/kernel.md), which is a reference for
people who already know what a GDT is and just want the Hylian syntax. If
you already know x86 kernel development from C, read that instead and come
back here only for specific chapters.

## What you'll build

By the end of this tutorial you'll have a kernel that:

- Boots under [Limine](https://github.com/limine-bootloader/limine) and
  prints to the screen and serial port
- Has its own Global Descriptor Table (GDT)
- Has its own Interrupt Descriptor Table (IDT) and handles a real hardware
  interrupt (the timer)
- Has a physical memory manager (PMM) built from the memory map the
  bootloader hands you
- Understands how paging and the higher-half mapping Limine already set up
  for you work, and how to make new mappings yourself
- Has the MSR plumbing for a `syscall`/`sysret`-based system call entry point

Every chapter's code is something you can actually build and boot in QEMU —
none of it is pseudocode. Where a topic goes beyond what's been verified
end-to-end (mainly: the very last mile of a full ring 3 → ring 0 → ring 3
syscall round trip), that's called out explicitly rather than presented as
working when it hasn't been proven to.

## A note on assembly

Most kernel tutorials lean on inline assembly constantly — for interrupt
prologues, for `iretq`, for `syscall`/`sysret`, for saving and restoring
registers. Hylian has compiler **builtins** for almost all of this
(`save_regs()`, `restore_regs()`, `iret()`, `sysret()`, `lgdt()`, `outb()`,
and so on — see the full list in the
[Kernel Development Guide](../language/kernel.md#the-kernel-module)), so this
tutorial writes real, complete interrupt handlers and syscall entry points
**without a single line of `asm { }`**. The one exception is firing the
`syscall` instruction itself in the syscalls chapter's test code, since
there's no builtin for "execute this raw instruction with no inputs or
outputs" and it isn't worth inventing one for a single opcode.

## Chapters

1. [Setting Up Your Environment](01-setup.md) — the toolchain: `hylian`,
   `zora`, `limine`, `xorriso`, `qemu`
2. [Your First Kernel](02-hello-kernel.md) — the smallest possible kernel,
   built and booted end to end
3. [The GDT](03-gdt.md) — what segmentation is, why every kernel sets up its
   own GDT, and how to build one with a `packed class`
4. [The IDT and Interrupts](04-idt-interrupts.md) — interrupt vectors, the
   PIC, and a real, asm-free timer interrupt handler
5. [The Physical Memory Manager](05-pmm.md) — physical vs. virtual memory,
   reading the bootloader's memory map, and a working bitmap allocator
6. [Paging and Virtual Memory](06-paging.md) — how the page tables Limine
   already built for you work, and how to map a new page yourself
7. [System Calls](07-syscalls.md) — `syscall`/`sysret`, the MSRs that
   configure them, and a dispatch table — plus an honest account of what's
   still unverified about the full ring 3 round trip
8. [Where to Go Next](08-next-steps.md) — what a real kernel needs that this
   tutorial doesn't cover, and pointers for building it

## Prerequisites

You should be comfortable with:

- Hylian itself (types, classes, pointers, `unsafe`)
- Hexadecimal and binary, and basic bitwise operations
- The idea that a CPU executes machine code and has registers — you do
  *not* need to already know x86 assembly

You do not need any prior OS development experience. That's the whole
point of this tutorial.
