/**
  M4 storage beacon: connect the eMMC stack, then report how far it got.

  Copyright (c) Microsoft Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent

  Why this driver has to *connect*, not just report (2026-10-29):

  MsdcDxe installs gEfiSdMmcPassThruProtocolGuid and EmmcDxe is a UEFI_DRIVER
  whose DriverBinding sits on top of it, but nothing in this firmware calls
  gBS->ConnectController for that handle.  Normally BDS does that for the whole
  platform (EfiBootManagerConnectAll) -- and on this board BDS either never gets
  to run or is not to be trusted yet, so the pass-thru handle stayed unbound and
  no block device ever appeared.  Measured on hardware: MsdcDxe's own markers
  showed CMD2/CMD3 succeeding (card identified, EmmcDxe-visible subtype), yet
  neither BlockIo nor SimpleFileSystem ever appeared.

  So this driver, dispatched the moment the pass-thru protocol is installed,
  does the connect sweep itself: for every handle, ConnectController with
  Recursive = TRUE.  That starts EmmcDxe (-> BlockIo), then PartitionDxe and
  FatPkg (-> SimpleFileSystem), which is exactly the order BDS would have used.

  Reporting is the same framebuffer cell strip MsdcImplLib uses, because ConOut
  does not exist yet at this point:

      cell 6 yellow  this driver ran, i.e. the pass-thru protocol is installed
      cell 4 purple  at least one BlockIo handle exists
      cell 5 green   a SimpleFileSystem mounted

  ---------------------------------------------------------------------------
  The power probe (2026-10-29, section 5 of docs/RESUME-1029.md)

  Everything above assumes the card answers.  It does not, and the first
  explanation tried was "it has no power" -- LK used to leave the eMMC rails on,
  but MsdcImplLib's PowerControl() is deliberately gutted (the in-tree PMIC
  driver is MT6358's, dandelion carries an MT6357).  This driver measured it
  instead of assuming it:

      P_before   MSDC_PS, read as the very first load this driver performs
      V          MT6357 LDO_VEMC/VMC/VIO18 CON0 + VEMC_ANA_CON0 via the wrapper
      P_after    MSDC_PS again, after V and a short settle

  Hardware, 2026-10-29 (IMG_5738): P = 0x81FF0002 both times, VEMC CON0 = 0x0001.
  With the real MSDC_PS layout (DAT at bits 23..16, CMD at bit 24) that means
  DAT = 0xFF, CMD = 1, card present, rail enabled -- so "no power" is refuted,
  and the next question is whether the card can hear the controller at all.
  That is what ProbeCardAndMux() answers: a hand-sent CMD1 with the pin state
  sampled in a tight loop, then the same ping in the other pin-mux mode.

  Reading table: docs/POWER-BEFORE-AFTER.md.  Deliberately no stage cells: 16..23
  are painted by MsdcDxe/EmmcDxe, and this driver runs after them, so a cell
  here would erase the M4 evidence.
**/

#include <Uefi.h>

#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>

#include <Protocol/BlockIo.h>
#include <Protocol/SimpleFileSystem.h>

#define BEACON_FB_BASE     0x7EC50000ULL
#define BEACON_FB_STRIDE   2944
#define BEACON_FB_PIXELS   736
// Same three-row strip as MsdcImplLib: y 1400 / 1448 / 1496, cells 80x44 on an
// 88-pixel pitch, stages 0..7 in row A, 8..15 in row B, 16..23 in row C.
#define BEACON_STAGE_SIZE  80
#define BEACON_STAGE_HIGH  44
#define BEACON_STAGE_PITCH 88
#define BEACON_STAGE_LEFT  8
#define BEACON_ROW0_TOP    1400
#define BEACON_ROW1_TOP    1448
#define BEACON_ROW2_TOP    1496

//
// MSDC0 register window (LK: platform/mt6765/include/platform/mt_reg_base.h,
// and bootshim/msdc.c uses the same value).  Offsets follow the MTK MSDC block;
// the upstream MsdcDxe header has the same ones -- keep them in sync if either
// side changes.  Read-only here, see ProbeMsdcLeftovers().
//
#define MSDC0_MMIO_BASE    0x11230000ULL
#define MSDC1_MMIO_BASE    0x11240000ULL
#define MSDC_CFG_OFS       0x00
#define MSDC_PS_OFS        0x08
#define MSDC_INT_OFS       0x0C
#define MSDC_INTEN_OFS     0x10
#define SDC_CFG_OFS        0x30
#define SDC_CMD_OFS        0x34
#define SDC_ARG_OFS        0x38
#define SDC_STS_OFS        0x3C
#define SDC_RESP0_OFS      0x40
#define SDC_RESP1_OFS      0x44
#define SDC_RESP2_OFS      0x48
#define SDC_RESP3_OFS      0x4C
#define SDC_BLK_NUM_OFS    0x50
//
// Pad configuration (mtk-sd.c: MSDC_PAD_CTL0 = 0xE0, MSDC_PAD_CTL1 = 0xE4).
// Read-only here: if these came back zero the pads would not be driving at all,
// which would explain a command that never reaches the card.
//
#define MSDC_PAD_CTL0_OFS  0xE0
#define MSDC_PAD_CTL1_OFS  0xE4

#define SDC_STS_BUSY       (1u << 0)
#define SDC_STS_CMDBUSY    (1u << 1)
#define SDC_CMD_STOP_CMD   (1u << 14)
#define SDC_CMD_RSP_SHIFT  7
#define MSDC_INT_CMDRDY    (1u << 8)
#define MSDC_INT_CMDTMO    (1u << 9)

//
// Data phase.  SDC_CMD carries the transfer description, not a separate length
// register: [12:11] data type (1 = single block read), [13] direction (1 =
// write), [27:16] block length, [14] stop (CMD12 only).
//
// The data-type field is the one that cost a round: it lives at bit 11, not at
// bit 9.  docs/research/mtk-msdc-pio.md section 1.3 has the vendor's own
// comment -- `vol_swt << 30 | auto_cmd << 28 | blklen << 16 | go_irq << 15 |
// stop << 14 | rw << 13 | dtype << 11 | rsptyp << 7 | brk << 6 | opcode` --
// and its pre-computed CMD8 word is 0x02000888 = opcode 8, rsptyp 1, dtype 1
// at bit 11, blklen 512.  Writing 1 at bit 9 sets rsptyp to 5 instead and
// leaves dtype at 0, so the controller never enabled its data path and the card
// -- which always sends 512 bytes for CMD8, because it cannot see the host's
// dtype field -- was left holding DAT0 low.  IMG_5752: `C8 I=00000100` (CMDRDY
// and nothing else, no XFER_COMPL, no DATTMO) and `B1 S=00000000`.
//
#define MSDC_FIFOCS_OFS        0x14
#define MSDC_TXDATA_OFS        0x18
#define MSDC_RXDATA_OFS        0x1C
#define MSDC_CFG_MODE          (1u << 0)
#define MSDC_CFG_PIO           (1u << 3)
#define MSDC_CFG_CKPDN         (1u << 1)
#define MSDC_CFG_RST           (1u << 2)
#define MSDC_CFG_CKSTB         (1u << 7)
//
// MSDC_CFG[19:8] is the divider; [21:20] is CKMOD, which is inherited on
// purpose (bootshim/msdc.c:694-697).  Masking only the divider keeps CKMOD,
// CCKPD and CKSTB exactly as LK left them.
//
#define MSDC_CFG_CKDIV_MASK    (0xFFFu << 8)
#define MSDC_CFG_CKDIV(d)      (((UINT32)(d) & 0xFFFu) << 8)
//
// SDC_CFG[31:24] is the card data timeout (DTOC) and [14:2] the write/busy
// timeout (WRDTOC).  Both are hardware backstops; our software deadlines are
// the real limit.  BUSWIDTH ([17:16]) is NOT touched by the inherit path.
//
#define SDC_CFG_DTOC_MASK      (0xFFu << 24)
#define SDC_CFG_WRDTOC_MASK    (0x1FFFu << 2)
//
// The divider bootshim uses against this controller: 260, i.e. ~385 kHz off
// MSDC PLL/2 (bootshim/msdc.c:1019).  At that clock there is no sampling point
// to get wrong, which is the whole point of slowing the bus down first.
//
#define INHERIT_CKDIV          260u
//
// What SDC_CFG must hold after a controller reset: the value this platform
// inherits from LK (IMG_5757 read it back as 0xFF007FFC), i.e. 1-bit bus, a data
// timeout of 0xFF and a write/busy timeout of 0x1FFF.
//
#define SDC_CFG_INHERITED      0xFF007FFCu
#define MSDC_FIFOCS_CLR        (1u << 31)
#define MSDC_FIFOCS_RXCNT(x)   ((x) & 0xFFu)
#define MSDC_INT_XFER_COMPL    (1u << 12)
#define MSDC_INT_DATTMO        (1u << 14)
#define MSDC_INT_DATCRCERR     (1u << 15)
#define MSDC_INT_DAT_ALL       (MSDC_INT_XFER_COMPL | MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)
#define SDC_CMD_DTYPE_SHIFT    11
#define SDC_CMD_DTYPE_READ     1u
#define SDC_CMD_BLKLEN_SHIFT   16
//
// EXT_CSD byte offsets (JEDEC JESD84-B51 section 10): SEC_COUNT is 512-byte
// units and is the capacity number BlockIo needs.
//
#define EXT_CSD_REV            192u
#define EXT_CSD_BUS_WIDTH      183u
#define EXT_CSD_HS_TIMING      185u
#define EXT_CSD_SECTOR_COUNT   212u

//
// MSDC_PS is pin state, not controller state, and the field layout is NOT
// "DAT in the low byte" -- that misreading is what produced the "the card has
// no power" conclusion in docs/RESUME-1029.md section 3.  The vendor header
// (ComboA/msdc_reg.h, and the same masks in mtk-sd.c upstream) says:
//
//   BIT(0)      CDEN          card-detect enable          (RW)
//   BIT(1)      CDSTS         card-detect status          (R)
//   BIT(15:12)  CDDEBOUNCE                                (RW)
//   BIT(23:16)  DAT[7:0]      data lines, one bit each    (R)
//   BIT(24)     CMD                                       (R)
//   BIT(31)     WP            write protect               (R)
//
// Measured on this board: PS = 0x81FF0002 -> DAT = 0xFF, CMD = 1, WP = 1,
// CDSTS = 1: a powered card sitting on a clean, pulled-up idle bus.
//
#define MSDC_PS_DAT_SHIFT  16
#define MSDC_PS_DAT_MASK   0xFFu
#define MSDC_PS_CMD_BIT    (1u << 24)
#define MSDC_PS_CDSTS_BIT  (1u << 1)
#define MSDC_PS_WP_BIT     (1u << 31)

//
// Pinctrl / GPIO (MT6765): base 0x10005000 (GpioImplLib.c gPlatformInfo:
// ModeOffset 0x300, DataInOffset 0x200, 8 pins per register, 4 bits per pin).
// Pins 122..133 are the eMMC0 group: mode register 0x3F0 covers 120..127 and
// 0x400 covers 128..135, which is exactly the vendor's MSDC0_GPIO_MODE0/1.
//
#define PINCTRL_MMIO_BASE    0x10005000ULL
#define PINCTRL_DIN_OFS      0x200
#define PINCTRL_DOUT_OFS     0x100
#define PINCTRL_DIR_OFS      0x000
#define PINCTRL_MODE_OFS     0x300
#define PINCTRL_MODE_STRIDE  0x10
#define PINCTRL_STRAP_ADDR   0x100056F0ULL
#define PINCTRL_STRAP_BIT    13
#define EMMC_PIN_FIRST       122
#define EMMC_PIN_LAST        133
//
// The MSDC0 pads live in their own block (docs/research/mtk6765-drivers.md
// section 1.6: MSDC0_IO_PAD_BASE = 0x10002000, DRV +0x00, IES +0x10,
// PUPD +0x30, R0 +0x50, R1 +0x60), and that block is NOT in this platform's
// memory map: reading it faults (IMG_5755, X8 = 0x10002010).  Do not read it
// from here without adding the region to the memory map first; the input-enable
// question is answered through the GPIO block instead (see PinGroupBits()).
//
#define MSDC0_PAD_BASE       0x10002000ULL
#define MSDC0_PAD_IES_OFS    0x10

