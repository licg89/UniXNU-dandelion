##
#  MediaTek MT6765 / MT6762G (Helio P22 / G25) SoC configuration for Mu-Silicium.
#
#  Derived from Silicon/MediaTek/MT6768Pkg/MT6768Pkg.dsc.inc
#  (mouseos/uefi-a04br3 @ Silicon/MediaTek/MT6768Pkg). Structure and section
#  order are unchanged so the diff against upstream stays reviewable.
#
#  Target board: Xiaomi Redmi 9A ("dandelion", MT6762G).
#
#  Value provenance tags used below:
#    [MEASURED]  read on the dandelion itself (shim probe / Android dmesg).
#    [DT]        from the dandelion vendor kernel device tree:
#                MiCode/Xiaomi_Kernel_OpenSource branch dandelion-q-oss @ 8974b91,
#                arch/arm64/boot/dts/mediatek/mt6765.dts
#                (local copy: D:\aios\research\refs\mt6765-vendor).
#    [SRC]       from vendor kernel / LK source read locally (file+symbol named).
#    [INHERITED] copied unchanged from MT6768Pkg, NOT verified on MT6765.
#
#  The one value in this file that must NEVER be copied from MT6768Pkg is
#  gArmTokenSpaceGuid.PcdGicRedistributorsBase (see the GIC block).
#
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  #
  # Timer
  #
  # [MEASURED] The arch timer is PPI 30 (INTID 30 = GIC_PPI 14 in the DT).
  # Android /proc/interrupts: "GICv3 30 arch_timer". The non-secure EL1
  # physical timer is the one we want, hence USE_PHYSICAL_TIMER = 1.
  #
  USE_PHYSICAL_TIMER                         = 1

!include MediaTekPkg/MediaTekPkg.dsc.inc

[BuildOptions]
  #
  # Two deviations from MT6768Pkg, both deliberate:
  #
  # 1. -DSOC_TYPE=$(SOC_TYPE)
  #    Upstream MT6768Pkg did not expose SOC_TYPE to the compiler at all (it
  #    was only used by the DSC preprocessor). The libraries in this package
  #    use "#if SOC_TYPE == 1" to keep MT6765/MT6762G register data next to
  #    the inherited MT6768 data, so the macro has to reach C/asm.
  #    Same technique as Silicon/Qualcomm/PakalaPkg/PakalaPkg.dsc.inc.
  #
  # 2. -march=armv8-a+crc  (not -march=armv8.2-a+crypto+rcpc)
  #    MT6765, MT6762 and MT6762G are all 8x Cortex-A53 = ARMv8.0-A. Upstream's
  #    flags target Cortex-A75/A55 (MT6768/MT6769T) and allow ARMv8.1 LSE
  #    atomics and ARMv8.2 RCpc, which fault on A53. Same reasoning as
  #    Silicon/MediaTek/MT8167Pkg/MT8167Pkg.dsc.inc (Cortex-A35).
  #    If a future MT676x variant in this package is ARMv8.2, this line has
  #    to become conditional.
  #
  # 3. SOC_TYPE also has to reach the AArch64 .S preprocessor.
  #    CC_FLAGS alone is NOT enough: build_rule.txt preprocesses *.S with
  #    "$(PP)" $(PP_FLAGS), and for CLANGPDB that is a separate clang -E
  #    invocation that never sees CC_FLAGS. Without this line
  #    PlatformSecLib/AArch64/PlatformHelper.S trips its own
  #    "#if !defined (SOC_TYPE) #error" guard (observed 2026-10-28).
  #    PP_FLAGS is platform-wide here on purpose: the define is inert for
  #    every other AARCH64 module.
  #
  *_*_AARCH64_CC_FLAGS  = -march=armv8-a+crc -DSOC_TYPE=$(SOC_TYPE)
  *_*_AARCH64_PP_FLAGS  = -DSOC_TYPE=$(SOC_TYPE)

[PcdsFixedAtBuild]
  #
  # ACPI
  #
  # [INHERITED] SOC_TYPE 0 keeps the MT6768Pkg SOC_TYPE 0 payload verbatim
  #             ("MT6768  ", revision 0x6768) so a P22 board still gets a sane
  #             OEM table id. Change it if this package is ever used on a
  #             real MT6765 device.
  # [INHERITED] SOC_TYPE 1 payload reuses upstream's MT6769T branch slot but
  #             now carries dandelion's identity: OEM table id "MT6762  "
  #             (0x4D 0x54 0x36 0x37 0x36 0x32 0x20 0x20 little-endian) and
  #             revision 0x6762.
  #
!if $(SOC_TYPE) == 0
  # MT6765 (Helio P22). Inherited from MT6768Pkg SOC_TYPE 0, unverified.
  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemTableId|0x202038363736544D
  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemRevision|0x6768
