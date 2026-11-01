/**
  M2 progress beacon, stage 3 of 3: proves that the DXE core and dispatcher
  are alive on dandelion.

  Copyright (c) Microsoft Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent

  Why this exists: dandelion has no UART, so "how long until the SoC resets"
  is the only channel the firmware has before any storage driver works. The
  ladder is

      ~10 s  -> the FD never executed (LK's watchdog is still armed)
      ~20 s  -> SEC entry ran (PlatformSecLib), died before PlatformPeim
      ~30 s  -> passed memory init (PlatformPeiLib), died before DXE dispatch
      ~2 s   -> this driver ran: DXE core + dispatcher + FV are working

  This driver deliberately leaves the watchdog ARMED at 2 s instead of
  disarming it, so that "reached DXE" is a positive, visible signal rather
  than silence. Flip BEACON_SECONDS to 0 to disarm instead once DXE is known
  to be reached - that is the first thing to change when this stops being a
  diagnostic build.
**/

#include <Uefi.h>

#include <Library/IoLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>

//
// TOPRGU. Must stay equal to the "WatchDog Timer" entry in
// Platforms/Xiaomi/dandelionPkg/Library/MemoryMapLib/MemoryMapLib.c
// (0x10007000). Hardcoded because this driver is meant to be as independent
// as possible from the rest of the platform: if the memory map itself is
// wrong, the beacon still has to work.
//
#define TOPRGU_BASE         0x10007000

//
// Register layout and key values: see
// Silicon/MediaTek/MT6765Pkg/Library/PlatformSecLib/PlatformRegisters.h
// (verified against the MT6765 vendor kernel mtk_wdt.h / mtk_wdt_v2.c).
//
#define WDT_MODE_OFFSET     0x0000
#define WDT_LENGTH_OFFSET   0x0004
#define WDT_MODE_KEY        0x22000000
#define WDT_MODE_ENABLE     0x0001
#define WDT_MODE_EXTEN      0x0004
#define WDT_LENGTH_KEY      0x0008

#define BEACON_SECONDS      2

//
// M2 screen postcode, stage 3: white band under PEI's blue one.  If this band
// appears, the DXE core, the dispatcher and our FV all work -- which is the
// real M2 question, and it answers it without any console stack.
//
#define DANDELION_FB_BASE    0x7EC50000ULL
#define DANDELION_FB_STRIDE  2944
#define DANDELION_FB_WIDTH   720
#define DANDELION_FB_HEIGHT  1600

#include "DandelionFont.h"

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

STATIC
VOID
DandelionFillRect (
  UINTN   Left,
  UINTN   Top,
  UINTN   Width,
  UINTN   Height,
  UINT32  Color
  )
{
  volatile UINT32  *Row;
  UINTN            X;
  UINTN            Y;

  for (Y = Top; Y < Top + Height; Y++) {
    Row = (volatile UINT32 *)(UINTN)(DANDELION_FB_BASE + Y * DANDELION_FB_STRIDE) + Left;
    for (X = 0; X < Width; X++) {
      Row[X] = Color;
    }
  }
}

STATIC
VOID
DandelionDrawString (
  UINTN        X,
  UINTN        Y,
  CONST CHAR8  *Text,
  UINT32       Fg,
  UINT32       Bg
  )
{
  volatile UINT32  *Row;
  UINTN            Column;
  UINTN            Line;
  UINT8            Bits;

  for ( ; *Text != '\0'; Text++, X += 8) {
    if ((*Text < 0x20) || (*Text > 0x7E)) {
      continue;
    }

    for (Line = 0; Line < 16; Line++) {
      Bits = mDandelionFont8x16[*Text - 0x20][Line];
      Row  = (volatile UINT32 *)(UINTN)(DANDELION_FB_BASE + (Y + Line) * DANDELION_FB_STRIDE) + X;
      for (Column = 0; Column < 8; Column++) {
        Row[Column] = (Bits & (0x80 >> Column)) ? Fg : Bg;
      }
    }
  }
}

STATIC
VOID
DandelionDrawHex (
  UINTN   X,
  UINTN   Y,
  UINT64  Value,
  UINTN   Digits,
  UINT32  Fg
  )
{
  CHAR8  Buf[20];
  UINTN  I;

  for (I = 0; I < Digits; I++) {
    Buf[I] = "0123456789ABCDEF"[(Value >> ((Digits - 1 - I) * 4)) & 0xF];
  }

  Buf[Digits] = '\0';
  DandelionDrawString (X, Y, Buf, Fg, 0x00000000);
}