//
// SDC_CMD response type field (MsdcDxe.c switch on ResponseType):
// R1/R5/R6/R7 = 1, R2 = 2, R3 = 3, R4 = 4, R1b = 7, shifted left by 7.
//
#define SDC_CMD_RSP_R1      1u
#define SDC_CMD_RSP_R2      2u
#define SDC_CMD_RSP_R3      3u
#define SDC_CMD_RSP_R1B     7u
//
// CMD1 argument: high-capacity support (bit 30) and the 3.2-3.4 V window,
// i.e. what an eMMC answers with OCR bit 31 set.  Only used to see whether the
// card reacts at all; the exact value does not matter for that.
//
#define CMD1_OCR            0x40FF8080u

//
// PMIC wrapper (PWRAP) at 0x1000D000 on MT6765/MT6762G: vendor DT
// pwrap@1000d000, LK PWRAP_BASE.  The offsets and the WACS2 encoding below are
// the ones PmicWrapperImplLib.c selects for SOC_TYPE == 1 and the ones
// MediaTekPkg's PmicWrapperDxe uses (upstream repository), so this is the same
// transaction the PMIC driver itself issues -- just with a ceiling on it.
//
#define PWRAP_MMIO_BASE        0x1000D000ULL
#define PWRAP_INIT_DONE2_OFS   0x0A0
#define PWRAP_WACS2_CMD_OFS    0xC20
#define PWRAP_WACS2_RDATA_OFS  0xC24
#define PWRAP_WACS2_VLDCLR_OFS 0xC28
#define PWRAP_FSM_SHIFT        16
#define PWRAP_FSM_MASK         0x7
#define PWRAP_FSM_IDLE         0
#define PWRAP_FSM_WF_VLDCLR    6
#define PWRAP_RETRIES          200
#define PWRAP_RETRY_STALL_US   50   // 200 x 50 us = 10 ms per transaction, hard cap

//
// MT6357 regulator addresses.  dandelion carries an MT6357, NOT the MT6358 the
// in-tree PMIC driver was written for, and the two maps do not overlap:
//
//   rail             MT6357 (used here)  MT6358 (what MT6358PmicDxe pokes)
//   ldo_vemc.Con0    0x1988              0x1B1C
//   ldo_vmc.Con0     0x1AB0              0x1CC4
//   ldo_vmch.Con0    0x1AC4              0x1CD8
//
// Sources: LK platform/mt6765/include/platform/upmu_hw.h (MT6357 map), mainline
// drivers/regulator/mt6357-regulator.c with include/linux/mfd/mt6357/
// registers.h, and the vendor kernel's power_gs_6357 golden-setting tables.
//
// Where each field lives -- this cost a wrong reading once, so it is written
// down: mainline's MT6357_LDO() takes (enreg, vosel, vosel_mask), and the table
// entry is
//
//   MT6357_LDO("ldo-vemc", VEMC, vemc_voltages,
//              MT6357_LDO_VEMC_CON0, MT6357_VEMC_ANA_CON0, 0x700)
//
// so **CON0 bit 0 = enable** and **VOSEL bits 10..8 live in ANA_CON0 (0x1C20)**,
// not in CON0.  Reading VOSEL out of CON0 prints "vosel=0 0mV", which is
// meaningless.  vemc_voltages[] maps VOSEL 2/3/5 to 2.9/3.0/3.3 V.
//
#define MT6357_LDO_VIO18_CON0   0x1958
#define MT6357_LDO_VEMC_CON0    0x1988
#define MT6357_LDO_VMC_CON0     0x1AB0
#define MT6357_VEMC_ANA_CON0    0x1C20
#define MT6357_VMC_ANA_CON0     0x1C3C
#define MT6357_LDO_ENABLE_BIT   (1u << 0)
#define MT6357_VEMC_VOSEL_SHIFT 8
#define MT6357_VEMC_VOSEL_MASK  0x7

//
// Panel rows: directly under the two status banners (1100 and 1140, 30 px
// tall) and above the numeric readout that starts at 1244.  Being outside
// DandelionConsoleDxe's text area (rows 0..67), these rows survive BDS's screen
// resets, which the console text does not.
//
#define BEACON_STRIP_PS_BEFORE 1172   // idle pin state, first read
#define BEACON_STRIP_CMD_AND   1186   // PS bits that stayed high during CMD1
#define BEACON_STRIP_CMD_OR    1200   // PS bits ever high during CMD1
#define BEACON_VERDICT_LEFT    400
#define BEACON_VERDICT_TOP     1197
#define BEACON_VERDICT_W       160
#define BEACON_VERDICT_H       12
#define BEACON_DOTS_TOP        1216
#define BEACON_DOTS_VOLUMES_X  380

//
// MT6357 ldo_vemc voltage table (mainline mt6357-regulator.c, vemc_voltages[]),
// indexed by VOSEL.  Zero means "not a documented step".
//
STATIC CONST UINT16  mVemcMillivolts[8] = { 0, 0, 2900, 3000, 0, 3300, 0, 0 };

/**
  Repaint the status band (y 40..47, full width).

  Same strip that DandelionConsoleDxe (magenta) and the MSDC stages
  (yellow/orange/cyan) use, so the colour on screen at reset is the last stage
  reached.  Purple = block device, green = filesystem.
**/
STATIC
VOID
PaintBand (
  IN UINT32  Colour
  )
{
  volatile UINT32  *Band;
  UINTN            Y;
  UINTN            X;

  for (Y = 40; Y < 48; Y++) {
    Band = (volatile UINT32 *)(UINTN)(BEACON_FB_BASE + (UINT64)Y * BEACON_FB_STRIDE);
    for (X = 0; X < BEACON_FB_PIXELS; X++) {
      Band[X] = Colour;
    }
  }
}

/**
  Paint one of the stage cells at the bottom of the panel.

  Same strip MsdcImplLib paints (eight 80x80 cells on an 88-pixel pitch at
  y 1400, plus a shorter second row at y 1488); this driver owns cells 4, 5 and 6
  so the whole eMMC chain reports into one row of cells.
**/
STATIC
VOID
StageCell (
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
    Top = BEACON_ROW0_TOP;
  } else if (Stage < 16) {
    Top = BEACON_ROW1_TOP;
  } else {
    Top = BEACON_ROW2_TOP;
  }

  Left = BEACON_STAGE_LEFT + (Stage & 7) * BEACON_STAGE_PITCH;
  for (Y = Top; Y < Top + BEACON_STAGE_HIGH; Y++) {
    Row = (volatile UINT32 *)(UINTN)(BEACON_FB_BASE + (UINT64)Y * BEACON_FB_STRIDE) + Left;
    for (X = 0; X < BEACON_STAGE_SIZE; X++) {
      Row[X] = Colour;
    }
  }
}

/**
  Paint a full-width banner below the text area.

  y 1100 and up is outside DandelionConsoleDxe's text area (rows 0..67), so
  ClearScreen/ScrollUp/SetMode -- and therefore BDS's own console reset -- cannot
  erase it.  That matters: by the time BDS prints "No Operating System Found",
  everything it printed has scrolled or been cleared, but this banner stays.
**/
STATIC
VOID
PaintBanner (
  IN UINTN   Top,
  IN UINT32  Colour
  )
{
  volatile UINT32  *Row;
  UINTN            X;
  UINTN            Y;

  for (Y = Top; Y < Top + 30; Y++) {
    Row = (volatile UINT32 *)(UINTN)(BEACON_FB_BASE + (UINT64)Y * BEACON_FB_STRIDE);
    for (X = 0; X < BEACON_FB_PIXELS; X++) {
      Row[X] = Colour;
    }
  }
}

/**
  Paint `Count` small squares in a row -- a number you can count in a photograph.

  Why: the text console is unusable as a measurement channel here (BDS clears it,
  and small text on this panel photographs badly).  A row of 20x20 squares is
  countable from an arm's length away.

  `Left` exists so two independent counts can share one row: block devices from
  x 8 (blue) and mounted volumes from x 380 (green).  They used to be two rows,
  which cost the space the power probe now needs.
**/
STATIC
VOID
PaintDots (
  IN UINTN   Top,
  IN UINTN   Left,
  IN UINTN   Count,
  IN UINT32  Colour
  )
{
  volatile UINT32  *Row;
  UINTN            Index;
  UINTN            X;
  UINTN            Y;

  if (Count > 12) {
    Count = 12;
  }

  for (Index = 0; Index < Count; Index++) {
    for (Y = Top; Y < Top + 20; Y++) {
      Row = (volatile UINT32 *)(UINTN)(BEACON_FB_BASE + (UINT64)Y * BEACON_FB_STRIDE) + Left + Index * 28;
      for (X = 0; X < 20; X++) {
        Row[X] = Colour;
      }
    }
  }
}

STATIC
VOID
Say (
  IN EFI_SYSTEM_TABLE  *SystemTable,
  IN CHAR16            *Text
  )
{
  if ((SystemTable != NULL) && (SystemTable->ConOut != NULL)) {
    SystemTable->ConOut->OutputString (SystemTable->ConOut, Text);
  }
}

/**
  Format a UINT32 as eight upper-case hex digits.
**/
STATIC
VOID
HexString (
  IN  UINT32  Value,
  OUT CHAR16  *Out
  )
{
  STATIC CONST CHAR16  Digits[] = L"0123456789ABCDEF";
  UINTN                Index;

  for (Index = 0; Index < 8; Index++) {
    Out[Index] = Digits[(Value >> ((7 - Index) * 4)) & 0xF];
  }

  Out[8] = L'\0';
}

/**
  Print "labelXXXXXXXX" in one call, so the value follows its label even if the
  console interrupts between OutputString calls (it does not, but a reader of
  the photo cannot tell the difference, and one call is one line of code).
**/
STATIC
VOID
SayHex (
  IN EFI_SYSTEM_TABLE  *SystemTable,
  IN CHAR16            *Label,
  IN UINT32            Value
  )
{
  CHAR16  Text[10];

  HexString (Value, Text);
  Say (SystemTable, Label);
  Say (SystemTable, Text);
}

/**
  Print `label` followed by exactly Digits hex digits.

  The eight-digit SayHex() is right for registers but wasteful in a log that
  has a 12-row budget: DAT is one byte and a sector count is not, so lines are
  written with the width that fits.  This is also why IMG_5750's CMD2..CMD13
  never appeared -- see ProbeCardAndMux().
**/
STATIC
VOID
SayHexN (
  IN EFI_SYSTEM_TABLE  *SystemTable,
  IN CHAR16            *Label,
  IN UINT32            Value,
  IN UINTN             Digits
  )
{
  STATIC CONST CHAR16  Hex[] = L"0123456789ABCDEF";
  CHAR16               Text[9];
  UINTN                Index;

  if (Digits > 8) {
    Digits = 8;
  }

  for (Index = 0; Index < Digits; Index++) {
    Text[Index] = Hex[(Value >> ((Digits - 1 - Index) * 4)) & 0xF];
  }

  Text[Digits] = L'\0';
  Say (SystemTable, Label);
  Say (SystemTable, Text);
}

/**
  Print a decimal number (stays inside 10 digits; every caller passes a small
  value such as a bit, a VOSEL index or a millivolt figure).
**/
STATIC
VOID
SayDec (
  IN EFI_SYSTEM_TABLE  *SystemTable,
  IN UINTN             Value
  )
{
  CHAR16  Text[12];
  UINTN   Index;

  Index        = 11;
  Text[Index]  = L'\0';
  do {
    Text[--Index] = (CHAR16)(L'0' + (Value % 10));
    Value        /= 10;
  } while (Value != 0);

  Say (SystemTable, &Text[Index]);
}

/**
  Paint a solid rectangle -- used for the power verdict next to the VEMC strip.
  Kept separate from PaintBand/PaintBanner because those span the whole width.
**/
STATIC
VOID
PaintPatch (
  IN UINTN   Left,
  IN UINTN   Top,
  IN UINTN   Width,
  IN UINTN   Height,
  IN UINT32  Colour
  )
{
  volatile UINT32  *Row;
  UINTN            X;
  UINTN            Y;

  for (Y = Top; Y < Top + Height; Y++) {
    Row = (volatile UINT32 *)(UINTN)(BEACON_FB_BASE + (UINT64)Y * BEACON_FB_STRIDE) + Left;
    for (X = 0; X < Width; X++) {
      Row[X] = Colour;
    }
  }
}

