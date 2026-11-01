/**
  MT6765 / MT6762G MSDC (eMMC / SD) glue.

  Derived from MT6768Pkg/Library/MsdcImplLib/MsdcImplLib.c. The code is
  unchanged; everything below the header is the upstream logic plus notes.

  ---------------------------------------------------------------------------
  Where the MSDC0 base address lives (it is not in this file)

  [VERIFIED] MSDC0 controller = 0x11230000 on MT6765/MT6762G. Three sources:
    - vendor DT: msdc0:msdc@11230000 { reg = <0 0x11230000 0 0x10000> }
    - LK: platform/mt6765/include/platform/mt_reg_base.h MSDC0_BASE
    - dandelion shim: bootshim/msdc.c (#define MSDC0_BASE 0x11230000)

  This is the SAME value lancelot (MT6768) uses, so "MSDC base" is *not* a
  difference between the two SoCs. And this library does not contain it
  anyway: MsdcDxe locates the "MSDC-0" memory region through the platform
  MemoryMapLib. The memory map is the single source of truth; if it says
  something else, this library will drive the wrong block.

  TODO(MT6765): one MSDC-adjacent memory map entry DOES differ from lancelot.
  Dandelion's msdc0_top (the HS200/HS400 delay-line tuning block) is at
  0x11CD0000 (vendor DT msdc0_top@11cd0000), while lancelot's MemoryMapLib
  maps "MSDC Top-0" at 0x11C90000. A dandelion platform must use 0x11CD0000.
  Nothing in this library or in MT6765Pkg reads it (MsdcPadTuneReg and
  TuningStep below are plain register offsets), so this is a platform task,
  not a package task.

  ---------------------------------------------------------------------------
  Clock side

  [VERIFIED] The clock gates and the top-level mux this file asks for by name
  are bit-identical between MT6768Pkg's ClockImplLib and the MT6765 vendor
  kernel (drivers/clk/mediatek/clk-mt6765.c):

    name (ClockImplLib)   register                      MT6765 source
    INFRA_MSDC0           0x88 set / 0x8c clr / 0x94 sta, bit 2
                          GATE_IFR3(CLK_IFR_MSDC0, ..., 2)
    INFRA_MSDC1           same block, bit 4
                          GATE_IFR3(CLK_IFR_MSDC1, ..., 4)
    INFRA_FAES_FDE        0xa4 / 0xa8 / 0xac, bit 29
                          GATE_IFR4(CLK_IFR_FAES_FDE, ..., 29)
    INFRA_MSDC0_SRC       0xc0 / 0xc4 / 0xc8, bit 9
                          GATE_IFR5(CLK_IFR_MSDC0_SRC, ..., 9)
    INFRA_MSDC1_SRC       same block, bit 10
    INFRA_AP_MSDC0        same block, bit 7
    INFRA_MD_MSDC0        same block, bit 8
    TOP_MSDC50_0_SEL      CLK_CFG_3 = 0x70, set 0x74, clr 0x78, shift 8, width 3
    TOP_MSDC50_0_HCLK_SEL same, shift 0, width 2
    TOP_MSDC30_1_SEL      same, shift 16, width 3

  So the GetId()/GetFrequency()/SetEnable() calls below land on the right
  registers on MT6765, provided the ClockImplLib is the one in this package.

  [VERIFIED] All three parent tables match MT6768Pkg entry for entry, MSDCPLL
  in slot 1: msdc5hclk_parents (4 entries), msdc50_0_parents (8) and
  msdc30_1_parents (8) are identical arrays on MT6765 (clk-mt6765.c lines
  473-500). So unlike the apmixed PLL block, the MSDC clock path needs no
  SOC_TYPE branch anywhere.

  ---------------------------------------------------------------------------
  GPIO side

  [VERIFIED] MT6765's eMMC pin group is GPIO122..133: the MT6765 LK explicitly
  skips them in its generic GPIO init
  (platform/mt6765/mt_gpio_init.c:229, "skip msdc pins init GPIO122~133"),
  and the vendor kernel drives exactly MSDC0_GPIO_MODE0 (pins 120..127) and
  MODE1 (pins 128..135) for this block. The pin numbers used in InitGpio()
  below (122,123,124,125,126,127,128,129,130,132) all fall inside that range,
  so they are at least self-consistent for MT6765.

  TODO(MT6765): the individual signal-to-pin assignment (which of 122..133 is
  CMD/CLK/DAT0..7/DS/RSTB) was NOT confirmed from MT6765 source - the
  inherited MT6768 assignment is plausible but unproven. If eMMC init hangs
  with CMD line idle, this is the first thing to check: read
  Pinctrl+0x3F0 and Pinctrl+0x400 after InitGpio() and compare against the
  vendor kernel's msdc_set_pin_mode() result
  (ComboA/mt6765/msdc_cust.c, which sets MODE0[25:8] and MODE1[23:0] to
  0x222222 or 0x111111 depending on MSDC0_GPIO_MODE_TRAP bit 13,
  i.e. Pinctrl+0x6F0 & (0x3 << 13)).

  ---------------------------------------------------------------------------
  Regulator side (blocking for real hardware)

  TODO(MT6765): PowerControl() calls the PMIC protocol with the names
  "ldo_vemc", "ldo_vmch", "ldo_vmc". Those names resolve inside the shared
  MediaTekPkg/Drivers/MT6358PmicDxe driver, whose register table is MT6358's.
  Dandelion's PMIC is MT6357 and the addresses differ (see
  PmicWrapperImplLib.c). Until an MT6357 driver replaces it, PowerControl()
  on a dandelion build will poke the wrong PMIC registers - which on an eMMC
  path means the flash may be left unpowered or, worse, another rail is
  touched. This is the single biggest blocker between "MT6765Pkg builds" and
  "MT6765Pkg reads a block".

  ---------------------------------------------------------------------------
  Platform configuration values (gPlatformInfo)

  [INHERITED] All of these come from MT6768Pkg unverified:
    NumberOfHosts = 2      - MT6765 also has msdc0+msdc1 (vendor DT), fine.
    UseTop        = TRUE   - needs the msdc0_top base to be mapped (0x11CD0000
                             above) or every pad-tune write goes nowhere.
    MsdcPadTuneReg = 0xf0  - OFFSET_MSDC_PAD_TUNE0 in the vendor msdc_reg.h,
                             verified to exist at 0xf0 for MT6765.
    TuningStep    = {32,64} - board/timing specific, unverified on dandelion.
    AsyncFifo/BusyCheck/StopClkFix/EnhanceRx - MT6768-class quirks; unverified.
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MsdcImplLib.h>

#include <Protocol/MtkGpio.h>
#include <Protocol/MtkClock.h>
#include <Protocol/MtkPmic.h>

//
// 3x5 font for the numeric readout (see MsdcPanelValue below).  Generated by
// tools/gen_small_font.py; the console driver's own 8x16 font is not reachable
// from a library.
//
#include "MsdcSmallFont.h"

MSDC_PLATFORM_INFO gPlatformInfo = {
  .NumberOfHosts = 2,
  .UseTop = TRUE,
  .MsdcPadTuneReg = 0xf0,
  .TuningStep = {32, 64},
  .AsyncFifo = TRUE,
  .BusyCheck = TRUE,
  .StopClkFix = TRUE,
  .EnhanceRx = TRUE
};

STATIC MTK_GPIO_PROTOCOL *mGpio = NULL;
STATIC MTK_CLOCK_PROTOCOL *mClock = NULL;
STATIC MTK_PMIC_PROTOCOL *mPmic = NULL;

/**
  Print one short line to the panel console.

  Why a library prints to the screen: dandelion has no UART, so the only way to
  find out where the eMMC stack stops is to put markers on the panel.  DEBUG()
  goes to a serial port that does not exist, so it is useless here.

  Silent and harmless when ConOut is not up yet (gST may be NULL very early,
  and ConOut is NULL before a console driver installs itself).
**/
/**
  Paint the status band and print one short line.

  Why a colour band: a photo of small text on this panel is unreliable (there
  is a band of corrupted pixels from row ~34 down), but a solid colour strip
  is unmissable even in a bad photo.  Each MSDC stage repaints the same strip,
  so the colour on screen at reset is the LAST stage that was reached:

      magenta   DandelionConsoleDxe ran            (painted by the console)
      yellow    MsdcDxe constructor ran
      orange    GPIO configured
      cyan      clocks configured
      purple    (reserved) block device produced
      red       (reserved) filesystem produced
**/
/**
  Stage cells: a persistent, ConOut-independent record of how far the eMMC
  bring-up got.

  Why this exists (2026-10-29): MsdcDxe is dispatched from the APRIORI block
  BEFORE DandelionConsoleDxe, so on that build gST->ConOut was still NULL when
  every one of these markers ran -- MsdcSay() returned immediately and the
  panel said nothing at all about storage.  A whole flash cycle produced one
  magenta band and no information.  So the markers must not touch ConOut:

    * eight fixed cells at the bottom of the panel (rows 1400..1480), one per
      stage, painted straight into the framebuffer;
    * a cell that is present was reached, a cell that is absent was not, so a
      single photo of the strip is a complete state vector -- no scrolling, no
      "last writer wins" ambiguity;
    * the row-26..33 band is still repainted with thecolour of the latest stage,
      for the case where only the top of the panel is legible.

  The strip sits below DandelionConsoleDxe's two proof bars (1200..1280 and
  1300..1380) and above PlatformProgressDxe's white band (1560), in an area
  nothing else draws into.

    stage 0 yellow   MsdcDxe constructor entered (MSDC library constructor)
    stage 1 orange   GPIO122..133 muxed   (red = no GPIO protocol)
    stage 2 cyan     MSDC clocks enabled  (red = no clock protocol)
    stage 3 white    power/PMIC path done (red = missing protocols)
    stage 4 purple   block device produced   [StorageBeaconDxe]
    stage 5 green    filesystem mounted      [StorageBeaconDxe]

  Stages 8..15 live in a second, shorter row just below (y 1488..1543) and are
  driven from inside MediaTekPkg's MsdcDxe.c through the exported MsdcTrace()
  below (see Resources/MuPatches/0002-*).  They split the gap between "host
  initialised" and "block device appeared", which is where a silent eMMC
  typically hangs: msdc0 memory regions resolved, controller out of reset,
  a command actually on the bus, CMD1 answered, CMD2/CMD3 done.

    stage  8 green     both msdc0 memory regions resolved
    stage  9 yellow    controller reset completed (MsdcReset returned)
    stage 10 magenta   a command reached the bus during identification
    stage 11 cyan      CMD1 answered (OCR bit 31 set -> card powered up)
    stage 12 white     CMD2+CMD3 ok, CardType = EmmcCard (EmmcDxe can bind)
**/
#define MSDC_FB_BASE     0x7EC50000ULL
#define MSDC_FB_STRIDE   2944
#define MSDC_FB_PIXELS   736
// Instrument strip: three rows of eight 80x44 cells on an 88-pixel pitch, at
// y 1400 / 1448 / 1496.  Stages 0..7 in row A, 8..15 in row B, 16..23 in row C.
// Everything stays inside the 1600-row panel -- an earlier probe at y 2000 was
// off the panel entirely and wrote 117 KB past the end of the framebuffer.
#define MSDC_STAGE_SIZE  80
#define MSDC_STAGE_HIGH  44
#define MSDC_STAGE_PITCH 88
#define MSDC_STAGE_LEFT  8
#define MSDC_ROW0_TOP    1400
#define MSDC_ROW1_TOP    1448
#define MSDC_ROW2_TOP    1496

STATIC
VOID
MsdcStage (
  IN UINTN   Stage,
  IN UINT32  Colour
  )
{
  volatile UINT32  *Row;
  UINTN            Left;
  UINTN            Top;
  UINTN            X;
  UINTN            Y;

  if (Stage > 23) {
    return;
  }

  if (Stage < 8) {
    Top = MSDC_ROW0_TOP;
  } else if (Stage < 16) {
    Top = MSDC_ROW1_TOP;
  } else {
    Top = MSDC_ROW2_TOP;
  }

  Left = MSDC_STAGE_LEFT + (Stage & 7) * MSDC_STAGE_PITCH;
  for (Y = Top; Y < Top + MSDC_STAGE_HIGH; Y++) {
    Row = (volatile UINT32 *)(UINTN)(MSDC_FB_BASE + (UINT64)Y * MSDC_FB_STRIDE) + Left;
    for (X = 0; X < MSDC_STAGE_SIZE; X++) {
      Row[X] = Colour;
    }
  }
}

/**
  Public entry point for markers that live outside this library.

  MediaTekPkg's MsdcDxe.c is upstream code, so the card-identification markers
  cannot call the file-static MsdcSay(); Resources/MuPatches/0002 declares this
  one symbol and calls it with a stage number and a colour.  Nothing else about
  the SoC library is exported.
**/
VOID
MsdcTrace (
  IN UINTN   Stage,
  IN UINT32  Colour
  )
{
  MsdcStage (Stage, Colour);
}

