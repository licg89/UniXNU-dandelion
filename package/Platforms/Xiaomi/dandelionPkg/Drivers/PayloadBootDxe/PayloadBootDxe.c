/** @file
  Find a boot payload and run it (M5).

  This is the step after M4: once eMMC produces BlockIo and FatPkg produces a
  SimpleFileSystem, something has to actually load and start the next stage.
  BdsDxe cannot do it here -- it was deliberately taken out of the APRIORI list
  because ConSplitter's console-mode negotiation dead-loops on this panel (see
  DandelionSetMode in DandelionConsoleDxe.c and CORRECTIONS.md C10's neighbours)
  -- so this driver does the only part of BDS this board needs: find a payload,
  LoadImage it, StartImage it, and paint the result where a photo can read it.

  Two sources, tried in order:

    1. A file on a mounted FAT volume.  Candidates are '\UniXNU.efi',
       '\EFI\BOOT\BOOTAA64.EFI' and '\xnu.efi', in that order.  The file is read
       into a pool buffer and LoadImage'd from memory, which keeps this driver
       free of DevicePath-manipulation libraries.
    2. A payload embedded in the firmware volume itself, found by file GUID
       (mPayloadFileGuid, which must match Applications/PayloadHello/
       PayloadHello.inf).  This exists so the load-and-start path can be proven
       on this board *without* eMMC working: if the FV payload runs, the chain
       is good and only the file lookup remains untested.

  Reporting: a full-width bar at framebuffer y 1570..1596, which nothing else in
  this firmware uses (the progress band is at 1560..1568, the readout at
  1244..1389, the stage cells at 1400..1540).

    blue    looking for a payload
    green   payload started and returned EFI_SUCCESS
    red     found but LoadImage/StartImage failed, or nothing was found

  Copyright (c) Microsoft Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

#include <Pi/PiFirmwareVolume.h>
#include <Pi/PiFirmwareFile.h>

#include <Protocol/SimpleFileSystem.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/LoadedImage.h>
#include <Guid/EventGroup.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DandelionLog.h>

#include <Guid/FileInfo.h>

#define PAYLOAD_FB_BASE    0x7EC50000ULL
#define PAYLOAD_FB_STRIDE  2944
#define PAYLOAD_FB_WIDTH   720
#define PAYLOAD_BAR_TOP    1570
#define PAYLOAD_BAR_HEIGHT 26

#define PAYLOAD_BAR_SEARCH 0x00FF8000
#define PAYLOAD_BAR_OK     0x0000FF00
#define PAYLOAD_BAR_FAIL   0x00FF0000

//
// Must equal the FILE_GUID in Applications/PayloadHello/PayloadHello.inf.
// If they ever drift apart the only symptom is "no payload found", so the value
// is repeated here with a searchable name rather than hidden behind a header.
//
STATIC EFI_GUID  mPayloadFileGuid = {
  0x7E1B3A5C, 0x2C4D, 0x4F6A, { 0x9B, 0x10, 0xDA, 0x9D, 0x0E, 0x11, 0x00, 0x01 }
};

//
// The device tree, embedded in the firmware volume as a FREEFORM file (see
// dandelion.fdf) and published as an EFI configuration table before the payload
// is started.  B1B621D5-F19C-41A5-830B-D9152C69AAE0 is gFdtTableGuid: the GUID
// DarwinBoot and UniXNU already look for, so nothing on the payload side has to
// learn a new name.  The FFS file uses the same GUID, which keeps this driver
// down to one constant.
//
STATIC EFI_GUID  mFdtTableGuid = {
  0xB1B621D5, 0xF19C, 0x41A5, { 0x83, 0x0B, 0xD9, 0x15, 0x2C, 0x69, 0xAA, 0xE0 }
};

STATIC
VOID
PaintBar (
  IN UINT32  Colour
  )
{
  volatile UINT32  *Row;
  UINTN            X;
  UINTN            Y;

  for (Y = PAYLOAD_BAR_TOP; Y < PAYLOAD_BAR_TOP + PAYLOAD_BAR_HEIGHT; Y++) {
    Row = (volatile UINT32 *)(UINTN)(PAYLOAD_FB_BASE + (UINT64)Y * PAYLOAD_FB_STRIDE);
    for (X = 0; X < PAYLOAD_FB_WIDTH; X++) {
      Row[X] = Colour;
    }
  }
}

STATIC
VOID
Say (
  IN CONST CHAR16  *Text
  )
{
  //
  // Console only.  DEBUG() does not take a CHAR16 string here, and a wrong
  // format specifier in a bring-up driver is a crash, not a typo.
  //
  if ((gST != NULL) && (gST->ConOut != NULL)) {
    gST->ConOut->OutputString (gST->ConOut, (CHAR16 *)Text);
  }
}

/**
  Read a whole file into a freshly allocated buffer.

  The buffer is intentionally not freed: the image it feeds has to stay resident
  for as long as the started image lives, which is forever on this board.
**/
STATIC
EFI_STATUS
ReadWholeFile (
  IN  EFI_FILE_PROTOCOL  *File,
  OUT VOID               **Buffer,
  OUT UINTN              *Size
  )
{
  EFI_STATUS  Status;
  EFI_FILE_INFO  *Info;
  UINTN       InfoSize;
  UINTN       ReadSize;
  VOID        *Data;

  InfoSize = 0;
  Status   = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return EFI_LOAD_ERROR;
  }

  Status = gBS->AllocatePool (EfiBootServicesData, InfoSize, (VOID **)&Info);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, Info);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ReadSize = (UINTN)Info->FileSize;
  if ((ReadSize == 0) || (ReadSize > (16 * 1024 * 1024))) {
    return EFI_LOAD_ERROR;
  }

  Status = gBS->AllocatePool (EfiBootServicesData, ReadSize, &Data);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = File->Read (File, &ReadSize, Data);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *Buffer = Data;
  *Size   = ReadSize;
  return EFI_SUCCESS;
}

/**
  Load and start an image from a memory buffer.

  LoadImage with a SourceBuffer and no DevicePath is the documented way to start
  something that was not read through a device path -- which is exactly our
  situation for both the FAT file and the FV-embedded payload.
**/
STATIC
EFI_STATUS
StartFromBuffer (
  IN VOID   *Image,
  IN UINTN  ImageSize
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  NewHandle;
  UINTN       ExitDataSize;
  CHAR16      *ExitData;

  NewHandle    = NULL;
  ExitData     = NULL;
  ExitDataSize = 0;

  Status = gBS->LoadImage (FALSE, gImageHandle, NULL, Image, ImageSize, &NewHandle);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "PayloadBoot: LoadImage failed %r\n", Status));
    return Status;
  }

  Status = gBS->StartImage (NewHandle, &ExitDataSize, &ExitData);
  DEBUG ((DEBUG_ERROR, "PayloadBoot: StartImage -> %r\n", Status));
  return Status;
}

