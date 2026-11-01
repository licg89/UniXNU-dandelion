/**
  Platform PEI hooks for MediaTek MT6765 / MT6762G.

  Copyright (c) 2011-2014, ARM Limited. All rights reserved.
  Copyright (c) 2014, Linaro Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

  Replaces SiliciumNullPkg/Library/PlatformPeiLibNull for this platform, for
  one reason only: the M2 progress beacon.

  PlatformPeim() runs from SecMain() *after* InitializeMemory(), which makes
  it the natural "SEC got past memory init" milestone. dandelion has no UART,
  so the only channel available at this point is the watchdog: this file
  re-arms TOPRGU to 30 s, and PlatformProgressDxe (a DXE driver) later
  re-arms it to 2 s. The observed reset delay therefore says how far the
  firmware got:

      ~10 s  -> the FD never executed (LK's own watchdog is still armed)
      ~20 s  -> SEC entry ran, died before PlatformPeim
      ~30 s  -> passed memory init, died before the DXE driver ran
      ~2 s   -> the DXE driver ran (DXE core + dispatcher are alive)

  Everything else in this library must stay identical to the Null instance:
  it builds the FV HOB that PEI needs to find the firmware volume.
**/

#include <PiPei.h>

#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/PcdLib.h>

//
// Watchdog register definitions. Deliberately duplicated instead of shared:
// MT6765Pkg has no .dec, so there is no package include path for a common
// header, and adding a .dec to a package that upstream ships without one
// would change the DSC [Packages] list for every platform in this tree.
// Canonical copy with full provenance: Library/PlatformSecLib/PlatformRegisters.h
// (verified against drivers/watchdog/mediatek/wdt/common/wdt_v2/mtk_wdt.h
// and mtk_wdt_v2.c in the MT6765 vendor kernel).
//
#define WDT_MODE_OFFSET     0x0000
#define WDT_LENGTH_OFFSET   0x0004
#define WDT_MODE_KEY        0x22000000
#define WDT_MODE_ENABLE     0x0001
#define WDT_MODE_EXTEN      0x0004
#define WDT_LENGTH_KEY      0x0008

STATIC
VOID
SetWatchdogBeacon (
  IN UINTN  Seconds
  )
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR WDogRegion;
  UINT32                       Length;

  Status = LocateMemoryRegionByName ("WatchDog Timer", &WDogRegion);
  if (EFI_ERROR (Status)) {
    return;
  }

  // Disarm (key only, ENABLE clear), rewrite LENGTH, then arm with ext reset.
  MmioWrite32 (WDogRegion.Address + WDT_MODE_OFFSET, WDT_MODE_KEY);

  Length = (((UINT32)Seconds * 64) << 5) | WDT_LENGTH_KEY;
  MmioWrite32 (WDogRegion.Address + WDT_LENGTH_OFFSET, Length);

  MmioWrite32 (
    WDogRegion.Address + WDT_MODE_OFFSET,
    WDT_MODE_KEY | WDT_MODE_EXTEN | WDT_MODE_ENABLE
    );
}

//
// M2 screen postcode, stage 2: blue band just under SEC's green one.
// Same direct framebuffer write as PlatformSecLib (see the comment there);
// this proves PEI was reached without needing a console stack.
//
#define DANDELION_FB_BASE    0x7EC50000ULL
#define DANDELION_FB_STRIDE  2944
#define DANDELION_FB_WIDTH   720

STATIC
VOID
DandelionPaintBand (
  UINT32  Color,
  UINTN   Top
  )
{
  volatile UINT32  *Row;
  UINTN            X;
  UINTN            Y;

  for (Y = Top; Y < Top + 32; Y++) {
    Row = (volatile UINT32 *)(UINTN)(DANDELION_FB_BASE + Y * DANDELION_FB_STRIDE);
    for (X = 0; X < DANDELION_FB_WIDTH; X++) {
      Row[X] = Color;
    }
  }
}

EFI_STATUS
EFIAPI
PlatformPeim ()
{
  // Build FV HOB - required, same as the Null instance.
  BuildFvHob (PcdGet64 (PcdFvBaseAddress), PcdGet32 (PcdFvSize));

  // Blue band under SEC's green one: "PEI ran".
  DandelionPaintBand (0x000000FF, 32);

  // M2 progress beacon, stage 2 of 3.
  SetWatchdogBeacon (30);

  return EFI_SUCCESS;
}