/*
  Persistent numeric readout: "INTR=00004000" in 15x25-pixel digits, painted
  straight into the framebuffer at fixed positions below the console's text
  area, so it is never scrolled away.

  Why this exists (2026-10-29): the only debug channel on this board is the
  panel, and DEBUG() text is worthless for evidence that has to survive until a
  photo is taken -- BdsDxe and EmmcDxe print enough afterwards to push the
  interesting lines out of the console's 68-row window, and a photo of a
  scrolling log is a race.  Cells (MsdcTrace) survive, but a cell can only say
  "this stage was reached", not "MSDC_INT was 0x4000".  These values stay on
  screen forever once written.

  Layout: two columns of five slots, y 1232..1362, left x 8 and x 372.  The
  stage cells start at y 1400 and the banners are at y 1100..1220, so this band
  is the only free space, and it must never grow past y 1390.
*/
#define MSDC_PANEL_TOP    1244
#define MSDC_PANEL_LEFT   8
#define MSDC_PANEL_COL2   372
#define MSDC_PANEL_ROW    30
#define MSDC_PANEL_SCALE  5
#define MSDC_PANEL_FG     0x00FFFFFF
#define MSDC_PANEL_BG     0x00000000

//
// Every label must be exactly four characters.  A shorter literal ("CFG", "R1")
// puts a NUL into Text[] when it is copied, and MsdcPanelText() stops at that
// NUL: the label is painted and the value silently is not.  Measured on
// hardware 2026-10-29 (IMG_5723): the CFG and R1 rows came out as bare labels
// with nothing after them, which is how this was found.
//
STATIC CONST CHAR8 *mMsdcPanelLabel[10] = {
  "INTR", "MCFG", "FIFO", "SSTS", "DCRC",
  "LEFT", "R1  ", "SCMD", "SCFG", "BLKN"
};