STATIC CHAR16  *mCandidates[] = {
  L"\\UniXNU.efi",
  L"\\EFI\\BOOT\\BOOTAA64.EFI",
  L"\\xnu.efi"
};

/**
  Try every mounted FAT volume for each candidate name.
**/
STATIC
EFI_STATUS
TryFileSystem (
  OUT CHAR16  **WhichName
  )
{
  EFI_STATUS               Status;
  EFI_HANDLE               *Handles;
  UINTN                    HandleCount;
  UINTN                    Index;
  UINTN                    Candidate;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL        *Root;
  EFI_FILE_PROTOCOL        *File;
  VOID                     *Image;
  UINTN                    ImageSize;

  Handles     = NULL;
  HandleCount = 0;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status) || (HandleCount == 0)) {
    DEBUG ((DEBUG_ERROR, "PayloadBoot: no SimpleFileSystem handle\n"));
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Root = NULL;
    if (EFI_ERROR (Fs->OpenVolume (Fs, &Root)) || (Root == NULL)) {
      continue;
    }

    for (Candidate = 0; Candidate < ARRAY_SIZE (mCandidates); Candidate++) {
      File = NULL;
      Status = Root->Open (Root, &File, mCandidates[Candidate], EFI_FILE_MODE_READ, 0);
      if (EFI_ERROR (Status) || (File == NULL)) {
        continue;
      }

      Image     = NULL;
      ImageSize = 0;
      Status    = ReadWholeFile (File, &Image, &ImageSize);
      File->Close (File);
      if (EFI_ERROR (Status)) {
        continue;
      }

      Say (L"PayloadBoot: found ");
      Say ((CHAR16 *)mCandidates[Candidate]);
      Say (L"\r\n");

      *WhichName = (CHAR16 *)mCandidates[Candidate];
      return StartFromBuffer (Image, ImageSize);
    }
  }

  return EFI_NOT_FOUND;
}

/**
  Fall back to a payload embedded in the firmware volume.

  FirmwareVolume2->ReadSection copies the whole section into the caller's
  buffer, and for an application FFS file the section we want is the PE32 one,
  i.e. a ready-to-load image with no file system in the way.
**/
STATIC
EFI_STATUS
TryFirmwareVolume (
  VOID
  )
{
  EFI_STATUS                    Status;
  EFI_HANDLE                    *Handles;
  UINTN                         HandleCount;
  UINTN                         Index;
  EFI_FIRMWARE_VOLUME2_PROTOCOL *Fv;
  VOID                          *Image;
  UINTN                         ImageSize;
  UINT32                        AuthenticationStatus;

  Handles     = NULL;
  HandleCount = 0;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiFirmwareVolume2ProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiFirmwareVolume2ProtocolGuid, (VOID **)&Fv);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Image     = NULL;
    ImageSize = 0;
    Status    = Fv->ReadSection (
                      Fv,
                      &mPayloadFileGuid,
                      EFI_SECTION_PE32,
                      0,
                      &Image,
                      &ImageSize,
                      &AuthenticationStatus
                      );
    if (EFI_ERROR (Status) || (Image == NULL) || (ImageSize == 0)) {
      continue;
    }

    Say (L"PayloadBoot: using the payload embedded in the FV\r\n");
    return StartFromBuffer (Image, ImageSize);
  }

  return EFI_NOT_FOUND;
}

/**
  Draw a white block at the right end of the bar.

  One small mark, one meaning: the device tree made it into the configuration
  table.  The bar's colour keeps its own meaning (searching / started / failed),
  so the two facts stay readable in the same photo.
**/
STATIC
VOID
TickBar (
  VOID
  )
{
  volatile UINT32  *Row;
  UINTN            X;
  UINTN            Y;

  for (Y = PAYLOAD_BAR_TOP; Y < PAYLOAD_BAR_TOP + PAYLOAD_BAR_HEIGHT; Y++) {
    Row = (volatile UINT32 *)(UINTN)(PAYLOAD_FB_BASE + (UINT64)Y * PAYLOAD_FB_STRIDE);
    for (X = PAYLOAD_FB_WIDTH - 24; X < PAYLOAD_FB_WIDTH; X++) {
      Row[X] = 0x00FFFFFF;
    }
  }
}

/**
  Draw a white block at the left end of the bar.

  Second mark, second meaning: the firmware log was written to flash.  Left and
  right are deliberately far apart in the photo.
**/
STATIC
VOID
TickBarLeft (
  VOID
  )
{
  volatile UINT32  *Row;
  UINTN            X;
  UINTN            Y;

  for (Y = PAYLOAD_BAR_TOP; Y < PAYLOAD_BAR_TOP + PAYLOAD_BAR_HEIGHT; Y++) {
    Row = (volatile UINT32 *)(UINTN)(PAYLOAD_FB_BASE + (UINT64)Y * PAYLOAD_FB_STRIDE);
    for (X = 0; X < 24; X++) {
      Row[X] = 0x00FFFFFF;
    }
  }
}

/**
  Publish the device tree as an EFI configuration table.

  DarwinBoot and UniXNU find the device tree through gFdtTableGuid, so the
  firmware's whole job here is to make that table entry exist -- the payload does
  not have to know where the blob lives.  Embedding the tree in the firmware
  volume (dandelion.fdf, FREEFORM file with the same GUID) also gives it a
  version that travels with the firmware instead of with a FAT partition which
  does not exist on this board yet.

  Returns EFI_NOT_FOUND when the firmware volume simply has no such file, which
  is not an error worth stopping for.
**/
//
// A minimal device tree, published when no firmware volume carries one.
//
// DarwinBoot reads /chosen/darwinboot-memory-size to size the window it allocates for the
// kernel.  Neither this firmware volume nor the deployed one contains a device tree, so that
// property never existed, DarwinBoot used its 256 MiB default, and the kernel booted with
// 60633 pages - which is what makes exec stall in vm_page_grab_options.
//
// The tree below is the smallest one that carries the property: a root node, /chosen with
// darwinboot-memory-size = 2 GiB, and the string table those two names need.  FDT is
// big-endian throughout, hence the byte-wise writer.
//
STATIC UINT8 *mFdtCursor;

STATIC
VOID
FdtPut32 (
  UINT32  Value
  )
{
  *mFdtCursor++ = (UINT8)(Value >> 24);
  *mFdtCursor++ = (UINT8)(Value >> 16);
  *mFdtCursor++ = (UINT8)(Value >> 8);
  *mFdtCursor++ = (UINT8)Value;
}

//
// DarwinBoot allocates the window with AllocatePages, so it needs one contiguous run of this
// size at 2 MiB alignment.  2 GiB was refused ("out of resources") on this phone; 1 GiB is the
// next step down and still eight times the 256 MiB default the kernel was getting.
//
#define DANDELION_WINDOW_BYTES  0x20000000u   /* 512 MiB: 1 GiB is refused too */

STATIC
EFI_STATUS
PublishMinimalDeviceTree (
  VOID
  )
{
  STATIC UINT8        Blob[256];
  STATIC CONST CHAR8  Strings[] = "chosen\0darwinboot-memory-size\0";
  UINT32              StringsLen;
  UINT32              StructOff;
  UINT32              StructSize;
  UINT32              Total;

  StringsLen = (UINT32)(sizeof (Strings) - 1);
  StructOff  = 40 + 16;                 // header + one (empty) memory reservation
  ZeroMem (Blob, sizeof (Blob));

  //
  // Structure block: root node, /chosen, one property, two end nodes, end of tree.
  //
  mFdtCursor = Blob + StructOff;
  FdtPut32 (1);                          // FDT_BEGIN_NODE, root, name ""
  FdtPut32 (0);
  FdtPut32 (1);                          // FDT_BEGIN_NODE, "chosen"
  CopyMem (mFdtCursor, "chosen", 7);
  mFdtCursor += 8;
  FdtPut32 (3);                          // FDT_PROP
  FdtPut32 (8);                          // length: one u64
  FdtPut32 (7);                          // name offset in the string table
  FdtPut32 (0);                          // 0x00000000_80000000, big-endian
  FdtPut32 (DANDELION_WINDOW_BYTES);
  FdtPut32 (2);                          // FDT_END_NODE, /chosen
  FdtPut32 (2);                          // FDT_END_NODE, root
  FdtPut32 (9);                          // FDT_END

  StructSize = (UINT32)(mFdtCursor - (Blob + StructOff));
  Total      = StructOff + StructSize + StringsLen;
  if (Total > sizeof (Blob)) {
    return EFI_BAD_BUFFER_SIZE;
  }
  CopyMem (Blob + StructOff + StructSize, Strings, StringsLen);

  //
  // Header, big-endian, version 17.
  //
  mFdtCursor = Blob;
  FdtPut32 (0xD00DFEED);                 // magic
  FdtPut32 (Total);                      // totalsize
  FdtPut32 (StructOff);                  // off_dt_struct
  FdtPut32 (StructOff + StructSize);     // off_dt_strings
  FdtPut32 (40);                         // off_mem_rsvmap
  FdtPut32 (17);                         // version
  FdtPut32 (16);                         // last_comp_version
  FdtPut32 (0);                          // boot_cpuid_phys
  FdtPut32 (StringsLen);                 // size_dt_strings
  FdtPut32 (StructSize);                 // size_dt_struct

  return gBS->InstallConfigurationTable (&mFdtTableGuid, Blob);
}

STATIC
EFI_STATUS
PublishDeviceTree (
  VOID
  )
{
  EFI_STATUS                    Status;
  EFI_HANDLE                    *Handles;
  UINTN                         HandleCount;
  UINTN                         Index;
  EFI_FIRMWARE_VOLUME2_PROTOCOL *Fv;
  VOID                          *Blob;
  UINTN                         BlobSize;
  UINT32                        AuthenticationStatus;
  UINT32                        Magic;

  Handles     = NULL;
  HandleCount = 0;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiFirmwareVolume2ProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiFirmwareVolume2ProtocolGuid, (VOID **)&Fv);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Blob     = NULL;
    BlobSize = 0;
    Status   = Fv->ReadSection (
                     Fv,
                     &mFdtTableGuid,
                     EFI_SECTION_RAW,
                     0,
                     &Blob,
                     &BlobSize,
                     &AuthenticationStatus
                     );
    if (EFI_ERROR (Status) || (Blob == NULL) || (BlobSize < 16)) {
      continue;
    }

    //
    // A device tree always starts with the magic 0xD00DFEED in big-endian, and
    // shipping one that does not is worse than shipping none: every consumer
    // would fail somewhere far away from here.
    //
    // Read it byte by byte.  This used to be `*(volatile UINT32 *)Blob !=
    // 0xD00DFEED`, which is a *little-endian* load of the four bytes D0 0D FE ED
    // and therefore compares 0xEDFE0DD0 against 0xD00DFEED -- always false.  The
    // device tree was never published, and the only trace was the log line
    // `FV device tree has a bad magic EDFE0DD0`, whose two halves are the same
    // number backwards.
    //
    Magic = ((UINT32)((UINT8 *)Blob)[0] << 24) |
            ((UINT32)((UINT8 *)Blob)[1] << 16) |
            ((UINT32)((UINT8 *)Blob)[2] << 8) |
            (UINT32)((UINT8 *)Blob)[3];
    if (Magic != 0xD00DFEEDu) {
      DEBUG ((DEBUG_ERROR, "PayloadBoot: FV device tree has a bad magic %08x\n", Magic));
      continue;
    }

    Status = gBS->InstallConfigurationTable (&mFdtTableGuid, Blob);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "PayloadBoot: InstallConfigurationTable failed %r\n", Status));
      return Status;
    }

    DEBUG ((DEBUG_ERROR, "PayloadBoot: device tree %u bytes published\n", (UINT32)BlobSize));
    return EFI_SUCCESS;
  }

  //
  // Nothing in any firmware volume carries a tree, so publish the minimal one that gives
  // DarwinBoot a 2 GiB window.  Returning EFI_NOT_FOUND here is what left the kernel with
  // 256 MiB and exec stuck in the page allocator.
  //
  return PublishMinimalDeviceTree ();
}

/*
  Writing the log to flash.

  This is the answer to the real complaint: console text is one photograph away
  from useless, and the ring only lives in RAM.  The moment a block device exists
  the log goes into unused space inside the 'flashinfo' partition -- the same
  place, found the same way, as bootshim/msdc.c already does on this phone, so the
  region is known to be free and the recovery path is known to work.

  Three guards, each of them load-bearing:

    1. only the flashinfo partition is ever written, found through the GPT;
    2. the target span must read back as all-0xFF or all-0x00 before a single byte
       is written -- anything else is somebody's data and we do nothing at all;
    3. what was written is read back and compared, and the result is painted on
       the panel, so a silent failure cannot happen.
*/
#define LOG_MAGIC             "DDNLOG01"
#define LOG_HEADER_VERSION    1
#define LOG_WRITE_SECTORS     128        // 64 KiB, one ring's worth
#define LOG_PARTITION_NAME     L"flashinfo"
//
// Where the log lives inside 'flashinfo', and why it moved.
//
// It started 1 MiB in, because the line-A shim put it there and the first 64 KiB
// of the partition is a vendor record.  That left the volume the payload needs
// squeezed into 1 MiB, which is fine for a boot manager but not for a kernel.
// The layout is now:
//
//     0            .. 15.875 MiB  FAT volume: \UniXNU.efi, \EFI\UniXNU\*, kernel
//                              and, later, the kernel
//     16256 KiB    .. 16256 KiB+  this log block (128 sectors)
//
// 15872 KiB of 32768 sectors, so 32512 -- the volume was 14 MiB until the kernel  tools/mk_unixnu_esp.sh builds a filesystem
// that stops short of it, and extract_log_block.py scans for the magic on every
// sector boundary, so the pull offset does not have to be kept in sync anywhere.
//
#define LOG_PARTITION_OFFSET   30512
#define LOG_SECTOR            512
//
// Where FlushLogToFlash found 'flashinfo', in whole-disk sectors.  Set by
// FlushLogToFlash; PublishEmmcHandoff uses it instead of scanning the GPT a
// second time, because that second scan reported no partition while this one
// works every boot.
//
STATIC EFI_LBA  mFlashInfoStart;
#define LOG_TEXT_MAX          ((LOG_WRITE_SECTORS - 1) * LOG_SECTOR)
//
// One buffer each, reused for every flush including the periodic ones.
//
STATIC CHAR8  mLogText[LOG_TEXT_MAX];
STATIC UINT8  mLogSectors[LOG_WRITE_SECTORS * LOG_SECTOR];
STATIC EFI_EVENT  mFlushTimer;
STATIC BOOLEAN    mFlushBusy;
//
// Set once the payload is about to start.  From then on the flush is a heartbeat,
// and it must not print: DarwinBoot draws its own diagnostics on the framebuffer
// (AllocateWindow prints "NO WINDOW ... BIG RUN ..." and up to seven "FREE ..."
// rows at y rows 0..7 when it cannot place the kernel), and two DEBUG lines every
// two seconds overwrote exactly that.  Measured 2026-10-30: the whole screen was
// our own "replacing the previous log" flood and the diagnostic that would have
// explained the failure was gone.
//
STATIC BOOLEAN    mFlushQuiet;
STATIC EFI_EVENT  mEbsEvent;
STATIC VOID       EFIAPI  FlushTickEntry (IN EFI_EVENT Event, IN VOID *Context);

STATIC
UINT32
ReadLe32 (
  IN CONST UINT8  *P
  )
{
  return (UINT32)P[0] | ((UINT32)P[1] << 8) | ((UINT32)P[2] << 16) | ((UINT32)P[3] << 24);
}

STATIC
UINT64
ReadLe64 (
  IN CONST UINT8  *P
  )
{
  return (UINT64)ReadLe32 (P) | ((UINT64)ReadLe32 (P + 4) << 32);
}

/**
  Little-endian writers.

  Hand-rolled rather than WriteUnaligned32/64 because this tree's BaseLib declares
  those with UINTN-sized arguments, and the block layout is a byte string anyway.
**/
STATIC
VOID
PutLe32 (
  OUT UINT8   *P,
  IN  UINT32  Value
  )
{
  P[0] = (UINT8)(Value & 0xFF);
  P[1] = (UINT8)((Value >> 8) & 0xFF);
  P[2] = (UINT8)((Value >> 16) & 0xFF);
  P[3] = (UINT8)((Value >> 24) & 0xFF);
}

STATIC
VOID
PutLe64 (
  OUT UINT8   *P,
  IN  UINT64  Value
  )
{
  PutLe32 (P, (UINT32)(Value & 0xFFFFFFFF));
  PutLe32 (P + 4, (UINT32)(Value >> 32));
}

/**
  Compare a GPT partition name (36 UTF-16LE units, NUL padded) with an ASCII
  literal.  Only the ASCII subset is supported, which is all a partition name
  from a MediaTek layout ever uses.
**/
STATIC
BOOLEAN
PartitionNameEquals (
  IN CONST UINT8   *Entry,
  IN CONST CHAR16  *Name
  )
{
  UINTN  Index;

  for (Index = 0; Name[Index] != L'\0'; Index++) {
    UINT16  Unit;

    CopyMem (&Unit, Entry + 56 + Index * 2, sizeof (Unit));
    if (Unit != (UINT16)Name[Index]) {
      return FALSE;
    }
  }

  return TRUE;
}

/**
  Is [Lba, Lba+Sectors) free space?

  Free means every byte is 0xFF (never written) or every byte is 0x00 (a
  partition that was zeroed once and never used since -- which is what flashinfo
  on this device turned out to look like, despite an earlier note claiming 0xFF).
  Anything else is somebody's data.
**/
STATIC
BOOLEAN
SpanIsFree (
  IN  EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN  EFI_LBA                Lba,
  IN  UINT32                 Sectors
  )
{
  UINT8   Chunk[4096];
  UINT32  Pattern;
  UINT32  Done;

  Pattern = 0;
  for (Done = 0; Done < Sectors; ) {
    UINT32  Take;
    UINT32  Size;
    UINT32  Index;
    BOOLEAN AllFf;
    BOOLEAN All00;

    Take = ((Sectors - Done) > 8) ? 8 : (Sectors - Done);
    Size = Take * LOG_SECTOR;

    if (EFI_ERROR (BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId, Lba + Done, Size, Chunk))) {
      return FALSE;
    }

    AllFf = TRUE;
    All00 = TRUE;
    for (Index = 0; Index < Size; Index++) {
      if (Chunk[Index] != 0xFF) {
        AllFf = FALSE;
      }

      if (Chunk[Index] != 0x00) {
        All00 = FALSE;
      }

      if (!AllFf && !All00) {
        return FALSE;
      }
    }

    if (Pattern == 0) {
      Pattern = AllFf ? 0xFF : 0x00;
    }

    Done += Take;
  }

  return TRUE;
}

/**
  Does [Lba, Lba+1) already hold a previous log block of ours?

  A span that is not free is not automatically somebody else's data: it may be
  the block this driver wrote on the last boot, which is exactly the thing we
  want to replace.  bootshim wrote a DLXLOG01 block into flashinfo+1 MiB on
  2026-10-27, so on this device the span is *always* occupied by one of our own
  logs and the free-space check alone would refuse forever.
**/
STATIC
BOOLEAN
SpanIsOurs (
  IN  EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN  EFI_LBA                Lba
  )
{
  UINT8  Header[LOG_SECTOR];

  if (EFI_ERROR (BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId, Lba, LOG_SECTOR, Header))) {
    return FALSE;
  }

  if (CompareMem (Header, LOG_MAGIC, 8) == 0) {
    return TRUE;
  }

  //
  // Line A's block, same place, same layout, different magic (bootshim's
  // DLXLOG01).  That block has been sitting at flashinfo+1 MiB since
  // 2026-10-27, so without this the span would look occupied forever.
  //
  return (BOOLEAN)(CompareMem (Header, "DLXLOG01", 8) == 0);
}

