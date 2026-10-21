/*
 * Minimal Test Kernel for Chainloader Verification
 *
 * This is a simple ARM64 kernel that:
 * 1. Initializes UART
 * 2. Prints test messages
 * 3. Loops forever
 *
 * Used to verify chainloader works before XNU is available
 */

    .section .text.boot
    .globl _start
    .type _start, @function

_start:
    /* Disable interrupts */
    msr daifset, #0xf

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
    bl kernel_main

    /* Should never return */
_hang:
    wfe
    b _hang

    .size _start, . - _start

/*
 * Stack
 */
    .section .bss
    .align 16
_stack_bottom:
    .space 65536  /* 64KB stack */
_stack_top:
