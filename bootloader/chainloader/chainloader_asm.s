/*
 * Chainloader ARM64 assembly entry point
 */

    .section .text.boot
    .globl _start
    .type _start, @function

_start:
    /* Disable interrupts */
    msr daifset, #0xf

    /* Save boot parameters from LK */
    /* x0 = DTB address (from LK) */
    /* x1 = boot args (from LK) */

    /* Set up stack */
    adr x0, _stack_top
    mov sp, x0

    /* Clear BSS */
    adr x0, __bss_start
    adr x1, __bss_end
    cmp x0, x1
    b.ge _bss_done
_bss_loop:
    str xzr, [x0], #8
    cmp x0, x1
    b.lt _bss_loop
_bss_done:

    /* Call C main */
    bl chainloader_main

    /* Should never return */
_hang:
    wfe
    b _hang

    .size _start, . - _start

/*
 * Stack (256KB)
 */
    .section .bss
    .align 16
_stack_bottom:
    .space 262144  /* 256KB stack */
_stack_top:
