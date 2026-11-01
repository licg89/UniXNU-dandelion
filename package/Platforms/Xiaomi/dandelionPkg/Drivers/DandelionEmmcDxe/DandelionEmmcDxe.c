/** @file
  dandelion eMMC storage: inherit LK's MSDC0 state and publish BlockIo.

  This driver exists because of one measurement and one piece of existing code.

  The measurement is IMG_5760: with the controller reset and the card walked up
  from IDLE again (what MsdcDxe does), every command answers and no data ever
  arrives -- `I=0100` (CMDRDY alone), `ps=EF/FF`, `k=14999`.  Four bus modes
  produced byte-identical lines.

  The existing code is bootshim/msdc.c, which reads *and writes* this same
  controller on this same phone from bare metal (docs/EMMC-LOG.md: 5159 bytes
  written into `flashinfo` and read back verified).  Its header states the policy
  this driver follows:

      "LK has just loaded *this image* through MSDC0, so the controller is
       clocked, the pads are muxed, the card is selected with RCA 1 and it is in
       the transfer state.  We therefore inherit rather than re-initialise: no
       CMD0, no CMD1, no re-tuning."
      "CKMOD and SDC_CFG.BUSWIDTH are left exactly as LK set them, because the
       card's own bus width lives in EXT_CSD[183] and the two must keep keeping
       matching."

  IMG_5762 is the result: `INH C8 I=00001100 rev=08 bw=06 hs=01 sec=0747C000`,
  i.e. XFER_COMPL, eMMC 5.1, **8-bit DDR**, high speed, 122,159,104 sectors
  (62.5 GB), and `INH B1 S=45464920` = "EFI " read from LBA 1.  The card was in
  8-bit DDR the whole time; the host had been setting itself to 1 bit.

  So: inherit, slow the clock down until a read verifies, keep BUSWIDTH, and
  expose the result as BlockIo.  Nothing here is written from scratch -- the
  register sequences, the command words and the R1 masks are transcribed from
  bootshim/msdc.c.

  Copyright (c) Microsoft Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>

//
// MSDC0 on MT6765/MT6762G.  Offsets and field positions: bootshim/msdc.c:39-121
// and docs/research/mtk-msdc-pio.md section 1.2.
//
#define MSDC0_BASE          0x11230000ULL
//
// The console framebuffer, so this driver can paint one progress bar without
// needing GOP or ConOut (neither of which exists when it runs).
//
#define MSDC0_BASE_FB       0x7EC50000ULL
#define R_CFG               0x000u
#define R_PS                0x008u
#define R_INT               0x00Cu
#define R_INTEN             0x010u
#define R_FIFOCS            0x014u
#define R_TXDATA            0x018u
#define R_RXDATA            0x01Cu
#define R_SDC_CFG           0x030u
#define R_SDC_CMD           0x034u
#define R_SDC_ARG           0x038u
#define R_SDC_STS           0x03Cu
#define R_RESP0             0x040u
#define R_BLK_NUM           0x050u

#define CFG_MODE            (1u << 0)
#define CFG_PIO             (1u << 3)
#define CFG_CKSTB           (1u << 7)
#define CFG_CKDIV(d)        (((UINT32)(d) & 0xFFFu) << 8)
#define CFG_CKDIV_MASK      (0xFFFu << 8)

#define STS_SDCBUSY         (1u << 0)
#define STS_CMDBUSY         (1u << 1)

#define INT_CMDRDY          (1u << 8)
#define INT_CMDTMO          (1u << 9)
#define INT_RSPCRCERR       (1u << 10)
#define INT_XFER_COMPL      (1u << 12)
#define INT_DATTMO          (1u << 14)
#define INT_DATCRCERR       (1u << 15)
#define INT_CMD_ALL         (INT_CMDRDY | INT_CMDTMO | INT_RSPCRCERR)
#define INT_DAT_ALL         (INT_XFER_COMPL | INT_DATTMO | INT_DATCRCERR)

#define FIFOCS_CLR          (1u << 31)
#define FIFOCS_RXCNT(x)     ((x) & 0xFFu)
#define FIFOCS_TXCNT(x)     (((x) >> 16) & 0xFFu)
#define FIFO_DEPTH          128u

#define SDC_DTOC_MASK       (0xFFu << 24)
#define SDC_WRDTOC_MASK     (0x1FFFu << 2)

//
// Command words.  SDC_CMD is
//   [5:0] opcode  [9:7] response type  [12:11] data type (1 = single block,
//   2 = multiple block)  [13] direction (1 = write)  [14] stop  [27:16] length
// -- the layout bootshim/msdc.c encodes in its CMDW() macro.
//
#define CMDW(opc, rsp, dtype, rw, blk) \
  ((UINT32)(opc) | ((UINT32)(rsp) << 7) | ((UINT32)(dtype) << 11) | \
   ((UINT32)(rw) << 13) | ((UINT32)(blk) << 16))

#define CMD6_W              CMDW (6,  7, 0, 0, 0)      /* R1b               */
#define CMD8_W              CMDW (8,  1, 1, 0, 512)    /* read 512 B        */
#define CMD12_W             CMDW (12, 7, 0, 0, 0)      /* R1b + stop bit    */
#define CMD13_W             CMDW (13, 1, 0, 0, 0)      /* R1                */
#define CMD17_W             CMDW (17, 1, 1, 0, 512)    /* read one block    */
#define CMD18_W             CMDW (18, 1, 2, 0, 512)    /* read many         */
#define CMD24_W             CMDW (24, 1, 1, 1, 512)    /* write one         */
#define CMD25_W             CMDW (25, 1, 2, 1, 512)    /* write many        */
#define CMD12_STOP          (1u << 14)

#define R1_READY_FOR_DATA   (1u << 8)
#define R1_STATE(s)         (((s) >> 9) & 0xFu)
#define R1_STATE_TRAN       4u
#define R1_FATAL_MASK       0xFFFFA080u

#define MMC_RCA             1u
#define EXT_CSD_REV         192u
#define EXT_CSD_BUS_WIDTH   183u
#define EXT_CSD_HS_TIMING   185u
#define EXT_CSD_SEC_COUNT   212u

#define BLOCK_SIZE          512u
#define CHUNK_BLOCKS        128u           /* 64 KiB per CMD18/CMD25 */

//
// Divider candidates, fastest first.  LK ran CKDIV=1 at 200 MHz with CKMOD=2
// (inherited CFG was 0x02200199); nobody here knows which sampling point this
// board needs, so the driver calibrates: slow down until a read verifies.
// bootshim hard-codes 260 (~385 kHz) because it only ever has to write 5 KiB.
//
#define DDN_DIVIDERS        4
STATIC CONST UINT32  mDividers[DDN_DIVIDERS] = { 26, 52, 104, 260 };

STATIC UINT32  mDivider;
STATIC UINT32  mSectors;
STATIC UINT8   *mBounce;

STATIC EFI_BLOCK_IO_MEDIA  mMedia;
STATIC EFI_BLOCK_IO_PROTOCOL mBlockIo;

STATIC CONST EFI_GUID  mVendorGuid = {
  0x8E9C1234, 0x4B7A, 0x4C21, { 0x9A, 0x11, 0xDA, 0x9D, 0x0E, 0x11, 0x00, 0x02 }
};

#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH        Vendor;
  EFI_DEVICE_PATH_PROTOCOL  End;
} DDN_DEVICE_PATH;
#pragma pack()

STATIC DDN_DEVICE_PATH  mDevicePath = {
  { { HARDWARE_DEVICE_PATH, HW_VENDOR_DP, { (UINT8)sizeof (VENDOR_DEVICE_PATH), 0 } },
    { 0x8E9C1234, 0x4B7A, 0x4C21, { 0x9A, 0x11, 0xDA, 0x9D, 0x0E, 0x11, 0x00, 0x02 } } },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, { 4, 0 } }
};