/**
  Paint a value as a row of squares, bit 0 on the left, in groups of eight.

  Survives BDS clearing the console, and is readable off a photo without any
  text rendering.  A cleared bit is dim grey rather than black so the row is
  visible even when the value is zero -- "all zero" is itself the evidence we
  are looking for.  `Count` is 32 for a whole MSDC register and 16 for a PMIC
  half-word, whose upper bits are always zero and would just be noise.
**/
STATIC
VOID
PaintBitStrip (
  IN UINTN   Top,
  IN UINTN   Count,
  IN UINT32  Value,
  IN UINT32  Colour
  )
{
  volatile UINT32  *Row;
  UINTN            Bit;
  UINTN            Left;
  UINTN            X;
  UINTN            Y;

  if (Count > 32) {
    Count = 32;
  }

  for (Bit = 0; Bit < Count; Bit++) {
    Left = 8 + Bit * 21 + (Bit >> 3) * 6;
    for (Y = Top; Y < Top + 9; Y++) {
      Row = (volatile UINT32 *)(UINTN)(BEACON_FB_BASE + (UINT64)Y * BEACON_FB_STRIDE) + Left;
      for (X = 0; X < 9; X++) {
        Row[X] = ((Value >> Bit) & 1) ? Colour : 0x00202020;
      }
    }
  }
}

/**
  Read one MSDC0 register.  Everything in this file that touches the controller
  is a load, except the command writes in SendCommand()/ReadDataBlock() and the
  one SDC_CFG bus-width store in SetHostBusWidth().
**/
STATIC
UINT32
Msdc0Read32 (
  IN UINTN  Offset
  )
{
  return *(volatile UINT32 *)(UINTN)(MSDC0_MMIO_BASE + Offset);
}

STATIC
VOID
Msdc0Write32 (
  IN UINTN   Offset,
  IN UINT32  Value
  )
{
  *(volatile UINT32 *)(UINTN)(MSDC0_MMIO_BASE + Offset) = Value;
}

/**
  Set the host's data bus width (SDC_CFG[17:16]: 0 = 1-bit, 1 = 4-bit, 2 = 8-bit).

  Kept separate from everything else because it is the one controller setting
  the probe changes that LK also changes, and the point of the attempts in
  ProbeCardAndMux() is to compare the two states rather than assume one.
**/
STATIC
VOID
SetHostBusWidth (
  IN UINTN  Bits
  )
{
  UINT32  Cfg;
  UINT32  Field;

  Field = (Bits >= 8) ? 2u : ((Bits >= 4) ? 1u : 0u);
  Cfg   = Msdc0Read32 (SDC_CFG_OFS) & ~(3u << 16);
  Msdc0Write32 (SDC_CFG_OFS, Cfg | (Field << 16));
}

/**
  Soft-reset the controller and put back the settings the reset clears.

  Why this exists: LK reads this eMMC in the same boot (it loads the image
  through MSDC0), so the card and the pads are known good minutes before this
  probe runs.  Between the two, MediaTek's MsdcDxe drives the controller through
  a full init that ends in a failed data phase, and the controller's data state
  machine is the one thing a failed transfer can leave wedged.  MSDC_CFG.RST is
  the vendor's own recovery (msdc_reset_hw() in both the Linux and the vendor
  driver), it self-clears, and it does not touch the pads or the clock divider.

  What the reset does clear is SDC_CFG, so the bus width and the two timeouts are
  written back here; the bus width is then set per attempt anyway.
**/
STATIC
VOID
ResetHost (
  VOID
  )
{
  UINT32  Cfg;
  UINTN   Tries;

  Cfg = Msdc0Read32 (MSDC_CFG_OFS);

  Msdc0Write32 (MSDC_CFG_OFS, Cfg | MSDC_CFG_RST);
  for (Tries = 0; Tries < 200; Tries++) {
    if ((Msdc0Read32 (MSDC_CFG_OFS) & MSDC_CFG_RST) == 0) {
      break;
    }

    gBS->Stall (100);
  }

  for (Tries = 0; Tries < 200; Tries++) {
    if ((Msdc0Read32 (MSDC_CFG_OFS) & MSDC_CFG_CKSTB) != 0) {
      break;
    }

    gBS->Stall (100);
  }

  Msdc0Write32 (MSDC_CFG_OFS, Msdc0Read32 (MSDC_CFG_OFS) | MSDC_CFG_PIO);
  Msdc0Write32 (SDC_CFG_OFS, SDC_CFG_INHERITED);

  Msdc0Write32 (MSDC_FIFOCS_OFS, Msdc0Read32 (MSDC_FIFOCS_OFS) | MSDC_FIFOCS_CLR);
  for (Tries = 0; Tries < 200; Tries++) {
    if ((Msdc0Read32 (MSDC_FIFOCS_OFS) & MSDC_FIFOCS_CLR) == 0) {
      break;
    }

    gBS->Stall (100);
  }

  Msdc0Write32 (MSDC_INT_OFS, 0xFFFFFFFF);
}

/**
  Read one 16-bit PMIC register through the PMIC wrapper.

  Same transaction PmicWrapperDxe issues -- wait for the WACS2 FSM to be idle,
  write the half-word address into WACS2_CMD, wait for WF_VLDCLR, take the low
  half of WACS2_RDATA, acknowledge with WACS2_VLDCLR -- but bounded.  The
  upstream wait is an unbounded while(), and a wedged wrapper would then hang
  the boot with nothing on the panel, which is the one failure this board
  cannot report.

  Returns FALSE (and 0xFFFFFFFF in *Value) if the transaction does not complete
  within 10 ms, so "the PMIC window is wrong" stays distinguishable from "the
  register reads as zero".
**/
STATIC
BOOLEAN
PmicRead16 (
  IN  UINT32  Address,
  OUT UINT32  *Value
  )
{
  volatile UINT32  *Wrap;
  UINT32           Rdata;
  UINTN            Tries;

  *Value = 0xFFFFFFFF;

  Wrap = (volatile UINT32 *)(UINTN)PWRAP_MMIO_BASE;

  for (Tries = 0; Tries < PWRAP_RETRIES; Tries++) {
    if (((Wrap[PWRAP_WACS2_RDATA_OFS / 4] >> PWRAP_FSM_SHIFT) & PWRAP_FSM_MASK) == PWRAP_FSM_IDLE) {
      break;
    }

    gBS->Stall (PWRAP_RETRY_STALL_US);
  }

  if (Tries == PWRAP_RETRIES) {
    return FALSE;
  }

  //
  // ArbCapabilities = FALSE encoding (PmicWrapperImplLib.c, SOC_TYPE 1 = this
  // SoC): a read command is the byte address shifted right by one, in the
  // address field, with the write bit clear.
  //
  Wrap[PWRAP_WACS2_CMD_OFS / 4] = (Address >> 1) << 16;

  for (Tries = 0; Tries < PWRAP_RETRIES; Tries++) {
    Rdata = Wrap[PWRAP_WACS2_RDATA_OFS / 4];
    if (((Rdata >> PWRAP_FSM_SHIFT) & PWRAP_FSM_MASK) == PWRAP_FSM_WF_VLDCLR) {
      Wrap[PWRAP_WACS2_VLDCLR_OFS / 4] = 1;
      *Value                           = Rdata & 0xFFFF;
      return TRUE;
    }

    gBS->Stall (PWRAP_RETRY_STALL_US);
  }

  return FALSE;
}

/**
  The power probe: P_before, V, P_after -- in that order (docs/RESUME-1029.md
  section 5), plus the pin-mux state and the GPIO input levels of the eMMC pins.

  Measured on hardware 2026-10-29 (IMG_5738) and the reason this file now
  interprets MSDC_PS differently: P_before = P_after = 0x81FF0002, which with
  the real field layout means DAT7..0 = 0xFF, CMD = 1, WP = 1, CDSTS = 1, and
  MT6357 LDO_VEMC_CON0 = 0x0001 (enable set).  So the card is powered, present
  and sitting on a clean pulled-up bus -- "no power" is refuted, and the verdict
  below says so instead of looking for a rail that was never off.

  Read-only with respect to power: nothing here writes a PMIC register.
**/
STATIC
VOID
ProbePower (
  IN  EFI_SYSTEM_TABLE  *SystemTable,
  OUT UINT32            *VerdictColour
  )
{
  UINT32  PsBefore;
  UINT32  PsAfter;
  UINT32  Vemc;
  UINT32  VemcAna;
  UINT32  Vmc;
  UINT32  Vio18;
  UINT32  WrapInit;
  UINT32  Failures;
  UINT32   Vosel;
  UINT32   Verdict;
  CHAR16   *VerdictText;

  //
  // 1. P_before -- the first load this driver performs.  Nothing else in here
  // runs before it, and that is on purpose: a paint or a Say would be time the
  // hardware has to settle, and the point is to sample the state MsdcDxe left
  // behind.
  //
  PsBefore = Msdc0Read32 (MSDC_PS_OFS);

  //
  // The first strip is also the "I got this far" marker, and it is painted
  // right after the read rather than before it: a photo that shows this row
  // but neither of the rows painted later means the driver died in the PMIC
  // access, which is the only blocking transaction in here.
  //
  // Note what this does *not* do: it paints no stage cell.  Cells 16..23 all
  // belong to MsdcDxe/EmmcDxe (20 = CMD10, 21 = CMD8/ext-CSD, 22 = data-read
  // verdict, 23 = command failed), and this driver runs after them -- painting
  // a cell here would erase the M4 evidence the photo is for.
  //
  PaintBitStrip (BEACON_STRIP_PS_BEFORE, 32, PsBefore, 0x0000FFFF);
  PaintBand (0x008000FF);
  StageCell (6, 0x00FFFF00);
  Say (SystemTable, L"\r\nMSDC PASSTHRU\r\n");

  //
  // 2. V.  INIT_DONE2 bit 0 is the wrapper's own "WACS2 usable" flag, read
  // first so that "wrapper not initialised" is not reported as "registers read
  // back as zero".
  //
  Failures = 0;
  WrapInit = *(volatile UINT32 *)(UINTN)(PWRAP_MMIO_BASE + PWRAP_INIT_DONE2_OFS);

  if (!PmicRead16 (MT6357_LDO_VEMC_CON0, &Vemc)) {
    Failures++;
  }

  if (!PmicRead16 (MT6357_LDO_VMC_CON0, &Vmc)) {
    Failures++;
  }

  if (!PmicRead16 (MT6357_LDO_VIO18_CON0, &Vio18)) {
    Failures++;
  }

  if (!PmicRead16 (MT6357_VEMC_ANA_CON0, &VemcAna)) {
    Failures++;
  }

  //
  // 3. P_after, after a short settle (a rail that is ramping would show up as
  // a difference between the two readings).
  //
  gBS->Stall (10 * 1000);
  PsAfter = Msdc0Read32 (MSDC_PS_OFS);

  //
  // The verdict, in the order the questions were asked: wrapper alive, rail
  // enabled, bus pulled high.  It goes in two places -- a patch beside the
  // strips and the y 40 band -- because this panel's top rows are exactly where
  // photographs go bad.
  //
  if (Failures != 0) {
    Verdict     = 0x00FF00FF;
    VerdictText = L"BUS NO-WRAP";
  } else if ((Vemc & MT6357_LDO_ENABLE_BIT) == 0) {
    Verdict     = 0x00FF0000;
    VerdictText = L"BUS RAIL-OFF";
  } else if (((PsAfter >> MSDC_PS_DAT_SHIFT) & MSDC_PS_DAT_MASK) != MSDC_PS_DAT_MASK) {
    Verdict     = 0x00FF8000;
    VerdictText = L"BUS DAT-LOW";
  } else if ((PsAfter & MSDC_PS_CMD_BIT) == 0) {
    Verdict     = 0x00FF8000;
    VerdictText = L"BUS CMD-LOW";
  } else {
    Verdict     = 0x0000FF00;
    VerdictText = L"BUS OK";
  }

  //
  // Text first: it is the readable version and the one that reaches the flash
  // log.  The strips are the backstop when the photo is bad.
  //
  // This used to be six lines.  It is one now, and that is a correctness fix,
  // not a tidy-up: the console's own self-test leaves the cursor at row 53 of
  // its 68-row text area, so this driver has a 15-row budget for everything it
  // prints.  IMG_5750 shows what overrunning it does -- CMD1 landed on the last
  // row and CMD2..CMD13 were wrapped off the bottom, which is why the init
  // sequence looked like it had stopped after one line.
  //
  Say (SystemTable, L"PWR ");
  SayHex (SystemTable, L"PS=", PsBefore);
  SayHexN (SystemTable, L" DAT=", (PsAfter >> MSDC_PS_DAT_SHIFT) & MSDC_PS_DAT_MASK, 2);
  Say (SystemTable, L" CMD=");
  SayDec (SystemTable, (PsAfter & MSDC_PS_CMD_BIT) ? 1 : 0);
  Say (SystemTable, L" CD=");
  SayDec (SystemTable, (PsAfter & MSDC_PS_CDSTS_BIT) ? 1 : 0);
  Say (SystemTable, L" VEMC=");
  SayDec (SystemTable, (Vemc & MT6357_LDO_ENABLE_BIT) ? 1 : 0);

  if (VemcAna != 0xFFFFFFFF) {
    Vosel = (VemcAna >> MT6357_VEMC_VOSEL_SHIFT) & MT6357_VEMC_VOSEL_MASK;
    Say (SystemTable, L" vsel=");
    SayDec (SystemTable, Vosel);
    Say (SystemTable, L" ");
    SayDec (SystemTable, mVemcMillivolts[Vosel]);
    Say (SystemTable, L"mV");
  }

  Say (SystemTable, L" w=");
  SayDec (SystemTable, WrapInit & 0x1);
  Say (SystemTable, L" f=");
  SayDec (SystemTable, Failures);
  Say (SystemTable, L" ");
  Say (SystemTable, VerdictText);
  Say (SystemTable, L"\r\n");

  PaintPatch (BEACON_VERDICT_LEFT, BEACON_VERDICT_TOP, BEACON_VERDICT_W, BEACON_VERDICT_H, Verdict);

  *VerdictColour = Verdict;
}

