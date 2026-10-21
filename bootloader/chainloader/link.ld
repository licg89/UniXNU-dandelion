/*
 * Linker script for chainloader
 */

ENTRY(_start)

SECTIONS
{
    /* Load at 0x40000000 (where LK loads boot.img kernel) */
    . = 0x40000000;

    /* Text section */
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

    /* BSS */
    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /* End */
    _end = .;

    /* Discard unwanted sections */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
