/** @file
  The console's text log, exposed as a protocol.

  Why this exists: on this board the only debug channel is the panel, and console
  text is a photograph away from being useless -- 68 rows, a wrap-around window,
  and whatever BdsDxe printed last.  DandelionConsoleDxe already sees *every*
  character that goes to the console (DEBUG() is redirected to ConOut, see
  DebugLib in dandelion.dsc), so it keeps a copy in a ring buffer and hands it out
  here.  Whoever can persist it -- a payload, DarwinBoot, or a future eMMC
  writer -- gets the whole log instead of the tail of it.

  The protocol is deliberately one function and no state machine: dump the ring
  into a caller buffer, oldest byte first.

  Copyright (c) Microsoft Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef DANDELION_LOG_H_
#define DANDELION_LOG_H_

#include <Uefi.h>

#define DANDELION_LOG_SIGNATURE  SIGNATURE_32 ('D', 'L', 'O', 'G')
#define DANDELION_LOG_VERSION    1

typedef struct _DANDELION_LOG_PROTOCOL  DANDELION_LOG_PROTOCOL;

/**
  Copy the log out, oldest byte first.

  Call with Buffer == NULL to learn how many bytes are available; the size is
  returned in *Size and the status is EFI_BUFFER_TOO_SMALL.  Call again with a
  buffer of at least that size.  *Size is always updated to the number of bytes
  the log currently holds.

  @param[in]     This    The protocol instance.
  @param[out]    Buffer  Destination, or NULL to query.
  @param[in,out] Size    On input the capacity of Buffer; on output the byte count.

  @retval EFI_SUCCESS            The bytes were copied out.
  @retval EFI_BUFFER_TOO_SMALL   Buffer was NULL or too small; *Size says how big.
  @retval EFI_INVALID_PARAMETER  Size was NULL.
**/
typedef
EFI_STATUS
(EFIAPI *DANDELION_LOG_DUMP)(
  IN     DANDELION_LOG_PROTOCOL  *This,
  OUT    CHAR8                   *Buffer OPTIONAL,
  IN OUT UINTN                   *Size
  );

struct _DANDELION_LOG_PROTOCOL {
  UINT32               Signature;
  UINT32               Version;
  //
  // Capacity of the ring in bytes.  Useful to a caller that wants to know how
  // much history it is looking at before dumping.
  //
  UINT32               Capacity;
  DANDELION_LOG_DUMP   Dump;
};

extern EFI_GUID  gDandelionLogProtocolGuid;
extern EFI_GUID  gDandelionLogTableGuid;

/*
  The configuration-table form of the same log.

  PayloadBootDxe registers this under gDandelionLogTableGuid just before it
  starts a payload, so the payload needs no protocol lookup and no knowledge of
  this firmware: read the table, get a pointer and a length.

  The text is a copy allocated as EfiRuntimeServicesData, so it is still valid
  after ExitBootServices -- which matters because DarwinBoot is exactly the kind
  of consumer that reads the firmware log on the other side of that call.
*/
#define DANDELION_LOG_TABLE_SIGNATURE  SIGNATURE_32 ('D', 'L', 'G', 'T')
#define DANDELION_LOG_TABLE_VERSION    1

typedef struct {
  UINT32       Signature;
  UINT32       Version;
  UINT32       Size;      ///< bytes of text, not including any terminator
  UINT32       Reserved;
  UINT64       Text;      ///< physical address of the text
} DANDELION_LOG_TABLE;

#endif