/**
  Which pinctrl register holds the 4-bit mode field of one pin.  GpioImplLib.c:
  ModeOffset 0x300, one register per eight pins, 4 bits per pin -- which is how
  0x300 + (122/8)*0x10 lands on the vendor's MSDC0_GPIO_MODE0 (0x3F0).
**/
STATIC
UINTN
EmmcModeRegister (
  IN UINTN  Pin
  )
{
  return PINCTRL_MODE_OFS + (Pin / 8) * PINCTRL_MODE_STRIDE;
}

STATIC
UINT32
PinctrlRead32 (
  IN UINTN  Offset
  )
{
  return *(volatile UINT32 *)(UINTN)(PINCTRL_MMIO_BASE + Offset);
}

/**
  Pack one per-pin property of GPIO122..133 into 12 bits, bit 0 = pin 122.

  The data registers are NOT laid out like the mode registers: GpioImplLib.c
  uses DirOffset 0x0, DataOutOffset 0x100, DataInOffset 0x200, ModeOffset 0x300,
  `(pin/32)*0x10` and `bit = pin%32` for the first three, and `(pin/8)*0x10`
  with four bits per pin only for mode.  Reading DIN with the mode formula (as
  the first version of this probe did) lands on the wrong register and always
  returns 0, which is what IMG_5743 showed.
**/
STATIC
UINT32
PinGroupBits (
  IN UINTN  BaseOffset
  )
{
  UINTN   Pin;
  UINT32  Bits;

  Bits = 0;
  for (Pin = EMMC_PIN_FIRST; Pin <= EMMC_PIN_LAST; Pin++) {
    if (((PinctrlRead32 (BaseOffset + (Pin / 32) * PINCTRL_MODE_STRIDE) >> (Pin % 32)) & 1) != 0) {
      Bits |= (1u << (Pin - EMMC_PIN_FIRST));
    }
  }

  return Bits;
}

/**
  Write the eMMC pin group's mode the way the vendor does it: one plain 32-bit
  store per register, whole field at once, instead of per-pin read-modify-write.

  Why this matters: GpioDxe.c's GpioSetMode() writes to `Offset + 0x4` and
  `Offset + 0x8`, i.e. it treats the mode register as a set/reset alias pair and
  clears with a 3-bit mask (0x7) even though a field is four bits.  On this SoC
  that left pins 122/123 reading back as mode 0xF while 124..127 read 2 -- see
  IMG_5743's `m0=2222FF00`.  The vendor's msdc_set_pin_mode() simply stores
  0x22222200 (or 0x11111100) into 0x3F0 and 0x222222 (0x111111) into 0x400.
**/
STATIC
VOID
WriteEmmcPinMode (
  IN UINT32  Mode
  )
{
  UINT32  Pattern;
  UINT32  M0;
  UINT32  M1;

  //
  // 0x3F0 covers GPIO120..127: bits 7..0 are 120/121 and must be preserved.
  // 0x400 covers GPIO128..135: bits 31..24 are 134/135 and must be preserved.
  //
  Pattern = (Mode & 0xFu) * 0x111111u;

  M0 = PinctrlRead32 (EmmcModeRegister (EMMC_PIN_FIRST));
  M0 = (M0 & 0x000000FFu) | (Pattern << 8);
  *(volatile UINT32 *)(UINTN)(PINCTRL_MMIO_BASE + EmmcModeRegister (EMMC_PIN_FIRST)) = M0;

  M1 = PinctrlRead32 (EmmcModeRegister (128));
  M1 = (M1 & 0xFF000000u) | Pattern;
  *(volatile UINT32 *)(UINTN)(PINCTRL_MMIO_BASE + EmmcModeRegister (128)) = M1;
}

//
// The GPIO input words that carry the eMMC pads.  Data registers are 32 pins
// per register (GpioDxe.c: PIN_OFFSET(Pin) = (Pin/32)*0x10, bit Pin%32), so
// the twelve pads 122..133 live in exactly two of them:
//   0x230  pins 120..127  -> 122=bit26, 123=bit27, 124=bit28, ... 127=bit31
//   0x240  pins 128..135  -> 128=bit0 ... 133=bit5
//
#define EMMC_DIN0_OFS  (PINCTRL_DIN_OFS + (120 / 32) * PINCTRL_MODE_STRIDE)
#define EMMC_DIN1_OFS  (PINCTRL_DIN_OFS + (128 / 32) * PINCTRL_MODE_STRIDE)
#define EMMC_DIN_CMD   (1u << (122 % 32))
#define EMMC_DIN_CLK   (1u << (124 % 32))

//
// How long to watch the bus after a command is issued.  At roughly 300 ns per
// iteration this is about 9 ms -- two orders of magnitude more than a 48-bit
// command frame (58 us at the inherited 833 kHz), so a response that arrives
// late is still inside the window.
//
#define CMD_SAMPLES  30000

//
// One command's evidence.  Kept in a struct because each field answers a
// different question and they have to stay together in the log.
//
typedef struct {
  BOOLEAN    Answered;   // CMDRDY seen: the controller saw a response
  UINT32     IntStatus;  // MSDC_INT after the command
  UINT32     Resp0;      // R3/OCR
  UINT32     PsAnd;      // MSDC_PS bits held high during the command
  UINT32     PsOr;       // MSDC_PS bits ever high during the command
  UINT32     CmdLow;     // samples where the CMD pad (GPIO122) read 0
  UINT32     ClkLow;     // samples where the CLK pad (GPIO124) read 0
  UINT32     FirstLow;   // iteration index of the first such sample
  UINT32     LastLow;    // iteration index of the last such sample
  UINT32     DinAnd;     // eMMC pins held high during the command
  UINT32     DinOr;      // eMMC pins ever high during the command
} CARD_CMD_RESULT;

/**
  Send one command by hand and watch the CMD pad while it is on the wire.

  Why hand-rolled and why straight off the pad: the question is no longer what
  the driver did but whether anything reaches the card, and MSDC_PS's CMD bit
  turned out to be useless for that -- IMG_5744 shows `cl=0` (PS never saw CMD
  low) while the GPIO input of the very same pin did go low (`dA=00000DFA`).
  The controller's PS bit is masked while it drives CMD; the pad's input buffer
  is not, so the count and the timestamps come from GPIO122.

  The timestamps are iteration indices, not microseconds, and that is enough:
  the command frame is 48 bits, so a lone frame shows up as a run of about
  FirstLow..FirstLow+N samples.  A response (if the card gave one) starts a few
  clocks after the frame and would stretch LastLow to roughly twice N -- which
  separates "nothing came back" from "the card answered and this controller
  failed to sample it".

  Interrupts are cleared before the command because MSDC_INT is W1C: a stale
  CMDTMO from MsdcDxe would otherwise be read as this command's result.
**/
STATIC
CARD_CMD_RESULT
SendCommandSampled (
  IN UINT32  Index,
  IN UINT32  Arg,
  IN UINT32  RspTyp
  )
{
  volatile UINT32  *Regs;
  CARD_CMD_RESULT   Result;
  UINT32            Ps;
  UINT32            Din0;
  UINT32            Din1;
  UINT32            Din;
  UINTN             Tries;

  Result.Answered  = FALSE;
  Result.IntStatus = 0;
  Result.Resp0     = 0;
  Result.PsAnd     = 0xFFFFFFFF;
  Result.PsOr      = 0;
  Result.CmdLow    = 0;
  Result.ClkLow    = 0;
  Result.FirstLow  = 0;
  Result.LastLow   = 0;
  Result.DinAnd    = 0xFFFFFFFF;
  Result.DinOr     = 0;

  Regs = (volatile UINT32 *)(UINTN)MSDC0_MMIO_BASE;

  //
  // Wait for the controller to be idle.  A command issued while SDC_STS says
  // busy is dropped by the hardware, which would look like "no answer".
  //
  for (Tries = 0; Tries < 2000; Tries++) {
    if ((Regs[SDC_STS_OFS / 4] & (SDC_STS_BUSY | SDC_STS_CMDBUSY)) == 0) {
      break;
    }

    gBS->Stall (100);
  }

  Regs[MSDC_INT_OFS / 4] = 0xFFFFFFFF;
  Regs[SDC_ARG_OFS / 4]  = Arg;
  Regs[SDC_CMD_OFS / 4]  = ((Index & 0x3Fu) | ((RspTyp & 0x7u) << SDC_CMD_RSP_SHIFT));

  for (Tries = 0; Tries < CMD_SAMPLES; Tries++) {
    Ps = Regs[MSDC_PS_OFS / 4];

    Din0 = PinctrlRead32 (EMMC_DIN0_OFS);
    Din1 = PinctrlRead32 (EMMC_DIN1_OFS);
    Din  = ((Din0 >> (122 % 32)) & 0x3Fu) | ((Din1 & 0x3Fu) << 6);

    if ((Din0 & EMMC_DIN_CLK) == 0) {
      Result.ClkLow++;
    }

    if ((Din0 & EMMC_DIN_CMD) == 0) {
      if (Result.CmdLow == 0) {
        Result.FirstLow = (UINT32)Tries;
      }

      Result.LastLow = (UINT32)Tries;
      Result.CmdLow++;
    }

    Result.PsAnd  &= Ps;
    Result.PsOr   |= Ps;
    Result.DinAnd &= Din;
    Result.DinOr  |= Din;
  }

  for (Tries = 0; Tries < 2000; Tries++) {
    Result.IntStatus = Regs[MSDC_INT_OFS / 4];
    if ((Result.IntStatus & (MSDC_INT_CMDRDY | MSDC_INT_CMDTMO)) != 0) {
      break;
    }

    gBS->Stall (1000);
  }

  Result.IntStatus = Regs[MSDC_INT_OFS / 4];
  Result.Resp0     = Regs[SDC_RESP0_OFS / 4];
  Regs[MSDC_INT_OFS / 4] = Result.IntStatus;

  Result.Answered = ((Result.IntStatus & MSDC_INT_CMDRDY) != 0);

  return Result;
}

