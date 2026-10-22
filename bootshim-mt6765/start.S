/* SPDX-License-Identifier: MIT
 * AArch64 Image framing follows dipa-studio/ios-sweet (see CREDITS.md).
 * This entry supports the observed MT8167 EL2 handoff and EL1 diagnostics.
 */
.section .text.head, "ax"
.global _head
_head:
    b _start
    .long 0
    .quad 0x80000     /* text_offset: must match kernel_offset for MTK LK */
    .quad _end        /* image_size */
    .quad 0xa         /* flags: relocatable */
    .quad 0, 0, 0
    .ascii "ARM\x64"
    .long 0
.text
.global _start
_start:
    msr daifset, #0xf

    /* Disable MTK watchdog at 0x10007000 (toprgu) */
    /* Try different values to disable */
    movz x1, #0x7000
    movk x1, #0x1000, lsl #16

    /* Try writing 0 to disable */
    mov w2, #0
    str w2, [x1]
    dsb sy

    /* Try writing to WDT_MODE register (offset 0) */
    mov w2, #0x11  /* Disable bit */
    str w2, [x1]
    dsb sy

    /* Try writing to WDT_SWRST register (offset 0x14) */
    movz w2, #0x2019
    movk w2, #0x1209, lsl #16  /* Magic value 0x12092019 for reset disable */
    str w2, [x1, #0x14]
    dsb sy

    /* We're at EL2 (confirmed) */
    msr spsel, #1
    adr x1, _stack_top
    mov sp, x1

    /* Infinite loop - do nothing else */
test_loop:
    wfe
    b test_loop

/* Enter the EL1h state required by XNU start_first_cpu; x0 retains the FDT.
 * Called only with EL2 MMU and caches off, as checked by shim_main.
 */
.global enter_el1
enter_el1:
    mov x1, #(1 << 31)
    msr hcr_el2, x1
    mov x1, #3
    msr cnthctl_el2, x1
    msr cntvoff_el2, xzr
    mov x1, #0x33ff
    msr cptr_el2, x1
    msr hstr_el2, xzr
    msr vttbr_el2, xzr
    mov x1, #0x0800
    movk x1, #0x30d0, lsl #16
    msr sctlr_el1, x1
    adr x1, vectors
    msr vbar_el1, x1
    adr x1, _stack_top
    msr sp_el1, x1
    adr x1, el1_start
    msr elr_el2, x1
    mov x1, #0x3c5
    msr spsr_el2, x1
    dsb sy
    isb
    eret
el1_start:
    mov x29, xzr
    mov x30, xzr
    bl shim_el1
    b halt

.macro vector slot
    .balign 128
    mov x0, #\slot
    b exception_report
.endm
.balign 2048
vectors:
    vector 0
    vector 1
    vector 2
    vector 3
    vector 4
    vector 5
    vector 6
    vector 7
    vector 8
    vector 9
    vector 10
    vector 11
    vector 12
    vector 13
    vector 14
    vector 15