STATIC
UINT16
MsdcGlyph (
  IN CHAR8  Ch
  )
{
  UINTN  Index;

  if ((Ch >= '0') && (Ch <= '9')) {
    Index = (UINTN)(Ch - '0');
  } else if ((Ch >= 'A') && (Ch <= 'Z')) {
    Index = (UINTN)(Ch - 'A') + 10;
  } else if (Ch == '=') {
    Index = 36;
  } else {
    Index = 37;
  }

  if (Index >= MSDC_SMALL_FONT_COUNT) {
    Index = 37;
  }

  return mMsdcSmallFont[Index];
}

STATIC
VOID
MsdcPanelText (
  IN UINTN        X,
  IN UINTN        Y,
  IN CONST CHAR8  *Text
  )
{
  volatile UINT32  *Line;
  UINT16           Bits;
  UINTN            GlyphRow;
  UINTN            GlyphCol;
  UINTN            Dx;
  UINTN            Dy;
  CHAR8            Ch;

  for ( ; *Text != '\0'; Text++) {
    Ch   = *Text;
    Bits = MsdcGlyph (Ch);
    for (GlyphRow = 0; GlyphRow < 5; GlyphRow++) {
      for (Dy = 0; Dy < MSDC_PANEL_SCALE; Dy++) {
        Line = (volatile UINT32 *)(UINTN)(
                 MSDC_FB_BASE + (UINT64)(Y + GlyphRow * MSDC_PANEL_SCALE + Dy) * MSDC_FB_STRIDE
                 ) + X;
        for (GlyphCol = 0; GlyphCol < 3; GlyphCol++) {
          UINT32  Colour = ((Bits >> (14 - (GlyphRow * 3 + GlyphCol))) & 1) ? MSDC_PANEL_FG : MSDC_PANEL_BG;
          for (Dx = 0; Dx < MSDC_PANEL_SCALE; Dx++) {
            Line[GlyphCol * MSDC_PANEL_SCALE + Dx] = Colour;
          }
        }
      }
    }

    X += 4 * MSDC_PANEL_SCALE;
  }
}

