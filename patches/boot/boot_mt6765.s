/*
 * XNU Early Boot Code for MT6765 (MediaTek Helio G25)
 *
 * Copyright (c) 2026 UniXNU Project
 *
 * ARM64 EL1 entry point — called from chainloader.
 *
 * Calling convention (from chainloader.h jump_to_xnu()):
 *   x0 = DTB address       (0x47000000)
 *   x1 = bootargs address  (0x40001000)
 *
 * Boot flow:
 *   1. Disable interrupts, save boot params
 *   2. Set up boot stack
 *   3. Clear BSS
 *   4. Initialise early UART console
 *   5. Build identity-mapped page tables (2MB blocks)
 *   6. Clean D-cache, enable MMU + I/D-cache
 *   7. Initialise GICv2 interrupt controller
 *   8. Initialise ARM generic timer
 *   9. Park secondary CPUs (SMP hold)
 *  10. Call C _kernel_main(dtb, bootargs)
 */

    .section .text.boot
    .globl _start
    .type _start, @function

/* -------------------------------------------------------------------------
 * System register constants (for assemblers that lack symbolic names)
 *
 *   SCTLR_EL1  = s3_0_c1_c0_0
 *   MAIR_EL1   = s3_0_c10_c2_0
 *   TCR_EL1    = s3_0_c2_c0_2
 *   TTBR0_EL1  = s3_0_c2_c0_0
 *   VBAR_EL1   = s3_0_c12_c0_0
 *   CLIDR_EL1  = s3_1_c0_c0_1
 *   CNTP_TVAL_EL0  = s3_3_c14_c2_0
 *   CNTP_CTL_EL0   = s3_3_c14_c2_1
 *   CNTPCT_EL0     = s3_3_c14_c0_0
 *
 *   IC_IVAU    = CRm=5, op2=1, Rt=0b11111  →  0xD50B751F
 *   DC_CISW    = CRm=14, op2=1, Rt=0b11111 →  0xD50B7F20
 *   IC_IALLU   = CRm=5, op2=0, Rt=0b11111  →  0xD50B731F
 * ----------------------------------------------------------------------- */

#define SCTLR_EL1    s3_0_c1_c0_0
#define MAIR_EL1     s3_0_c10_c2_0
#define TCR_EL1      s3_0_c2_c0_2
#define TTBR0_EL1    s3_0_c2_c0_0
#define VBAR_EL1     s3_1_c12_c0_0
#define CLIDR_EL1    s3_1_c0_c0_1
#define CNTP_TVAL    s3_3_c14_c2_0
#define CNTP_CTL     s3_3_c14_c2_1

/* Page-table memory region (must match PAGE_TABLE_BASE/SIZE in memory_map) */
#define PT_MEM_BASE   0x44000000
#define PT_MEM_SIZE   0x00080000   /* 512KB — generous for 5 actual tables */

/* Boot stack top (must match BOOT_STACK_TOP in memory_map) */
#define BOOT_SP       0x44840000

/* ----------------------------------------------------------------------- */

_start:
    /* ----- 1. Disable all exceptions and interrupts ----- */
    .inst   0xD50346DF             /* MSR DAIFSet, #0xF (mask all) */

    /* ----- 2. Save boot parameters in callee-saved regs ----- */
    /* chainloader passes: x0 = DTB, x1 = bootargs */
    mov     x19, x0                /* x19 = DTB   (preserved across C calls) */
    mov     x20, x1                /* x20 = bootargs */

    /* ----- 3. Set up boot stack ----- */
    /* Must be done BEFORE any C call (which uses SP for locals/return) */
    ldr     x0, =BOOT_SP
    mov     sp, x0

    /* ----- 4. Set exception vectors ----- */
    adr     x0, _exception_vectors
    msr     VBAR_EL1, x0
    isb

    /* ----- 5. Clear BSS ----- */
    bl      _clear_bss

    /* ----- 6. Initialise early UART console ----- */
    bl      early_console_init

    adr     x0, _banner
    bl      early_console_puts

    adr     x0, _msg_dtb
    bl      early_console_puts
    mov     x0, x19
    bl      early_console_put_hex
    adr     x0, _msg_bootargs
    bl      early_console_puts
    mov     x0, x20
    bl      early_console_put_hex
    adr     x0, _nl
    bl      early_console_puts

    /* ----- 7. Build page tables (C: mmu_build_page_tables) ----- */
    adr     x0, _msg_pt
    bl      early_console_puts

    bl      mmu_build_page_tables
    cbz     x0, _pt_ok

    adr     x0, _err_pt
    bl      early_console_puts