STATIC
UINT32
EmmcR (
  IN UINT32  Offset
  )
{
  return *(volatile UINT32 *)(UINTN)(MSDC0_BASE + Offset);
}

STATIC
VOID
EmmcW (
  IN UINT32  Offset,
  IN UINT32  Value
  )
{
  *(volatile UINT32 *)(UINTN)(MSDC0_BASE + Offset) = Value;
}

STATIC
BOOLEAN
WaitStsClear (
  IN UINT32  Mask,
  IN UINTN   MilliSeconds
  )
{
  UINTN  Index;

  for (Index = 0; Index < MilliSeconds; Index++) {
    if ((EmmcR (R_SDC_STS) & Mask) == 0) {
      return TRUE;
    }

    gBS->Stall (1000);
  }

  return (EmmcR (R_SDC_STS) & Mask) == 0;
}

STATIC
BOOLEAN
FifoClear (
  VOID
  )
{
  UINTN  Index;

  EmmcW (R_FIFOCS, EmmcR (R_FIFOCS) | FIFOCS_CLR);
  for (Index = 0; Index < 20; Index++) {
    if ((EmmcR (R_FIFOCS) & FIFOCS_CLR) == 0) {
      return TRUE;
    }

    gBS->Stall (1000);
  }

  return FALSE;
}

/**
  Program the divider and wait for the clock to be stable again.

  Everything else in MSDC_CFG is preserved on purpose: CKMOD and CCKPD are LK's,
  and bootshim/msdc.c:691-702 is explicit that inheriting them is the point.
**/
STATIC
BOOLEAN
SetClock (
  IN UINT32  Divider
  )
{
  UINTN  Index;

  EmmcW (R_CFG, (EmmcR (R_CFG) & ~CFG_CKDIV_MASK) | CFG_MODE | CFG_PIO | CFG_CKDIV (Divider));

  for (Index = 0; Index < 20; Index++) {
    if ((EmmcR (R_CFG) & CFG_CKSTB) != 0) {
      return TRUE;
    }

    gBS->Stall (1000);
  }

  return FALSE;
}

/**
  Issue one command and collect RESP0.  bootshim/msdc.c:357-391.
**/
STATIC
BOOLEAN
MsdcCmd (
  IN  UINT32  CmdW,
  IN  UINT32  Arg,
  OUT UINT32  *R1 OPTIONAL
  )
{
  UINT32  Opcode;
  UINT32  Status;
  UINTN   Index;

  Opcode = CmdW & 0x3Fu;

  //
  // CMD12 and CMD13 are allowed to run while DAT0 is busy; everything else
  // needs the whole controller idle.
  //
  if (!WaitStsClear (((Opcode == 12) || (Opcode == 13)) ? STS_CMDBUSY : STS_SDCBUSY, 100)) {
    return FALSE;
  }

  EmmcW (R_INT, 0xFFFFFFFFu);     /* W1C: drop stale flags */
  EmmcW (R_SDC_ARG, Arg);
  EmmcW (R_SDC_CMD, CmdW);

  for (Index = 0; Index < 200; Index++) {
    Status = EmmcR (R_INT) & INT_CMD_ALL;
    if (Status != 0) {
      EmmcW (R_INT, Status);
      if ((Status & (INT_CMDTMO | INT_RSPCRCERR)) != 0) {
        return FALSE;
      }

      if (R1 != NULL) {
        *R1 = EmmcR (R_RESP0);
      }

      return TRUE;
    }

    gBS->Stall (1000);
  }

  return FALSE;
}