//
// Same glyphs, drawn Scale times larger.  The UEFI console's 8x19 font is
// unreadable at arm's length on this panel, and while it is being debugged we
// cannot trust anything it renders -- so the important values get drawn here,
// straight into the framebuffer, in 40x80 pixel digits.
//
STATIC
VOID
DandelionDrawStringScaled (
  UINTN        X,
  UINTN        Y,
  CONST CHAR8  *Text,
  UINT32       Fg,
  UINT32       Bg,
  UINTN        Scale
  )
{
  volatile UINT32  *Row;
  UINTN            Column;
  UINTN            Line;
  UINTN            Dx;
  UINTN            Dy;
  UINT8            Bits;

  for ( ; *Text != '\0'; Text++, X += 8 * Scale) {
    if ((*Text < 0x20) || (*Text > 0x7E)) {
      continue;
    }

    for (Line = 0; Line < 16; Line++) {
      Bits = mDandelionFont8x16[*Text - 0x20][Line];
      for (Dy = 0; Dy < Scale; Dy++) {
        Row = (volatile UINT32 *)(UINTN)(DANDELION_FB_BASE + (Y + Line * Scale + Dy) * DANDELION_FB_STRIDE) + X;
        for (Column = 0; Column < 8; Column++) {
          for (Dx = 0; Dx < Scale; Dx++) {
            Row[Column * Scale + Dx] = (Bits & (0x80 >> Column)) ? Fg : Bg;
          }
        }
      }
    }
  }
}