/**
  Write "LABEL=XXXXXXXX" into one of the ten fixed readout slots.

  Slot 0..4 are the left column, 5..9 the right one, top to bottom.  Values are
  printed as eight uppercase hex digits because that is the width of every MSDC
  register that matters here; digits only, so nothing has to be inferred from a
  font.
**/
VOID
MsdcPanelValue (
  IN UINTN   Slot,
  IN UINT32  Value
  )
{
  CHAR8   Text[16];
  UINTN   Index;
  UINTN   X;
  UINTN   Y;
  UINTN   Nibble;

  if (Slot > 9) {
    return;
  }

  X = (Slot < 5) ? MSDC_PANEL_LEFT : MSDC_PANEL_COL2;
  Y = MSDC_PANEL_TOP + (Slot % 5) * MSDC_PANEL_ROW;

  for (Index = 0; Index < 4; Index++) {
    Text[Index] = mMsdcPanelLabel[Slot][Index];
  }

  Text[4] = '=';
  for (Index = 0; Index < 8; Index++) {
    Nibble = (UINTN)((Value >> (28 - Index * 4)) & 0xF);
    Text[5 + Index] = (CHAR8)((Nibble < 10) ? ('0' + Nibble) : ('A' + Nibble - 10));
  }

  Text[13] = '\0';

  MsdcPanelText (X, Y, Text);
}

STATIC
VOID
MsdcSay (
  IN CONST CHAR8  *Tag,
  IN UINTN        Stage,
  IN UINT32       Colour
  )
{  CHAR16        Line[48];
  UINTN         Index;
  UINTN         Y;
  UINTN         X;
  STATIC UINTN  Row = 27;

  //
  // Framebuffer part first and unconditionally: it must work before any
  // console exists.
  //
  MsdcStage (Stage, Colour);

  //
  // Status band: y 40..47 (moved from 26..33, which overlapped the console
  // driver's own header rows at y 16..31), full width including the 16 hidden
  // pixels, so nothing old can bleed through from either side.
  //
  for (Y = 40; Y < 48; Y++) {
    volatile UINT32  *Line32 = (volatile UINT32 *)(UINTN)(MSDC_FB_BASE + (UINT64)Y * MSDC_FB_STRIDE);
    for (X = 0; X < MSDC_FB_PIXELS; X++) {
      Line32[X] = Colour;
    }
  }

  //
  // Text is a bonus: only meaningful once a console exists.  MsdcDxe now runs
  // after DandelionConsoleDxe (see APRIORI.inc), so in practice it prints.
  //
  if ((gST == NULL) || (gST->ConOut == NULL) || (Tag == NULL)) {
    return;
  }

  if (Row >= 34) {
    Row = 27;
  }

  gST->ConOut->SetCursorPosition (gST->ConOut, 0, Row);

  Line[0] = L'M';
  Line[1] = L'S';
  Line[2] = L'D';
  Line[3] = L'C';
  Line[4] = L':';
  Line[5] = L' ';

  for (Index = 0; (Index < 30) && (Tag[Index] != '\0'); Index++) {
    Line[6 + Index] = (CHAR16)Tag[Index];
  }

  Line[6 + Index] = L'\r';
  Line[7 + Index] = L'\n';
  Line[8 + Index] = L'\0';

  gST->ConOut->OutputString (gST->ConOut, Line);

  Row++;
}

VOID
GetSourceClockRate (
  UINT32 Index,
  UINTN *Hz)
{
  EFI_STATUS Status;
  UINT32 ClockId;

  // Get Clock Id
  Status = mClock->GetId(Index == 0 ? "TOP_MSDC50_0" : "TOP_MSDC30_1", &ClockId);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to Get Clock Id! Status = %r\n", Status));
    ASSERT_EFI_ERROR (Status);
  }

  // Get Clock Frequency
  Status = mClock->GetFrequency(ClockId, Hz);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to Get Clock Frequency! Status = %r\n", Status));
    ASSERT_EFI_ERROR (Status);
  }
}

