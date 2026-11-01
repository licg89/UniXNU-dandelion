/** @file
  A deliberately tiny UEFI application used as the M5 payload.

  Its only job is to prove that PayloadBootDxe can find, load and start an
  image on this board.  That matters because M4 (eMMC) is not finished yet: with
  this payload embedded in the firmware volume, the load-and-start half of M5 can
  be verified on hardware that has no working block device at all.

  What it does, in an order that assumes nothing about the firmware state:

    1. paints a green bar at framebuffer y 1570..1596 -- the same band
       PayloadBootDxe uses, so "green" can only mean the payload itself ran;
    2. prints one line to ConOut if a console exists;
    3. returns EFI_SUCCESS, which PayloadBootDxe turns into its own green bar.

  It touches the framebuffer directly rather than going through GOP, for the same
  reason every other diagnostic in this project does: the panel is the only
  channel this board has, and it must work even if the console did not come up.

  Copyright (c) Microsoft Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>

#define PAYLOAD_FB_BASE    0x7EC50000ULL
#define PAYLOAD_FB_STRIDE  2944
#define PAYLOAD_FB_WIDTH   720
#define PAYLOAD_BAR_TOP    1570
#define PAYLOAD_BAR_HEIGHT 26

STATIC
VOID
PaintGreenBar (
  VOID
  )
{
  volatile UINT32  *Row;
  UINTN            X;
  UINTN            Y;

  for (Y = PAYLOAD_BAR_TOP; Y < PAYLOAD_BAR_TOP + PAYLOAD_BAR_HEIGHT; Y++) {
    Row = (volatile UINT32 *)(UINTN)(PAYLOAD_FB_BASE + (UINT64)Y * PAYLOAD_FB_STRIDE);
    for (X = 0; X < PAYLOAD_FB_WIDTH; X++) {
      Row[X] = 0x0000FF00;
    }
  }
}

EFI_STATUS
EFIAPI
PayloadHelloEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  PaintGreenBar ();

  if ((gST != NULL) && (gST->ConOut != NULL)) {
    gST->ConOut->OutputString (gST->ConOut, L"PAYLOAD OK - the load/start path works\r\n");
  }

  DEBUG ((DEBUG_ERROR, "PAYLOAD OK: PayloadHello ran\n"));
  return EFI_SUCCESS;
}