/**
  Print the CMD-pad trace of one command: how many samples saw it low, and the
  iteration range they fell in.
**/
STATIC
VOID
SayCmdTrace (
  IN EFI_SYSTEM_TABLE   *SystemTable,
  IN CARD_CMD_RESULT    *Result
  )
{
  Say (SystemTable, L" cl=");
  SayDec (SystemTable, Result->CmdLow);
  Say (SystemTable, L" f=");
  SayDec (SystemTable, Result->FirstLow);
  Say (SystemTable, L" l=");
  SayDec (SystemTable, Result->LastLow);
  Say (SystemTable, L" kc=");
  SayDec (SystemTable, Result->ClkLow);
  Say (SystemTable, L"\r\n");
}

//
// A command's reply: all four response words, because R2 (CID/CSD) is 128 bits
// and R6 carries the RCA in the top half of RESP0.
//
typedef struct {
  BOOLEAN  Answered;
  UINT32   IntStatus;
  UINT32   Resp[4];
} CARD_RESP;

#define SDC_RESP_OFS(i)  (SDC_RESP0_OFS + 4 * (i))

/**
  Send one command and wait for its result, without sampling the pads.

  Same register dance as SendCommandSampled(), but the return is the reply
  rather than the waveform: this is what the initialization sequence below
  needs, and a 1 ms poll is coarse enough that the command is over long before
  the first look.  Extra carries command-word bits outside the opcode/response
  fields; the only one used here is SDC_CMD_STOP_CMD, which is what makes CMD12
  a stop rather than an ordinary R1b (docs/research/mtk-msdc-pio.md section 1.3
  row 14).
**/
STATIC
CARD_RESP
SendCommand (
  IN UINT32  Index,
  IN UINT32  Arg,
  IN UINT32  RspTyp,
  IN UINT32  Extra
  )
{
  volatile UINT32  *Regs;
  CARD_RESP         Result;
  UINTN             Tries;
  UINTN             Word;

  Result.Answered  = FALSE;
  Result.IntStatus = 0;
  for (Word = 0; Word < 4; Word++) {
    Result.Resp[Word] = 0;
  }

  Regs = (volatile UINT32 *)(UINTN)MSDC0_MMIO_BASE;

  for (Tries = 0; Tries < 2000; Tries++) {
    if ((Regs[SDC_STS_OFS / 4] & (SDC_STS_BUSY | SDC_STS_CMDBUSY)) == 0) {
      break;
    }

    gBS->Stall (100);
  }

  Regs[MSDC_INT_OFS / 4] = 0xFFFFFFFF;
  Regs[SDC_ARG_OFS / 4]  = Arg;
  Regs[SDC_CMD_OFS / 4]  = ((Index & 0x3Fu) |
                            ((RspTyp & 0x7u) << SDC_CMD_RSP_SHIFT) |
                            (Extra & SDC_CMD_STOP_CMD));

  for (Tries = 0; Tries < 200; Tries++) {
    Result.IntStatus = Regs[MSDC_INT_OFS / 4];
    if ((Result.IntStatus & (MSDC_INT_CMDRDY | MSDC_INT_CMDTMO)) != 0) {
      break;
    }

    gBS->Stall (1000);
  }

  Result.IntStatus = Regs[MSDC_INT_OFS / 4];
  for (Word = 0; Word < 4; Word++) {
    Result.Resp[Word] = Regs[SDC_RESP_OFS (Word) / 4];
  }

  Regs[MSDC_INT_OFS / 4] = Result.IntStatus;
  Result.Answered = ((Result.IntStatus & MSDC_INT_CMDRDY) != 0);

  return Result;
}

/**
  Print one reply as `I=........ R=........`.
**/
STATIC
VOID
SayResp (
  IN EFI_SYSTEM_TABLE  *SystemTable,
  IN CHAR16            *Tag,
  IN CARD_RESP         *Resp
  )
{
  SayHex (SystemTable, Tag, Resp->IntStatus);
  SayHex (SystemTable, L" R=", Resp->Resp[0]);
  Say (SystemTable, L"\r\n");
}

typedef struct {
  BOOLEAN  Ok;
  UINT32   IntStatus;
  UINT32   Resp0;
  UINT32   BytesLeft;
  UINT32   PsAnd;      // DAT[7:0] bits that never went low during the wait
  UINT32   PsOr;       // DAT[7:0] bits that were high at least once
  UINT32   GpAnd;      // GPIO122..133 the same way, straight off the pads
  UINT32   GpOr;
  UINT32   ClkLow;     // samples in which GPIO124, the CLK pad, read 0
  UINT32   Fifo;       // MSDC_FIFOCS at the end: RXCNT in [7:0]
} DATA_RESULT;

//
// One sector: EXT_CSD (CMD8) and the GPT header (CMD17 LBA 1) both fit, and the
// first is decoded before the second overwrites it.  File scope and explicitly
// initialised so the linker gives it a real .data image rather than a .bss the
// PE loader has to know to zero.
//
STATIC UINT8  mDataBlock[512] = { 0 };

/**
  Read one 512-byte block with the controller's polled PIO path.

  This is the data half that a plain command does not need and that the previous
  rounds never exercised: SDC_CMD carries the transfer type and block length,
  the card streams into the FIFO, and the loop below drains it while watching
  MSDC_INT for XFER_COMPL.  The sequence is the one bootshim/msdc.c uses against
  this same controller (CMDW(): rsp type 1, data type 1, direction 0, length
  512), which is why the register writes look identical.

  Bounded at ~3 s in 100 us steps, so a card that never starts the data phase
  costs a boot a few seconds and a line in the log -- not a hang.
**/
STATIC
DATA_RESULT
ReadDataBlock (
  IN  UINT32  Opcode,
  IN  UINT32  Arg,
  OUT UINT8   *Buffer,
  IN  UINTN   Bytes
  )
{
  volatile UINT32  *Regs;
  DATA_RESULT       Result;
  UINTN             Tries;
  UINTN             Step;
  UINT32            Left;
  UINT32            Count;
  UINT32            Word;
  UINT32            Off;
  UINT32            Chunk;
  UINT32            Ps;
  UINT32            Gp;

  Result.Ok        = FALSE;
  Result.IntStatus = 0;
  Result.Resp0     = 0;
  Result.BytesLeft = (UINT32)Bytes;
  Result.PsAnd     = 0xFF;
  Result.PsOr      = 0x00;
  Result.GpAnd     = 0xFFF;
  Result.GpOr      = 0x000;
  Result.ClkLow    = 0;
  Result.Fifo      = 0;

  Regs = (volatile UINT32 *)(UINTN)MSDC0_MMIO_BASE;

  for (Tries = 0; Tries < 2000; Tries++) {
    if ((Regs[SDC_STS_OFS / 4] & (SDC_STS_BUSY | SDC_STS_CMDBUSY)) == 0) {
      break;
    }

    gBS->Stall (100);
  }

  Regs[MSDC_FIFOCS_OFS / 4] |= MSDC_FIFOCS_CLR;
  for (Tries = 0; Tries < 200; Tries++) {
    if ((Regs[MSDC_FIFOCS_OFS / 4] & MSDC_FIFOCS_CLR) == 0) {
      break;
    }

    gBS->Stall (100);
  }

  Regs[SDC_BLK_NUM_OFS / 4] = 1;
  if ((Regs[MSDC_CFG_OFS / 4] & MSDC_CFG_PIO) == 0) {
    Regs[MSDC_CFG_OFS / 4] |= MSDC_CFG_PIO;
  }

  Regs[MSDC_INT_OFS / 4] = 0xFFFFFFFF;

  Regs[SDC_ARG_OFS / 4] = Arg;
  Regs[SDC_CMD_OFS / 4] = ((Opcode & 0x3Fu) |
                           (SDC_CMD_RSP_R1 << SDC_CMD_RSP_SHIFT) |
                           (SDC_CMD_DTYPE_READ << SDC_CMD_DTYPE_SHIFT) |
                           (((UINT32)Bytes & 0x1FFFu) << SDC_CMD_BLKLEN_SHIFT));

  Left = (UINT32)Bytes;
  Off  = 0;

  for (Tries = 0; Tries < 30000; Tries++) {
    Result.IntStatus = Regs[MSDC_INT_OFS / 4];
    if ((Result.IntStatus & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) != 0) {
      break;
    }

    //
    // Sample the pin levels the controller sees.  MSDC_PS DAT[7:0] is bits
    // 23..16; accumulating AND and OR over the whole wait answers exactly the
    // question a dead data phase leaves open: is the card not driving the bus,
    // or is the controller not taking what is on it?  ps=<and>/<or> with bit 0
    // clear means DAT0 was low at some point, i.e. the card did start sending.
    //
    Ps = (Regs[MSDC_PS_OFS / 4] >> MSDC_PS_DAT_SHIFT) & MSDC_PS_DAT_MASK;
    Result.PsAnd &= Ps;
    Result.PsOr  |= Ps;

    //
    // The same question asked through a different window: the GPIO block reads
    // the pads itself, so if the pins move here and not in MSDC_PS it is the
    // controller's input path (pad IES, mux) and not the card.
    //
    Gp = PinGroupBits (PINCTRL_DIN_OFS);
    Result.GpAnd &= Gp;
    Result.GpOr  |= Gp;

    //
    // Pin 124 is CLK (the same pin -- and the same evidence -- as the kc= field
    // of the command trace: it only reads low while the controller is clocking).
    // Counting it over the data wait says whether the host keeps the clock
    // running once the response is over, which is what a card needs in order to
    // shift the block out.
    //
    if ((Gp & (1u << (124 - EMMC_PIN_FIRST))) == 0) {
      Result.ClkLow++;
    }

    for (;;) {
      Count = MSDC_FIFOCS_RXCNT (Regs[MSDC_FIFOCS_OFS / 4]);
      if ((Count == 0) || (Left == 0)) {
        break;
      }

      if (Count > Left) {
        Count = Left;
      }

      while ((Count > 0) && (Left > 0)) {
        Word  = Regs[MSDC_RXDATA_OFS / 4];
        Chunk = (Count >= 4) ? 4 : Count;
        for (Step = 0; (Step < Chunk) && (Left > 0); Step++) {
          Buffer[Off++] = (UINT8)(Word >> (8 * Step));
          Left--;
        }

        Count -= Chunk;
      }
    }

    if (((Result.IntStatus & MSDC_INT_XFER_COMPL) != 0) && (Left == 0)) {
      break;
    }

    gBS->Stall (100);
  }

  Result.IntStatus = Regs[MSDC_INT_OFS / 4];
  Result.Resp0     = Regs[SDC_RESP0_OFS / 4];
  Result.BytesLeft = Left;
  Result.Fifo      = Regs[MSDC_FIFOCS_OFS / 4];
  Regs[MSDC_INT_OFS / 4] = 0xFFFFFFFF;

  Result.Ok = (((Result.IntStatus & MSDC_INT_XFER_COMPL) != 0) && (Left == 0));

  return Result;
}

