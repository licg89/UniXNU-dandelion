//
// Copyright (c) 2012-2013, ARM Limited. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Derived from MT6768Pkg/Library/PlatformSecLib/AArch64/PlatformHelper.S.
//
// Code that was already at EL1 is unchanged (branch "1: ret").
//
// The EL2 -> EL1 drop keeps upstream's HCR_EL2 / ELR_EL2 / SPSR_EL2 sequence.
// For SOC_TYPE == 1 (MT6762G / MT6765) it additionally programs the EL2 timer
// and ID-register state, because:
//
//  - USE_PHYSICAL_TIMER = 1 means EL1 reads CNTPCT_EL0 and writes
//    CNTP_CVAL_EL0/CNTP_CTL_EL0. With CNTHCTL_EL2.EL1PCTEN/EL1PCEN clear those
//    accesses trap to EL2 (EC 0x18) and there is no EL2 vector installed yet,
//    so the machine hangs with no output. That is exactly the shape of the
//    "UEFI comes up but never gets a timer interrupt" failure this port has to
//    avoid, so the bits are set explicitly rather than assumed.
//  - CNTVOFF_EL2 = 0 keeps the virtual and physical counters equal (XNU reads
//    the physical counter; the shim's hand-off assumes the same time base).
//  - CPTR_EL2 = 0x33FF (RES1 bits, non-VHE layout) leaves FP/SIMD untrapped:
//    there is no EL2 handler for an FP trap.
//  - VPIDR_EL2/VMPIDR_EL2 are reset to UNKNOWN when EL2 is implemented, and
//    XNU reads MIDR_EL1 and MPIDR_EL1 at EL1 to pick errata handling and to
//    identify the boot CPU. Copy the real values, as Linux does in
//    __init_el2_nvhe_idregs.
//
// This block is copied from Silicon/MediaTek/MT8167Pkg's PlatformSecLib, the
// only MediaTek port in this tree that has actually booted a device (A04BR3).
// It has NOT been validated on dandelion: if the payload is entered at EL1
// (which is what the current shim does) none of it executes and it is dead
// code; if entered at EL2 it is required.
//

#include <AsmMacroLib.h>

//
// EDK2 preprocesses .S with "$(PP)" $(PP_FLAGS), which for CLANGPDB is a
// separate clang -E pass that does not inherit CC_FLAGS; MT6765Pkg.dsc.inc
// therefore sets -DSOC_TYPE in *_*_AARCH64_PP_FLAGS as well as in CC_FLAGS.
// Assert it, because if it ever stops arriving the timer setup below is
// silently compiled out and the failure shows up much later, as a hang.
//
#if !defined (SOC_TYPE)
#error "MT6765Pkg needs -DSOC_TYPE=<n>; see MT6765Pkg.dsc.inc [BuildOptions]."
#endif

ASM_FUNC (PlatformAssemblyInitialize)
  EL1_OR_EL2 (x0)
1:ret
// Switch to EL1
2:mov x0, #(1 << 31)
  msr hcr_el2, x0

#if SOC_TYPE == 1
  // MT6765 / MT6762G: make the EL1 physical timer usable and pin the ID regs.
  mov x0, #0x3            // CNTHCTL_EL2: EL1PCTEN | EL1PCEN
  msr cnthctl_el2, x0
  msr cntvoff_el2, xzr    // virtual counter == physical counter

  mov x0, #0x33FF         // CPTR_EL2: RES1 bits only, no FP/SIMD trap
  msr cptr_el2, x0

  mrs x0, midr_el1        // VPIDR_EL2  <- MIDR_EL1
  msr vpidr_el2, x0
  mrs x0, mpidr_el1       // VMPIDR_EL2 <- MPIDR_EL1
  msr vmpidr_el2, x0
#else
  // MT6768/MT6769T: inherited behaviour, unchanged.
#endif

  msr elr_el2, x30

  mov x0, #0x3C5 // EL1h
  msr spsr_el2, x0
  isb
  eret
