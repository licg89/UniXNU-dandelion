/*
 * Linker Script for XNU Boot on MT6765
 *
 * Memory layout:
 * 0x40100000 - Kernel entry (_start)     ← matches XNU_KERNEL_BASE in chainloader.h
 * 0x401xxxxx - Text section
 * 0x41100000 - Data section
 * 0x41900000 - BSS section
 */

ENTRY(_start)

SECTIONS
{
    /* Kernel starts at 0x40100000 — must match XNU_KERNEL_BASE in chainloader.h */
    . = 0x40100000;

    /* Text section (code) */
    .text : {
        *(.text.boot)
        *(.text)
        *(.text.*)
    }

    /* Read-only data */
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    /* Read-write data */
    .data : {
        *(.data)
        *(.data.*)
    }

    /* BSS (uninitialized data) */
    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /* Align to page boundary */
    . = ALIGN(16384);

    /* End of kernel */
    _end = .;

    /* Discard unwanted sections */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