/**
  One data-phase attempt: optionally CMD6 the card into another bus mode, set the
  host's width to match, then read EXT_CSD and print where it stopped.

  Print shape:
  `<tag> I=.... R=........ sz=........ ps=AA/BB gp=CCC/DD f=.. k=.. c6=.. t=. r=.`

    I    MSDC_INT at the end.  ...0100 is CMDRDY alone (command ok, no data);
         ...1100 adds XFER_COMPL; ...4100 includes DATTMO; ...8100 DATCRCERR.
    sz   EXT_CSD SEC_COUNT -- meaningful only when the read worked.
    ps   MSDC_PS DAT[7:0] that never went low / that went high at least once,
         accumulated over the whole wait: the controller's own view of the pads.
         ps=FF/FF means it never saw the card drive the bus at all; anything
         darker means the card did start sending and the host missed it.
    gp   the same AND/OR taken from the GPIO block's DIN for pins 122..133 (bit
         0 = pin 122, so DAT0 is bit 2 if 122/123 are CMD/CLK).  Two
         independent windows onto the same pads: if the pins move in gp and not
         in ps, the controller's input path is the broken half, not the card.
    f    MSDC_FIFOCS.RXCNT still queued at the end: non-zero means bytes arrived.
    c6   CMD6's R1, all zeroes when this attempt did not switch anything (bit 7
         set would mean the card refused the switch).
    t    the card's CURRENT_STATE, read back with CMD13 right after the failed
         read: 4 = TRAN (the card never started the transfer), 5 = DATA (it did
         start and is being clocked, so the loss is on the host side).
    r    READY_FOR_DATA from that same status.

  Returning Data.Ok is what lets the caller stop at the first mode that works
  instead of flashing one image per hypothesis.
**/
STATIC
BOOLEAN
ExtCsdAttempt (
  IN EFI_SYSTEM_TABLE  *SystemTable,
  IN CHAR16            *Tag,
  IN UINT32            Rca,
  IN UINTN             HostBits,
  IN BOOLEAN           DoSwitch,
  IN UINT32            SwitchIndex,
  IN UINT32            SwitchValue
  )
{
  CARD_RESP    Sw;
  CARD_RESP    Stat;
  DATA_RESULT  Data;
  UINT32       Sectors;
  UINTN        Tries;

  Sw.IntStatus = 0;

  if (DoSwitch) {
    Sw = SendCommand (6, (3u << 24) | (SwitchIndex << 16) | (SwitchValue << 8), SDC_CMD_RSP_R1B, 0);

    //
    // R1b: DAT0 is busy while the card reconfigures, so wait for TRAN before
    // touching the data path.
    //
    Tries = 0;
    do {
      Stat = SendCommand (13, Rca << 16, SDC_CMD_RSP_R1, 0);
      Tries++;
      gBS->Stall (1000);
    } while ((((Stat.Resp[0] >> 9) & 0xFu) != 4) && (Tries < 100));
  }

  SetHostBusWidth (HostBits);
  SendCommand (16, 512, SDC_CMD_RSP_R1, 0);

  Data    = ReadDataBlock (8, 0, mDataBlock, 512);

  //
  // Ask the card what it thinks happened.  If the data phase really started, the
  // card is in DATA (5) with READY_FOR_DATA clear while it waits for its block to
  // be shifted out; if it never started, the card is still in TRAN (4).  That
  // separates 'the card is sending and the host drops it' from 'the card never
  // starts', which nothing on the host side can tell apart.
  //
  Stat = SendCommand (13, Rca << 16, SDC_CMD_RSP_R1, 0);

  Sectors = (UINT32)mDataBlock[EXT_CSD_SECTOR_COUNT] |
            ((UINT32)mDataBlock[EXT_CSD_SECTOR_COUNT + 1] << 8) |
            ((UINT32)mDataBlock[EXT_CSD_SECTOR_COUNT + 2] << 16) |
            ((UINT32)mDataBlock[EXT_CSD_SECTOR_COUNT + 3] << 24);

  Say (SystemTable, Tag);
  SayHexN (SystemTable, L"I=", Data.IntStatus, 4);
  SayHex (SystemTable, L" R=", Data.Resp0);
  SayHex (SystemTable, L" sz=", Sectors);
  SayHexN (SystemTable, L" ps=", Data.PsAnd, 2);
  Say (SystemTable, L"/");
  SayHexN (SystemTable, L"", Data.PsOr, 2);
  SayHexN (SystemTable, L" gp=", Data.GpAnd, 3);
  Say (SystemTable, L"/");
  SayHexN (SystemTable, L"", Data.GpOr, 3);
  SayHexN (SystemTable, L" f=", Data.Fifo, 2);
  Say (SystemTable, L" k=");
  SayDec (SystemTable, Data.ClkLow);
  SayHex (SystemTable, L" c6=", Sw.Resp[0]);
  Say (SystemTable, L" t=");
  SayDec (SystemTable, (Stat.Resp[0] >> 9) & 0xFu);
  Say (SystemTable, L" r=");
  SayDec (SystemTable, (Stat.Resp[0] >> 8) & 1u);
  Say (SystemTable, L"\r\n");

  return Data.Ok;
}

/**
  Read a block the way bootshim/msdc.c does it: inherit, do not re-initialise.

  This is the one thing every round up to IMG_5760 never tried.  Line A already
  wrote 5159 bytes into the `flashinfo` partition from bare metal on this exact
  phone (docs/EMMC-LOG.md), so a working MSDC0 read path exists in this
  repository and its policy is stated in its own header:

      "LK has just loaded *this image* through MSDC0, so the controller is
       clocked, the pads are muxed, the card is selected with RCA 1 and it is in
       the transfer state.  We therefore inherit rather than re-initialise: no
       CMD0, no CMD1, no re-tuning."
      "CKMOD and SDC_CFG.BUSWIDTH are left exactly as LK set them, because the
       card's own bus width lives in EXT_CSD[183] and the two must keep matching."

  Everything below us in this file does the opposite of all four of those: it
  resets the controller, sends CMD0 and walks the card up from IDLE again, and
  sets the host to 1 bit while the card is still whatever width LK left it.  Four
  attempts across three bus modes (IMG_5757..IMG_5760, `A8`/`B8`/`C8`/`D8`) all
  failed identically -- `I=0100` (CMDRDY alone, no XFER_COMPL, no DATTMO),
  `ps=EF/FF`, `k=14999` (the clock pad toggling half the samples, so the clock is
  running and the card is simply not driving DAT0..7).  A card in TRAN with
  READY_FOR_DATA that is asked for 512 bytes always sends them, so the remaining
  explanation is that the host is not talking to the card it thinks it is.

  Hence this probe, which runs FIRST -- before anything in this driver or in the
  FV touches MSDC0 -- and does only what bootshim does:

    - MSDC_INTEN = 0, MSDC_INT = all ones (polling only, stale flags dropped)
    - MSDC_CFG: keep everything, replace only the divider, wait for CKSTB
    - SDC_CFG:  keep BUSWIDTH, widen DTOC/WRDTOC
    - CMD13 (RCA 1) to confirm TRAN, then CMD8 (EXT_CSD), then CMD17 (LBA 1)

  Four lines of output: the inherited registers, the status, EXT_CSD's revision /
  bus width / HS timing / sector count, and the GPT signature.
**/
STATIC
BOOLEAN
InheritProbe (
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT32       CfgBefore;
  UINT32       SdcBefore;
  UINT32       CfgAfter;
  UINT32       Sdc;
  UINT32       Tries;
  UINT32       Sectors;
  UINT32       Signature;
  CARD_RESP    Status;
  DATA_RESULT  Ext;
  DATA_RESULT  Gpt;

  CfgBefore = Msdc0Read32 (MSDC_CFG_OFS);
  SdcBefore = Msdc0Read32 (SDC_CFG_OFS);

  //
  // Cell 0 is the "this driver was dispatched at all" marker.  It has to be the
  // first thing painted, before a single register write and before any ConOut
  // call, because the two failure modes this build has to tell apart are
  // "the dispatcher never got here" and "we got here and something below hung".
  //
  StageCell (0, 0x00FFFFFF);

  Msdc0Write32 (MSDC_INTEN_OFS, 0);
  Msdc0Write32 (MSDC_INT_OFS, 0xFFFFFFFFu);

  Msdc0Write32 (MSDC_CFG_OFS,
                (CfgBefore & ~MSDC_CFG_CKDIV_MASK) |
                MSDC_CFG_MODE | MSDC_CFG_PIO | MSDC_CFG_CKDIV (INHERIT_CKDIV));

  //
  // CKSTB (bit 7) is the "clock is stable" bit, and bootshim treats its absence
  // as a hard stop: with the clock gated nothing else may be touched.
  //
  for (Tries = 0; Tries < 20; Tries++) {
    if ((Msdc0Read32 (MSDC_CFG_OFS) & MSDC_CFG_CKSTB) != 0) {
      break;
    }

    gBS->Stall (1000);
  }

  CfgAfter = Msdc0Read32 (MSDC_CFG_OFS);

  Sdc  = Msdc0Read32 (SDC_CFG_OFS);
  Sdc &= ~(SDC_CFG_DTOC_MASK | SDC_CFG_WRDTOC_MASK);
  Sdc |= (0xFFu << 24) | (0x1FFFu << 2);
  Msdc0Write32 (SDC_CFG_OFS, Sdc);

  //
  // Cell 1: every register write above completed.  Anything that hangs between
  // here and cell 2 is in the print path or in the first command.
  //
  StageCell (1, 0x00FFFFFF);

  SayHex (SystemTable, L"INH C=", CfgBefore);
  SayHex (SystemTable, L" S=", SdcBefore);
  Say (SystemTable, L" k=");
  SayDec (SystemTable, ((CfgAfter & MSDC_CFG_CKSTB) != 0) ? 1u : 0u);
  SayHex (SystemTable, L" C'=", CfgAfter);
  Say (SystemTable, L"\r\n");

  if ((CfgAfter & MSDC_CFG_CKSTB) == 0) {
    //
    // No clock.  bootshim returns MSDC_ENOCLK here and so do we: with the clock
    // gated, CMD13 costs 200 ms of stall and CMD8 costs three seconds of it, and
    // neither can possibly succeed.
    //
    StageCell (1, 0x00FF0000);
    return FALSE;
  }

  //
  // The card should still be selected with RCA 1 and idle in TRAN, because LK
  // left it that way to load this very image.  If it is not, the rest of this
  // probe is meaningless and the line says so.
  //
  Status = SendCommand (13, 1u << 16, SDC_CMD_RSP_R1, 0);
  SayHex (SystemTable, L"INH C13 I=", Status.IntStatus);
  SayHex (SystemTable, L" R=", Status.Resp[0]);
  Say (SystemTable, L" st=");
  SayDec (SystemTable, (Status.Resp[0] >> 9) & 0xFu);
  Say (SystemTable, L"\r\n");

  StageCell (2, 0x00FFFFFF);

  Ext     = ReadDataBlock (8, 0, mDataBlock, 512);
  Sectors = (UINT32)mDataBlock[EXT_CSD_SECTOR_COUNT] |
            ((UINT32)mDataBlock[EXT_CSD_SECTOR_COUNT + 1] << 8) |
            ((UINT32)mDataBlock[EXT_CSD_SECTOR_COUNT + 2] << 16) |
            ((UINT32)mDataBlock[EXT_CSD_SECTOR_COUNT + 3] << 24);

  SayHex (SystemTable, L"INH C8 I=", Ext.IntStatus);
  SayHexN (SystemTable, L" rev=", mDataBlock[EXT_CSD_REV], 2);
  SayHexN (SystemTable, L" bw=", mDataBlock[EXT_CSD_BUS_WIDTH], 2);
  SayHexN (SystemTable, L" hs=", mDataBlock[EXT_CSD_HS_TIMING], 2);
  SayHex (SystemTable, L" sec=", Sectors);
  Say (SystemTable, L"\r\n");

  StageCell (3, Ext.Ok ? 0x0000FF00 : 0x00FFFF00);

  //
  // LBA 1 is the GPT header, and 45464920 is "EFI " in big-endian reading order
  // -- the same signature bootshim checks before it will write anything.
  //
  Gpt       = ReadDataBlock (17, 1, mDataBlock, 512);
  Signature = ((UINT32)mDataBlock[0] << 24) |
              ((UINT32)mDataBlock[1] << 16) |
              ((UINT32)mDataBlock[2] << 8) |
              (UINT32)mDataBlock[3];

  SayHex (SystemTable, L"INH B1 I=", Gpt.IntStatus);
  SayHex (SystemTable, L" S=", Signature);
  Say (SystemTable, L"\r\n");

  return (BOOLEAN)(Gpt.Ok && (Signature == 0x45464920u));
}