/**
  One data phase, read or write, polled PIO.  bootshim/msdc.c:434-521 / 524-610.
**/
STATIC
BOOLEAN
DataXfer (
  IN UINT32   CmdW,
  IN UINT32   Arg,
  IN UINT8    *Buffer,
  IN UINT32   Blocks,
  IN BOOLEAN  Write
  )
{
  UINT32  R1;
  UINT32  Status;
  UINT32  Left;
  UINT32  Off;
  UINT32  Count;
  UINT32  Word;
  UINT32  Step;
  UINT32  Burst;
  UINTN   Millis;
  BOOLEAN Done;
  BOOLEAN Ok;

  Done = FALSE;
  Ok   = FALSE;

  if (!WaitStsClear (STS_SDCBUSY, 100)) {
    return FALSE;
  }

  if (!FifoClear ()) {
    return FALSE;
  }

  EmmcW (R_BLK_NUM, Blocks);
  EmmcW (R_INT, 0xFFFFFFFFu);
  if ((EmmcR (R_CFG) & CFG_PIO) == 0) {
    EmmcW (R_CFG, EmmcR (R_CFG) | CFG_PIO);
  }

  if (!MsdcCmd (CmdW, Arg, &R1)) {
    goto Stop;
  }

  if ((R1 & R1_FATAL_MASK) != 0) {
    goto Stop;
  }

  Left = Blocks * BLOCK_SIZE;
  Off  = 0;

  for (Millis = 0; Millis < 3000; Millis++) {
    Status = EmmcR (R_INT) & INT_DAT_ALL;
    if (Status != 0) {
      EmmcW (R_INT, Status);
      if ((Status & (INT_DATTMO | INT_DATCRCERR)) != 0) {
        goto Stop;
      }

      if ((Status & INT_XFER_COMPL) != 0) {
        Done = TRUE;
      }
    }

    if (Done && (Left == 0) && !Write) {
      Ok = TRUE;
      break;
    }

    if (Write) {
      //
      // MediaTek's own policy: refill only when the FIFO is empty.
      //
      if ((Left != 0) && (FIFOCS_TXCNT (EmmcR (R_FIFOCS)) == 0)) {
        Burst = (Left < FIFO_DEPTH) ? Left : FIFO_DEPTH;
        for (Step = 0; Step < Burst; Step += 4) {
          CopyMem (&Word, Buffer + Off, 4);
          EmmcW (R_TXDATA, Word);
          Off  += 4;
        }

        Left -= Burst;
      }

      if (Done && (Left == 0) && (FIFOCS_TXCNT (EmmcR (R_FIFOCS)) == 0)) {
        Ok = TRUE;
        break;
      }
    } else {
      for ( ; ; ) {
        Count = FIFOCS_RXCNT (EmmcR (R_FIFOCS));
        if ((Count == 0) || (Left == 0)) {
          break;
        }

        if (Count > Left) {
          Count = Left;
        }

        if ((Count & 3u) == 0) {
          for (Step = 0; Step < Count; Step += 4) {
            Word = EmmcR (R_RXDATA);
            CopyMem (Buffer + Off, &Word, 4);
            Off += 4;
          }
        } else {
          for (Step = 0; Step < Count; Step++) {
            Buffer[Off++] = (UINT8)EmmcR (R_RXDATA);
          }
        }

        Left -= Count;
      }
    }

    gBS->Stall (1000);
  }

Stop:
  if (Blocks > 1) {
    (VOID)MsdcCmd (CMD12_W | CMD12_STOP, 0, NULL);
  }

  if (!Ok) {
    (VOID)FifoClear ();
    EmmcW (R_INT, 0xFFFFFFFFu);
  }

  return Ok;
}

//
// A one-pixel-per-something progress bar for reads, because the interesting
// question right now is not "does BlockIo work" (it does) but "how far did the
// 14.8 MB kernel read get before the machine stopped making progress".
//
// y 1092..1098 is between DandelionConsoleDxe's text area (rows 0..67, i.e.
// y 0..1087) and the banner strip at 1100, so nothing overwrites it and it does
// not cost a line of the scarce text area.
//
#define EMMC_BAR_TOP     1092u
#define EMMC_BAR_BOTTOM  1099u
#define EMMC_BAR_WIDTH   720u
#define EMMC_BAR_PIXELS_PER_MIB  45u

STATIC UINT64  mBytesRead;

