/*
 * XNU Early Boot Code for MT6765
 *
 * Copyright (c) 2026 UniXNU Project
 *
 * ARM64 assembly entry point for XNU on MT6765
 * Called from LK bootloader
 */

    .section .text.boot
    .globl _start
    .type _start, @function

_start:
    /* Disable interrupts */
    msr daifset, #0xf

    /* Save boot parameters */
    /* x0 = dtb address */
    /* x1 = boot args address */
    adr x20, _boot_dtb
    str x0, [x20]
    adr x20, _boot_args
    str x1, [x20]

    /* Set up exception vectors */
    adr x0, _exception_vectors
    msr vbar_el1, x0
    isb

    /* Enable MMU and caches */
    bl _setup_mmu

    /* Initialize BSS */
    bl _clear_bss

    /* Initialize early console (UART) */
    bl _init_early_console

    /* Print boot message */
    adr x0, _boot_msg
    bl _early_puts

    /* Initialize GIC */
    bl _init_gic

    /* Initialize timer */
    bl _init_timer

    /* Initialize SMP (bring up other CPUs) */
    bl _init_smp

    /* Jump to C entry point */
    adr x0, _boot_dtb
    ldr x0, [x0]
    adr x1, _boot_args
    ldr x1, [x1]
    bl _kernel_main

    /* Should not reach here */
_hang:
    wfe
    b _hang

    .size _start, . - _start

/*
 * Setup MMU
 */
_setup_mmu:
    /* TODO: Implement page table setup */
    /* For now, just return */
    ret

/*
 * Clear BSS section
 */
_clear_bss:
    /* TODO: Clear BSS */
    ret

/*
 * Initialize early console
 */
_init_early_console:
    /* TODO: Initialize UART */
    ret

/*
 * Early printf
 */
_early_puts:
    /* TODO: Implement early UART output */
    ret

/*
 * Initialize GIC
 */
_init_gic:
    /* TODO: Initialize interrupt controller */
    ret

/*
 * Initialize timer
 */
_init_timer:
    /* TODO: Initialize system timer */
    ret

/*
 * Initialize SMP
 */
_init_smp:
    /* TODO: Bring up secondary CPUs */
    ret

/*
 * Exception vectors
 */
    .align 11
_exception_vectors:
    /* Current EL with SP0 */
    .rept 8
    b .
    .endr

    /* Current EL with SPx */
    .rept 8
    b .
    .endr

    /* Lower EL using AArch64 */
    .rept 8
    b .
    .endr

    /* Lower EL using AArch32 */
    .rept 8
    b .
    .endr

/*
 * Data section
 */
    .section .data
_boot_dtb:
    .quad 0
_boot_args:
    .quad 0

    .section .rodata
_boot_msg:
    .string "UniXNU: Booting on MT6765...\n"