/**
  Prove that SDC_RESP0 tracks commands, instead of assuming that it does.

  Every reading in docs/POWER-BEFORE-AFTER.md that says "R1 = 0x00000900, so the
  card is in TRAN and accepted the command" depends on one unstated assumption:
  that RESP0 holds the answer to the command just sent.  If it were stale -- left
  over from MsdcDxe's last CMD13 -- then that R1 would be evidence of nothing, and
  the whole verdict table would need re-reading.  (docs/ANALYSIS-INTEN-GATED-POLL.md
  already flags the same suspicion for MSDC_CFG reading 0x02000000.)

  Two commands that cannot produce the same reply are the discriminator:

    a  = RESP0 after CMD13 (R1: card status)
    b0 = RESP0 after CMD10 (R2: the CID, whose low word is 3BB919C9 on this card)
    c  = RESP0 after CMD13 again

  IMG_5760 answered it: `PV pre=00000900 a=00000900 b0=00000000 c=00400900`.
  RESP0 followed CMD10 (b0 is not a), so the register is live and every R1 in
  this log can be believed.  Two details explain the rest:

    - CMD10 is only legal in STBY (JEDEC JESD84-B51: SEND_CID is not a TRAN
      command), so the card answered it with a 48-bit error response while the
      host was waiting for 136 bits: RESP0 ends up zero, not the CID.
    - ILLEGAL_COMMAND is latched in the card's status, so the *next* CMD13
      reports bit 22 -- `c` = 0x00400900 instead of 0x00000900.  That is a
      second, independent proof that RESP0 is being written per command.

  So the verdict is `b0 != a`, and `c != a` with bit 22 set is the confirmation
  rather than a contradiction.  One line of output, because the text area budget
  is the scarce resource here (see the header of ProbeCardAndMux).  `pre` is
  printed as context only: it is whatever the register held before this probe
  touched anything.
**/
STATIC
VOID
ProbeRespProvenance (
  IN EFI_SYSTEM_TABLE  *SystemTable,
  IN UINT32             Rca
  )
{
  CARD_RESP  First;
  CARD_RESP  Cid;
  CARD_RESP  Second;
  UINT32     Pre;
  BOOLEAN    Live;

  Pre    = Msdc0Read32 (SDC_RESP0_OFS);
  First  = SendCommand (13, Rca << 16, SDC_CMD_RSP_R1, 0);
  Cid    = SendCommand (10, Rca << 16, SDC_CMD_RSP_R2, 0);
  Second = SendCommand (13, Rca << 16, SDC_CMD_RSP_R1, 0);

  Live = (BOOLEAN)(Cid.Resp[0] != First.Resp[0]);

  Say (SystemTable, L"PV ");
  SayHex (SystemTable, L"pre=", Pre);
  SayHex (SystemTable, L" a=", First.Resp[0]);
  SayHex (SystemTable, L" b0=", Cid.Resp[0]);
  SayHex (SystemTable, L" c=", Second.Resp[0]);
  Say (SystemTable, Live ? L" LIVE\r\n" : L" STALE\r\n");
}

/**
  Walk the card up to TRAN by hand and then read from it: CMD0, CMD1 until
  ready, CMD2, CMD3, CMD7, CMD13, and -- past TRAN -- CMD16, CMD8 (EXT_CSD) and
  CMD17 (the GPT header at LBA 1).

  Two rounds of evidence feed this.  IMG_5747 was the first reply this hardware
  ever gave: `CMD0` then `CMD1` answered, because the card was not in idle state
  and had been ignoring CMD1 as an illegal command.  IMG_5750 is the answer to
  that:

      C1 n=1 I=00000100 R=C0FF8080

  OCR bit 31 set, so the card finished powering up on the first poll and the
  sequence went on to CMD2 -- except nothing after C1 appeared.  Not a failure:
  the console's text area is 68 rows, its own self-test leaves the cursor at row
  53, and this driver had 15 lines to say 21 things, so CMD2..CMD13 were printed
  past the last row and lost.  Hence the compression above (power on one line,
  H1 and CMD12 gone, no PING line) and hence the shape of the log below.

  What each line means:

      C0  I=..  cl=.. f=.. l=.. kc=..   transmitter self-check, CMD0 has no reply
      C1  n=..  I=..  R=........        CMD1 attempts until OCR bit 31
      C2  3=.. 2=.. 1=.. 0=..           the 128-bit CID (R2)
      C3  ..    C7  ..                  R6 (RCA 1 accepted), R1b (selected)
      C13 I=..  R=..  st=..             CURRENT_STATE, 4 = TRAN
      C6  I=../.. n=.. st=..            CMD6 back to 1-bit / default speed
      C8  I=..  sz=.. rv=.. bw=.. hs=.. EXT_CSD: capacity, revision, bus, timing
      B1  I=..  S=..                    LBA 1's first four bytes, 45464920 = "EFI "

  IMG_5752 is the round that separated "commands work" from "data does not":
  C13 st=4, a full CID, and then `C8 I=00000100` -- CMDRDY and nothing else, no
  XFER_COMPL and no DATTMO -- with B1 reading zeroes.  The card had been left in
  8-bit HS200 by LK (EXT_CSD 183/185 are card configuration and survive CMD0)
  while the host runs 1-bit default speed, and the command word that was
  supposed to start the transfer had its data-type field at bit 9 instead of
  bit 11.  Both are fixed above and in SDC_CMD_DTYPE_SHIFT; the C6 line is the
  switch back down, and a failed C8 is followed by a CMD12 so the card does not
  stay stuck holding DAT0 low.

  `st=4` plus `C8 I=...100` plus `B1 S=45464920` is the whole storage stack the
  UEFI side needs: commands, long responses, a 512-byte data read and a sector
  address.  Anything less names the step that broke, which is the next thing to
  port into MsdcDxe/EmmcDxe.
**/
STATIC
VOID
ProbeCardAndMux (
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT32           Strap;
  UINT32           Mode;
  CARD_CMD_RESULT  Cmd0;
  CARD_RESP        Ready;
  CARD_RESP        Cid;
  CARD_RESP        R6;
  CARD_RESP        Sel;
  CARD_RESP        Status;
  DATA_RESULT      Gpt;
  UINTN            Tries;
  UINT32           Rca;
  UINT32           Signature;
  BOOLEAN          Worked;

  Strap = *(volatile UINT32 *)(UINTN)PINCTRL_STRAP_ADDR;
  Mode  = ((Strap & (1u << PINCTRL_STRAP_BIT)) != 0) ? 1 : 2;

  //
  // Write the mux the vendor's way once.  It has read back 0x22222200 and
  // survived a reboot in every round since IMG_5744, so its readback is no
  // longer printed -- rows again; docs/POWER-BEFORE-AFTER.md keeps the number.
  //
  WriteEmmcPinMode (Mode);

  //
  // IMG_5757: the clock pad (124) is the only pin that moves during the data
  // wait -- gp=DF8/DFF, bit 2 is the CLK -- while the card answers CMD8 with
  // R1 = 0x00000900 (TRAN, READY_FOR_DATA) and then sends nothing.  The card and
  // its pads are known good in this boot because LK loaded this image through
  // them, so the difference has to be host state, and the one piece of host
  // state a failed transfer can wedge is the controller.  Reset it before the
  // sequence instead of inheriting MsdcDxe's wreckage.
  //
  ResetHost ();

  //
  // CMD0 first: no response, no argument, only CMD and CLK needed.  Its pin
  // trace is the transmitter self-check -- CMD0's frame is almost all zeros,
  // so a healthy one reads as "about 100 samples low, all in one run".  The
  // strips keep that measurement in pixels now that the line is shorter.
  //
  // CFG after the reset rides on this line rather than getting one of its own:
  // the text area has 68 rows and this driver has about 14 of them, so every new
  // line has to be paid for (see ProbeRespProvenance for the line that needed
  // one).
  //
  Cmd0 = SendCommandSampled (0, 0, 0);
  SayHex (SystemTable, L"C0 I=", Cmd0.IntStatus);
  SayHex (SystemTable, L" C=", Msdc0Read32 (MSDC_CFG_OFS));
  SayCmdTrace (SystemTable, &Cmd0);
  PaintBitStrip (BEACON_STRIP_CMD_AND, 32, Cmd0.PsAnd, 0x00FFFFFF);
  PaintBitStrip (BEACON_STRIP_CMD_OR,  32, Cmd0.PsOr,  0x0000FFFF);

  //
  // CMD1 until OCR bit 31 (power-up done).  ~1 ms between attempts, 300
  // attempts = 0.3 s, far more than a card that was already initialized once.
  //
  Tries = 0;
  do {
    Ready = SendCommand (1, CMD1_OCR, SDC_CMD_RSP_R3, 0);
    Tries++;
    gBS->Stall (1000);
  } while (Ready.Answered && ((Ready.Resp[0] & 0x80000000u) == 0) && (Tries < 300));

  Say (SystemTable, L"C1 n=");
  SayDec (SystemTable, Tries);
  SayResp (SystemTable, L" I=", &Ready);

  if (!Ready.Answered || ((Ready.Resp[0] & 0x80000000u) == 0)) {
    Say (SystemTable, L"CARD NOT READY\r\n");
    return;
  }

  //
  // CID: 128-bit R2, printed as four words.  This also proves a long response
  // can be received, which is the one thing CMD1 never exercises.
  //
  Cid = SendCommand (2, 0, SDC_CMD_RSP_R2, 0);
  Say (SystemTable, L"C2 ");
  SayHex (SystemTable, L"3=", Cid.Resp[3]);
  SayHex (SystemTable, L" 2=", Cid.Resp[2]);
  SayHex (SystemTable, L" 1=", Cid.Resp[1]);
  SayHex (SystemTable, L" 0=", Cid.Resp[0]);
  Say (SystemTable, L"\r\n");

  //
  // RCA 1 is what LK uses on this board; any value works, but matching the
  // inherited one keeps the log comparable with RESP0's earlier 0xB00.
  //
  Rca  = 1;
  R6   = SendCommand (3, Rca << 16, SDC_CMD_RSP_R1, 0);
  Sel  = SendCommand (7, Rca << 16, SDC_CMD_RSP_R1B, 0);

  Say (SystemTable, L"C3 ");
  SayHex (SystemTable, L"I=", R6.IntStatus);
  SayHex (SystemTable, L" R=", R6.Resp[0]);
  Say (SystemTable, L" C7 ");
  SayHex (SystemTable, L"I=", Sel.IntStatus);
  SayHex (SystemTable, L" R=", Sel.Resp[0]);
  Say (SystemTable, L"\r\n");

  Status = SendCommand (13, Rca << 16, SDC_CMD_RSP_R1, 0);
  Say (SystemTable, L"C13 ");
  SayHex (SystemTable, L"I=", Status.IntStatus);
  SayHex (SystemTable, L" R=", Status.Resp[0]);
  Say (SystemTable, L" st=");
  SayDec (SystemTable, (Status.Resp[0] >> 9) & 0xFu);
  Say (SystemTable, L"\r\n");

  if (((Status.Resp[0] >> 9) & 0xFu) != 4) {
    Say (SystemTable, L"NOT IN TRAN\r\n");
    return;
  }

  //
  // Before trusting a single R1 in the rest of this log, prove that RESP0 follows
  // commands.  This is the one measurement the whole verdict table rests on, and
  // it costs one line.
  //
  ProbeRespProvenance (SystemTable, Rca);

  //
  // Past TRAN the card is addressable -- but addressable is not the same as
  // ready to move data.  IMG_5752 and IMG_5753 both show the same shape: every
  // command answers, the data phase never starts (`I=00000100`, no XFER_COMPL,
  // no DATTMO -- and at 400 kHz the controller's DTOC is ~11 minutes, so a
  // missing DATTMO inside three seconds means nothing yet).  IMG_5753 also
  // shows the CMD6 switches being accepted (I=00000100 twice, st=4 after n=1),
  // so "the card ignored the switch" is not it either.
  //
  // IMG_5754 answers it, and the answer is 'neither side is sending': all three
  // attempts read ps=EF/FF -- the controller's DAT[7:0] stayed high except for
  // DAT4, which merely wobbled -- while commands, CID and selection kept
  // working.  IMG_5757 then put a second window on the same pins: gp=DF8/DFF
  // says only CMD, CLK and one neighbour moved at all during the whole wait, and
  // every attempt returned R1 = 0x00000900, i.e. the card is in TRAN with
  // READY_FOR_DATA and reports no error -- right before declining to send.
  //
  // A card that is happy and silent, on a bus whose clock pad is powered down
  // (MSDC_CFG.CKPDN set in H0 C=), has one obvious explanation: it never gets
  // clocked, so it never shifts a bit, and the controller never counts its data
  // timeout either.  That is what k= measures and what D8 changes.
  //
  // Four attempts, each a complete "make host and card agree, read a block"
  // cycle, so one image covers the plausible modes instead of one flash per
  // guess:
  //
  //   A8  host 8-bit, card as LK left it (8-bit, HS200) -- what bootshim does
  //   B8  host 8-bit, card HS_TIMING = 0 (default speed)
  //   C8  host 1-bit, card BUS_WIDTH = 0 (+ the HS_TIMING from B8)
  //
  // The first one that works stops the ladder, and then CMD17 reads LBA 1 --
  // the GPT header -- with the mode that just worked.
  //
  //
  // One attempt, not the four-round ladder B8/C8/D8 used to be.  IMG_5760 is
  // that ladder's answer: A8, B8, C8 and D8 all came back byte-identical --
  // `I=0100 sz=00000000 ps=EF/FF gp=DF8/DFF f=00 k=14999 c6=.. t=4 r=1` -- so
  // the bus mode is not the variable, and each rung cost a line of a fifteen-line
  // budget to say so.  The variable it did NOT cover is the one InheritProbe now
  // tests: whether re-initialising the card at all is the mistake.  A8 stays as
  // the control, because it is the mode that matches what LK left behind.
  //
  // `k=14999` is also the end of the CKPDN theory this ladder was built on:
  // that field counts CLK-pad-low samples over the whole wait, so half the
  // samples saw the clock pad low, i.e. the clock was running the entire time
  // the card declined to send.  D8 cleared MSDC_CFG[1] and changed nothing.
  //
  Worked = ExtCsdAttempt (SystemTable, L"A8 ", Rca, 8, FALSE, 0, 0);

  //
  // A failed data phase leaves the card mid-transfer with DAT0 held low; LK
  // sends CMD12 after every failed transfer and MediaTek's driver never did,
  // which is why the round after a failure looked like a dead card
  // (docs/ANALYSIS-EMMC-DATA-PATH.md section 1.2).
  //
  if (!Worked) {
    (VOID) SendCommand (12, 0, SDC_CMD_RSP_R1B, SDC_CMD_STOP_CMD);
    Say (SystemTable, L"B1 -  no data phase\r\n");
    return;
  }

  Gpt       = ReadDataBlock (17, 1, mDataBlock, 512);
  Signature = ((UINT32)mDataBlock[0] << 24) |
              ((UINT32)mDataBlock[1] << 16) |
              ((UINT32)mDataBlock[2] << 8) |
              (UINT32)mDataBlock[3];

  Say (SystemTable, L"B1 ");
  SayHex (SystemTable, L"I=", Gpt.IntStatus);
  SayHex (SystemTable, L" S=", Signature);
  Say (SystemTable, L"\r\n");
}

