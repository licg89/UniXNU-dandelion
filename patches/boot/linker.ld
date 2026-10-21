/*
 * Linker Script for XNU Boot on MT6765
 *
 * Memory layout:
 * 0x40080000 - Kernel entry (_start)
 * 0x40081000 - Text section
 * 0x40100000 - Data section
 * 0x40180000 - BSS section
 */

ENTRY(_start)

SECTIONS
{
    /* Kernel starts at 0x40080000 */
    . = 0x40080000;

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