/**
  Find the flashinfo partition and write the log into free space inside it.
**/
STATIC
EFI_STATUS
FlushLogToFlash (
  VOID
  )
{
  EFI_STATUS             Status;
  EFI_HANDLE             *Handles;
  UINTN                  HandleCount;
  UINTN                  Index;
  EFI_BLOCK_IO_PROTOCOL  *BlockIo;
  UINT8                  Header[LOG_SECTOR];
  UINT8                  *Table;
  UINTN                  TableSize;
  UINT32                 EntryCount;
  UINT32                 EntrySize;
  UINT64                 FlashInfoStart;
  UINT64                 FlashInfoLast;
  EFI_LBA                Target;
  UINT8                  *Sectors;
  UINTN                  LogSize;
  UINT32                 TextSize;
  UINT32                 Sum;
  UINT32                 Written;
  DANDELION_LOG_PROTOCOL *Log;
  CHAR8                  *Text;

  Log = NULL;
  Status = gBS->LocateProtocol (&gDandelionLogProtocolGuid, NULL, (VOID **)&Log);
  if (EFI_ERROR (Status) || (Log == NULL)) {
    return EFI_NOT_FOUND;
  }

  LogSize = 0;
  Status  = Log->Dump (Log, NULL, &LogSize);
  if ((Status != EFI_BUFFER_TOO_SMALL) || (LogSize == 0)) {
    return EFI_NOT_FOUND;
  }

  if (LogSize > (LOG_WRITE_SECTORS - 1) * LOG_SECTOR) {
    LogSize = (LOG_WRITE_SECTORS - 1) * LOG_SECTOR;
  }

  //
  // Static buffers rather than pool: this function runs again on a timer while
  // the payload is running (see FlushTick), and allocating 64 KiB per tick would
  // leak.  LogSize is already clamped to the size of mLogText below LOG_TEXT_MAX.
  //
  Text = mLogText;

  Status = Log->Dump (Log, Text, &LogSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  TextSize = (UINT32)LogSize;

  Handles     = NULL;
  HandleCount = 0;
  Status      = gBS->LocateHandleBuffer (
                       ByProtocol,
                       &gEfiBlockIoProtocolGuid,
                       NULL,
                       &HandleCount,
                       &Handles
                       );
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo);
    if (EFI_ERROR (Status)) {
      continue;
    }

    //
    // Whole devices only, 512-byte sectors only: a partition handle's LBAs are
    // relative to the partition, and a GPT lookup through one would read the
    // wrong sectors.
    //
    if ((BlockIo->Media == NULL) || !BlockIo->Media->MediaPresent ||
        BlockIo->Media->LogicalPartition || (BlockIo->Media->BlockSize != LOG_SECTOR))
    {
      continue;
    }

    if (EFI_ERROR (BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId, 1, LOG_SECTOR, Header))) {
      continue;
    }

    if (CompareMem (Header, "EFI PART", 8) != 0) {
      continue;
    }

    EntryCount = ReadLe32 (Header + 80);
    EntrySize  = ReadLe32 (Header + 84);
    if ((EntryCount == 0) || (EntryCount > 128) || (EntrySize < 128) || (EntrySize > 512)) {
      continue;
    }

    TableSize = EntryCount * EntrySize;
    TableSize = (TableSize + LOG_SECTOR - 1) & ~(UINTN)(LOG_SECTOR - 1);
    Status    = gBS->AllocatePool (EfiBootServicesData, TableSize, (VOID **)&Table);
    if (EFI_ERROR (Status)) {
      continue;
    }

    if (EFI_ERROR (BlockIo->ReadBlocks (
                            BlockIo,
                            BlockIo->Media->MediaId,
                            (EFI_LBA)ReadLe64 (Header + 72),
                            TableSize,
                            Table
                            )))
    {
      continue;
    }

    FlashInfoStart = 0;
    FlashInfoLast  = 0;
    for (Written = 0; Written < EntryCount; Written++) {
      if (PartitionNameEquals (Table + Written * EntrySize, LOG_PARTITION_NAME)) {
        FlashInfoStart = ReadLe64 (Table + Written * EntrySize + 32);
        FlashInfoLast  = ReadLe64 (Table + Written * EntrySize + 40);
        break;
      }
    }

    if ((FlashInfoStart == 0) || (FlashInfoLast <= FlashInfoStart)) {
      continue;
    }

    //
    // Stay inside the partition with room to spare on both sides.  If the
    // partition is too small for 1 MiB of lead-in plus 64 KiB, skip it rather
    // than write anywhere else.
    //
    if ((FlashInfoStart + LOG_PARTITION_OFFSET + LOG_WRITE_SECTORS) > FlashInfoLast) {
      continue;
    }

    mFlashInfoStart = (EFI_LBA)FlashInfoStart;
    Target = (EFI_LBA)(FlashInfoStart + LOG_PARTITION_OFFSET);

    if (!SpanIsFree (BlockIo, Target, LOG_WRITE_SECTORS)) {
      if (SpanIsOurs (BlockIo, Target)) {
        if (!mFlushQuiet) {
          DEBUG ((DEBUG_ERROR, "PayloadBoot: replacing the previous log at LBA %lu\n", (UINT64)Target));
        }
      } else {
        DEBUG ((DEBUG_ERROR, "PayloadBoot: log span at LBA %lu is not free, refusing\n", (UINT64)Target));
        continue;
      }
    }

    //
    // Build the block: one header sector, then the text, zero padded to a whole
    // number of sectors.  The sum is a cheap integrity check for the reader.
    //
    Sectors = mLogSectors;

    ZeroMem (Sectors, LOG_WRITE_SECTORS * LOG_SECTOR);
    CopyMem (Sectors, LOG_MAGIC, 8);
    PutLe32 (Sectors + 8, LOG_HEADER_VERSION);
    PutLe32 (Sectors + 12, TextSize);
    PutLe32 (Sectors + 16, LOG_WRITE_SECTORS);
    PutLe64 (Sectors + 24, (UINT64)Target);

    Sum = 0;
    for (Written = 0; Written < TextSize; Written++) {
      Sum = Sum * 31 + (UINT8)Text[Written];
    }

    PutLe32 (Sectors + 20, Sum);
    CopyMem (Sectors + LOG_SECTOR, Text, TextSize);

    Status = BlockIo->WriteBlocks (
                        BlockIo,
                        BlockIo->Media->MediaId,
                        Target,
                        LOG_WRITE_SECTORS * LOG_SECTOR,
                        Sectors
                        );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "PayloadBoot: log write failed %r\n", Status));
      continue;
    }

    //
    // Read it back.  A write that returns success and did not happen is a real
    // failure mode on a bus that is being brought up, and the only way to know
    // is to look.
    //
    ZeroMem (Sectors, LOG_WRITE_SECTORS * LOG_SECTOR);
    if (EFI_ERROR (BlockIo->ReadBlocks (
                            BlockIo,
                            BlockIo->Media->MediaId,
                            Target,
                            LOG_WRITE_SECTORS * LOG_SECTOR,
                            Sectors
                            )))
    {
      continue;
    }

    if (CompareMem (Sectors, LOG_MAGIC, 8) != 0) {
      DEBUG ((DEBUG_ERROR, "PayloadBoot: log read-back magic mismatch\n"));
      continue;
    }

    if (!mFlushQuiet) {
      DEBUG ((
        DEBUG_ERROR,
        "PayloadBoot: firmware log written to LBA %lu (%u bytes)\n",
        (UINT64)Target,
        TextSize
        ));
    }
    TickBarLeft ();
    return EFI_SUCCESS;
  }

  return EFI_NOT_FOUND;
}

