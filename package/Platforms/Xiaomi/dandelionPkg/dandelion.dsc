##
#  Xiaomi Redmi 9A ("dandelion", MediaTek MT6762G / MT6765) platform.
#
#  Derived from Platforms/Xiaomi/lancelotPkg/lancelot.dsc (MT6768Pkg / MT6769T).
#  Deviations, all deliberate:
#
#   1. !include MT6765Pkg/MT6765Pkg.dsc.inc instead of MT6768Pkg's, and
#      SOC_TYPE = 1 (the MT6762G payload in MT6765Pkg; MT6765Pkg's SOC_TYPE 1
#      is MT6762G, NOT upstream MT6768Pkg's MT6769T).
#   2. PcdSystemMemoryBase / stack base 0x48000000 (shim-measured; lancelot
#      uses 0x40000000).
#   3. MemoryMapLib from dandelionPkg: GICR 0x0C100000, MSDC Top-0 0x11CD0000.
#      The GICR fix has to happen in BOTH places (the PCD in MT6765Pkg.dsc.inc
#      and this map) - see MT6765Pkg/README.md section 2.
#   4. SMBIOS / frame buffer / ACPI identity changed to the Redmi 9A.
#
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = dandelion
  PLATFORM_GUID                  = 57BB9BE1-18F9-4891-9C06-1F52BC21B57D
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/dandelionPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = dandelionPkg/dandelion.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 1   # 2026-10-28 21:2x: keep SimpleFbDxe (and therefore GOP) out of the FV to test whether the 720-pitch streaks come from a GOP Blt consumer

  #
  # 0 = MT6765 (Helio P22)
  # 1 = MT6762G (Helio G25)   <-- dandelion
  #
  SOC_TYPE                       = 1

!include MT6765Pkg/MT6765Pkg.dsc.inc

[PcdsFixedAtBuild]
  #
  # DDR Memory
  #
  # [MEASURED] The dandelion shim sees a 384 MiB window from 0x48000000
  #            (docs/UEFI-DANDELION-PLAN.md section 2). lancelot (MT6768) uses
  #            0x40000000; do not copy that value.
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x48000000

  #
  # UEFI Stack
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0x48000000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Xiaomi"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Redmi 9A"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"dandelion"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"Redmi_9A_dandelion"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"Redmi 9A"

  #
  # Simple Frame Buffer
  #
  # 720x1600, NT36525B MIPI DSI (docs/UEFI-DANDELION-PLAN.md section 2).
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|720
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|1600
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

  #
  # Storage
  #
  gMediaTekPkgTokenSpaceGuid.PcdStorageIsEMMC|TRUE

  #
  # Debug output
  #
  # 2026-10-29: DEBUG() used to go to a PL011 UART that this board does not
  # have, i.e. into the void, which is why every failure on this platform had to
  # be reverse-engineered from coloured cells.  With DebugLib pointing at ConOut
  # (below) the messages land on the panel, so the only question left is how
  # much of it to print.
  #
  # 0x80000040 = DEBUG_ERROR | DEBUG_INFO.  Bit definitions in MdePkg.dec
  # (gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel).
  #
  # 2026-10-29 04:xx: dropped INFO.  The console keeps only its last 68 lines, and
  # Mu's memory-attribute code prints several dozen INFO lines ("Subsys type N
  # with size 0x.. is added to 64-bit table") in the middle of the eMMC bring-up.
  # Those lines pushed the only lines that matter -- the `ddn ...` register dumps,
  # which are DEBUG_ERROR -- out of the window between the transfer failing and
  # the photo being taken.  Every diagnostic this project added uses DEBUG_ERROR
  # for exactly this reason, and EmmcDxe's own useful messages
  # ("Found EMMC device", "Failed to detect card") are errors too.
  #
  # Put 0x80000040 back if some INFO line turns out to be the one that explains a
  # failure; it is a single-character change and CI builds in six minutes.
  #
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000000

[LibraryClasses]
  #
  # Memory Libraries
  #
  MemoryMapLib|dandelionPkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # Input Libraries
  #
  KeypadDeviceLib|dandelionPkg/Library/KeypadDeviceLib/KeypadDeviceLib.inf