VOID
SourceClockControl (
  UINT32 Index,
  BOOLEAN Enable)
{
  MsdcSay ("clock", 2, 0x0000FFFF);

  EFI_STATUS Status;
  UINT32 ClockId;

  if (Index == 0) {
    // Get Clock Id
    Status = mClock->GetId("INFRA_FAES_FDE", &ClockId);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "Failed to Get Clock Id! Status = %r\n", Status));
      ASSERT_EFI_ERROR (Status);
    }

    // Enable Clock
    Status = mClock->SetEnable(ClockId, Enable);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "Failed to Enable Clock! Status = %r\n", Status));
      ASSERT_EFI_ERROR (Status);
    }
  }

  // Get Clock Id
  Status = mClock->GetId(Index == 0 ? "INFRA_MSDC0_SRC" : "INFRA_MSDC1_SRC", &ClockId);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to Get Clock Id! Status = %r\n", Status));
    ASSERT_EFI_ERROR (Status);
  }

  // Enable Clock
  Status = mClock->SetEnable(ClockId, Enable);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to Enable Clock! Status = %r\n", Status));
    ASSERT_EFI_ERROR (Status);
  }
}

VOID
ClockControl (
  UINT32 Index,
  BOOLEAN Enable)
{
  MsdcSay ("clock", 2, 0x0000FFFF);

  EFI_STATUS Status;
  UINT32 ClockId;

  // Get Clock Id
  Status = mClock->GetId(Index == 0 ? "TOP_MSDC50_0" : "TOP_MSDC30_1", &ClockId);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to Get Clock Id! Status = %r\n", Status));
    ASSERT_EFI_ERROR (Status);
  }

  // Enable Clock
  Status = mClock->SetEnable(ClockId, Enable);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to Enable Clock! Status = %r\n", Status));
    ASSERT_EFI_ERROR (Status);
  }

  // Get Clock Id
  Status = mClock->GetId(Index == 0 ? "INFRA_MSDC0" : "INFRA_MSDC1", &ClockId);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to Get Clock Id! Status = %r\n", Status));
    ASSERT_EFI_ERROR (Status);
  }

  // Enable Clock
  Status = mClock->SetEnable(ClockId, Enable);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to Enable Clock! Status = %r\n", Status));
    ASSERT_EFI_ERROR (Status);
  }
}

VOID
PowerControl (
  UINT32 Index,
  BOOLEAN Enable)
{
  MsdcSay ("power skipped", 3, 0x00FF8000);

  //
  // 2026-10-28: deliberately does NOT touch the PMIC yet.
  //
  // The inherited body called mPmic->RegulatorSetEnable("ldo_vemc") and
  // friends, but those names resolve to MT6358 register addresses inside
  // MediaTekPkg/Drivers/MT6358PmicDxe.  Dandelion carries an MT6357, so those
  // writes would land on the wrong registers -- the failure mode is a wrong
  // rail voltage, not an error code, which is exactly the kind of thing that
  // damages hardware.
  //
  // What makes skipping safe: the eMMC is LK's own boot device.  LK powers it
  // up, reads from it and hands over with the rails still on, so the card is
  // alive when DXE reaches MsdcDxe.  The SD/MMC stack only needs host
  // initialisation (GPIO, clock, controller reset) to talk to it.
  //
  // If the card turns out to be unresponsive, the follow-up is to fork the
  // PMIC driver for MT6357 (regulator table from the vendor kernel's
  // mt6357-regulator.c) rather than to guess addresses here.
  //
  (VOID)Index;
  (VOID)Enable;

  DEBUG ((
    DEBUG_INFO,
    "MT6765: PowerControl(%d, %d) skipped - MT6357 PMIC not supported yet\n",
    Index,
    Enable
    ));
}