EFI_STATUS
EFIAPI
PlatformProgressDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  //
  // No local state: the watchdog is only disarmed now (see the end of this
  // function), never programmed with a timeout, so WDT_LENGTH and the seconds
  // value are gone.
  //

  //
  // GraphicsConsoleDxe defaults to 80x25 with an 8x19 font, which occupies
  // only the top-left 640x475 of a 720x1600 panel and is unreadable at arm's
  // length.  Ask for whichever text mode has the most cells; on this panel
  // that is the resolution-derived one (~90x84).
  //
  // SETMODE DISABLED 2026-10-28: ConSplitter's SetMode path is a hang suspect.
  if (FALSE && (SystemTable->ConOut != NULL) && (SystemTable->ConOut->Mode != NULL)) {
    UINTN  BestMode;
    UINTN  BestArea;
    UINTN  Mode;
    UINTN  Cols;
    UINTN  Rows;

    BestMode = SystemTable->ConOut->Mode->Mode;
    BestArea = 0;
    for (Mode = 0; Mode < SystemTable->ConOut->Mode->MaxMode; Mode++) {
      if (EFI_ERROR (SystemTable->ConOut->QueryMode (SystemTable->ConOut, Mode, &Cols, &Rows))) {
        continue;
      }

      if ((Cols * Rows) > BestArea) {
        BestArea = Cols * Rows;
        BestMode = Mode;
      }
    }

    SystemTable->ConOut->SetMode (SystemTable->ConOut, BestMode);
  }

  //
  // Experiment 2026-10-28 19:0x: wipe the WHOLE panel before drawing anything.
  //
  // The previous boot showed ~800 rows of 720-pitch streaks between this
  // driver's text and the console bars, yet the fd contains no 720-pitch
  // writer (GraphicsConsoleDxe's file GUID is absent from the decompressed
  // FVMAIN) and DandelionConsoleDxe draws with 2944 by construction.  Two
  // explanations remain: (a) the streaks are DRAM leftovers from an earlier
  // boot that survive a warm reset, or (b) something writes them after this
  // driver.  Filling every row here separates the two: leftovers disappear,
  // a live writer comes back.
  //
  // If the streaks vanish, the follow-up question is what happens to BDS
  // output; if they survive this fill, the writer runs later and the next
  // experiment is to drop BdsDxe from APRIORI.inc and look again.
  //
  DandelionFillRect (0, 0, DANDELION_FB_WIDTH, DANDELION_FB_HEIGHT, 0x00000000);

  //
  // Real text now: clear the top of the panel and write what we know.
  // There is no GOP and no ConSplitter on this board yet, so this is the same
  // direct framebuffer trick the bootshim uses -- and it means the DXE stage
  // can report state without any console stack at all.
  //
  DandelionFillRect (0, 0, DANDELION_FB_WIDTH, 200, 0x00000000);
  DandelionDrawString (16, 24,  "DANDELION UEFI", 0x00FFFFFF, 0x00000000);
  DandelionDrawString (16, 56,  "DXE OK", 0x0000FF00, 0x00000000);
  DandelionDrawString (16, 88,  (SystemTable->ConOut != NULL) ? "CONOUT YES" : "CONOUT NO", 0x00FFFF00, 0x00000000);
  DandelionDrawString (16, 120, "FD 40080000", 0x00AAAAAA, 0x00000000);
  DandelionPaintBand (0x00FFFFFF, 1560);

  //
  // Is there a GOP at all?  SimpleFbDxe is already in the build, so if this
  // says NONE the driver never found the panel (missing base address), and if
  // it says FOUND the problem is only ConSplitter ordering.
  //
  {
    EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop = NULL;
    EFI_STATUS                     GopStatus;

    GopStatus = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
    DandelionDrawString (16, 152, EFI_ERROR (GopStatus) ? "GOP NONE" : "GOP FOUND", 0x00FF00FF, 0x00000000);
    if (!EFI_ERROR (GopStatus) && (Gop->Mode != NULL) && (Gop->Mode->Info != NULL)) {
      DandelionDrawString (
        16, 184,
        (Gop->Mode->Info->HorizontalResolution == 720) ? "GOP 720 OK" : "GOP RES BAD",
        0x00FF00FF, 0x00000000
        );

      //
      // Where does the GOP say the pixels live?  If this is not 0x7EC50000 the
      // console is drawing into memory nobody can see, and no amount of
      // console plumbing will fix it.
      //
      DandelionDrawString (16, 248, "FB", 0x00FF8000, 0x00000000);
      DandelionDrawHex (48, 248, (UINT64)Gop->Mode->FrameBufferBase, 8, 0x00FF8000);
      DandelionDrawString (16, 280, "STRIDE", 0x00FF8000, 0x00000000);
      DandelionDrawHex (96, 280, (UINT64)Gop->Mode->Info->PixelsPerScanLine, 4, 0x00FF8000);
    }
  }

  //
  // ConOut exists either way (ConSplitter always creates it), so the only real
  // test of "is there a working console on this board" is to write through it
  // and look at the panel.  If this text shows up, every later UEFI stage --
  // UniXNU.efi, DarwinBoot -- can print normally.
  //
  if (SystemTable->ConOut != NULL) {
    EFI_HANDLE  *GopHandles;
    UINTN       GopCount;
    UINTN       Index;

    GopHandles = NULL;
    GopCount   = 0;

    //
    // Our Depex is TRUE so we run before GraphicsConsoleDxe has connected to
    // the GOP, and ConSplitter therefore has no console child yet.  Connect it
    // ourselves; after this the console really reaches the panel.
    //
    if (!EFI_ERROR (
          gBS->LocateHandleBuffer (
                 ByProtocol,
                 &gEfiGraphicsOutputProtocolGuid,
                 NULL,
                 &GopCount,
                 &GopHandles
                 )
          ))
    {
      for (Index = 0; Index < GopCount; Index++) {
        gBS->ConnectController (GopHandles[Index], NULL, NULL, TRUE);
      }

      gBS->FreePool (GopHandles);
    }

    //
    // A/B test (2026-10-28 19:2x): is ConOut wired to OUR console?
    //
    // Line at row 46 goes through ConOut -> ConSplitter -> whichever console
    // devices are attached.  Line at row 48 is written by DandelionConsoleDxe
    // itself and is known good (the SELFTEST lines at rows 43/44 came out
    // crisp).  Comparing the two answers the only remaining question:
    //
    //   both crisp      -> ConOut reaches us, M3.5 is done
    //   only row 48     -> ConSplitter never adopted our console, and whatever
    //                      draws the smeared line is a different device
    //
    // Row 46/48 (y 736..784) sits between the SELFTEST lines and the smeared
    // line near y 1150, so nothing else overwrites it.
    //
    SystemTable->ConOut->SetAttribute (SystemTable->ConOut, EFI_LIGHTGRAY | EFI_BACKGROUND_BLACK);
    SystemTable->ConOut->SetCursorPosition (SystemTable->ConOut, 0, 46);
    SystemTable->ConOut->OutputString (SystemTable->ConOut, L"CONOUT VIA SPLITTER 0123456789");

    //
    // And report what the console thinks it is.  "MODE X" means
    // ConOut->Mode->Mode is -1, i.e. no output device is attached at all and
    // OutputString is a no-op -- which rules out "it is drawn but hidden".
    //
    {
      INT32  Mode;
      CHAR8  Buf[8];

      Mode = (SystemTable->ConOut->Mode != NULL) ? (INT32)SystemTable->ConOut->Mode->Mode : -1;
      Buf[0] = 'M';
      Buf[1] = 'O';
      Buf[2] = 'D';
      Buf[3] = 'E';
      Buf[4] = ' ';
      Buf[5] = ((Mode >= 0) && (Mode <= 9)) ? (CHAR8)('0' + Mode) : 'X';
      Buf[6] = '\0';
      DandelionDrawString (16, 216, Buf, 0x00FF8000, 0x00000000);
    }

    //
    // 90 x 84 means a real graphics console; 80 x 25 means ConOut is backed by
    // a text-only device (serial) and nothing can ever reach the panel.
    //
    {
      UINTN       Cols;
      UINTN       Rows;
      EFI_STATUS  QueryStatus;

      Cols = 0;
      Rows = 0;
      QueryStatus = SystemTable->ConOut->QueryMode (
                                        SystemTable->ConOut,
                                        SystemTable->ConOut->Mode->Mode,
                                        &Cols,
                                        &Rows
                                        );
      DandelionDrawString (16, 312, "COLS", 0x00FF8000, 0x00000000);
      DandelionDrawHex (64, 312, EFI_ERROR (QueryStatus) ? 0xFFFF : (UINT64)Cols, 4, 0x00FF8000);
      DandelionDrawString (120, 312, "ROWS", 0x00FF8000, 0x00000000);
      DandelionDrawHex (168, 312, EFI_ERROR (QueryStatus) ? 0xFFFF : (UINT64)Rows, 4, 0x00FF8000);
    }
    DandelionDrawString (16, 344, "CUR", 0x00FF8000, 0x00000000);
    DandelionDrawHex (64, 344, (UINT64)SystemTable->ConOut->Mode->CursorRow, 4, 0x00FF8000);
    DandelionDrawHex (112, 344, (UINT64)SystemTable->ConOut->Mode->CursorColumn, 4, 0x00FF8000);

    //
    // Drawn last, on top of anything the console wrote: if the GOP pitch is
    // still 0x02D0 (720) the console output above is sheared and the fix did
    // not reach the compiled module; 0x02E0 (736) means the image is correct.
    //
    {
      EFI_GRAPHICS_OUTPUT_PROTOCOL  *LateGop = NULL;

      if (!EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&LateGop)) &&
          (LateGop->Mode != NULL) &&
          (LateGop->Mode->Info != NULL))
      {
        DandelionDrawString (16, 376, "STRIDE", 0x00FF8000, 0x00000000);
        DandelionDrawHex (96, 376, (UINT64)LateGop->Mode->Info->PixelsPerScanLine, 4, 0x00FF8000);
        DandelionDrawString (16, 408, "FB", 0x00FF8000, 0x00000000);
        DandelionDrawHex (48, 408, (UINT64)LateGop->Mode->FrameBufferBase, 8, 0x00FF8000);

        //
        // 5x scaled, drawn last: whatever the console does, this is readable.
        // 02E0 = 736 (correct), 02D0 = 720 (sheared).
        //
        {
          CHAR8   Big[6];
          UINT64  Value;
          UINTN   Digit;

          Value = (UINT64)LateGop->Mode->Info->PixelsPerScanLine;
          for (Digit = 0; Digit < 4; Digit++) {
            Big[Digit] = "0123456789ABCDEF"[(Value >> ((3 - Digit) * 4)) & 0xF];
          }

          Big[4] = '\0';
          DandelionDrawStringScaled (16, 460, "S", 0x00FFFF00, 0x00000000, 5);
          DandelionDrawStringScaled (60, 460, Big, 0x00FFFF00, 0x00000000, 5);
          DandelionDrawStringScaled (16, 560, "UEFI", 0x00FFFFFF, 0x00000000, 5);
        }
      }
    }
  }

  //
  // Disarm, do not re-arm (2026-10-29).
  //
  // This used to leave the watchdog armed at BEACON_SECONDS so that a hang would
  // reset the phone and could be timed.  Now that the boot gets far enough to
  // print a screenful of DEBUG text, the watchdog is actively destructive: it
  // resets the machine a few seconds after whatever went wrong, so the last page
  // -- the only place the failure is written down -- is gone before it can be
  // photographed.  With the watchdog off, a hang stays on screen indefinitely.
  //
  // WDT_MODE takes 0x22000000 as a key field; writing the key alone clears
  // ENABLE and therefore stops the counter.
  //
  MmioWrite32 (TOPRGU_BASE + WDT_MODE_OFFSET, WDT_MODE_KEY);

  return EFI_SUCCESS;
}
