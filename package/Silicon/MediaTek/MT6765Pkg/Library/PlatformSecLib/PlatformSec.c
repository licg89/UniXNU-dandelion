/**
  Copyright (c) 2011-2012, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

  Derived from MT6768Pkg/Library/PlatformSecLib/PlatformSec.c.
  The code is unchanged; only the notes below are new.

  What this does on MT6765/MT6762G (both looked up through the platform
  MemoryMapLib, this library contains no absolute addresses):

    DisableWatchDogTimer()  - "WatchDog Timer" region = TOPRGU at 0x10007000.
                              WDT_MODE_KEY (0x22000000) is confirmed against the
                              vendor LK source for MT6765
                              (platform/mt6765/include/platform/mtk_wdt.h,
                              MTK_WDT_MODE_KEY) and against the vendor kernel
                              driver mtk_wdt_v2.c. On entry from LK the watchdog
                              is armed (dual mode, IRQ + ext reset, ~10 s), so
                              this call is not optional.

    EnableConstantBlending() - "Display OVL" region. See PlatformRegisters.h:
                              the OVL register offsets below are inherited from
                              MT6768 and are NOT verified for dandelion's DDP.

    SetWatchdogBeacon()     - re-arms TOPRGU with a known timeout instead of
                              just disarming it. This is the M2 progress
                              beacon: dandelion has no UART, so the only free
                              channel is "how long until the SoC resets
                              itself". See the table in docs/HANDOVER-UEFI-1028.md.
**/

#include <Library/IoLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/PlatformSecLib.h>

#include "PlatformRegisters.h"

/**
  Re-arm the TOPRGU watchdog so the SoC resets itself after Seconds.

  Every call first disarms (WDT_MODE = KEY with ENABLE clear), then rewrites
  WDT_LENGTH, then enables with EXTEN so expiry is a real SoC reset. Writing
  WDT_LENGTH while the watchdog is running is not documented anywhere, hence
  the disarm/arm sandwich.

  @param[in] Seconds  Timeout in seconds, 1..WDT_LENGTH_MAX_SECONDS.
**/
VOID
SetWatchdogBeacon (
  IN UINTN  Seconds
  )
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR WDogRegion;
  UINT32                       Length;

  if ((Seconds == 0) || (Seconds > WDT_LENGTH_MAX_SECONDS)) {
    return;
  }

  Status = LocateMemoryRegionByName ("WatchDog Timer", &WDogRegion);
  if (EFI_ERROR (Status)) {
    return;
  }

  // Disarm: key only, ENABLE clear.
  MmioWrite32 (WDogRegion.Address + WDT_MODE_OFFSET, WDT_MODE_KEY);

  // WDT_LENGTH[15:5] = Seconds * 64, [4:0] = LENGTH_KEY.
  Length = (((UINT32)Seconds * 64) << 5) | WDT_LENGTH_KEY;
  MmioWrite32 (WDogRegion.Address + WDT_LENGTH_OFFSET, Length);

  // Arm with external reset.
  MmioWrite32 (
    WDogRegion.Address + WDT_MODE_OFFSET,
    WDT_MODE_KEY | WDT_MODE_EXTEN | WDT_MODE_ENABLE
    );
}

VOID
DisableWatchDogTimer ()
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR WDogRegion;

  // Locate "WatchDog Timer" Memory Region
  Status = LocateMemoryRegionByName ("WatchDog Timer", &WDogRegion);
  if (EFI_ERROR (Status)) {
    return;
  }

  // Disable WatchDog Timer
  MmioWrite32 (WDogRegion.Address, WDT_MODE_KEY);
}

VOID
EnableConstantBlending ()
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR OvlRegion;

  // Locate "Display OVL" Memory Region
  Status = LocateMemoryRegionByName ("Display OVL", &OvlRegion);
  if (EFI_ERROR (Status)) {
    return;
  }

  // Enable Constant Blending
  MmioOr32 (OvlRegion.Address + OVL_PITCH_OFFSET (0), OVL_CONST_BLEND);
}

//
// M2 screen postcode: paint a band straight into the framebuffer LK already
// set up.  No GOP, no graphics stack, no DXE -- just a write loop, so it works
// from SEC onwards.  [MEASURED] base/stride/resolution come from bootshim/fb.h,
// which draws on this exact device every day.
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

VOID
PlatformInitialize ()
{
  //
  // Green band at the very top: "SEC ran".  Do this before anything that can
  // hang, it is the cheapest possible proof of execution.
  //
  DandelionPaintBand (0x0000FF00, 0);

  //
  // M2 progress beacon, stage 1 of 3: this is the first code we own in SEC.
  // 20 s so it cannot be confused with the ~10 s watchdog LK is believed to
  // hand over armed, and cannot fire during a healthy SEC (which is < 1 s).
  // Re-armed to 30 s by PlatformPeiLib and to 2 s by PlatformProgressDxe.
  //
  SetWatchdogBeacon (20);

  // Enable Constant Blending
  EnableConstantBlending ();
}