VOID
InitGpio (
  UINT32 Index)
{
  MsdcSay ("gpio", 1, 0x00FF8000);

#if SOC_TYPE == 1
  //
  // MT6765 / MT6762G: the pin *range* is verified (GPIO122..133, MT6765 LK
  // mt_gpio_init.c:229), the individual signal assignment is inherited from
  // MT6768 and unproven. See the header for how to check it on silicon.
  //
  DEBUG ((DEBUG_INFO, "MT6765: MSDC0 GPIO pin assignment inherited from MT6768, unverified\n"));
#endif

  if (Index == 0 && FixedPcdGetBool(PcdStorageIsEMMC)) {
    //
    // [VERIFIED] MT6765 eMMC pin muxing, from both the vendor kernel and LK:
    //
    //   vendor mt6765/msdc_cust.c:827-841  -> msdc_set_pin_mode()
    //   LK     msdc_io.c:624-638           (identical)
    //
    //   MODE0 = 0x100053F0[31:8] and MODE1 = 0x10005400[23:0] are driven to
    //   0x222222 -- twelve pins, GPIO122..133, four bits each -- and to
    //   0x111111 only when the board strap MSDC0_GPIO_MODE_TRAP
    //   (0x100056F0 bit 13) is set.
    //
    // What this file used to do: mode 1 on ten pins (122..130, 132), never
    // touching 131/133 and never reading the strap.  With the strap clear that
    // muxes CMD/CLK to the wrong function, so the card cannot answer at all --
    // which is exactly the symptom we have: no block device ever appears.
    //
    {
      UINT32  Trap;
      UINT32  Mode;
      UINT32  Pin;

      Trap = MmioRead32 (0x100056F0);
      Mode = ((Trap & (1U << 13)) != 0) ? 1 : 2;

      for (Pin = 122; Pin <= 133; Pin++) {
        mGpio->SetMode (Pin, Mode);
      }

      MsdcSay (((Mode == 2) ? "gpio mode2 x12" : "gpio mode1 x12"), 1, 0x00FF8000);
    }
  }
  else {
    // Setup SDCard GPIO
    // TODO(MT6765): SD pins (161..171 here) unverified - dandelion has no SD
    // slot wired for our use; the vendor DT does not describe one, so this
    // path is dead code on this board.
    mGpio->SetMode (161, 1); // DAT0
    mGpio->SetMode (162, 1); // DAT1
    mGpio->SetMode (163, 1); // DAT2
    mGpio->SetMode (164, 1); // DAT3
    mGpio->SetMode (170, 1); // CMD
    mGpio->SetMode (171, 1); // CLK
  }
}

EFI_STATUS
EFIAPI
MsdcLibConstructor (VOID)
{
  EFI_STATUS Status;

  MsdcSay ("ctor", 0, 0x00FFFF00);

  // Locate Gpio Protocol
  Status = gBS->LocateProtocol (&gMediaTekGpioProtocolGuid, NULL, (VOID **)&mGpio);
  if (EFI_ERROR (Status)) {
    //
    // 2026-10-28: was ASSERT_EFI_ERROR.  In a DEBUG build an ASSERT is a hang,
    // and a hang here blocks the rest of the DXE dispatch -- BdsDxe and every
    // later driver simply never run, which looks like "the firmware stops"
    // with no way to tell why.  Report it on the panel and carry on; the MSDC
    // init that follows will fail visibly instead of silently.
    //
    MsdcSay ("no gpio protocol", 1, 0x00FF0000);
    mGpio = NULL;
  }

  // Locate Clock Protocol
  Status = gBS->LocateProtocol (&gMediaTekClockProtocolGuid, NULL, (VOID **)&mClock);
  if (EFI_ERROR (Status)) {
    MsdcSay ("no clock protocol", 2, 0x00FF0000);
    mClock = NULL;
  }

  // Locate PMIC Protocol
  Status = gBS->LocateProtocol (&gMediaTekPmicProtocolGuid, NULL, (VOID **)&mPmic);
  if (EFI_ERROR (Status)) {
    //
    // Expected on dandelion: the only PMIC driver in the tree is MT6358, and
    // this board carries an MT6357.  Not fatal any more -- the eMMC rails are
    // already on because LK boots from that card.
    //
    MsdcSay ("no pmic (ok)", 3, 0x00FF8000);
    mPmic = NULL;
  }

  if ((mGpio == NULL) || (mClock == NULL)) {
    MsdcSay ("missing protocols", 3, 0x00FF0000);
    return EFI_DEVICE_ERROR;
  }

  if (mPmic == NULL) {
    MsdcSay ("no pmic, skip volt", 3, 0x00FF8000);
    return EFI_SUCCESS;
  }

  //
  // 2026-10-28: do NOT write voltages through this protocol.
  //
  // The only PMIC driver in the tree is MT6358; dandelion carries an MT6357.
  // Its regulator *names* resolve, so the calls would succeed and write
  // MT6358 VOSEL encodings into MT6357 registers -- a wrong rail voltage, not
  // an error code.  The eMMC rails are already on (LK boots from that card),
  // so there is nothing to do here.
  //
  // The protocol itself must stay located: removing MT6358PmicDxe from the FV
  // once made the whole DXE chain stop before DandelionConsoleDxe, so some
  // other driver depends on it.  Keep the driver, skip the writes.
  //
  MsdcSay ("pmic: no writes", 3, 0x00FFFFFF);
  Status = EFI_SUCCESS;

  return Status;
}