/**
  Hand the firmware's own text log to the payload, as a configuration table.
  The console driver keeps a 64 KiB ring of everything the firmware printed --
  DEBUG() included, because DebugLib is redirected to ConOut on this board.  That
  is the difference between "a photo of the last 68 lines" and "the whole log":
  whatever runs next can persist it, search it, or print it on a working console.

  The copy is allocated as EfiRuntimeServicesData on purpose.  DarwinBoot reads
  the log across ExitBootServices, and a BootServicesData buffer would be gone by
  then.

  Best effort: if there is no log protocol (a build without the console, say) the
  payload simply does not get one.
**/
STATIC EFI_STATUS
PublishLog (
  VOID
  )
{
  EFI_STATUS             Status;
  DANDELION_LOG_PROTOCOL *Log;
  UINTN                  Size;
  DANDELION_LOG_TABLE    *Table;
  CHAR8                  *Copy;

  Log = NULL;
  Status = gBS->LocateProtocol (&gDandelionLogProtocolGuid, NULL, (VOID **)&Log);
  if (EFI_ERROR (Status) || (Log == NULL)) {
    return EFI_NOT_FOUND;
  }

  Size   = 0;
  Status = Log->Dump (Log, NULL, &Size);
  if ((Status != EFI_BUFFER_TOO_SMALL) || (Size == 0)) {
    return EFI_NOT_FOUND;
  }

  Status = gBS->AllocatePool (EfiRuntimeServicesData, Size + 1, (VOID **)&Copy);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Log->Dump (Log, Copy, &Size);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Copy[Size] = '\0';

  Status = gBS->AllocatePool (EfiRuntimeServicesData, sizeof (*Table), (VOID **)&Table);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Table->Signature = DANDELION_LOG_TABLE_SIGNATURE;
  Table->Version   = DANDELION_LOG_TABLE_VERSION;
  Table->Size      = (UINT32)Size;
  Table->Reserved  = 0;
  Table->Text      = (UINT64)(UINTN)Copy;

  Status = gBS->InstallConfigurationTable (&gDandelionLogTableGuid, Table);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((DEBUG_ERROR, "PayloadBoot: firmware log handed over, %u bytes\n", (UINT32)Size));
  return EFI_SUCCESS;
}

/**
  Tell the console to stop rendering.

  BDS runs after this driver and prints a screenful of its own; the text area holds
  68 lines, so that output is what pushed the eMMC evidence out of earlier
  photographs.  The console recognises a line starting with the marker below and
  drops everything after it (see DandelionOutputString).  This has to be the last
  thing printed here: it is, by construction, the point at which the firmware
  stops talking and the panel becomes stable evidence.
**/
/**
  Periodic log flush, armed just before the payload starts.

  Why: everything that happens after StartImage is invisible to the flash log,
  because StartImage does not return until the payload does -- and the interesting
  payloads do not return.  DarwinBoot is the first of them: it loads the kernel,
  builds XNU's command line, paints a green bar and branches, and the log on
  flash stops at "looking for a payload".  That is how a whole boot's worth of
  diagnosis turns back into a photograph, which is the one thing this channel
  exists to avoid.

  UEFI event notifications are delivered whenever something calls RestoreTPL,
  and boot services calls inside the payload do that, so a periodic event keeps
  getting serviced while the payload runs.  The write itself is 64 KiB of polled
  PIO -- about 8 ms at the calibrated clock -- so a 2 s period costs well under
  half a percent of the machine.

  mFlushBusy guards against re-entering from a nested notification.
**/
STATIC
VOID
EFIAPI
FlushTickEntry (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mFlushBusy) {
    return;
  }

  mFlushBusy = TRUE;
  (VOID)FlushLogToFlash ();
  mFlushBusy = FALSE;
}

/**
  One last flush on the way out of boot services.

  The heartbeat timer dies the moment ExitBootServices runs, so everything the
  payload prints after the last tick -- and DarwinBoot prints a lot there: the
  kernel's virtual and physical base, the entry point, the command line,
  "inputs validated; exiting boot services", "hand-off to the kernel" -- was lost
  to the log every time.  An EVT_SIGNAL_EXIT_BOOT_SERVICES notification runs
  inside ExitBootServices, while boot services still work, which is the last
  moment a BlockIo write is possible.

  This is what makes the hand-off readable without a photograph: after it, the
  log ends with whatever DarwinBoot said right before it left.
**/
STATIC
VOID
EFIAPI
OnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  (VOID)FlushLogToFlash ();
}

STATIC
BOOLEAN
FindFlashInfo (
  OUT EFI_BLOCK_IO_PROTOCOL  **OutIo,
  OUT EFI_LBA                *OutStart,
  OUT EFI_LBA                *OutLast
  );

//
// The eMMC hand-off record XNU's console looks for.
//
// patches/xnu/dandelion-early-trace.patch reads a fixed record out of physical
// memory rather than from the device tree, and refuses the channel unless the
// magic and the "ok" flag are right:
//
//     handoff = (struct dandelion_emmc_handoff *)DANDELION_EMMC_HANDOFF_PA;
//     if (handoff->magic != DANDELION_EMMC_HANDOFF_MAGIC || handoff->ok != 1 ||
//         handoff->sectors < 2) { printf ("bad handoff, channel disabled"); ... }
//
// The layout is from pexpert/pexpert/arm64/DANDELION.h.  Nothing wrote it before,
// which is why the kernel's log region stayed all zeros while the firmware log
// reached "hand-off to the kernel" without complaint.
//
#define EMMC_HANDOFF_PA       0x5FD00000ULL
#define EMMC_HANDOFF_MAGIC    0x444D4844U   /* "DHMD" */
#define EMMC_LOG_LBA_OFFSET   30000u        /* sectors into flashinfo */
#define EMMC_LOG_SECTORS      512u
#define EMMC_LOG_CLKDIV       26u           /* what DandelionEmmcDxe calibrated */
#define EMMC_MSDC_BASE        0x11230000ULL
#define EMMC_MSDC_VERSION_OFS 0x114u
#define EMMC_MSDC_VERSION_WANT 0x20170426u

typedef struct {
  UINT32  Magic;
  UINT32  Ok;
  UINT32  Lba;
  UINT32  Sectors;
  UINT32  ClockDiv;
  UINT32  Reserved;
} EMMC_HANDOFF;