#
# DEBUG() goes to the panel, but only for boot-services drivers.
#
# Why per module type instead of one global entry: UefiDebugLibConOut needs
# gST->ConOut, and SEC/PEI/RUNTIME modules either have no system table or are
# not allowed to touch boot services, so a global override fails the build with
# "Module type [SEC] is not supported by library instance
# .../UefiDebugLibConOut.inf" (measured).  DXE_DRIVER covers MsdcDxe and our own
# drivers; UEFI_DRIVER covers EmmcDxe/SdDxe/FatPkg, which are the ones whose
# failures we cannot see any other way.
#
# Messages printed before DandelionConsoleDxe installs itself are dropped
# (ConOut is NULL then), which is exactly the window that does not matter.
#
[LibraryClasses.common.DXE_DRIVER]
  DebugLib|MdePkg/Library/UefiDebugLibConOut/UefiDebugLibConOut.inf

[LibraryClasses.common.UEFI_DRIVER]
  DebugLib|MdePkg/Library/UefiDebugLibConOut/UefiDebugLibConOut.inf

#
# Console geometry: say 90x100, because that is the only text mode this panel
# has.
#
# Why it matters (2026-10-29, measured): ConSplitterDxe's
# ConsplitterSetConsoleOutMode() looks for a text mode matching
# PcdConOutColumn/PcdConOutRow (default 80x25) and, failing that, calls
# SetMode (0xFF) on the text device and then ASSERT (!EFI_ERROR (Status)) on the
# result.  With only a 90x100 mode advertised that ASSERT fires -- the panel
# froze on "ASSERT [ConSplitterDxe] .../ConSplitter.c(2909)" and the watchdog
# reset the phone.  Declaring the real geometry makes PreferMode resolve to our
# single mode and the whole fallback path disappear.
#
[PcdsDynamicDefault]
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn|80
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow|68

[Components]
  #
  # ACPI Tables
  #
  # Lives in Silicium-ACPI/Platforms/Xiaomi/dandelion (PackagesPath entry
  # "Silicium-ACPI/Platforms/Xiaomi").
  #
#  dandelion/AcpiTables.inf   # TODO: needs Silicium-ACPI/Platforms/Xiaomi/dandelion

  #
  # Input
  #
  SiliciumPkg/Drivers/KeypadDxe/KeypadDxe.inf
  SiliciumPkg/Drivers/KeypadDeviceDxe/KeypadDeviceDxe.inf

  #
  # M2 progress beacon (re-arms the watchdog once DXE dispatch works).
  # dandelion has no UART, so this is how "reached DXE" becomes observable.
  # Temporary: see the comment in the driver.
  #
  dandelionPkg/Drivers/PlatformProgressDxe/PlatformProgressDxe.inf
  dandelionPkg/Drivers/DandelionConsoleDxe/DandelionConsoleDxe.inf

  #
  # M4 eMMC: inherit LK's MSDC0 controller and card state, then publish BlockIo.
  # This has to be the first thing in the firmware that touches MSDC0 -- see
  # docs/ANALYSIS-INHERIT-VS-REINIT.md.
  #
  dandelionPkg/Drivers/DandelionEmmcDxe/DandelionEmmcDxe.inf

  #
  # M4 storage beacon: reports how far eMMC -> BlockIo -> partition -> FAT got.
  # Its line on the panel is the M4 criterion.
  #
  dandelionPkg/Drivers/StorageBeaconDxe/StorageBeaconDxe.inf

  #
  # M5: find a payload and start it.  BdsDxe is not in this firmware (it
  # dead-loops in ConSplitter against this panel), so this driver is the whole
  # boot policy: FAT file first, firmware-volume image second.
  #
  dandelionPkg/Drivers/PayloadBootDxe/PayloadBootDxe.inf

  #
  # The M5 payload itself.  An application is not dispatched by DXE, so it
  # costs nothing to have in the FV: PayloadBootDxe picks it up by file GUID and
  # LoadImage's it.  It is how the load-and-start path gets verified before eMMC
  # works.
  #
  dandelionPkg/Applications/PayloadHello/PayloadHello.inf
