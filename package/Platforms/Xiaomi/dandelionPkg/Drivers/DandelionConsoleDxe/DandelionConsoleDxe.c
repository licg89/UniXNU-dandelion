/** @file
  A framebuffer-backed EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL for dandelion.

  Why this exists: GraphicsConsoleDxe draws sheared text on this panel even
  though the GOP reports a 736-pixel pitch (0x02E0) and our own framebuffer
  writes at a 2944-byte stride are crisp -- both were measured in the same
  boot.  Rather than debug that path, this driver owns the console outright:
  8x16 glyphs written straight into the panel at 0x7EC50000 with the real
  stride, no GOP, no Blt, no FrameBufferBltLib.

  Because it installs gEfiConsoleOutDeviceGuid, ConPlatformDxe and ConSplitter
  adopt it automatically, so BDS, UiApp and the BootManagerMenu all render
  through it -- that is the point: readable UEFI output without a photograph.

  Copyright (c) Microsoft Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>

#include <Protocol/DevicePath.h>
#include <Protocol/SimpleTextOut.h>
#include <Protocol/DandelionLog.h>

#include "DandelionFont.h"

//
// Measured on hardware: the panel scans out 736 pixels per line (2944 bytes)
// while 720 are visible.  Writing with 2944 is what makes text crisp; the
// 720-wide assumption is what shears every row by 16 pixels.
//
#define DANDELION_FB_BASE    0x7EC50000ULL
#define DANDELION_FB_STRIDE  2944
#define DANDELION_FB_WIDTH   720
#define DANDELION_FB_HEIGHT  1600

//
// 80 columns, not the 90 the 720-pixel width would allow at 8 px per glyph.
//
// Everything that runs above this console -- UniXNU's boot menu and, later, XNU's
// own console -- lays text out for 80 columns and pads each line to 80 characters,
// relying on the terminal to wrap.  At 90 columns it never wraps there, so a padded
// line runs into the next one and the beginning of it scrolls off: on hardware that
// looked like the UniXNU title reading "...U dandelion" and "...nfig:".
//
// DrawGlyph() advances 8 px per glyph regardless (Column * 8), so this change only
// affects where lines wrap, what QueryMode() reports and where the cursor is
// clamped.  The cost is 80 unused pixels on the right; the benefit is that a guest
// which assumes 80 columns is readable, which matters more than our own log lines
// being 10 characters wider.
//
// PcdConOutColumn in dandelion.dsc must stay equal to this, or ConSplitterDxe
// asserts (CORRECTIONS C-series, "PcdConOutColumn/Row must equal QueryMode").
//
#define DANDELION_COLS       80
#define DANDELION_ROWS       (DANDELION_FB_HEIGHT / 16)  // 100

//
// Text area = rows 0..67 (framebuffer y 0..1087).  Everything below is the
// instrument strip and must survive ClearScreen, SetMode and ScrollUp, because
// a photo of that strip is how this bring-up is measured:
//
//   y 1000..1030  magenta  console entry reached
//   y 1040..1070  yellow   console entry returned
//   y 1080..1088  green    (thin) entry point returned to the dispatcher
//
// and below that, MsdcImplLib/StorageBeaconDxe own y 1400..1540 for the stage
// cells, PlatformProgressDxe owns the white band at y 1560.
//
#define DANDELION_CLEAR_ROWS  68

//
// The header line reports the geometry QueryMode() returns, computed at run time.
// It used to be a hand-written "CONSOLE OK 90x100" -- 100 is DANDELION_ROWS (the
// whole framebuffer over the cell height), not the 68 rows the text area has.
// Nothing broke, because QueryMode() and PcdConOutColumn/PcdConOutRow both say
// 90x68, but a log line that contradicts the firmware costs an hour the day
// someone trusts it.  Stringizing the macros does not work either: DANDELION_COLS
// is an expression, so it expands to the text "(720 / 8)".  Hence a real format.
//
STATIC
UINTN
AppendUint (
  IN OUT CHAR16  *Out,
  IN     UINTN   At,
  IN     UINTN   Value
  )
{
  CHAR16  Digits[12];
  UINTN   Count;

  Count = 0;
  do {
    Digits[Count++] = (CHAR16)(L'0' + (Value % 10));
    Value /= 10;
  } while (Value != 0);

  while (Count > 0) {
    Out[At++] = Digits[--Count];
  }

  return At;
}

STATIC EFI_SIMPLE_TEXT_OUTPUT_MODE  mMode = {
  1,                // MaxMode
  0,                // Mode
  EFI_LIGHTGRAY,    // Attribute
  0,                // CursorColumn
  0,                // CursorRow
  TRUE              // CursorVisible
};

STATIC UINT32  mFg = 0x00FFFFFF;
STATIC UINT32  mBg = 0x00000000;

//
// Set when the next character written to the current row must first erase that
// row.  Only the wrap-around path needs it (see WrapToTop): after wrapping, row
// 0 still holds the line that was printed a page ago, and a shorter new line
// would leave the tail of the old one behind.
//
STATIC BOOLEAN  mRowNeedsClear = FALSE;

//
// Set once the console has been told to stop rendering (see the marker check in
// DandelionOutputString).  Everything printed afterwards is dropped, which is the
// point: BDS prints a screenful of its own after bring-up and the text area only
// holds 68 lines.
//
STATIC BOOLEAN  mFrozen = FALSE;

/*
  Text log ring.

  Every character that reaches this console is also appended here, because the
  panel is the only channel this board has and a photo of 68 rows is not a log.
  DEBUG() output arrives through ConOut as well (DebugLib is UefiDebugLibConOut,
  see dandelion.dsc), so this captures the whole firmware's diagnostics, not just
  what our own drivers print.

  64 KiB is roughly 700 lines of 90 columns: several times what a photo can show,
  and small enough to stay comfortably inside DXE's pool.
*/
#define DANDELION_LOG_CAPACITY  (64 * 1024)