/**
  Publish the hand-off record, and report the two register checks the kernel makes.

  Both of the kernel's remaining refusals are silent unless they print through the
  channel they just disabled, so the firmware says them out loud instead: the MSDC
  version register has to read 0x20170426, and MSDC_CFG.CKSTB has to be set.
**/
STATIC
VOID
PublishEmmcHandoff (
  VOID
  )
{
  EMMC_HANDOFF           *Handoff;
  UINT32                 Version;
  UINT32                 Cfg;

  //
  // Unconditional, before the early return: the point of this line is to say
  // whether FlushLogToFlash recorded the partition at all.  Guessing at it cost
  // two boots.
  //
  DEBUG ((
    DEBUG_ERROR,
    "PayloadBoot: emmc handoff: flashinfo start %lu\n",
    (UINT64)mFlashInfoStart
    ));

  if (mFlashInfoStart == 0) {
    return;
  }

  Version = *(volatile UINT32 *)(UINTN)(EMMC_MSDC_BASE + EMMC_MSDC_VERSION_OFS);
  Cfg     = *(volatile UINT32 *)(UINTN)(EMMC_MSDC_BASE + 0x00);

  Handoff           = (EMMC_HANDOFF *)(UINTN)EMMC_HANDOFF_PA;
  Handoff->Magic     = EMMC_HANDOFF_MAGIC;
  Handoff->Ok        = 1;
  Handoff->Lba       = (UINT32)(mFlashInfoStart + EMMC_LOG_LBA_OFFSET);
  Handoff->Sectors   = EMMC_LOG_SECTORS;
  Handoff->ClockDiv  = EMMC_LOG_CLKDIV;
  Handoff->Reserved  = 0;

  DEBUG ((
    DEBUG_ERROR,
    "PayloadBoot: emmc handoff lba %lu sectors %u div %u; msdc ver 0x%08x (want 0x%08x) cfg 0x%08x ckstb %u\n",
    (UINT64)(mFlashInfoStart + EMMC_LOG_LBA_OFFSET),
    EMMC_LOG_SECTORS,
    EMMC_LOG_CLKDIV,
    Version,
    EMMC_MSDC_VERSION_WANT,
    Cfg,
    (Cfg >> 7) & 1u
    ));
}

STATIC
VOID
FreezeConsole (
  VOID
  )
{
  if ((gST != NULL) && (gST->ConOut != NULL)) {
    gST->ConOut->OutputString (gST->ConOut, L"@@FREEZE@@\r\n");
  }
}

/*
  A payload that lives on the medium rather than in the firmware volume.

  Why this exists: the FV has about 1.4 MiB of room left and the payload this
  project is actually waiting for -- UniXNU.efi -- will not fit there together
  with anything else.  The phone's own partitions cannot be used either: they are
  ext4/f2fs, and EDK2 has no ext4 driver ("FAT NONE" in the log is that fact, not
  a bug).  The one partition Android does not use is 'flashinfo', 16 MiB, and the
  firmware already writes its log into it at +1 MiB.

  So the payload goes at +2 MiB in the same partition, behind an eight-byte magic
  and a length, and gets dropped in with adb from Android:

      python tools/put_flash_payload.py UniXNU.efi -o out\payload.bin
      adb push out\payload.bin /sdcard/payload.bin
      adb shell su -c "dd if=/sdcard/payload.bin of=/dev/block/by-name/flashinfo bs=512 seek=4096 conv=notrunc"

  The header is not decoration.  `LoadImage` needs the exact image size, and a PE
  file does not carry one that can be trusted for that, so the sender states it
  and a checksum proves the bytes survived the trip.
*/
#define PAYLOAD_FROM_FLASH_MAGIC   "DDNPAY01"
#define PAYLOAD_FROM_FLASH_OFFSET  4096u        /* +2 MiB, log is at +1 MiB   */
#define PAYLOAD_FROM_FLASH_MAX     (12u * 1024u * 1024u)

/**
  Find the 'flashinfo' partition on any whole-disk BlockIo handle.

  Its own copy of the GPT walk rather than a refactor of FlushLogToFlash()'s: that
  path is proven on hardware now, and the thing being added here is a fallback.
  Folding the two together is a tidy-up, not a step towards M6.
**/
STATIC
BOOLEAN
FindFlashInfo (
  OUT EFI_BLOCK_IO_PROTOCOL  **OutIo,
  OUT EFI_LBA                *OutStart,
  OUT EFI_LBA                *OutLast
  )
{
  EFI_STATUS             Status;
  EFI_HANDLE             *Handles;
  UINTN                  HandleCount;
  UINTN                  Index;
  EFI_BLOCK_IO_PROTOCOL  *BlockIo;
  UINT8                  Header[512];
  UINT8                  *Table;
  UINTN                  TableSize;
  UINT32                 EntryCount;
  UINT32                 EntrySize;
  UINT32                 Walk;

  Handles     = NULL;
  HandleCount = 0;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo))) {
      continue;
    }

    if ((BlockIo->Media == NULL) || !BlockIo->Media->MediaPresent ||
        BlockIo->Media->LogicalPartition || (BlockIo->Media->BlockSize != 512))
    {
      continue;
    }

    if (EFI_ERROR (BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId, 1, 512, Header))) {
      continue;
    }

    if (CompareMem (Header, "EFI PART", 8) != 0) {
      continue;
    }

    EntryCount = ReadLe32 (Header + 80);
    EntrySize  = ReadLe32 (Header + 84);
    if ((EntryCount == 0) || (EntryCount > 128) || (EntrySize < 128) || (EntrySize > 512)) {
      continue;
    }

    TableSize = EntryCount * EntrySize;
    TableSize = (TableSize + 511) & ~(UINTN)511;
    Table     = NULL;
    if (EFI_ERROR (gBS->AllocatePool (EfiBootServicesData, TableSize, (VOID **)&Table))) {
      continue;
    }

    if (EFI_ERROR (BlockIo->ReadBlocks (
                            BlockIo,
                            BlockIo->Media->MediaId,
                            (EFI_LBA)ReadLe64 (Header + 72),
                            TableSize,
                            Table
                            )))
    {
      gBS->FreePool (Table);
      continue;
    }

    for (Walk = 0; Walk < EntryCount; Walk++) {
      if (!PartitionNameEquals (Table + Walk * EntrySize, LOG_PARTITION_NAME)) {
        continue;
      }

      *OutIo    = BlockIo;
      *OutStart = (EFI_LBA)ReadLe64 (Table + Walk * EntrySize + 32);
      *OutLast  = (EFI_LBA)ReadLe64 (Table + Walk * EntrySize + 40);
      gBS->FreePool (Table);
      return (BOOLEAN)(*OutLast > *OutStart);
    }

    gBS->FreePool (Table);
  }

  return FALSE;
}