STATIC
VOID
EmmcPaintReadBar (
  VOID
  )
{
  volatile UINT32  *Row;
  UINTN            Y;
  UINTN            X;
  UINTN            Lit;

  Lit = (UINTN)((mBytesRead * EMMC_BAR_PIXELS_PER_MIB) >> 20);
  if (Lit > EMMC_BAR_WIDTH) {
    Lit = EMMC_BAR_WIDTH;
  }

  for (Y = EMMC_BAR_TOP; Y < EMMC_BAR_BOTTOM; Y++) {
    Row = (volatile UINT32 *)(UINTN)(MSDC0_BASE_FB + (UINT64)Y * 2944);

    for (X = 0; X < EMMC_BAR_WIDTH; X++) {
      if (mBytesRead >= (15ULL << 20)) {
        Row[X] = 0x0000FF00;
      } else {
        Row[X] = (X < Lit) ? 0x00FFFF00 : 0x00101010;
      }
    }
  }
}

STATIC
BOOLEAN
ReadSectors (
  IN UINT64  Lba,
  IN UINT8   *Buffer,
  IN UINTN   Bytes
  )
{
  UINT32  Blocks;

  while (Bytes != 0) {
    Blocks = (UINT32)(Bytes / BLOCK_SIZE);
    if (Blocks > CHUNK_BLOCKS) {
      Blocks = CHUNK_BLOCKS;
    }

    if (!DataXfer ((Blocks > 1) ? CMD18_W : CMD17_W, (UINT32)Lba, mBounce, Blocks, FALSE)) {
      return FALSE;
    }

    mBytesRead += Blocks * BLOCK_SIZE;
    EmmcPaintReadBar ();

    CopyMem (Buffer, mBounce, Blocks * BLOCK_SIZE);
    Lba   += Blocks;
    Buffer = Buffer + Blocks * BLOCK_SIZE;
    Bytes -= Blocks * BLOCK_SIZE;
  }

  return TRUE;
}

STATIC
BOOLEAN
WriteSectors (
  IN UINT64  Lba,
  IN UINT8   *Buffer,
  IN UINTN   Bytes
  )
{
  UINT32  Blocks;

  while (Bytes != 0) {
    Blocks = (UINT32)(Bytes / BLOCK_SIZE);
    if (Blocks > CHUNK_BLOCKS) {
      Blocks = CHUNK_BLOCKS;
    }

    CopyMem (mBounce, Buffer, Blocks * BLOCK_SIZE);
    if (!DataXfer ((Blocks > 1) ? CMD25_W : CMD24_W, (UINT32)Lba, mBounce, Blocks, TRUE)) {
      return FALSE;
    }

    Lba   += Blocks;
    Buffer = Buffer + Blocks * BLOCK_SIZE;
    Bytes -= Blocks * BLOCK_SIZE;
  }

  return TRUE;
}