STATIC CHAR8   *mLogBuffer   = NULL;
STATIC UINTN   mLogWrite     = 0;    // next slot to write
STATIC UINTN   mLogFill      = 0;    // valid bytes, saturates at capacity
STATIC UINT64  mLogTotal     = 0;    // monotonic count of bytes ever appended

STATIC DANDELION_LOG_PROTOCOL  mLogProtocol = {
  DANDELION_LOG_SIGNATURE,
  DANDELION_LOG_VERSION,
  DANDELION_LOG_CAPACITY,
  NULL
};

STATIC
VOID
LogChar (
  IN CHAR8  C
  )
{
  if (mLogBuffer == NULL) {
    return;
  }

  mLogBuffer[mLogWrite] = C;
  mLogWrite             = (mLogWrite + 1) % DANDELION_LOG_CAPACITY;
  if (mLogFill < DANDELION_LOG_CAPACITY) {
    mLogFill++;
  }

  mLogTotal++;
}

STATIC
EFI_STATUS
EFIAPI
DandelionLogDump (
  IN     DANDELION_LOG_PROTOCOL  *This,
  OUT    CHAR8                   *Buffer OPTIONAL,
  IN OUT UINTN                   *Size
  )
{
  UINTN  Oldest;
  UINTN  Index;

  if (Size == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Buffer == NULL) || (*Size < mLogFill)) {
    *Size = mLogFill;
    return EFI_BUFFER_TOO_SMALL;
  }

  //
  // Oldest byte sits mLogFill entries behind the write pointer.  When the ring
  // has not wrapped yet that is simply offset 0.
  //
  Oldest = (mLogWrite + DANDELION_LOG_CAPACITY - mLogFill) % DANDELION_LOG_CAPACITY;

  for (Index = 0; Index < mLogFill; Index++) {
    Buffer[Index] = mLogBuffer[(Oldest + Index) % DANDELION_LOG_CAPACITY];
  }

  *Size = mLogFill;
  return EFI_SUCCESS;
}

typedef struct {
  VENDOR_DEVICE_PATH        Vendor;
  EFI_DEVICE_PATH_PROTOCOL  End;
} DANDELION_CONSOLE_DEVICE_PATH;

