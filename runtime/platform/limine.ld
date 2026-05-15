/* Limine higher-half kernel linker script for Hylian */
/* Load address: 0xFFFFFFFF80000000 + 1 MiB */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

KERNEL_VMA = 0xFFFFFFFF80000000;

SECTIONS {
    . = KERNEL_VMA + 0x100000;

    .limine_requests : {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    }

    .text : {
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    . = ALIGN(4096);
    .data : {
        *(.data .data.*)
    }

    . = ALIGN(4096);
    .bss : {
        *(COMMON)
        *(.bss .bss.*)
    }

    /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
    }
}