STATIC
EFI_STATUS
TryFlashPayload (
  VOID
  )
{
  EFI_BLOCK_IO_PROTOCOL  *BlockIo;
  EFI_LBA                Start;
  EFI_LBA                Last;
  EFI_LBA                Target;
  UINT8                  Header[512];
  UINT32                 Size;
  UINT32                 WantSum;
  UINT32                 Sum;
  UINT32                 Index;
  UINTN                  Sectors;
  UINT8                  *Image;
  EFI_STATUS             Status;

  if (!FindFlashInfo (&BlockIo, &Start, &Last)) {
    return EFI_NOT_FOUND;
  }

  Target = Start + PAYLOAD_FROM_FLASH_OFFSET;
  if ((Target + 2) >= Last) {
    return EFI_NOT_FOUND;
  }

  if (EFI_ERROR (BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId, Target, 512, Header))) {
    return EFI_NOT_FOUND;
  }

  if (CompareMem (Header, PAYLOAD_FROM_FLASH_MAGIC, 8) != 0) {
    return EFI_NOT_FOUND;
  }

  Size    = ReadLe32 (Header + 8);
  WantSum = ReadLe32 (Header + 12);
  if ((Size < 64) || (Size > PAYLOAD_FROM_FLASH_MAX)) {
    Say (L"PayloadBoot: flash payload has a bad size\r\n");
    return EFI_NOT_FOUND;
  }

  Sectors = ((UINTN)Size + 511) / 512;
  if ((Target + 1 + Sectors) > Last) {
    Say (L"PayloadBoot: flash payload runs past the partition\r\n");
    return EFI_NOT_FOUND;
  }

  Image = NULL;
  Status = gBS->AllocatePool (EfiBootServicesData, Sectors * 512, (VOID **)&Image);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (EFI_ERROR (BlockIo->ReadBlocks (
                          BlockIo,
                          BlockIo->Media->MediaId,
                          Target + 1,
                          Sectors * 512,
                          Image
                          )))
  {
    Say (L"PayloadBoot: flash payload read failed\r\n");
    return EFI_NOT_FOUND;
  }

  //
  // The same cheap integrity check the log block uses, for the same reason: a
  // read that silently returns garbage is the failure mode this bus produces.
  //
  Sum = 0;
  for (Index = 0; Index < Size; Index++) {
    Sum = Sum * 31 + Image[Index];
  }

  if (Sum != WantSum) {
    Say (L"PayloadBoot: flash payload checksum mismatch\r\n");
    return EFI_NOT_FOUND;
  }

  Say (L"PayloadBoot: using the payload from flashinfo\r\n");
  return StartFromBuffer (Image, Size);
}

EFI_STATUS
EFIAPI
PayloadBootEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  CHAR16      *WhichName;

  WhichName = NULL;
  //
  // Which firmware is this, exactly?
  //
  // Four flashes produced byte-identical 2673-byte logs while the built
  // PayloadBootDxe.dll demonstrably contained the new strings, the FD hashes
  // matched and fastboot said OKAY every time.  Before another line of logic is
  // touched, every boot states its own build stamp, so that 'the image is
  // current' is a measurement rather than an assumption.  CORRECTIONS C16 was the
  // same lesson from the packaging side.
  //
  //
  // Does this firmware run at all?  Two markers, neither of which needs a logging
  // system: the payload bar at y 1570 turns red, and the build stamp is the first thing
  // said.  A reset does not clear the panel (CORRECTIONS C9/C11), so a frozen UEFI
  // screen proves nothing by itself.
  //
  PaintBar (0x00FF0000);   /* the payload bar turns red: this firmware ran */
  DEBUG ((DEBUG_ERROR, "PayloadBoot: build %a %a\n", __DATE__, __TIME__));

  PaintBar (PAYLOAD_BAR_SEARCH);

  //
  // Device tree first: a payload that finds a tree can describe the hardware
  // even before it can read anything from storage.
  //
  if (!EFI_ERROR (PublishDeviceTree ())) {
    Say (L"PayloadBoot: device tree published as an EFI configuration table\r\n");
    TickBar ();
  }

  //
  // Then the log, so the payload can report what the firmware did before it.
  //
  if (!EFI_ERROR (PublishLog ())) {
    Say (L"PayloadBoot: firmware log published as an EFI configuration table\r\n");
  }

  //
  // And put it somewhere that survives the phone: unused space inside the
  // 'flashinfo' partition.  Needs a block device, so before M4 this simply does
  // not run -- which is why the whole thing is guarded and painted instead of
  // asserted.
  //
  if (!EFI_ERROR (FlushLogToFlash ())) {
    Say (L"PayloadBoot: firmware log written to flash\r\n");
  }

  //
  // After the first flush, because it is what learns where flashinfo is.
  // The line it prints is captured by the ExitBootServices flush.
  //
  PublishEmmcHandoff ();

  //
  // From here on the payload owns the machine, and it may never come back.  Keep
  // flushing the log on a timer so whatever it printed before it left stays on
  // the medium.
  //
  mFlushTimer = NULL;
  mFlushBusy  = FALSE;
  mFlushQuiet = TRUE;

  //
  // Last chance to write: a notification that runs inside ExitBootServices.
  //
  mEbsEvent = NULL;
  (VOID)gBS->CreateEventEx (
               EVT_NOTIFY_SIGNAL,
               TPL_NOTIFY,
               OnExitBootServices,
               NULL,
               &gEfiEventExitBootServicesGuid,
               &mEbsEvent
               );
  if (!EFI_ERROR (gBS->CreateEvent (
                          EVT_TIMER | EVT_NOTIFY_SIGNAL,
                          TPL_CALLBACK,
                          FlushTickEntry,
                          NULL,
                          &mFlushTimer
                          )))
  {
    if (EFI_ERROR (gBS->SetTimer (mFlushTimer, TimerPeriodic, 20000000ULL))) {
      gBS->CloseEvent (mFlushTimer);
      mFlushTimer = NULL;
    } else {
      Say (L"PayloadBoot: log flush on a 2 s timer\r\n");
    }
  }

  Say (L"PayloadBoot: looking for a payload (FAT, then flashinfo, then FV)\r\n");

  Status = TryFileSystem (&WhichName);
  if (Status == EFI_NOT_FOUND) {
    Status = TryFlashPayload ();
  }

  if (Status == EFI_NOT_FOUND) {
    Status = TryFirmwareVolume ();
  }

  if (EFI_ERROR (Status)) {
    //
    // Not fatal.  Without a payload the machine still reaches BDS, which prints
    // "No boot options" -- the red bar is how we tell "no payload" apart from
    // "payload ran and failed".
    //
    PaintBar (PAYLOAD_BAR_FAIL);
    Say (L"PayloadBoot: no payload found\r\n");
    //
    // Flush again: the first flush happens before the search, so without this
    // the log on flash can never contain the search's own verdict.  Same span,
    // same magic, so it simply replaces the earlier block.
    //
    (VOID)FlushLogToFlash ();
    FreezeConsole ();
    return EFI_SUCCESS;
  }

  PaintBar (PAYLOAD_BAR_OK);
  (VOID)FlushLogToFlash ();
  FreezeConsole ();
  return EFI_SUCCESS;
}