!elseif $(SOC_TYPE) == 1
  # MT6762G (Helio G25) - dandelion.
  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemTableId|0x202032363736544D
  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemRevision|0x6762
!endif

  #
  # CPU
  #
  # [MEASURED] 8 cores, 2 clusters of 4. Affinity values seen: 0x000-0x003
  #            (cluster 0) and 0x100-0x103 (cluster 1). Identical to
  #            MT6768Pkg, so these two lines are unchanged.
  #
  gArmPlatformTokenSpaceGuid.PcdCoreCount|8
  gArmPlatformTokenSpaceGuid.PcdClusterCount|2

  #
  # GIC
  #
  # [MEASURED] PcdGicDistributorBase = 0x0C000000. GICD_IIDR reads 0x1143b at
  #            this address; the vendor DT node "gic: interrupt-controller@
  #            0c000000" is compatible = "arm,gic-v3" with reg <0 0x0c000000
  #            0 0x40000> (distributor). SAME VALUE AS MT6768Pkg.
  #
  # [MEASURED] PcdGicRedistributorsBase = 0x0C100000, 2 MiB (8 frames of
  #            0x20000). THREE independent sources agree:
  #              - vendor DT: reg <0 0x0c100000 0 0x200000> // redistributor
  #              - Android dmesg: "GICv3: CPU0: found redistributor 0 region
  #                0:0x000000000c100000"
  #              - shim probe: DANDELION_GICR_BASE in
  #                pexpert/pexpert/arm64/DANDELION.h
  #
  #  *** THIS IS THE ONE PCD THAT MUST NEVER BE COPIED FROM MT6768Pkg ***
  #  Upstream uses 0x0C040000. With that value the firmware boots and looks
  #  alive but never receives an arch-timer interrupt (the redistributor it
  #  programs is not the one the CPU is attached to), which is indistinguishable
  #  from a hang in the timer setup.
  #
  #  Also note: the *platform* MemoryMapLib must map the same window. A
  #  "GIC Redistributors" descriptor at 0x0C040000 (as in
  #  Platforms/Xiaomi/lancelotPkg/Library/MemoryMapLib/MemoryMapLib.c) will
  #  silently defeat this PCD because ArmGicV3 reads through the memory map.
  #
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x0C000000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x0C100000

  #
  # Generic timer interrupt numbers
  #
  # [DT] mt6765.dts timer node: interrupts = <GIC_PPI 13 ...>, <GIC_PPI 14 ...>,
  #      <GIC_PPI 11 ...>, <GIC_PPI 10 ...>. GIC_PPI n is INTID n + 16, so
  #      INTID 29 (secure phys), 30 (non-secure phys), 27 (virt), 26 (hyp).
  #      These are also ArmPkg's defaults; they are stated explicitly here so
  #      the measured value is visible in the build report.
  #
  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|29
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|30
  gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|27
  gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|26

  #
  # Timer frequency fallback
  #
  # [DT] mt6765.dts timer node: clock-frequency = <13000000> (this is a 13 MHz
  #      counter, not 26 MHz). The SiliciumPkg Timer.patch only consults this
  #      PCD when CNTFRQ_EL0 reads 0, so it is a safety net, not an override.
  #
  gSiliciumPkgTokenSpaceGuid.PcdTimerFreqOverwrite|13000000

  #
  # SMBIOS
  #
!if $(SOC_TYPE) == 0
  # MT6765. [INHERITED] text from MT6768Pkg SOC_TYPE 0, part number changed
  # only because this package is the MT6765 one.
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorModel|"MediaTek (TM) Helio P22 @ 2.00 GHz"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"MT6765"
!elseif $(SOC_TYPE) == 1
  # MT6762G - dandelion (Redmi 9A). 8x Cortex-A53 up to 2.0 GHz.
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorModel|"MediaTek (TM) Helio G25 @ 2.00 GHz"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"MT6762G"
!endif

[LibraryClasses]
  #
  # SEC Libraries
  #
  PlatformSecLib|MT6765Pkg/Library/PlatformSecLib/PlatformSecLib.inf

  #
  # PlatformPeiLib: overrides SiliciumNullPkg's null instance (mapped earlier
  # by SiliciumPkg.dsc.inc) so PlatformPeim() can re-arm the watchdog beacon
  # after memory init. It still builds the FV HOB the null instance built.
  #
  PlatformPeiLib|MT6765Pkg/Library/PlatformPeiLib/PlatformPeiLib.inf

  #
  # Custom Libraries
  #
  GpioImplLib|MT6765Pkg/Library/GpioImplLib/GpioImplLib.inf
  ClockImplLib|MT6765Pkg/Library/ClockImplLib/ClockImplLib.inf
  PmicWrapperImplLib|MT6765Pkg/Library/PmicWrapperImplLib/PmicWrapperImplLib.inf
  MsdcImplLib|MT6765Pkg/Library/MsdcImplLib/MsdcImplLib.inf