STATIC DANDELION_CONSOLE_DEVICE_PATH  mDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      { (UINT8)sizeof (VENDOR_DEVICE_PATH), 0 }
    },
    EFI_CALLER_ID_GUID
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    { (UINT8)sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 }
  }
};

STATIC
UINT32
AttributeToColor (
  IN UINTN  Index
  )
{
  STATIC CONST UINT32  Palette[16] = {
    0x00000000, 0x000000AA, 0x0000AA00, 0x0000AAAA,
    0x00AA0000, 0x00AA00AA, 0x00AA5500, 0x00AAAAAA,
    0x00555555, 0x005555FF, 0x0055FF55, 0x0055FFFF,
    0x00FF5555, 0x00FF55FF, 0x00FFFF55, 0x00FFFFFF
  };

  return Palette[Index & 0xF];
}

/**
  Clear one whole character cell -- all 16 scanlines of it.

  2026-10-29: this used to write a single scanline per call (Row * 16 *
  stride, then a loop over X only), so ClearScreen() cleared scanlines
  0, 16, 32, ... and left the other 15/16 of the panel untouched.  That is
  the "band of corrupted pixels from row ~34 down" that made every photo of
  this panel hard to read: it was never corruption, it was old content that
  no clear had ever erased.  Fixed here, so ClearScreen and ScrollUp really
  do erase, and anything visible is something a driver drew on purpose.
**/
STATIC
VOID
FillRow (
  IN UINTN   Row,
  IN UINT32  Color
  )
{
  volatile UINT32  *Dst;
  UINTN            Line;
  UINTN            X;

  for (Line = 0; Line < 16; Line++) {
    Dst = (volatile UINT32 *)(UINTN)(
            DANDELION_FB_BASE + (UINT64)(Row * 16 + Line) * DANDELION_FB_STRIDE
            );

    // Fill the hidden 16-pixel padding of the scanout line too, so nothing old
    // can bleed into the visible area when the panel re-reads the line.
    for (X = 0; X < DANDELION_FB_STRIDE / 4; X++) {
      Dst[X] = Color;
    }
  }
}

/**
  Wrap the cursor back to the top of the TEXT area instead of scrolling.

  Why not scroll (2026-10-29, second attempt): ScrollUp used CopyMem with the
  framebuffer as *both* source and destination.  Reading this panel's
  framebuffer back is not reliable -- the result was every scrolled line
  rendered as a double image on top of its neighbour, which is exactly the
  unreadable band across the top of every photo of this panel.  There is also no
  way to tell from a photo which of the two images is current.

  A RAM shadow would fix it (3 MB, one extra copy per line), but a page of text
  here is 68 lines and the bring-up only ever looks at the tail of the log, so
  the cheap correct answer is to wrap: row 0 overwrites the oldest line, and the
  panel ends up holding the last DANDELION_CLEAR_ROWS lines, each rendered once.
  The row is erased on first write (mRowNeedsClear) so old text cannot bleed
  through.
**/
STATIC
VOID
WrapToTop (VOID)
{
  mMode.CursorRow   = 0;
  mMode.CursorColumn = 0;
  mRowNeedsClear    = TRUE;
}

STATIC
VOID
DrawGlyph (
  IN UINTN   Column,
  IN UINTN   Row,
  IN CHAR16  Ch
  )
{
  volatile UINT32  *Line;
  UINTN            LineIndex;
  UINTN            Bit;
  UINT8            Bits;

  if ((Ch < 0x20) || (Ch > 0x7E)) {
    Ch = '?';
  }

  for (LineIndex = 0; LineIndex < 16; LineIndex++) {
    Bits = mDandelionFont8x16[Ch - 0x20][LineIndex];
    Line = (volatile UINT32 *)(UINTN)(
             DANDELION_FB_BASE + (UINT64)(Row * 16 + LineIndex) * DANDELION_FB_STRIDE
             ) + Column * 8;

    for (Bit = 0; Bit < 8; Bit++) {
      Line[Bit] = (Bits & (0x80 >> Bit)) ? mFg : mBg;
    }
  }
}