_broken:
    wfe
    b       _broken

_pt_ok:
    adr     x0, _msg_ok
    bl      early_console_puts

    /* ----- 8. Clean D-cache for page-table memory, then enable MMU ----- */
    adr     x0, _msg_mmu
    bl      early_console_puts

    bl      _clean_dcache_region   /* clean D-cache for [PT_MEM_BASE .. +PT_MEM_SIZE) */
    bl      _enable_mmu

    adr     x0, _msg_ok
    bl      early_console_puts

    /* ----- 9. GICv2 ----- */
    adr     x0, _msg_gic
    bl      early_console_puts
    bl      _init_gic
    adr     x0, _msg_ok
    bl      early_console_puts

    /* ----- 10. ARM generic timer ----- */
    adr     x0, _msg_timer
    bl      early_console_puts
    bl      _init_timer
    adr     x0, _msg_ok
    bl      early_console_puts

    /* ----- 11. SMP: park secondary CPUs ----- */
    adr     x0, _msg_smp
    bl      early_console_puts
    bl      _init_smp
    adr     x0, _msg_ok
    bl      early_console_puts

    /* ----- 12. Jump to kernel C entry point ----- */
    adr     x0, _msg_jump
    bl      early_console_puts

    mov     x0, x19                /* arg0 = DTB   */
    mov     x1, x20                /* arg1 = bootargs */
    bl      _kernel_main

    /* kernel_main should never return */
    adr     x0, _msg_returned
    bl      early_console_puts

_hang:
    .inst   0xD50346DF             /* MSR DAIFSet, #0xF */
    wfe
    b       _hang

    .size _start, . - _start

/* =========================================================================
 * BSS clear  (uses linker-provided symbols __bss_start, __bss_end)
 * ======================================================================= */
    .align 4
_clear_bss:
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
_clear_bss_loop:
    cmp     x0, x1
    b.ge    _clear_bss_done
    str     xzr, [x0], #8
    b       _clear_bss_loop
_clear_bss_done:
    ret

/* =========================================================================
 * Clean D-cache by set/way for a physical address range
 *
 *   x0 = region base   (physical, 4KB-aligned)
 *   x1 = region size   (bytes)
 *
 * Uses DC CISW with CLIDR/CCSIDR to enumerate sets/ways.
 * DC CISW is encoded as .inst because some toolchains lack the symbolic name.
 * ======================================================================= */
    .align 4