STATIC
EFI_STATUS
EFIAPI
EmmcReset (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN BOOLEAN                ExtendedVerification
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
EmmcReadBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  OUT VOID                  *Buffer
  )
{
  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (MediaId != mMedia.MediaId) {
    return EFI_MEDIA_CHANGED;
  }

  if ((BufferSize % BLOCK_SIZE) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if (Lba > mMedia.LastBlock) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Lba + (BufferSize / BLOCK_SIZE)) > (mMedia.LastBlock + 1)) {
    return EFI_INVALID_PARAMETER;
  }

  if (!ReadSectors (Lba, Buffer, BufferSize)) {
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
EmmcWriteBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  IN VOID                   *Buffer
  )
{
  if (mMedia.ReadOnly) {
    return EFI_WRITE_PROTECTED;
  }

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (MediaId != mMedia.MediaId) {
    return EFI_MEDIA_CHANGED;
  }

  if ((BufferSize % BLOCK_SIZE) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if ((Lba > mMedia.LastBlock) ||
      ((Lba + (BufferSize / BLOCK_SIZE)) > (mMedia.LastBlock + 1)))
  {
    return EFI_INVALID_PARAMETER;
  }

  return WriteSectors (Lba, Buffer, BufferSize) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

STATIC
EFI_STATUS
EFIAPI
EmmcFlushBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This
  )
{
  //
  // Nothing to flush: the only writer is the log path, which re-reads what it
  // wrote.  EXT_CSD[32] (CACHE_CTRL) is not enabled on this board either.
  //
  return EFI_SUCCESS;
}

STATIC
VOID
EmmcSay (
  IN CHAR16  *Text
  )
{
  if ((gST != NULL) && (gST->ConOut != NULL)) {
    gST->ConOut->OutputString (gST->ConOut, Text);
  }
}

STATIC
VOID
SayHex (
  IN CHAR16  *Label,
  IN UINT32  Value
  )
{
  STATIC CONST CHAR16  Digits[] = L"0123456789ABCDEF";
  CHAR16               Text[9];
  UINTN                Index;

  for (Index = 0; Index < 8; Index++) {
    Text[Index] = Digits[(Value >> ((7 - Index) * 4)) & 0xFu];
  }

  Text[8] = L'\0';
  EmmcSay (Label);
  EmmcSay (Text);
}

/**
  Read EXT_CSD and hand back the sector count.  Returns FALSE if the transfer
  did not complete.
**/
STATIC
BOOLEAN
ReadExtCsd (
  OUT UINT32  *Sectors
  )
{
  if (!DataXfer (CMD8_W, 0, mBounce, 1, FALSE)) {
    return FALSE;
  }

  *Sectors = (UINT32)mBounce[EXT_CSD_SEC_COUNT] |
             ((UINT32)mBounce[EXT_CSD_SEC_COUNT + 1] << 8) |
             ((UINT32)mBounce[EXT_CSD_SEC_COUNT + 2] << 16) |
             ((UINT32)mBounce[EXT_CSD_SEC_COUNT + 3] << 24);
  return TRUE;
}

/**
  Inherit, calibrate, and verify.  Returns TRUE when a 512-byte read of LBA 1
  produced "EFI ", which is the same signature bootshim insists on before it
  will write anything.
**/
STATIC
BOOLEAN
EmmcInherit (
  OUT UINT32  *SectorsOut
  )
{
  UINT32   Level;
  UINT32   Sectors;
  UINT32   R1;
  UINT32   Signature;
  BOOLEAN  Worked;

  Worked = FALSE;

  //
  // Polling only, stale flags dropped: same preamble as bootshim's
  // host_bringup() (bootshim/msdc.c:688-689).
  //
  EmmcW (R_INTEN, 0);
  EmmcW (R_INT, 0xFFFFFFFFu);

  //
  // Generous hardware backstops.  BUSWIDTH is deliberately NOT touched -- it
  // has to keep matching EXT_CSD[183], which on this card is 0x06 (8-bit DDR).
  //
  EmmcW (R_SDC_CFG, (EmmcR (R_SDC_CFG) & ~(SDC_DTOC_MASK | SDC_WRDTOC_MASK)) |
                    (0xFFu << 24) | (0x1FFFu << 2));

  if (!MsdcCmd (CMD13_W, MMC_RCA << 16, &R1)) {
    EmmcSay (L"EMMC CMD13 FAIL\r\n");
    return FALSE;
  }

  if ((R1 & R1_FATAL_MASK) != 0) {
    EmmcSay (L"EMMC NOT TRAN\r\n");
    return FALSE;
  }

  //
  // LK left the card in TRAN with RCA 1; R1_STATE is the read-back that proves
  // it rather than assuming it.
  //
  if ((R1_STATE (R1) != R1_STATE_TRAN) || ((R1 & R1_READY_FOR_DATA) == 0)) {
    EmmcSay (L"EMMC CARD BUSY\r\n");
  }

  for (Level = 0; Level < DDN_DIVIDERS; Level++) {
    if (!SetClock (mDividers[Level])) {
      continue;
    }

    (VOID)FifoClear ();

    if (!ReadExtCsd (&Sectors)) {
      continue;
    }

    if ((Sectors < 1024) || (Sectors == 0xFFFFFFFFu)) {
      continue;
    }

    if (!DataXfer (CMD17_W, 1, mBounce, 1, FALSE)) {
      continue;
    }

    Signature = ((UINT32)mBounce[0] << 24) |
                ((UINT32)mBounce[1] << 16) |
                ((UINT32)mBounce[2] << 8) |
                (UINT32)mBounce[3];

    if (Signature != 0x45464920u) {
      continue;
    }

    mDivider  = mDividers[Level];
    *SectorsOut = Sectors;
    Worked    = TRUE;
    break;
  }

  return Worked;
}

EFI_STATUS
EFIAPI
DandelionEmmcDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;
  UINT32      Sectors;

  mDivider = 0;
  mSectors = 0;
  Sectors  = 0;

  //
  // One 64 KiB bounce buffer: BlockIo callers are not required to hand us a
  // suitably aligned pointer, and multi-block PIO reads want a contiguous
  // destination anyway.
  //
  mBounce = AllocatePool (CHUNK_BLOCKS * BLOCK_SIZE);
  if (mBounce == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  if (!EmmcInherit (&Sectors)) {
    //
    // No BlockIo handle is installed, so everything downstream simply reports
    // the truth: no block device, no filesystem.  Returning an error here would
    // stop the dispatcher for no benefit.
    //
    EmmcSay (L"EMMC FAIL\r\n");
    FreePool (mBounce);
    mBounce = NULL;
    return EFI_SUCCESS;
  }

  mSectors = Sectors;

  ZeroMem (&mMedia, sizeof (mMedia));
  mMedia.MediaId          = 0x44444E30;   /* "DDN0" */
  mMedia.RemovableMedia   = FALSE;
  mMedia.MediaPresent     = TRUE;
  mMedia.LogicalPartition = FALSE;
  //
  // Writable as of 2026-10-29 21:1x, after the read path survived IMG_5765:
  // FAT mounts (or honestly reports that this disk has no FAT volume) and the
  // only writer is PayloadBootDxe's log path, which refuses any span that is
  // neither free nor already one of its own blocks, and re-reads what it wrote.
  //
  mMedia.ReadOnly         = FALSE;
  mMedia.WriteCaching     = FALSE;
  mMedia.BlockSize        = BLOCK_SIZE;
  mMedia.IoAlign          = 4;
  mMedia.LastBlock        = (EFI_LBA)mSectors - 1;

  mBlockIo.Revision    = EFI_BLOCK_IO_PROTOCOL_REVISION;
  mBlockIo.Media       = &mMedia;
  mBlockIo.Reset       = EmmcReset;
  mBlockIo.ReadBlocks  = EmmcReadBlocks;
  mBlockIo.WriteBlocks = EmmcWriteBlocks;
  mBlockIo.FlushBlocks = EmmcFlushBlocks;

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiDevicePathProtocolGuid,
                  &mDevicePath,
                  &gEfiBlockIoProtocolGuid,
                  &mBlockIo,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    EmmcSay (L"EMMC INSTALL FAIL\r\n");
    FreePool (mBounce);
    mBounce = NULL;
    return Status;
  }

  EmmcSay (L"EMMC OK div=");
  {
    CHAR16  Text[8];
    UINTN   Index;

    Text[0] = (CHAR16)(L'0' + (mDivider / 100) % 10);
    Text[1] = (CHAR16)(L'0' + (mDivider / 10) % 10);
    Text[2] = (CHAR16)(L'0' + mDivider % 10);
    Text[3] = L'\0';
    for (Index = 0; Index < 3; Index++) {
      if ((Index > 0) || (Text[Index] != L'0')) {
        break;
      }
    }

    EmmcSay (&Text[Index]);
  }
  SayHex (L" sec=", mSectors);
  EmmcSay (L"\r\n");

  return EFI_SUCCESS;
}