STATIC
VOID
ApplyAttribute (
  IN UINTN  Attribute
  )
{
  mFg = AttributeToColor (Attribute & 0xF);
  mBg = AttributeToColor ((Attribute >> 4) & 0xF);
}

STATIC
EFI_STATUS
EFIAPI
DandelionReset (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN BOOLEAN                          ExtendedVerification
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
DandelionOutputString (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN CHAR16                           *String
  )
{
  if (String == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Freeze request.
  //
  // After eMMC bring-up has printed its evidence, BDS still prints a screenful of
  // its own -- and the console keeps only the last 68 lines, so BDS output is
  // exactly what pushed the `ddn ...` lines out of earlier photographs.  A line
  // that begins with the marker below stops the console rendering anything else,
  // so what a photo shows from then on is what the bring-up decided to show.
  //
  // The marker itself is swallowed, and so is its newline; the last visible line
  // is whatever came before it.
  //
  if (mFrozen) {
    return EFI_SUCCESS;
  }

  if ((mMode.CursorColumn == 0) && (String[0] == L'@') && (String[1] == L'@') && (String[2] == L'F')) {
    mFrozen = TRUE;
    return EFI_SUCCESS;
  }

  for ( ; *String != L'\0'; String++) {
    //
    // Keep a copy of everything the firmware prints.  Non-ASCII becomes '?'
    // rather than being dropped, so column counts in the log still match what
    // was on screen.
    //
    LogChar ((*String < 0x80) ? (CHAR8)*String : '?');

    switch (*String) {
      case L'\r':
        mMode.CursorColumn = 0;
        break;

      case L'\n':
        mMode.CursorColumn = 0;
        if (mMode.CursorRow + 1 >= DANDELION_CLEAR_ROWS) {
          WrapToTop ();
        } else {
          mMode.CursorRow++;
        }

        break;

      case L'\b':
        if (mMode.CursorColumn > 0) {
          mMode.CursorColumn--;
        }

        break;

      default:
        if (mRowNeedsClear) {
          FillRow (mMode.CursorRow, mBg);
          mRowNeedsClear = FALSE;
        }

        DrawGlyph (mMode.CursorColumn, mMode.CursorRow, *String);
        mMode.CursorColumn++;
        if (mMode.CursorColumn >= DANDELION_COLS) {
          mMode.CursorColumn = 0;
          if (mMode.CursorRow + 1 >= DANDELION_CLEAR_ROWS) {
            WrapToTop ();
          } else {
            mMode.CursorRow++;
          }
        }

        break;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
DandelionTestString (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN CHAR16                           *String
  )
{
  return (String == NULL) ? EFI_INVALID_PARAMETER : EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
DandelionQueryMode (
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN  UINTN                            ModeNumber,
  OUT UINTN                            *Columns,
  OUT UINTN                            *Rows
  )
{
  if ((Columns == NULL) || (Rows == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (ModeNumber >= mMode.MaxMode) {
    return EFI_UNSUPPORTED;
  }

  *Columns = DANDELION_COLS;
  *Rows    = DANDELION_CLEAR_ROWS;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
DandelionSetMode (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN UINTN                            ModeNumber
  )
{
  if (ModeNumber >= mMode.MaxMode) {
    //
    // ConSplitterDxe does this, in ConsplitterSetConsoleOutMode():
    //
    //     Status = TextOut->SetMode (TextOut, PreferMode);   // 0xFF if no match
    //     if (EFI_ERROR (Status)) {
    //       Status = TextOut->SetMode (TextOut, BaseMode);   // 0xFF again
    //       ASSERT (!EFI_ERROR (Status));                    // <- dead loop
    //     }
    //
    // PreferMode is 0xFF whenever the platform asks for a mode we do not
    // advertise (PcdConOutColumn/Row default to 80x25; this panel is 90x100),
    // and BaseMode is 0xFF because no 80x25 mode exists here either.  So the
    // spec-correct EFI_UNSUPPORTED below turned into
    //
    //     ASSERT [ConSplitterDxe] .../ConSplitter.c(2909): !EFI_ERROR(Status)
    //
    // on the panel, followed by a CpuDeadLoop and a watchdog reset -- measured
    // on hardware 2026-10-29 01:0x.  Refusing an unknown mode is correct and
    // useless; clamping to the only mode we have keeps the machine alive.
    //
    // The PCDs are also set to 90x100 in dandelion.dsc so this path should not
    // be reached at all; this is the belt to that pair of braces.
    //
    ModeNumber = 0;
  }

  mMode.Mode         = (INT32)ModeNumber;
  mMode.CursorColumn = 0;
  mMode.CursorRow    = 0;
  ApplyAttribute (mMode.Attribute);

  //
  // Clear the TEXT area only: the instrument strip below it (status band, proof
  // bars, stage cells) is the measurement, and a mode change from ConSplitter
  // must not erase it.
  //
  for (UINTN Row = 0; Row < DANDELION_CLEAR_ROWS; Row++) {
    FillRow (Row, mBg);
  }

  mRowNeedsClear = FALSE;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
DandelionSetAttribute (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN UINTN                            Attribute
  )
{
  mMode.Attribute = (INT32)Attribute;
  ApplyAttribute (Attribute);
  return EFI_SUCCESS;
}

/**
  Clear the text area, but NOT the instrument strip at the bottom.

  Why the exception: MsdcDxe dispatches before this driver (APRIORI order), and
  its stage cells live at framebuffer y 1400..1540 -- below the text area.  A
  full-panel clear here would erase the very measurements this bring-up is
  waiting for.
**/
STATIC
EFI_STATUS
EFIAPI
DandelionClearScreen (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This
  )
{
  ApplyAttribute (mMode.Attribute);

  for (UINTN Row = 0; Row < DANDELION_CLEAR_ROWS; Row++) {
    FillRow (Row, mBg);
  }

  mRowNeedsClear = FALSE;

  mMode.CursorColumn = 0;
  mMode.CursorRow    = 0;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
DandelionSetCursorPosition (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN UINTN                            Column,
  IN UINTN                            Row
  )
{
  if ((Column >= DANDELION_COLS) || (Row >= DANDELION_ROWS)) {
    return EFI_UNSUPPORTED;
  }

  //
  // Rows 0..67 are text; anything from 68 down is the instrument strip, and a
  // caller that positions the cursor there would print over the measurements.
  // Clamp instead of failing: BiSerial/ConSplitter do position the cursor, and
  // an error here is another ASSERT (see DandelionSetMode).
  //
  if (Row >= DANDELION_CLEAR_ROWS) {
    Row = DANDELION_CLEAR_ROWS - 1;
  }

  mMode.CursorColumn = (INT32)Column;
  mMode.CursorRow    = (INT32)Row;
  mRowNeedsClear     = TRUE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
DandelionEnableCursor (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN BOOLEAN                          Visible
  )
{
  mMode.CursorVisible = Visible;
  return EFI_SUCCESS;
}

STATIC EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  mTextOut = {
  DandelionReset,
  DandelionOutputString,
  DandelionTestString,
  DandelionQueryMode,
  DandelionSetMode,
  DandelionSetAttribute,
  DandelionClearScreen,
  DandelionSetCursorPosition,
  DandelionEnableCursor,
  &mMode
};

EFI_STATUS
EFIAPI
DandelionConsoleDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  Handle = NULL;

  //
  // Allocate the text log ring before anything can print, so the very first
  // line this console renders is also the first line in the log.  A failed
  // allocation is not fatal: LogChar() checks for NULL and the console keeps
  // working, just without the log.
  //
  Status = gBS->AllocatePool (EfiBootServicesData, DANDELION_LOG_CAPACITY, (VOID **)&mLogBuffer);
  if (EFI_ERROR (Status)) {
    mLogBuffer = NULL;
  } else {
    ZeroMem (mLogBuffer, DANDELION_LOG_CAPACITY);
  }

  //
  // One-time full wipe of everything the instrument strip does not own.
  //
  // DandelionClearScreen() only covers the text area (y 0..1087) now, because
  // scrolling and mode changes must not touch the measurement strip.  But the
  // panel still holds whatever PEI/LK left in DRAM on the first boot into this
  // image, and that is what the "band of corrupted pixels" has been all along:
  // stale pixels nobody ever wrote.  Measured 2026-10-29: shrinking the clear
  // area brought that stale content straight back between y 1090 and y 1390.
  //
  // So wipe y 0..1391 once, here, and leave 1392..1599 alone: that is where
  // MsdcImplLib's stage cells (y 1400, 1448, 1496) and PlatformProgressDxe's
  // white band (y 1560) live, and MsdcDxe has already painted them by now.
  //
  {
    volatile UINT32  *Line;
    UINTN            Y;
    UINTN            X;

    for (Y = 0; Y < 1392; Y++) {
      Line = (volatile UINT32 *)(UINTN)(DANDELION_FB_BASE + (UINT64)Y * DANDELION_FB_STRIDE);
      for (X = 0; X < DANDELION_FB_STRIDE / 4; X++) {
        Line[X] = 0x00000000;
      }
    }
  }

  //
  // Start from a known state: light gray on black, cleared, cursor at home.
  // This also erases whatever the earlier stages left on the panel, which
  // makes the first BDS message unambiguous.
  //
  ApplyAttribute (mMode.Attribute);
  DandelionClearScreen (&mTextOut);

  //
  // FillRow() was broken until 2026-10-29 (one scanline per cell), so this
  // clear never actually erased PlatformProgressDxe's header and "COUT/VIA"
  // was read through a fog of leftovers.  Now that the clear works, the header
  // has to be redrawn here or it is gone.
  //
  mMode.CursorColumn = 0;
  mMode.CursorRow    = 0;
  DandelionOutputString (&mTextOut, L"DANDELION UEFI\r\n");
  {
    STATIC CONST CHAR16  Head[] = L"CONSOLE OK ";
    CHAR16               Line[32];
    UINTN                At;
    UINTN                Index;

    At = 0;
    for (Index = 0; Head[Index] != L'\0'; Index++) {
      Line[At++] = Head[Index];
    }

    At           = AppendUint (Line, At, DANDELION_COLS);
    Line[At++]   = L'x';
    At           = AppendUint (Line, At, DANDELION_CLEAR_ROWS);
    Line[At++]   = L'\r';
    Line[At++]   = L'\n';
    Line[At]     = L'\0';
    DandelionOutputString (&mTextOut, Line);
  }

  //
  // Build stamp.
  //
  // Between two flashes there are usually three images in play -- the one in
  // git, the one in D:\aios\artifacts, and the one on the phone -- and until now
  // the only way to tell which was running was to compare behaviour against
  // memory.  __DATE__/__TIME__ are the compilation moment of this file, which is
  // enough to identify a build in a photo, and they cost two lines of text.
  //
  {
    CHAR16  Stamp[48];
    UINTN   Source;
    UINTN   Target;

    Source = 0;
    Target = 0;
    for (Source = 0; (Source < sizeof (__DATE__)) && (__DATE__[Source] != '\0'); Source++) {
      Stamp[Target++] = (CHAR16)(UINT8)__DATE__[Source];
    }

    Stamp[Target++] = L' ';
    for (Source = 0; (Source < sizeof (__TIME__)) && (__TIME__[Source] != '\0'); Source++) {
      Stamp[Target++] = (CHAR16)(UINT8)__TIME__[Source];
    }

    Stamp[Target++] = L'\r';
    Stamp[Target++] = L'\n';
    Stamp[Target]   = L'\0';
    DandelionOutputString (&mTextOut, Stamp);
  }

  //
  // Status band, stage marker: magenta = this console ran.
  //
  // The band is y 40..47 (was 26..33, which overlapped this driver's own header
  // rows at y 16..31), full width, and every later stage repaints it (MSDC
  // yellow/orange/cyan, storage purple/green).  Reading a solid colour strip off
  // a photograph is reliable; reading 8x16 text through this panel is not.
  //
  {
    volatile UINT32  *Band;
    UINTN            Y;
    UINTN            X;

    for (Y = 40; Y < 48; Y++) {
      Band = (volatile UINT32 *)(UINTN)(DANDELION_FB_BASE + (UINT64)Y * DANDELION_FB_STRIDE);
      for (X = 0; X < DANDELION_FB_STRIDE / 4; X++) {
        Band[X] = 0x00FF00FF;
      }
    }
  }

  //
  // Self test, 2026-10-28 19:1x: two lines through our own OutputString, at a
  // row nothing else touches (row 43/44 = y 688..720, between the 5x "UEFI"
  // at 560..640 and the progress driver's band at 1560).
  //
  // Purpose: separate "our console cannot render" from "BDS never reaches our
  // console".  These two lines do not depend on ConSplitter, on BDS, or on any
  // protocol: they are the same code path BDS would use, driven directly.
  //
  //   crisp  -> our renderer is fine, so anything smeared later comes from a
  //             different writer, or from BDS not being connected to us
  //   smeared-> the bug is in this driver
  //
  mMode.CursorColumn = 0;
  mMode.CursorRow    = 43;
  DandelionOutputString (&mTextOut, L"SELFTEST 0123456789");
  mMode.CursorColumn = 0;
  mMode.CursorRow    = 44;
  DandelionOutputString (&mTextOut, L"ABCDEFGHIJKLMNOPQRSTUVWXYZ");

  //
  // Control line for the A/B test in PlatformProgressDxe: same digits, same
  // row spacing, written by the same OutputString -- but called directly here
  // instead of through ConOut.  Row 48 is deliberately one row below where
  // PlatformProgressDxe writes its ConOut probe (row 46).
  //
  mMode.CursorColumn = 0;
  mMode.CursorRow    = 48;
  DandelionOutputString (&mTextOut, L"DIRECT CALL 0123456789");

  //
  // Unmissable proof that this driver actually executed: two solid bars at the
  // bottom of the panel.  If they are absent, the DXE dispatcher never ran us
  // and the noise on screen comes from somewhere else entirely.
  //
  {
    volatile UINT32  *Line;
    UINTN            Y;
    UINTN            X;

    for (Y = 1000; Y < 1030; Y++) {
      Line = (volatile UINT32 *)(UINTN)(DANDELION_FB_BASE + (UINT64)Y * DANDELION_FB_STRIDE);
      for (X = 0; X < DANDELION_FB_WIDTH; X++) {
        Line[X] = 0x00FF00FF;
      }
    }

    for (Y = 1040; Y < 1070; Y++) {
      Line = (volatile UINT32 *)(UINTN)(DANDELION_FB_BASE + (UINT64)Y * DANDELION_FB_STRIDE);
      for (X = 0; X < DANDELION_FB_WIDTH; X++) {
        Line[X] = 0x00FFFF00;
      }
    }
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiDevicePathProtocolGuid,
                  &mDevicePath,
                  &gEfiSimpleTextOutProtocolGuid,
                  &mTextOut,
                  &gEfiConsoleOutDeviceGuid,
                  NULL,
                  NULL
                  );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Publish the text log.  Separate from the console protocols on purpose: a
  // consumer that only wants the log (the payload loader, a future eMMC log
  // writer) should not have to open SimpleTextOut, and this way the log is
  // already available to anything dispatched after this driver.
  //
  if (mLogBuffer != NULL) {
    mLogProtocol.Dump = DandelionLogDump;
    gBS->InstallProtocolInterface (
           &Handle,
           &gDandelionLogProtocolGuid,
           EFI_NATIVE_INTERFACE,
           &mLogProtocol
           );
  }

  //
  // Before taking ConOut over, record what it looked like: this is the only
  // remaining question about the console stack, and one photo answers it.
  //
  {
    static CONST CHAR8 Hex[] = "0123456789ABCDEF";
    CHAR8              Report[16];
    EFI_STATUS         QStatus;
    UINTN              Cols;
    UINTN              Rows;
    INT32              Mode;
    UINTN              Digit;

    Cols   = 0;
    Rows   = 0;
    Mode   = -1;
    QStatus = EFI_UNSUPPORTED;

    if ((gST->ConOut != NULL) && (gST->ConOut->Mode != NULL)) {
      Mode    = gST->ConOut->Mode->Mode;
      QStatus = gST->ConOut->QueryMode (
                               gST->ConOut,
                               (Mode >= 0) ? (UINTN)Mode : 0,
                               &Cols,
                               &Rows
                               );
    }

    Report[0] = 'C';
    Report[1] = 'O';
    Report[2] = 'U';
    Report[3] = 'T';
    Report[4] = ' ';
    Report[5] = EFI_ERROR (QStatus) ? 'X' : (CHAR8)('0' + ((Mode >= 0) && (Mode <= 9) ? Mode : 0));
    Report[6] = ' ';
    Report[7] = EFI_ERROR (QStatus) ? '-' : Hex[(Cols >> 4) & 0xF];
    Report[8] = EFI_ERROR (QStatus) ? '-' : Hex[Cols & 0xF];
    Report[9] = 'x';
    Report[10] = EFI_ERROR (QStatus) ? '-' : Hex[(Rows >> 4) & 0xF];
    Report[11] = EFI_ERROR (QStatus) ? '-' : Hex[Rows & 0xF];
    Report[12] = '\0';

    mMode.CursorColumn = 0;
    mMode.CursorRow    = 52;
    {
      CHAR16  Wide[16];

      for (Digit = 0; (Digit < 15) && (Report[Digit] != '\0'); Digit++) {
        Wide[Digit] = (CHAR16)Report[Digit];
      }

      Wide[Digit] = L'\0';
      DandelionOutputString (&mTextOut, Wide);
    }

    if (gST->ConOut != NULL) {
      gST->ConOut->SetCursorPosition (gST->ConOut, 0, 54);
      gST->ConOut->OutputString (gST->ConOut, L"VIA CONOUT 0123456789");
    }
  }

  //
  // Publish ourselves as THE console.
  //
  // Why this and not ConSplitter: ConPlatformDxe/ConSplitterDxe bind console
  // devices through the driver model, so a device only joins ConOut when
  // something calls ConnectController for it -- and that call, from inside our
  // own entry point, is what wedged the dispatcher in the previous build (the
  // bars and SELFTEST were on the panel, nothing after the connect ever ran,
  // and PlatformProgressDxe never got dispatched).
  //
  // On this board ConSplitter has nothing else to aggregate anyway: the serial
  // console is gone (no UART) and GraphicsConsoleDxe is gone (720 pitch).  So
  // pointing the system table's ConOut straight at our protocol is both
  // sufficient and the shortest path to readable UEFI text -- BdsDxe and every
  // UiApp read gST->ConOut, not ConSplitter, to print.
  //
  // This is a bring-up stage decision.  Once storage and UniXNU.efi work, the
  // tidy version is to let ConPlatform bind us normally and drop these two
  // lines.
  //
  gST->ConOut = &mTextOut;
  gST->StdErr = &mTextOut;

  //
  // Proof that this entry point ran to the END, not just to the Install call.
  //
  // 2026-10-29: the previous build showed the two proof bars (drawn before the
  // Install) and a dark patch where MsdcDxe's cells should have been, so the
  // question became "did this function return at all?".  Thin green bar at
  // y 1384..1394 = it returned.  If it is absent while the magenta/yellow bars
  // are present, this driver dies between the Install and here, and that alone
  // explains why nothing later in the dispatch ever ran.
  //
  // Note the y range: an earlier version of this probe used y 2000..2039, which
  // is *below* the 1600-row panel -- it wrote 117 KB past the end of the
  // framebuffer, i.e. into whatever memory follows the display carveout.  Every
  // framebuffer probe in this project must stay inside 0 <= y < 1600.
  //
  {
    volatile UINT32  *Line;
    UINTN            Y;
    UINTN            X;

    for (Y = 1080; Y < 1089; Y++) {
      Line = (volatile UINT32 *)(UINTN)(DANDELION_FB_BASE + (UINT64)Y * DANDELION_FB_STRIDE);
      for (X = 0; X < DANDELION_FB_WIDTH; X++) {
        Line[X] = 0x0000C000;
      }
    }
  }

  return Status;
}