_clean_dcache_region:
    stp     x19, x20, [sp, #-16]!
    stp     x21, x22, [sp, #-16]!
    stp     x23, x24, [sp, #-16]!

    add     x21, x0, x1            /* x21 = region end */

    /* Read CLIDR_EL1 — LoUIS = bits[2:0], LoC = bits[26:24] */
    mrs     x0, CLIDR_EL1
    lsr     x2, x0, #0
    and     x2, x2, #0x7           /* LoUIS */
    lsr     x3, x0, #24
    and     x3, x3, #0x7           /* LoC  */
    orr     x4, x2, x3
    cbz     x4, _cdc_done          /* no cache to clean */

    mov     x19, #0                /* cache level counter */

_cdc_level:
    lsl     x0, x19, #1
    add     x0, x0, x19            /* level * 3 */
    lsr     x0, x0, #1             /* index into CLIDR (2 bits per level) */

    mrs     x2, CLIDR_EL1
    lsr     x2, x2, x0
    and     x2, x2, #7             /* cache type at this level */
    cmp     x2, #2
    b.lt    _cdc_next_level        /* no data cache at this level */

    /* Select CSSELR for this level */
    lsl     x0, x19, #1
    msr     s3_1_c0_c0_0, x0      /* CSSIDR_EL1 = level */
    isb

    /* Read CCSIDR_EL1 */
    mrs     x3, s3_1_c0_c0_2      /* CCSIDR_EL1 */

    /* Number of sets: (CCSIDR[27:13] + 1) */
    ubfx    x4, x3, #13, #15
    add     x4, x4, #1

    /* Number of ways: (CCSIDR[2:0] + 1) */
    and     x5, x3, #0x7
    add     x5, x5, #1

    /* Line size: CCSIDR[15:3] → log2(words) → shift amount */
    ubfx    x6, x3, #3, #10

    /* Way position (log2 of number of ways) */
    clz     x7, x5
    mov     x8, #32
    sub     x7, x8, x7             /* x7 = log2(ways) */

    /* Loop over sets */
    mov     x9, #0
_cdc_set:
    /* Loop over ways */
    mov     x10, #0
_cdc_way:
    lsl     x11, x10, x7           /* way << log2(ways) */
    lsl     x12, x9,  x6           /* set << line_shift */
    lsl     x13, x19, #1           /* level << 1 */
    orr     x11, x11, x12
    orr     x11, x11, x13

    .inst   0xD50B7F20             /* DC CISW, x11 */
    add     x10, x10, #1
    cmp     x10, x5
    b.lt    _cdc_way

    add     x9, x9, #1
    cmp     x9, x4
    b.lt    _cdc_set

_cdc_next_level:
    add     x19, x19, #1
    cmp     x19, x3                /* LoC = max level + 1 */
    b.lt    _cdc_level

_cdc_done:
    dsb     sy
    .inst   0xD50B731F             /* IC IALLU — invalidate I-cache to PoU */
    dsb     sy
    isb

    ldp     x23, x24, [sp], #16
    ldp     x21, x22, [sp], #16
    ldp     x19, x20, [sp], #16
    ret

/* =========================================================================
 * Enable MMU + I-cache + D-cache
 *
 * Assumes:
 *   - Page tables already built at PT_MEM_BASE
 *   - MAIR/TCR values are constants
 * ======================================================================= */
    .align 4
_enable_mmu:
    /* MAIR: Attr0=Device-nGnRnE, Attr1=Normal-WB, Attr2=Normal-NC, Attr3=Device-nGnRwE */
    movz    x0, #0x4400
    movk    x0, #0xFF00, lsl #16
    msr     MAIR_EL1, x0

    /*
     * TCR_EL1:
     *   T0SZ  = 25   → 64-25 = 39-bit VA (lower)
     *   TG0   = 0b10 → 16KB granule
     *   SH0   = 0b11 → Inner Shareable
     *   ORGN0 = 0b01 → Write-Back
     *   IRGN0 = 0b01 → Write-Back
     *   T1SZ  = 25   → 39-bit VA (upper)
     *   TG1   = 0b01 → 16KB granule
     */
    ldr     x0, =0x80823519
    msr     TCR_EL1, x0

    /* TTBR0 = page-table base (PGD at 0x44000000) */
    ldr     x0, =PT_MEM_BASE
    msr     TTBR0_EL1, x0
    isb

    /* SCTLR: M=1 (MMU), C=1 (D-cache), I=1 (I-cache) */
    mrs     x0, SCTLR_EL1
    orr     x0, x0, #(1 << 0)     /* M  */
    orr     x0, x0, #(1 << 2)     /* C  */
    orr     x0, x0, #(1 << 12)    /* I  */
    msr     SCTLR_EL1, x0
    isb

    ret

/* =========================================================================
 * GICv2 initialisation
 *
 * Distributor  @ 0x10221000  (PLATFORM_GICD_BASE)
 * CPU interface @ 0x10222000  (PLATFORM_GICC_BASE)
 *
 * Strategy: all interrupts as Non-secure Group 1, default priority,
 * target CPU 0.  IRQ exceptions unmasked so timer interrupt can fire.
 * ======================================================================= */
#define GICD_BASE   0x10221000
#define GICC_BASE   0x10222000

/* Distributor registers */
#define GICD_CTLR        0x000
#define GICD_TYPER       0x004
#define GICD_IGROUPRn    0x080
#define GICD_ISENABLERn  0x100
#define GICD_ICENABLERn  0x180
#define GICD_ISPENDRn    0x200
#define GICD_ICPENDRn    0x280
#define GICD_IPRIORITYRn 0x400
#define GICD_ITARGETSRn  0x800

/* CPU interface registers */
#define GICC_CTLR        0x000
#define GICC_PMR         0x004
#define GICC_BPR         0x008
#define GICC_IAR         0x00C

    .align 4
_init_gic:
    stp     x19, x20, [sp, #-16]!
    stp     x21, x22, [sp, #-16]!

    ldr     x19, =GICD_BASE
    ldr     x20, =GICC_BASE

    /* --- Disable distributor while configuring --- */
    str     wzr, [x19, #GICD_CTLR]

    /* Determine number of SPIs from GICD_TYPER: ITLinesNumber = bits[4:0]
       max interrupt = (ITLinesNumber+1)*32 - 1 */
    ldr     w0, [x19, #GICD_TYPER]
    and     w0, w0, #0x1F
    add     w22, w0, #1            /* x22 = number of 32-int register groups */

    /* Route all interrupts to Group 1 (Non-secure) */
    ldr     w21, =0xFFFFFFFF
    mov     w2, #0
_gic_group_loop:
    add     x2, x19, w2, uxtw #2   /* x2 = GICD_BASE + n*4 */
    str     w21, [x2, #GICD_IGROUPRn]
    add     w2, w2, #1
    cmp     w2, w22
    b.lt    _gic_group_loop

    /* Set default priority (0xA0) for all 256 interrupts */
    ldr     w21, =0xA0A0A0A0
    mov     w2, #0
_gic_prio_loop:
    add     x2, x19, w2, uxtw #2
    str     w21, [x2, #GICD_IPRIORITYRn]
    add     w2, w2, #1
    cmp     w2, #64                 /* 256/4 = 64 registers */
    b.lt    _gic_prio_loop

    /* Target all interrupts to CPU 0 */
    ldr     w21, =0x01010101
    mov     w2, #0
_gic_target_loop:
    add     x2, x19, w2, uxtw #2
    str     w21, [x2, #GICD_ITARGETSRn]
    add     w2, w2, #1
    cmp     w2, #64
    b.lt    _gic_target_loop

    /* Enable distributor with Group 1 */
    mov     w0, #2                  /* EnableGrp1 = 1 */
    str     w0, [x19, #GICD_CTLR]

    /* --- CPU interface --- */
    mov     w0, #0x00000009        /* EnableGrp1=1, AckCtl1=1 */
    str     w0, [x20, #GICC_CTLR]

    movz    w0, #0xFF00            /* PMR: accept all priorities */
    str     w0, [x20, #GICC_PMR]

    mov     w0, #3                 /* BPR: preemption down to priority 0xF8 */
    str     w0, [x20, #GICC_BPR]

    /* Drain any spurious pending interrupt */
    ldr     w0, [x20, #GICC_IAR]

    /* Unmask IRQ exception at CPU level (clear IRQ bit in DAIF) */
    .inst   0xD503469F             /* MSR DAIFClr, #2 (unmask IRQ) */

    ldp     x21, x22, [sp], #16
    ldp     x19, x20, [sp], #16
    ret

/* =========================================================================
 * ARM Generic Timer initialisation
 *
 * CNTPCT_EL0  — physical counter (read-only)
 * CNTP_TVAL   — timer value register (countdown)
 * CNTP_CTL    — timer control (enable, mask, status)
 * ======================================================================= */
    .align 4
_init_timer:
    /* Read counter frequency */
    mrs     x0, cntfrq_el0
    cbz     x0, _no_timer

    /* Print frequency */
    adr     x1, _msg_freq
    bl      early_console_puts
    mov     x0, x0
    bl      early_console_put_dec
    adr     x0, _nl
    bl      early_console_puts

    /* Program a generous initial countdown (~10 seconds at typical 13 MHz) */
    ldr     x0, =130000000         /* 130M ticks ≈ 10s at 13 MHz */
    msr     CNTP_TVAL, x0

    /* Enable timer, unmask interrupt (CTRL = 1) */
    mov     x0, #1
    msr     CNTP_CTL, x0
    isb

_no_timer:
    ret

/* =========================================================================
 * SMP: park secondary CPUs
 *
 * CPU 0 = primary boot CPU → continue
 * CPU 1-3 = secondary → spin in _secondary_hold
 *
 * Uses MPIDR_EL1 Aff0 (bits[7:0]) for CPU ID.
 * Secondary CPUs are released later by the kernel via PSCI or a spin-table
 * mechanism in the device tree.
 * ======================================================================= */
    .align 4
_init_smp:
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF          /* Aff0 = CPU ID */
    cbz     x0, _is_primary

    adr     x0, _msg_secondary
    bl      early_console_puts

    /* Print CPU ID */
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    bl      early_console_put_dec
    adr     x0, _nl
    bl      early_console_puts

_secondary_hold:
    /* Wait for a non-zero release flag at _secondary_release[cpu_id] */
    wfe
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    ldr     x1, =_secondary_release
    ldr     x0, [x1, x0, lsl #3]   /* load release flag (64-bit stride) */
    cbz     x0, _secondary_hold

    /* Released — jump to address stored in the release entry */
    adr     x1, _secondary_release
    mrs     x2, mpidr_el1
    and     x2, x2, #0xFF
    ldr     x3, [x1, x2, lsl #3]
    br      x3

_is_primary:
    adr     x0, _msg_primary
    bl      early_console_puts

    /* Initialise the release flags to 0 (parked) */
    ldr     x0, =_secondary_release
    mov     x1, #1                 /* skip CPU 0 */
_secondary_clear:
    cmp     x1, #4
    b.ge    _smp_done
    str     xzr, [x0, x1, lsl #3]
    add     x1, x1, #1
    b       _secondary_clear

_smp_done:
    ret

/*
 * Secondary CPU release table (4 entries, one per CPU).
 *   0 = parked, non-zero = entry-point address to jump to.
 * Written by the kernel when it wants to bring up a secondary.
 */
    .bss
    .align 6                       /* 64-byte aligned */
_secondary_release:
    .skip 32                       /* 4 × 8 bytes */

/* =========================================================================
 * Exception vectors
 *
 * 16 entries (4 groups × 4 entries), each 128 bytes aligned.
 * Currently: all branches to self (halt).  The kernel will install proper
 * handlers once it boots.
 * ======================================================================= */
    .align 11                      /* VBAR requires 2KB alignment */
_exception_vectors:
    /* Current EL with SP0 */
    .rept 4
    b       .
    .org    . - 4 + 128
    .endr

    /* Current EL with SPx */
    .rept 4
    b       .
    .org    . - 4 + 128
    .endr

    /* Lower EL using AArch64 */
    .rept 4
    b       .
    .org    . - 4 + 128
    .endr

    /* Lower EL using AArch32 */
    .rept 4
    b       .
    .org    . - 4 + 128
    .endr

/* =========================================================================
 * Read-only data
 * ======================================================================= */
    .section .rodata
    .align 4

_banner:        .string "\n== UniXNU MT6765 ==\n"
_msg_dtb:       .string "  DTB       : "
_msg_bootargs:  .string "\n  Boot args : "
_msg_pt:        .string "  Page tables...\n"
_msg_mmu:       .string "  Enabling MMU...\n"
_msg_gic:       .string "  GICv2...\n"
_msg_timer:     .string "  Timer...\n"
_msg_smp:       .string "  SMP...\n"
_msg_jump:      .string "  Jumping to kernel_main...\n"
_msg_primary:   .string "  Primary CPU (core 0)\n"
_msg_secondary: .string "  Secondary CPU "
_msg_returned:  .string "ERROR: kernel_main returned!\n"
_msg_freq:      .string "  freq="
_msg_ok:        .string "  OK\n"
_nl:            .string "\n"

_err_pt:        .string "FATAL: mmu_build_page_tables failed!\n"

/* =========================================================================
 * _kernel_main — temporary stub
 *
 * Signature: void _kernel_main(uint64_t dtb, uint64_t bootargs)
 *   x0 = DTB address
 *   x1 = bootargs address
 *
 * This will be replaced by the real XNU kernel entry when the full kernel
 * is linked in.  For now it prints a message and halts so we can verify
 * the boot path up to this point via UART.
 * ======================================================================= */
    .section .text
    .globl _kernel_main
    .type _kernel_main, @function
_kernel_main:
    stp     x29, x30, [sp, #-16]!

    adr     x2, _msg_kmain
    bl      early_console_puts

    adr     x2, _msg_dtb_val
    bl      early_console_puts
    mov     x2, x0
    bl      early_console_put_hex
    adr     x2, _nl
    bl      early_console_puts

    adr     x2, _msg_ba_val
    bl      early_console_puts
    mov     x2, x1
    bl      early_console_put_hex
    adr     x2, _nl
    bl      early_console_puts

    adr     x2, _msg_kmain_done
    bl      early_console_puts

_kmain_hang:
    wfe
    b       _kmain_hang

    .size _kernel_main, . - _kernel_main

    .section .rodata
_msg_kmain:     .string "\n_kernel_main stub reached\n"
_msg_dtb_val:   .string "  dtb     = "
_msg_ba_val:    .string "  bootargs= "
_msg_kmain_done:.string "Halting here (XNU kernel not yet linked)\n"