/**
  Read-only probe of what MsdcDxe left in the MSDC0 controller, plus one
  bounded CMD12 to put a card stuck mid-transfer back into TRAN.

  Evidence this answers (2026-10-29, IMG_5727):

      ddn data: cmd 8 failed: status=0x12      <-- EFI_TIMEOUT
      ddn data: cmd 8 r1=00000900 len=512      <-- but R1 says TRAN (state 4)
      ddn data: stall int=00000000             <-- no interrupt bit at all

  A command that reports timeout yet leaves a valid R1 in RESP0 means the
  completion is not being observed.  Since the controller's own data timeout
  would set DATTMIO regardless, an all-zero INT suggests the status register is
  not the one being polled, is masked, or the block is clock-gated.

  Everything else here is read-only: a read cannot change card state, start a
  transfer, or clear an interrupt (INT is read, not written back).  The one
  write is the bounded CMD12 at the end.

  No stage cells either.  The previous version painted cells 20 and 21 as
  "probe started" / "probe finished", but those numbers are MsdcDxe's CMD10 and
  CMD8 (SEND_EXT_CSD) markers -- and this driver runs *after* MsdcDxe, so the
  paint erased the very evidence the cells exist for.  Text only.

  Graphics belong to ProbePower(), whose strips also carry their own
  "reached this point" information.
**/
STATIC
VOID
ProbeMsdcLeftovers (
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  volatile UINT32  *Regs;
  UINT32           Cfg;
  CHAR16           Text[10];

  //
  // Read BOTH controllers.  A host that is not in use keeps its reset values:
  // MSDC_CFG without MODE|CCKPD|PIO, and MSDC_INTEN at the reset mask.  The
  // previous single-host snapshot read CFG=0x02000000 (no BIT3), which is
  // exactly the signature of "this is not the host driving the eMMC".
  //
  Regs = (volatile UINT32 *)(UINTN)MSDC0_MMIO_BASE;
  Cfg  = Regs[MSDC_CFG_OFS / 4];

  //
  // One line instead of three: the values have not moved since IMG_5727, and the
  // text area has a fixed row budget (see ProbeCardAndMux).  H1 is gone for the
  // same reason -- it is the SD slot and has read its reset values every round.
  //
  Say (SystemTable, L"H0 C=");
  HexString (Cfg, Text);
  Say (SystemTable, Text);
  Say (SystemTable, L" S=");
  HexString (Regs[SDC_CFG_OFS / 4], Text);
  Say (SystemTable, Text);
  Say (SystemTable, L" P=");
  HexString (Regs[MSDC_PS_OFS / 4], Text);
  Say (SystemTable, Text);
  Say (SystemTable, L" N=");
  HexString (Regs[SDC_BLK_NUM_OFS / 4], Text);
  Say (SystemTable, Text);
  Say (SystemTable, L"\r\n");

  //
  // And one more line for the things that would make a data phase look dead
  // while every command works: the pads' input enables, whether the GPIO block
  // is driving the bus instead of the controller, and the three controller
  // registers the vendor driver touches that this probe has inherited.
  //
  Say (SystemTable, L"IO ");
  SayHexN (SystemTable, L"dir=", PinGroupBits (PINCTRL_DIR_OFS), 3);
  SayHexN (SystemTable, L" do=", PinGroupBits (PINCTRL_DOUT_OFS), 3);
  //
  // Deliberately not the MSDC0 pad block at 0x10002000: it is outside this
  // platform's memory map, and reading it is a translation fault at the third
  // level, which is what IMG_5755 is -- the DxeCore exception dump with
  // X8 = 0x10002010.  The pad's input-enable mask is therefore inferred from
  // the GPIO window (gp=) instead of read.
  //
  SayHex (SystemTable, L" din=", PinGroupBits (PINCTRL_DIN_OFS));
  Say (SystemTable, L"\r\n");
}

/**
  Connect every handle in the system, recursively -- what BDS would have done.

  Three passes with a short stall between them, because each pass can create
  handles that the next one needs: pass 1 binds EmmcDxe (BlockIo appears),
  pass 2 binds PartitionDxe (child handles appear), pass 3 binds FatPkg
  (SimpleFileSystem appears).
**/
STATIC
VOID
ConnectEverything (VOID)
{
  EFI_HANDLE  *Handles;
  UINTN       Count;
  UINTN       Index;
  UINTN       Pass;

  for (Pass = 0; Pass < 3; Pass++) {
    Handles = NULL;
    Count   = 0;
    if (EFI_ERROR (gBS->LocateHandleBuffer (AllHandles, NULL, NULL, &Count, &Handles))) {
      return;
    }

    for (Index = 0; Index < Count; Index++) {
      gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
    }

    gBS->FreePool (Handles);
    gBS->Stall (100 * 1000);
  }
}

STATIC
UINTN
CountProtocol (
  IN EFI_GUID  *Protocol
  )
{
  EFI_HANDLE  *Handles;
  UINTN       Count;

  Handles = NULL;
  Count   = 0;
  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, Protocol, NULL, &Count, &Handles))) {
    return 0;
  }

  gBS->FreePool (Handles);
  return Count;
}

EFI_STATUS
EFIAPI
StorageBeaconDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HANDLE  *Passthru;
  UINTN       PassthruCount;
  UINTN       BlockDevices;
  UINTN       Volumes;
  UINTN       Count;
  UINTN       Index;
  EFI_HANDLE  *Handles;
  UINT32      PowerVerdict;
  BOOLEAN     Inherited;

  PowerVerdict = 0x00FF00FF;

  //
  // FIRST, before anything else in this driver or in the FV touches MSDC0:
  // inherit the controller and the card exactly as LK handed them over and try
  // one read.  This is the one experiment that had not been run, and it is the
  // configuration line A proved works on this phone (bootshim/msdc.c, and
  // docs/EMMC-LOG.md for the verified result).  See InheritProbe().
  //
  Inherited = InheritProbe (SystemTable);

  //
  // The older register snapshot does one bounded CMD12 to put a card stuck
  // mid-transfer back into TRAN.  That is exactly the wrong thing to do to a
  // card that just proved it can read: CMD12 with nothing in flight latches an
  // error bit in the card's status, and the next command's R1 then carries it.
  // So it only runs when the inherit path failed and there is nothing to
  // protect.  Text-only either way.
  //
  if (!Inherited) {
    ProbeMsdcLeftovers (SystemTable);
  }

  //
  // The band, cell 6 and the header: reads MSDC_PS before it draws a single
  // pixel.  Read-only, so it is safe on a working bus.
  //
  ProbePower (SystemTable, &PowerVerdict);

  //
  // Last diagnostic: ping the card (CMD1) and, if it stays silent, try the
  // other pin-mux mode.  This runs before the connect sweep on purpose -- if a
  // mode change wakes the card up, EmmcDxe gets a working bus in this same boot.
  //
  // Skipped entirely when the inherit path above read the GPT: this probe
  // starts with CMD0 and a controller reset, and throwing away the one working
  // configuration this board has ever had, in the same boot that found it,
  // would be a strange thing to do.
  //
  if (Inherited) {
    Say (SystemTable, L"INHERIT OK\r\n");
  } else {
    ProbeCardAndMux (SystemTable);
  }

  Passthru      = NULL;
  PassthruCount = 0;
  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSdMmcPassThruProtocolGuid, NULL, &PassthruCount, &Passthru))) {
    PassthruCount = 0;
  } else {
    gBS->FreePool (Passthru);
  }

  ConnectEverything ();

  BlockDevices = CountProtocol (&gEfiBlockIoProtocolGuid);
  //
  // The count is the whole diagnostic now.  One BlockIo handle means our own
  // whole-disk media and nothing on top of it, i.e. PartitionDxe never bound;
  // more than one means the GPT was parsed and the partitions exist.  Without
  // the number, "BLOCK OK / FAT NONE" cannot be told apart from "BLOCK OK and
  // this disk genuinely has no FAT volume on it" -- which is where this phone
  // is: every Android partition here is ext4.
  //
  if (BlockDevices > 0) {
    PaintBand (0x008000FF);
    StageCell (4, 0x008000FF);
    PaintBanner (1100, 0x008000FF);
    Say (SystemTable, L"BLOCK OK n=");
    SayDec (SystemTable, BlockDevices);
    Say (SystemTable, L"\r\n");
  } else {
    StageCell (7, 0x00FF0000);
    PaintBanner (1100, 0x00FF0000);
    Say (SystemTable, L"BLOCK NONE\r\n");
  }

  PaintDots (BEACON_DOTS_TOP, 8, BlockDevices, 0x008000FF);

  //
  // Count mounted filesystems: OpenVolume is what proves a real FAT, not just a
  // SimpleFileSystem handle.
  //
  Volumes = 0;
  Count   = 0;
  Handles = NULL;
  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &Count, &Handles))) {
    for (Index = 0; Index < Count; Index++) {
      EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
      EFI_FILE_PROTOCOL                *Root;

      Fs   = NULL;
      Root = NULL;
      if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs))) {
        continue;
      }

      if (!EFI_ERROR (Fs->OpenVolume (Fs, &Root))) {
        Volumes++;
        Root->Close (Root);
      }
    }

    gBS->FreePool (Handles);
  }

  if (Volumes > 0) {
    PaintBand (0x0000FF00);
    StageCell (5, 0x0000FF00);
    PaintBanner (1140, 0x0000FF00);
    Say (SystemTable, L"FAT OK\r\n");
  } else {
    PaintBanner (1140, 0x00FF0000);
    Say (SystemTable, L"FAT NONE\r\n");
  }

  PaintDots (BEACON_DOTS_TOP, BEACON_DOTS_VOLUMES_X, Volumes, 0x0000FF00);

  //
  // The y 40 band is the one channel that survives everything, so it now
  // carries this round's answer: why storage is blocked.  A filesystem wins
  // over it (green means M4 is done); otherwise the colour is the power
  // verdict from ProbePower().
  //
  if (Volumes == 0) {
    PaintBand (PowerVerdict);
  }

  //
  // Deliberately keep running with the connected handles in place: BDS and
  // UniXNU.efi later read gST->ConOut / the same handles, and leaving them
  // connected is what a normal platform looks like after ConnectAll.
  //
  return EFI_SUCCESS;
}
// (power probe: P_before / MT6357 VEMC / P_after)
