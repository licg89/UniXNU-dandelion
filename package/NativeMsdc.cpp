// SPDX-License-Identifier: BSD-2-Clause-Patent
// NativeMsdc: eMMC block storage on the MediaTek MT8167 MSDC (MSDC0 of the Benesse TAB-A04-BR3).
//
// Vendored into UniXNU-dandelion from https://github.com/mouseos/NativeMsdc
// (mouseos, BSD-2-Clause-Patent) and adapted for the Redmi 9A / dandelion
// (MT6762G, Helio G25).  Only the personality's compatible string changed:
// this board's eMMC node is "mediatek,mt6765-msdc" (dts/dandelion.dts), and the
// firmware leaves the card in the transfer state with RCA 1 -- the same
// inheritance the driver documents for MT8167.  Everything else, including the
// PIO discipline that lets the eMMC console share the controller, is upstream's.
//
// Default-off: the personality matches the "mediatek,mt6765-msdc" node, but nothing touches the controller unless
// native-msdc-root or native-msdc-persist is set. The controller is inherited from the boot firmware as it was
// left (card in the transfer state, bus width, clock and sampling unchanged); this driver never resets or retunes it.
// Every transaction masks the controller interrupts and selects PIO, then restores both, so firmware services that
// share the controller see it as they left it. Commands are polled.
//
//   native-msdc-root=1      publish the windows of the SHNGROOT manifest in userdata as read-only block devices
//   native-msdc-root=2      as 1, and windows whose manifest entry clears the read-only flag accept writes
//   native-msdc-persist=1|2 persistent Data store at the end of userdata (1 proves it read-only, 2 opens it for writes)
//   native-msdc-reserve=MiB reserve at the end of userdata that the store stays clear of (default 1024)
//   native-msdc-dma=1       descriptor DMA for reads, enabled only after it reads the same bytes as PIO
//   native-msdc-verify=1    hash every window against its manifest SHA-256 before publishing it
//
// Writes are bounded multi-block writes (CMD23 + CMD25) that return only when the card is back in the transfer
// state. Forced unit access (the store, and window writes that ask for it) adds a FLUSH_CACHE when the card cache
// is on, so the data is on the medium when the request completes.
//
// NATIVE_MSDC_BLOCK_DEVICES (default 1) builds the window block devices. They subclass the original
// IOBlockStorageDevice of the prelinked IOStorageFamily, so the kernel link needs the storage bindings
// (build-native-virtio-bindings.py). With 0 the driver serves the persistent store only and links on its own.
//
// NATIVE_VERIFIED_BLOCK=1 (default off, needs the window block devices) serves the windows only through the
// owner-signed VerifiedRoot policies stored after them in userdata (layout in NativeMsdc.h, "Signed root policy
// region"). Before publishing, the publisher thread reads the whole region into read-only kernel memory; each window is
// published only if exactly one policy has its size and vr_open accepts it under verified_root_key. Every read is then
// served from whole 4096-byte blocks that passed vr_verify_block, writes are refused, and native_ufs_block_authenticates
// answers the root authorization of NativeVirtioBlock for these windows. Independent of native-msdc-verify.
#ifndef NATIVE_MSDC_BLOCK_DEVICES
#define NATIVE_MSDC_BLOCK_DEVICES 1
#endif
#if defined(NATIVE_VERIFIED_BLOCK) && NATIVE_VERIFIED_BLOCK
#define NATIVE_MSDC_VERIFIED 1
#if !NATIVE_MSDC_BLOCK_DEVICES
#error "NATIVE_VERIFIED_BLOCK=1 needs the window block devices (NATIVE_MSDC_BLOCK_DEVICES=1)"
#endif
#else
#define NATIVE_MSDC_VERIFIED 0
#endif
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IODeviceTreeSupport.h>
#if NATIVE_MSDC_BLOCK_DEVICES
#include <IOKit/storage/IOBlockStorageDevice.h>
#endif
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSString.h>
#include <libkern/crypto/sha2.h>
#include <libkern/libkern.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#if NATIVE_MSDC_VERIFIED
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include "verified_root.h"
extern "C" {
extern const struct vr_crypto vr_xnu_crypto;     // kits/VerifiedRoot/adapters/xnu_crypto.c
extern const uint8_t verified_root_key[];         // the owner key, VR_KEY_BYTES, linked separately
}
#endif
#include "NativeMsdc.h"

using namespace NativeMsdcHw;

extern "C" {
// Boot diagnostics: 1 started, 2 card ready, 3 userdata found, 4 manifest valid, 5 windows scheduled, 6 windows published.
volatile uint32_t native_msdc_step = 0;
const char *volatile native_msdc_failure = nullptr;
volatile uint32_t native_msdc_manifest_status = 0xffffffff;
volatile uint32_t native_msdc_dma = 0;                      // 0 PIO, 1 DMA verified, 2 DMA rejected, 3 quarantined
volatile uint32_t native_msdc_blocks_published = 0;
volatile uint64_t native_msdc_reads = 0, native_msdc_writes = 0, native_msdc_flushes = 0, native_msdc_failures = 0;
// Optional verified-root snapshot reuse: hits, misses, authenticated bytes reused.
volatile uint64_t native_msdc_verified_cache_stats[3] = {};
// read calls/requested bytes/ns, verify calls/bytes/ns; no hot-path logging.
volatile uint64_t native_msdc_verified_timing_stats[6] = {};
void native_root_timing_snapshot (uint64_t out[6])
{
  for (unsigned i = 0; i < 6; ++i)
    out[i] = __atomic_load_n (&native_msdc_verified_timing_stats[i], __ATOMIC_RELAXED);
}


// Signed root policy (NATIVE_VERIFIED_BLOCK): header status (PolicyStatus, 0xffffffff not read), windows that passed
// vr_open, verified 4096-byte blocks served and integrity failures.
volatile uint32_t native_msdc_policy_status = 0xffffffff;
volatile uint32_t native_msdc_verified_windows = 0;
volatile uint64_t native_msdc_verified_blocks = 0;
volatile uint32_t native_msdc_integrity_failures = 0;
}

namespace {

constexpr uint64_t kCommandTimeoutNs = 1000000000ULL;     // one command and its data phase
constexpr uint64_t kBusyTimeoutNs = 5000000000ULL;        // programming after a write, cache flush
constexpr unsigned kDmaDescriptorBytes = 4096;             // GPD at 0, BD at 64; the buffer follows
constexpr unsigned kTransferBytes = kMaxBlocks * kSector;

// The kernel's eMMC console (pe_serial.c) drives the same controller.  A console
// write interleaved with a driver transfer corrupts it -- APFS then reports
// ETIMEDOUT/EIO on checkpoint reads -- so the console checks this flag and keeps its
// line buffered while a transfer is in flight.
extern "C" volatile uint32_t native_msdc_busy = 0;

// dandelion: with native-msdc-area=<MiB> the iOS data area lives in the last N MiB of
// userdata, so the Android filesystem in front of it can still mount between tests.
// Everything the driver reads -- manifest, windows, policy region -- is then relative to
// the area's base sector instead of the partition's first sector.
uint64_t gAreaBytes = 0;
uint64_t gAreaBaseSector = 0;

// bsd_init asks this instead of panicking on an unauthenticated root; the branch is taken when
// the root-unverified boot argument is present (development bring-up only).
extern "C" int native_msdc_root_unverified = 0;
constexpr uint64_t kDmaPhysicalMask = 0x00000000fffff000ULL;
constexpr uint32_t kDmaSelfTestSector = 0x40;
constexpr unsigned kDmaSelfTestBlocks = 4;

struct MsdcHost {
  IOMemoryMap *map;
  uintptr_t base;
  IOLock *lock;
  uint8_t *bounce;            // kTransferBytes, used under lock
  uint32_t diskSectors;
  bool cache;                 // EXT_CSD CACHE_CTRL
  bool ready;
  bool poisoned;              // the DMA engine did not stop: no further transfers
  IOBufferMemoryDescriptor *dma;
  uint8_t *dmaVirt;
  uint64_t dmaPhys;
  bool dmaEnabled;
  GptLayout gpt;
  bool layoutValid;
  RootManifest manifest;
  bool manifestValid;
};
MsdcHost gHost;
bool gModeTelemetryEnabled; // exact native-msdc-mode=1, sampled once before bring-up

inline uint32_t rd (unsigned offset)
{
  return *reinterpret_cast<volatile uint32_t *> (gHost.base + offset);
}

inline void wr (unsigned offset, uint32_t value)
{
  *reinterpret_cast<volatile uint32_t *> (gHost.base + offset) = value;
  __asm__ volatile ("dsb sy" ::: "memory");
}

#if NATIVE_MSDC_VERIFIED
bool gRootTimingEnabled; // configured once before window publication
uint64_t rootTimingNow ()
{
  uint64_t ns = 0;
  absolutetime_to_nanoseconds (mach_absolute_time (), &ns);
  return ns;
}
#endif

uint64_t deadlineAfter (uint64_t nanoseconds)
{
  uint64_t ticks = 0;
  nanoseconds_to_absolutetime (nanoseconds, &ticks);
  return mach_absolute_time () + ticks;
}

IOReturn waitClear (unsigned offset, uint32_t mask)
{
  uint64_t end = deadlineAfter (kCommandTimeoutNs);
  do {
    if (!(rd (offset) & mask)) return kIOReturnSuccess;
  } while (mach_absolute_time () < end);
  return kIOReturnTimeout;
}

void cacheCleanInvalidate (const void *start, size_t length)
{
  uint64_t ctr;
  __asm__ volatile ("mrs %0, ctr_el0" : "=r" (ctr));
  uintptr_t line = uintptr_t (4) << ((ctr >> 16) & 15);
  uintptr_t address = reinterpret_cast<uintptr_t> (start) & ~(line - 1);
  uintptr_t end = reinterpret_cast<uintptr_t> (start) + length;
  for (; address < end; address += line) __asm__ volatile ("dc civac, %0" :: "r" (address) : "memory");
  __asm__ volatile ("dsb sy" ::: "memory");
}

// ---- Controller access (gHost.lock held, inside a HostSession) -------------------------------------------------------

IOReturn hostCommand (unsigned opcode, uint32_t argument, Data data, uint32_t *response)
{
  IOReturn result = waitClear (kSdcSts, busyMask (opcode));
  if (result != kIOReturnSuccess) return result;
  wr (kMsdcIntr, rd (kMsdcIntr));
  wr (kSdcArg, argument);
  wr (kSdcCmd, commandWord (opcode, data));
  uint64_t end = deadlineAfter (kCommandTimeoutNs);
  do {
    uint32_t status = rd (kMsdcIntr) & (kIntCmdReady | kIntCmdErrors);
    if (status) {
      wr (kMsdcIntr, status);
      if (status & kIntCmdErrors) return kIOReturnIOError;
      *response = rd (kSdcResp0);
      return (*response & kR1Errors) ? kIOReturnIOError : kIOReturnSuccess;
    }
  } while (mach_absolute_time () < end);
  return kIOReturnTimeout;
}

// SEND_STATUS until the card is back in the transfer state (after programming, or an R1b command).
IOReturn hostWaitTransfer (void)
{
  uint64_t end = deadlineAfter (kBusyTimeoutNs);
  do {
    uint32_t response = 0;
    IOReturn result = hostCommand (kCmdStatus, kStatusArgument, Data::None, &response);
    if (result != kIOReturnSuccess) return result;
    if (cardInTransfer (response)) return kIOReturnSuccess;
  } while (mach_absolute_time () < end);
  return kIOReturnTimeout;
}

IOReturn hostPrepareData (uint32_t blocks)
{
  wr (kSdcBlkNum, blocks);
  wr (kMsdcFifoCs, rd (kMsdcFifoCs) | kFifoClear);
  return waitClear (kMsdcFifoCs, kFifoClear);
}

// PIO read. Multi-block reads are bounded by SET_BLOCK_COUNT, so a completed transfer needs no STOP; an interrupted one
// is stopped and its original error kept.
IOReturn hostReadPio (unsigned opcode, uint32_t argument, uint8_t *buffer, uint32_t blocks)
{
  if (!blocks || blocks > kMaxBlocks) return kIOReturnBadArgument;
  uint32_t response = 0;
  bool multi = blocks > 1;
  IOReturn result;
  if (multi) {
    result = hostCommand (kCmdSetBlockCount, blocks, Data::None, &response);
    if (result != kIOReturnSuccess) return result;
    opcode = kCmdReadMulti;
  }
  result = hostPrepareData (blocks);
  if (result != kIOReturnSuccess) return result;
  result = hostCommand (opcode, argument, multi ? Data::ReadMulti : Data::ReadSingle, &response);
  if (result == kIOReturnSuccess) {
    uint64_t end = deadlineAfter (kCommandTimeoutNs);
    unsigned offset = 0, total = blocks * kSector;
    bool done = false;
    result = kIOReturnTimeout;
    do {
      uint32_t interrupts = rd (kMsdcIntr) & (kIntXferDone | kIntDataErrors);
      if (interrupts) {
        wr (kMsdcIntr, interrupts);
        if (interrupts & kIntDataErrors) {
          result = kIOReturnIOError;
          break;
        }
        if (interrupts & kIntXferDone) done = true;
      }
      unsigned available = rd (kMsdcFifoCs) & 255u;
      while (available && offset < total) {
        if (available >= 4 && total - offset >= 4) {
          uint32_t word = rd (kMsdcRxData);
          memcpy (buffer + offset, &word, 4);
          offset += 4;
          available -= 4;
        } else {
          buffer[offset++] = *reinterpret_cast<volatile uint8_t *> (gHost.base + kMsdcRxData);
          available--;
        }
      }
      if (done && offset == total) return kIOReturnSuccess;
    } while (mach_absolute_time () < end);
  }
  if (multi) (void) hostCommand (kCmdStop, 0, Data::None, &response);
  return result;
}

// Descriptor DMA read into the private bounce buffer, then a copy out. The buffer is never reused while the engine
// may still write to it: if the engine does not stop, the transport is quarantined and the memory kept.
IOReturn hostReadDma (uint32_t argument, uint8_t *buffer, uint32_t blocks)
{
  if (!gHost.dmaVirt || !blocks || blocks > kMaxBlocks) return kIOReturnBadArgument;
  unsigned bytes = blocks * kSector;
  DmaGpd *gpd = reinterpret_cast<DmaGpd *> (gHost.dmaVirt);
  DmaBd *bd = reinterpret_cast<DmaBd *> (gHost.dmaVirt + 64);
  uint8_t *data = gHost.dmaVirt + kDmaDescriptorBytes;
  if (!dmaDescriptors (gpd, bd, gHost.dmaPhys, gHost.dmaPhys + 64, gHost.dmaPhys + kDmaDescriptorBytes, bytes))
    return kIOReturnBadArgument;
  cacheCleanInvalidate (gHost.dmaVirt, kDmaDescriptorBytes);
  cacheCleanInvalidate (data, bytes);
  uint32_t response = 0;
  bool multi = blocks > 1;
  IOReturn result = kIOReturnSuccess;
  if (multi) {
    result = hostCommand (kCmdSetBlockCount, blocks, Data::None, &response);
    if (result != kIOReturnSuccess) return result;
  }
  result = hostPrepareData (blocks);
  if (result != kIOReturnSuccess) return result;
  wr (kMsdcCfg, rd (kMsdcCfg) & ~kCfgPio);
  result = hostCommand (multi ? kCmdReadMulti : kCmdReadSingle, argument, multi ? Data::ReadMulti : Data::ReadSingle,
                        &response);
  if (result == kIOReturnSuccess) {
    wr (kDmaSaHigh, 0);
    wr (kDmaSa, uint32_t (gHost.dmaPhys));
    wr (kDmaLength, 0);
    wr (kDmaCfg, rd (kDmaCfg) | kDmaCfgChecksum);
    wr (kDmaCtrl, (rd (kDmaCtrl) & ~(kDmaCtrlStart | kDmaCtrlStop | kDmaCtrlLastBuffer | kDmaCtrlBurstMask)) |
                  kDmaCtrlDescriptor | kDmaCtrlBurst64);
    wr (kDmaCtrl, rd (kDmaCtrl) | kDmaCtrlStart);
    uint64_t end = deadlineAfter (kCommandTimeoutNs);
    for (;;) {
      uint32_t interrupts = rd (kMsdcIntr) & (kIntXferDone | kIntDataErrors | kIntDmaErrors);
      if (interrupts) {
        wr (kMsdcIntr, interrupts);
        if (interrupts & (kIntDataErrors | kIntDmaErrors)) {
          result = kIOReturnIOError;
          break;
        }
        if (interrupts & kIntXferDone) break;
      }
      if (mach_absolute_time () >= end) {
        result = kIOReturnTimeout;
        break;
      }
    }
  }
  wr (kDmaCtrl, rd (kDmaCtrl) | kDmaCtrlStop);
  if (waitClear (kDmaCfg, kDmaCfgActive) != kIOReturnSuccess) {
    gHost.poisoned = true;
    gHost.dmaEnabled = false;
    native_msdc_dma = 3;
    native_msdc_failure = "DMA engine did not stop";
    IOLog ("NativeMsdc: DMA engine did not stop; transport quarantined\n");
    return kIOReturnNotReady;
  }
  wr (kDmaCfg, rd (kDmaCfg) & ~kDmaCfgChecksum);
  wr (kMsdcCfg, rd (kMsdcCfg) | kCfgPio);
  if (result != kIOReturnSuccess) {
    if (multi) (void) hostCommand (kCmdStop, 0, Data::None, &response);
    return result;
  }
  result = waitClear (kSdcSts, kStsSdcBusy);
  if (result != kIOReturnSuccess) return result;
  cacheCleanInvalidate (data, bytes);
  memcpy (buffer, data, bytes);
  return kIOReturnSuccess;
}

// Bounded PIO write (CMD24, or CMD23 + CMD25). Fills each FIFO burst without a barrier per word and returns once the
// card has finished programming.
IOReturn hostWrite (uint32_t sector, const uint8_t *buffer, uint32_t blocks)
{
  if (!blocks || blocks > kMaxBlocks) return kIOReturnBadArgument;
  uint32_t response = 0;
  bool multi = blocks > 1;
  IOReturn result;
  if (multi) {
    result = hostCommand (kCmdSetBlockCount, blocks, Data::None, &response);
    if (result != kIOReturnSuccess) return result;
  }
  result = hostPrepareData (blocks);
  if (result != kIOReturnSuccess) return result;
  result = hostCommand (multi ? kCmdWriteMulti : kCmdWriteSingle, sector, multi ? Data::WriteMulti : Data::WriteSingle,
                        &response);
  if (result == kIOReturnSuccess) {
    uint64_t end = deadlineAfter (kCommandTimeoutNs);
    unsigned offset = 0, total = blocks * kSector;
    bool done = false;
    result = kIOReturnTimeout;
    do {
      uint32_t interrupts = rd (kMsdcIntr) & (kIntXferDone | kIntDataErrors);
      if (interrupts) {
        wr (kMsdcIntr, interrupts);
        if (interrupts & kIntDataErrors) {
          result = kIOReturnIOError;
          break;
        }
        if (interrupts & kIntXferDone) done = true;
      }
      unsigned used = (rd (kMsdcFifoCs) >> 16) & 255u;
      unsigned room = used <= kFifoBytes ? kFifoBytes - used : 0;
      while (room >= 4 && offset < total) {
        uint32_t word;
        memcpy (&word, buffer + offset, 4);
        *reinterpret_cast<volatile uint32_t *> (gHost.base + kMsdcTxData) = word;
        offset += 4;
        room -= 4;
      }
      __asm__ volatile ("dsb sy" ::: "memory");
      if (done && offset == total) {
        result = hostWaitTransfer ();
        if (result == kIOReturnSuccess) return kIOReturnSuccess;
        break;
      }
    } while (mach_absolute_time () < end);
  }
  if (multi) (void) hostCommand (kCmdStop, 0, Data::None, &response);
  return result;
}

IOReturn hostFlush (void)
{
  if (!gHost.cache) return kIOReturnSuccess;
  uint32_t response = 0;
  IOReturn result = hostCommand (kCmdSwitch, kFlushCacheArgument, Data::None, &response);
  if (result == kIOReturnSuccess) result = hostWaitTransfer ();
  if (result == kIOReturnSuccess) __atomic_fetch_add (&native_msdc_flushes, 1, __ATOMIC_RELAXED);
  return result;
}

// Holds the controller for one request: interrupts masked and PIO selected, then the firmware's values restored.
class HostSession {
  uint32_t cfg = 0, interruptEnable = 0;
public:
  HostSession ()
  {
    IOLockLock (gHost.lock);
    cfg = rd (kMsdcCfg);
    interruptEnable = rd (kMsdcIntEn);
    wr (kMsdcIntEn, 0);
    wr (kMsdcCfg, cfg | kCfgPio);
  }
  ~HostSession ()
  {
    wr (kMsdcIntr, rd (kMsdcIntr));
    wr (kMsdcCfg, cfg);
    wr (kMsdcIntEn, interruptEnable);
    IOLockUnlock (gHost.lock);
  }
  HostSession (const HostSession &) = delete;
  HostSession &operator= (const HostSession &) = delete;
};

// One transfer of at most kMaxBlocks sectors anywhere on the user area; callers bound it to their own region.
IOReturn transfer (uint64_t sector, uint32_t blocks, uint8_t *buffer, bool write)
{

  struct BusyGuard {
    BusyGuard () { __atomic_add_fetch (&native_msdc_busy, 1, __ATOMIC_ACQ_REL); }
    ~BusyGuard () { __atomic_sub_fetch (&native_msdc_busy, 1, __ATOMIC_ACQ_REL); }
  } busyGuard;
  if (gHost.poisoned) return kIOReturnNotReady;
  if (!blocks || blocks > kMaxBlocks || sector >= gHost.diskSectors || blocks > gHost.diskSectors - sector)
    return kIOReturnBadArgument;
  IOReturn result;
  if (write) result = hostWrite (uint32_t (sector), buffer, blocks);
  else if (gHost.dmaEnabled) result = hostReadDma (uint32_t (sector), buffer, blocks);
  else result = hostReadPio (kCmdReadSingle, uint32_t (sector), buffer, blocks);
  if (result != kIOReturnSuccess) __atomic_fetch_add (&native_msdc_failures, 1, __ATOMIC_RELAXED);
  else __atomic_fetch_add (write ? &native_msdc_writes : &native_msdc_reads, 1, __ATOMIC_RELAXED);
  return result;
}

unsigned bootArgument (const char *name, unsigned maximum, unsigned fallback)
{
  unsigned value = fallback;
  if (!PE_parse_boot_argn (name, &value, sizeof (value)) || value > maximum) value = fallback;
  return value;
}

void fail (const char *why)
{
  native_msdc_failure = why;
  IOLog ("NativeMsdc: %s\n", why);
}

// Diagnostic reads only: no card command, clock/pad write, new transfer, reset,
// retune or mode transition. EXT_CSD is the already successful 512-byte CMD8.
// TOPCKGEN address/mux [13:11] are documented by the MT8167 Linux/EDK2 sources.
// A selector alone does not prove the live PLL/source frequency.
// EXT_CSD[183] is not readable: retain raw bytes only, never infer card
// width/DDR or agreement with the host. HS_TIMING[185] is readable.
void hostLogMode (const uint8_t *extCsd, uint32_t inheritedCfg)
{
  if (!gModeTelemetryEnabled) return;
  uint32_t cfg = rd (kMsdcCfg), io = rd (kMsdcIoCon), sdc = rd (kSdcCfg);
  uint32_t pad0 = rd (kPadTune0), pad1 = rd (kPadTune1);
  BusMode mode = decodeBusMode (cfg, sdc, extCsd[185]);
  uint32_t muxReg = 0;
  bool muxValid = false;
  IODeviceMemory *clockMemory = IODeviceMemory::withRange (0x10000000ULL, 0x1000);
  IOMemoryMap *clockMap = clockMemory ? clockMemory->map (kIOMapAnywhere | kIOMapInhibitCache) : nullptr;
  if (clockMap && clockMap->getLength () >= sizeof (uint32_t) && clockMap->getVirtualAddress ()) {
    muxReg = *reinterpret_cast<volatile uint32_t *> (uintptr_t (clockMap->getVirtualAddress ()));
    muxValid = true;
  }
  OSSafeReleaseNULL (clockMap);
  OSSafeReleaseNULL (clockMemory);
  // Keep every newline-terminated record below the 255-byte console buffer.
  IOLog ("NativeMsdc: mode raw inherited=%08x cfg=%08x iocon=%08x sdc=%08x pad0=%08x pad1=%08x\n",
         inheritedCfg, cfg, io, sdc, pad0, pad1);
  IOLog ("NativeMsdc: mode host ckmod=%u ckdiv=%u stable=%u bits=%u ddr=%u hs400clk=%u\n",
         mode.clockMode, mode.clockDivider, unsigned (mode.clockStable), mode.hostBits,
         unsigned (mode.hostDdr), unsigned ((cfg >> 22) & 1u));
  IOLog ("NativeMsdc: mode card rev=%u bus_width_raw=%02x bus_width=not_readable hs_timing=%02x card_type=%02x driver_strength=%02x timing=%u\n",
         unsigned (extCsd[192]), unsigned (extCsd[183]), unsigned (extCsd[185]), unsigned (extCsd[196]),
         unsigned (extCsd[197]), mode.cardTiming);
  IOLog ("NativeMsdc: mode source mux_valid=%u mux_reg=%08x msdc0_mux=%u source_hz=unmeasured\n",
         unsigned (muxValid), muxReg, muxValid ? (muxReg >> 11) & 7u : 255u);
}

// ---- Bring-up --------------------------------------------------------------------------------------------------------

bool listHas (OSObject *property, const char *wanted)
{
  const char *bytes = nullptr;
  size_t length = 0;
  if (OSData *data = OSDynamicCast (OSData, property)) {
    bytes = static_cast<const char *> (data->getBytesNoCopy ());
    length = data->getLength ();
  } else if (OSString *string = OSDynamicCast (OSString, property)) {
    bytes = string->getCStringNoCopy ();
    length = string->getLength ();
  }
  size_t wantedLength = strlen (wanted);
  for (size_t at = 0; bytes && at < length;) {
    size_t end = at;
    while (end < length && bytes[end]) ++end;
    if (end - at == wantedLength && !memcmp (bytes + at, wanted, wantedLength)) return true;
    at = end + 1;
  }
  return false;
}

uint32_t cellProperty (IORegistryEntry *entry, const char *name)
{
  OSData *data = entry ? OSDynamicCast (OSData, entry->getProperty (name)) : nullptr;
  return data && data->getLength () == 4 ? le32 (static_cast<const uint8_t *> (data->getBytesNoCopy ())) : 0;
}

// The register window: the platform expert's resolution of reg (getDeviceMemoryWithIndex), cross-checked against reg
// and the arm-io ranges read directly when both use 2 address and 2 size cells. A disagreement is refused.
bool controllerWindow (IOService *provider, uint64_t &phys, uint64_t &length, const char **why)
{
  bool resolved = false, computed = false;
  uint64_t resolvedPhys = 0, resolvedLength = 0, computedPhys = 0, computedLength = 0;
  if (IODeviceMemory *memory = provider->getDeviceMemoryWithIndex (0)) {
    resolvedPhys = memory->getPhysicalSegment (0, nullptr, kIOMemoryMapperNone);
    resolvedLength = memory->getLength ();
    resolved = true;
  }
  IORegistryEntry *bus = provider->getParentEntry (gIODTPlane);
  IORegistryEntry *root = bus ? bus->getParentEntry (gIODTPlane) : nullptr;
  OSData *reg = OSDynamicCast (OSData, provider->getProperty ("reg"));
  OSData *ranges = bus ? OSDynamicCast (OSData, bus->getProperty ("ranges")) : nullptr;
  if (reg && ranges && reg->getLength () >= 16 && ranges->getLength () && !(ranges->getLength () % 24) &&
      cellProperty (bus, "#address-cells") == 2 && cellProperty (bus, "#size-cells") == 2 &&
      cellProperty (root, "#address-cells") == 2) {
    const uint8_t *cells = static_cast<const uint8_t *> (reg->getBytesNoCopy ());
    const uint8_t *range = static_cast<const uint8_t *> (ranges->getBytesNoCopy ());
    uint64_t child = le64 (cells), size = le64 (cells + 8);
    for (unsigned at = 0; at < ranges->getLength (); at += 24) {
      uint64_t childBase = le64 (range + at), parentBase = le64 (range + at + 8), rangeSize = le64 (range + at + 16);
      if (child >= childBase && child - childBase < rangeSize && size <= rangeSize - (child - childBase)) {
        computedPhys = parentBase + (child - childBase);
        computedLength = size;
        computed = true;
        break;
      }
    }
  }
  if (resolved && computed && (resolvedPhys != computedPhys || resolvedLength != computedLength)) {
    *why = "controller address: platform resolution and reg/ranges disagree";
    return false;
  }
  if (!resolved && !computed) {
    *why = "controller address: no usable reg";
    return false;
  }
  phys = resolved ? resolvedPhys : computedPhys;
  length = resolved ? resolvedLength : computedLength;
  if (!phys || (phys & 4095) || length < 0x1000) {
    *why = "controller address: unaligned or short register window";
    return false;
  }
  return true;
}

// Compare DMA with PIO on the same sectors; enable DMA only when they match.
void hostEnableDma (void)
{
  gHost.dma = IOBufferMemoryDescriptor::inTaskWithPhysicalMask (
    kernel_task, static_cast<IOOptionBits> (kIODirectionInOut) | static_cast<IOOptionBits> (kIOMemoryPhysicallyContiguous) |
    static_cast<IOOptionBits> (kIOMemoryMapperNone), kDmaDescriptorBytes + kTransferBytes, kDmaPhysicalMask);
  if (!gHost.dma) {
    native_msdc_dma = 2;
    IOLog ("NativeMsdc: DMA buffer allocation failed; PIO\n");
    return;
  }
  IOByteCount contiguous = 0;
  gHost.dmaVirt = static_cast<uint8_t *> (gHost.dma->getBytesNoCopy ());
  gHost.dmaPhys = gHost.dma->getPhysicalSegment (0, &contiguous, kIOMemoryMapperNone);
  IOReturn result = kIOReturnNoMemory;
  if (gHost.dmaVirt && gHost.dmaPhys && !(gHost.dmaPhys & 4095) &&
      contiguous >= kDmaDescriptorBytes + kTransferBytes &&
      gHost.dmaPhys + kDmaDescriptorBytes + kTransferBytes <= 0x100000000ULL) {
    bzero (gHost.dmaVirt, kDmaDescriptorBytes + kTransferBytes);
    uint8_t *reference = gHost.bounce, *candidate = gHost.bounce + kDmaSelfTestBlocks * kSector;
    result = hostReadPio (kCmdReadSingle, kDmaSelfTestSector, reference, kDmaSelfTestBlocks);
    if (result == kIOReturnSuccess) result = hostReadDma (kDmaSelfTestSector, candidate, kDmaSelfTestBlocks);
    if (result == kIOReturnSuccess && memcmp (reference, candidate, kDmaSelfTestBlocks * kSector)) result = kIOReturnIOError;
  }
  if (gHost.poisoned) return;
  gHost.dmaEnabled = result == kIOReturnSuccess;
  native_msdc_dma = gHost.dmaEnabled ? 1 : 2;
  IOLog ("NativeMsdc: DMA comparison with PIO %s (0x%x) at 0x%llx\n", gHost.dmaEnabled ? "passed" : "failed, PIO kept",
         result, gHost.dmaPhys);
  if (!gHost.dmaEnabled) {
    gHost.dma->release ();
    gHost.dma = nullptr;
    gHost.dmaVirt = nullptr;
    gHost.dmaPhys = 0;
  }
}

bool hostBringUp (IOService *provider, bool useDma, const char **why)
{
  uint64_t phys = 0, length = 0;
  if (!controllerWindow (provider, phys, length, why)) return false;
  IODeviceMemory *memory = IODeviceMemory::withRange (phys, 0x1000);
  gHost.map = memory ? memory->map (kIOMapAnywhere | kIOMapInhibitCache) : nullptr;
  OSSafeReleaseNULL (memory);
  gHost.lock = IOLockAlloc ();
  gHost.bounce = static_cast<uint8_t *> (IOMalloc (kTransferBytes));
  if (!gHost.map || !gHost.lock || !gHost.bounce) {
    *why = "controller map or allocation failed";
    return false;
  }
  gHost.base = static_cast<uintptr_t> (gHost.map->getVirtualAddress ());
  const uint32_t inheritedCfg = rd (kMsdcCfg);
  IOLog ("NativeMsdc: controller 0x%llx inherited CFG=0x%08x STS=0x%08x\n", phys, inheritedCfg, rd (kSdcSts));
  HostSession session;
  uint32_t response = 0;
  if (hostCommand (kCmdStatus, kStatusArgument, Data::None, &response) != kIOReturnSuccess || !cardInTransfer (response)) {
    *why = "card not in the transfer state";
    return false;
  }
  native_msdc_step = 2;
  uint8_t *extCsd = gHost.bounce;
  if (hostReadPio (kCmdSendExtCsd, 0, extCsd, 1) != kIOReturnSuccess) {
    *why = "EXT_CSD read failed";
    return false;
  }
  gHost.diskSectors = le32 (extCsd + 212);
  gHost.cache = extCsd[33] & 1;
  IOLog ("NativeMsdc: EXT_CSD rev %u, %u sectors, cache %u, partition access %u\n", extCsd[192], gHost.diskSectors,
         gHost.cache, extCsd[179] & 7);
  if (!gHost.diskSectors || (extCsd[179] & 7)) {
    *why = "EXT_CSD: no capacity or a boot/RPMB partition selected";
    return false;
  }
  hostLogMode (extCsd, inheritedCfg);
  if (useDma) hostEnableDma ();
  if (gHost.poisoned) {
    *why = "DMA engine did not stop";
    return false;
  }
  gHost.ready = true;
  return true;
}

const char *manifestReason (unsigned status)
{
  switch (status) {
  case kManifestNoMagic: return "no SHNGROOT manifest in userdata";
  case kManifestVersion: return "manifest version or window count";
  case kManifestHash: return "manifest SHA-256 mismatch";
  case kManifestBounds: return "manifest window bounds";
  case kManifestNames: return "manifest window names";
  case kManifestOverlap: return "manifest windows overlap";
  case kManifestAttributes: return "manifest window block size or flags";
  default: return "manifest rejected";
  }
}

// Primary GPT, the userdata partition and its manifest. Read-only.
bool hostFindLayout (const char **why)
{
  HostSession session;
  GptLayout gpt;
  if (transfer (kGptHeaderLba, 1, gHost.bounce, false) != kIOReturnSuccess) {
    *why = "GPT header read failed";
    return false;
  }
  if (const char *reason = gptCheckHeader (gHost.bounce, gHost.diskSectors, gpt)) {
    *why = reason;
    return false;
  }
  if (transfer (gpt.entriesLba, gpt.entrySectors, gHost.bounce, false) != kIOReturnSuccess) {
    *why = "GPT entries read failed";
    return false;
  }
  if (const char *reason = gptFindData (gHost.bounce, gpt)) {
    *why = reason;
    return false;
  }
  gHost.gpt = gpt;
  gHost.layoutValid = true;
  native_msdc_step = 3;
  uint64_t partitionSectors = gpt.userdataLast - gpt.userdataFirst + 1;
  IOLog ("NativeMsdc: userdata sectors %llu..%llu\n", gpt.userdataFirst, gpt.userdataLast);
  gAreaBytes = partitionSectors * kSector;
  gAreaBaseSector = gpt.userdataFirst;
  native_msdc_root_unverified = int (bootArgument ("root-unverified", 1, 0));
  IOLog ("NativeMsdc: data partition sectors %llu..%llu (%llu MiB)\n",
         gpt.userdataFirst, gpt.userdataLast, gAreaBytes / (1024 * 1024));
  if (partitionSectors < kManifestBytes / kSector ||
      transfer (gAreaBaseSector, kManifestBytes / kSector, gHost.bounce, false) != kIOReturnSuccess) {
    *why = "userdata manifest read failed";
    return false;
  }
  uint8_t digest[32];
  SHA256_CTX context;
  SHA256_Init (&context);
  SHA256_Update (&context, gHost.bounce, kManifestHashed);
  SHA256_Final (digest, &context);
  native_msdc_manifest_status = parseManifest (gHost.bounce, digest, partitionSectors * kSector, gHost.manifest);
  gHost.manifestValid = native_msdc_manifest_status == kManifestOk;
  // dandelion diagnostic: name the first bytes of userdata and the parse verdict.
  // The console records this fine once the host has root again (adb root on this
  // userdebug build); a sixteen-argument print never reached the console, so this
  // stays at two lines of four and five arguments.
  IOLog ("NativeMsdc: userdata head %02x%02x%02x%02x\n",
         gHost.bounce[0], gHost.bounce[1], gHost.bounce[2], gHost.bounce[3]);
  IOLog ("NativeMsdc: manifest status %u\n", native_msdc_manifest_status);
  if (gHost.manifestValid) native_msdc_step = 4;
  return true;
}

} // namespace

#if NATIVE_MSDC_BLOCK_DEVICES
// ---- Window block devices --------------------------------------------------------------------------------------------

#if NATIVE_MSDC_VERIFIED
// ---- Signed root policy region -----------------------------------------------------------------------------------------
namespace {

// The region as read from userdata, then made read-only, followed by a read-only copy of the owner key on its own page.
// vr_open requires both to stay immutable for as long as the views opened on them are used: the windows never free it.
struct PolicyStore {
  vm_offset_t storage;
  vm_size_t storageBytes;
  const uint8_t *key;
  PolicyHeader header;
};
PolicyStore gPolicy;

void policyDigest (const uint8_t *header, uint8_t digest[32])
{
  static const uint8_t zero[32] = {};
  SHA256_CTX context;
  SHA256_Init (&context);
  SHA256_Update (&context, header, kPolicyHashed);
  SHA256_Update (&context, zero, sizeof (zero));
  SHA256_Final (digest, &context);
}

const char *policyReason (unsigned status)
{
  switch (status) {
  case kPolicyNoMagic: return "no A04RPOL1 policy region after the root windows";
  case kPolicyVersion: return "policy version or entry count";
  case kPolicyHash: return "policy header SHA-256 mismatch";
  case kPolicySize: return "policy region size";
  case kPolicyEntryBounds: return "policy entry bounds";
  case kPolicyEntryOverlap: return "policy entries overlap";
  default: return "policy region rejected";
  }
}

void freePolicy (void)
{
  if (gPolicy.storage) kmem_free (kernel_map, gPolicy.storage, gPolicy.storageBytes);
  gPolicy = {};
}

// Reads the whole region into kernel memory, checks its header again from that copy and makes it read-only.
// Returns nullptr or the reason the region is unusable.
const char *loadPolicy (void)
{
  uint64_t partitionBytes = (gHost.gpt.userdataLast - gHost.gpt.userdataFirst + 1) * kSector;
  uint64_t start = policyRegionOffset (gHost.manifest);
  if (start >= partitionBytes || partitionBytes - start < kPolicyHeaderBytes) return "policy region outside userdata";
  uint64_t available = partitionBytes - start, firstSector = gAreaBaseSector + start / kSector;
  PolicyHeader header;
  unsigned status;
  uint8_t digest[32];
  {
    HostSession session;
    if (transfer (firstSector, kPolicyHeaderBytes / kSector, gHost.bounce, false) != kIOReturnSuccess)
      return "policy header read failed";
    policyDigest (gHost.bounce, digest);
    status = parsePolicyHeader (gHost.bounce, digest, available, header);
  }
  native_msdc_policy_status = status;
  if (status != kPolicyOk) return policyReason (status);
  vm_size_t tableBytes = round_page (static_cast<vm_size_t> (header.totalBytes));
  vm_size_t keyBytes = round_page (static_cast<vm_size_t> (VR_KEY_BYTES));
  vm_offset_t storage = 0;
  if (kmem_alloc (kernel_map, &storage, tableBytes + keyBytes, static_cast<kma_flags_t> (KMA_KOBJECT | KMA_DATA),
                  VM_KERN_MEMORY_SECURITY) != KERN_SUCCESS)
    return "policy allocation";
  gPolicy.storage = storage;
  gPolicy.storageBytes = tableBytes + keyBytes;
  uint8_t *region = reinterpret_cast<uint8_t *> (storage);
  IOLog ("NativeMsdc: reading the signed root policy, %llu bytes at userdata +%llu\n", header.totalBytes, start);
  uint64_t began = mach_absolute_time ();
  uint64_t sectors = (header.totalBytes + kSector - 1) / kSector;
  for (uint64_t done = 0; done < sectors;) {
    uint32_t blocks = sectors - done > kMaxBlocks ? kMaxBlocks : uint32_t (sectors - done);
    HostSession session;
    if (transfer (firstSector + done, blocks, region + done * kSector, false) != kIOReturnSuccess) {
      freePolicy ();
      return "policy region read failed";
    }
    done += blocks;
  }
  uint64_t elapsed = 0;
  absolutetime_to_nanoseconds (mach_absolute_time () - began, &elapsed);
  // The header is taken from the copy that the envelopes are opened from, and must describe the same region.
  policyDigest (region, digest);
  status = parsePolicyHeader (region, digest, available, gPolicy.header);
  native_msdc_policy_status = status;
  if (status != kPolicyOk || gPolicy.header.totalBytes != header.totalBytes) {
    freePolicy ();
    return status != kPolicyOk ? policyReason (status) : "policy region changed while it was read";
  }
  uint8_t *key = region + tableBytes;
  memcpy (key, verified_root_key, VR_KEY_BYTES);
  if (vm_map_protect (kernel_map, storage, storage + tableBytes + keyBytes, VM_PROT_READ, TRUE) != KERN_SUCCESS) {
    freePolicy ();
    return "policy read-only protection";
  }
  gPolicy.key = key;
  IOLog ("NativeMsdc: signed root policy read in %llu ms, %u entries\n", elapsed / 1000000ULL, gPolicy.header.count);
  return nullptr;
}

const char *vrReason (int status)
{
  switch (status) {
  case VR_FORMAT: return "policy envelope format or generation";
  case VR_KEY: return "policy envelope is for another owner key";
  case VR_SIGNATURE: return "policy owner signature invalid";
  case VR_HASH_TABLE: return "policy block hash table does not match its signed digest";
  default: return "policy envelope rejected";
  }
}

} // namespace
#endif // NATIVE_MSDC_VERIFIED

#if NATIVE_MSDC_VERIFIED
// Reconfiguration shares the existing per-window read lock, including failures.
class VerifiedWindowGuard {
  IOLock *mutex;
public:
  explicit VerifiedWindowGuard (IOLock *lock) : mutex (lock) { if (mutex) IOLockLock (mutex); }
  ~VerifiedWindowGuard () { if (mutex) IOLockUnlock (mutex); }
};
#endif

class NativeMsdcBlock : public IOBlockStorageDevice {
  OSDeclareDefaultStructors (NativeMsdcBlock)
  uint64_t firstSector = 0;   // absolute, 512-byte sectors
  uint64_t sectors = 0;
  uint32_t window = 0;        // manifest index
  char label[32] = {};
  bool writable = false;
  bool online = false;
#if NATIVE_MSDC_VERIFIED
  bool verifiedActive = false;
  bool verifiedCacheEnabled = false;
  bool manifestReadOnly = false;
  uint64_t verifiedConfiguration = 0;
  VerifiedReadCache verifiedCache;
  struct vr_view verified = {};
  const uint8_t *envelope = nullptr;    // the selected VROOT01 envelope in the read-only policy copy
  uint8_t *verifiedBounce = nullptr;    // kTransferBytes, under verifiedLock from read through copy
  IOLock *verifiedLock = nullptr;
  IOReturn readVerified (IOMemoryDescriptor *buffer, UInt64 block, UInt64 count, IOStorageCompletion *completion,
                         uint64_t configuration);
#endif
public:
  bool configure (unsigned index, const RootWindow &entry, uint64_t absoluteSector, bool allowWrites);
#if NATIVE_MSDC_VERIFIED
  const char *configureVerification (int *status);
  bool authenticatesVolume (const char *uuid, uint8_t *manifestDigest) const;
  void free () override;
#endif
  const char *windowLabel () const { return label; }
  unsigned windowIndex () const { return window; }
  bool start (IOService *provider) override;
  IOReturn doEjectMedia () override { return kIOReturnUnsupported; }
  IOReturn doFormatMedia (UInt64) override { return kIOReturnUnsupported; }
  UInt32 doGetFormatCapacities (UInt64 *, UInt32) const override { return 0; }
  char *getVendorString () override { return const_cast<char *> ("Native"); }
  char *getProductString () override { return label; }
  char *getRevisionString () override { return const_cast<char *> ("1.0"); }
  char *getAdditionalDeviceInfoString () override { return const_cast<char *> ("MSDC eMMC userdata window"); }
  IOReturn reportBlockSize (UInt64 *value) override { *value = kSector; return kIOReturnSuccess; }
  IOReturn reportEjectability (bool *value) override { *value = false; return kIOReturnSuccess; }
  IOReturn reportMaxValidBlock (UInt64 *value) override { *value = sectors - 1; return kIOReturnSuccess; }
  IOReturn reportMediaState (bool *present, bool *changed) override
  {
    *present = online;
    if (changed) *changed = false;
    return kIOReturnSuccess;
  }
  IOReturn reportRemovability (bool *value) override { *value = false; return kIOReturnSuccess; }
  IOReturn reportWriteProtection (bool *value) override { *value = !writable; return kIOReturnSuccess; }
  IOReturn doSynchronizeCache () override;
  IOReturn doSynchronize (UInt64, UInt64, IOOptionBits) override { return doSynchronizeCache (); }
  IOReturn getWriteCacheState (bool *value) override { *value = writable && gHost.cache; return kIOReturnSuccess; }
  IOReturn setWriteCacheState (bool) override { return kIOReturnUnsupported; }
  IOReturn doAsyncReadWrite (IOMemoryDescriptor *, UInt64, UInt64, IOStorageAttributes *, IOStorageCompletion *) override;
};
OSDefineMetaClassAndStructors (NativeMsdcBlock, IOBlockStorageDevice)

bool NativeMsdcBlock::configure (unsigned index, const RootWindow &entry, uint64_t absoluteSector, bool allowWrites)
{
#if NATIVE_MSDC_VERIFIED
  // Invalidate before even a rejected reconfiguration; preserve lock ordering.
  VerifiedWindowGuard guard (verifiedLock);
  verifiedCache.invalidate ();
  __atomic_add_fetch (&verifiedConfiguration, 1, __ATOMIC_RELEASE);
  if (verifiedActive) online = false; // a failed live reconfiguration must fail closed
  verifiedActive = false;
#endif
  uint64_t count = entry.size / kSector;
  if (!count || absoluteSector < gHost.gpt.userdataFirst || absoluteSector > gHost.gpt.userdataLast ||
      count > gHost.gpt.userdataLast - absoluteSector + 1)
    return false;
  window = index;
  firstSector = absoluteSector;
  sectors = count;
  writable = allowWrites && !(entry.flags & kWindowReadOnly);
#if NATIVE_MSDC_VERIFIED
  manifestReadOnly = (entry.flags & kWindowReadOnly) != 0;
#endif
  strlcpy (label, entry.name, sizeof (label));
  return true;
}

bool NativeMsdcBlock::start (IOService *provider)
{
  if (!IOBlockStorageDevice::start (provider)) return false;
  online = true;
  registerService ();
  return true;
}

IOReturn NativeMsdcBlock::doSynchronizeCache ()
{
  if (!online) return kIOReturnNotReady;
  if (!writable) return kIOReturnSuccess;
  if (gHost.poisoned) return kIOReturnNotReady;
  HostSession session;
  return hostFlush ();
}

IOReturn NativeMsdcBlock::doAsyncReadWrite (IOMemoryDescriptor *buffer, UInt64 block, UInt64 count,
                                            IOStorageAttributes *attributes, IOStorageCompletion *completion)
{
#if NATIVE_MSDC_VERIFIED
  const uint64_t configuration = verifiedActive ?
    __atomic_load_n (&verifiedConfiguration, __ATOMIC_ACQUIRE) : 0;
  // Cache-off and Data windows retain their original write hot path.
  if (verifiedCacheEnabled && manifestReadOnly && buffer &&
      buffer->getDirection () == kIODirectionOut && verifiedLock) {
    IOLockLock (verifiedLock);
    verifiedCache.invalidate ();
    IOLockUnlock (verifiedLock);
  }
#endif
  if (!buffer || !completion || !completion->action || !count || block >= sectors || count > sectors - block ||
      buffer->getLength () < count * kSector)
    return kIOReturnBadArgument;
  IODirection direction = buffer->getDirection ();
  bool write = direction == kIODirectionOut;
  if (!write && direction != kIODirectionIn) return kIOReturnBadArgument;
#if NATIVE_MSDC_VERIFIED
  if (verifiedActive) {
    if (write) return kIOReturnNotWritable;     // a verified window is served read-only
    if (!online) return kIOReturnNotReady;
    return readVerified (buffer, block, count, completion, configuration);
  }
#endif
  if (write && !writable) return kIOReturnNotWritable;
  if (!online) return kIOReturnNotReady;
  bool forceUnitAccess = write && attributes && (attributes->options & kIOStorageOptionForceUnitAccess);
  IOReturn result = kIOReturnSuccess;
  uint64_t done = 0;
  while (done < count && result == kIOReturnSuccess) {
    uint32_t blocks = count - done > kMaxBlocks ? kMaxBlocks : uint32_t (count - done);
    IOByteCount bytes = IOByteCount (blocks) * kSector, offset = IOByteCount (done) * kSector;
    HostSession session;
    if (write) {
      result = buffer->readBytes (offset, gHost.bounce, bytes) == bytes ?
        transfer (firstSector + block + done, blocks, gHost.bounce, true) : kIOReturnIOError;
    } else {
      result = transfer (firstSector + block + done, blocks, gHost.bounce, false);
      if (result == kIOReturnSuccess && buffer->writeBytes (offset, gHost.bounce, bytes) != bytes) result = kIOReturnIOError;
    }
    if (result == kIOReturnSuccess) done += blocks;
  }
  if (result == kIOReturnSuccess && forceUnitAccess) {
    HostSession session;
    result = hostFlush ();
  }
  completion->action (completion->target, completion->parameter, result, done * kSector);
  return kIOReturnSuccess;
}

#if NATIVE_MSDC_VERIFIED
// Selects this window's policy and opens it (vr_open, owner key, minimum generation 1). Returns nullptr when the window
// is to be served verified, otherwise the reason it must not be published; *status gets the vr_open result or -1.
const char *NativeMsdcBlock::configureVerification (int *status)
{
  *status = -1;
  if (!verifiedLock) verifiedLock = IOLockAlloc ();
  if (!verifiedLock) return "verified read lock";
  VerifiedWindowGuard guard (verifiedLock);
  verifiedCache.invalidate ();
  __atomic_add_fetch (&verifiedConfiguration, 1, __ATOMIC_RELEASE);
  if (verifiedActive) online = false; // a failed live reconfiguration must fail closed
  verifiedActive = false;
  if (!gPolicy.storage || !gPolicy.key) return "no signed root policy";
  const uint8_t *region = reinterpret_cast<const uint8_t *> (gPolicy.storage);
  uint64_t windowBytes = sectors * kSector;
  unsigned index = 0;
  switch (selectPolicy (region, gPolicy.header, windowBytes, index)) {
  case kPolicySelected: break;
  case kPolicyDuplicate: return "two policy entries have this window's size";
  default: return "no policy entry has this window's size";
  }
  const PolicyEntry &entry = gPolicy.header.entry[index];
  struct vr_view view = {};
  *status = vr_open (region + entry.offset, static_cast<size_t> (entry.bytes), gPolicy.key, 1, &vr_xnu_crypto, &view);
  if (*status != VR_OK) return vrReason (*status);
  if (view.image_bytes != windowBytes) return "signed image size differs from the window";
  if (!verifiedBounce) verifiedBounce = static_cast<uint8_t *> (IOMalloc (kTransferBytes));
  if (!verifiedLock) verifiedLock = IOLockAlloc ();
  if (!verifiedBounce || !verifiedLock) return "verified read buffers";
  verified = view;
  envelope = region + entry.offset;
  writable = false;
  verifiedCacheEnabled = bootArgument ("native-msdc-vcache", 1, 0) == 1;
  verifiedCache.metrics = native_msdc_verified_cache_stats;
  verifiedActive = true;
  return nullptr;
}

IOReturn NativeMsdcBlock::readVerified (IOMemoryDescriptor *buffer, UInt64 block, UInt64 count,
                                        IOStorageCompletion *completion, uint64_t configuration)
{
  VerifiedReadKey cacheKey = {};
  const uint64_t windowBytes = sectors * kSector;
  struct Operations {
    IOLock *mutex;
    IOMemoryDescriptor *buffer;
    const vr_view *view;
    uint64_t firstSector;
    NativeMsdcBlock *owner;
    VerifiedReadKey *key;
    uint64_t expectedConfiguration, expectedWindowBytes;
    void lock () {
      IOLockLock (mutex);
      // Bounds were checked before taking this lock: reject a changed window,
      // even with cache disabled, before any read or requester copy.
      if (owner->verifiedConfiguration != expectedConfiguration ||
          owner->sectors * kSector != expectedWindowBytes || !owner->verifiedActive || !owner->online) {
        *key = {};
        return;
      }
      firstSector = owner->firstSector;
      *key = { owner->window, owner->firstSector, owner->sectors * kSector,
               owner->verifiedConfiguration, owner->verified.generation,
               owner->verifiedCacheEnabled && owner->verifiedActive &&
               owner->manifestReadOnly && !owner->writable };
    }
    void unlock () { IOLockUnlock (mutex); }
    int badArgument () { return kIOReturnBadArgument; }
    int ioError () { return kIOReturnIOError; }
    int read (uint64_t windowSector, uint32_t blocks, uint8_t *out)
    {
      if (!owner->verifiedActive || !owner->online) return kIOReturnNotReady;
      // Includes shared host-lock wait, DMA/cache maintenance and bounce copy.
      return verifiedTimedOperation (gRootTimingEnabled, native_msdc_verified_timing_stats, 0,
        uint64_t (blocks) * kSector, rootTimingNow, [&] () {
          HostSession session;
          return transfer (firstSector + windowSector, blocks, out, false);
        });
    }
    int verify (uint64_t index, const uint8_t *data, size_t length)
    {
      const int checked = verifiedTimedOperation (gRootTimingEnabled, native_msdc_verified_timing_stats, 3,
        length, rootTimingNow, [&] () { return vr_verify_block (view, index, data, length, &vr_xnu_crypto); });
      if (checked != VR_OK) {
        __atomic_fetch_add (&native_msdc_integrity_failures, 1, __ATOMIC_RELAXED);
        native_msdc_failure = "window block failed its signed hash";
        // Diagnosis: a failing block has to name itself -- the length it was checked
        // with, what it read, and both digests.  Without this the only visible symptom
        // is APFS refusing to mount.
        uint8_t actual[32];
        if (view && view->hashes && vr_xnu_crypto.sha256) {
          vr_xnu_crypto.sha256 (data, length, actual);
          IOLog ("NativeMsdc: verify %llu len %zu data %02x%02x%02x%02x %02x%02x%02x%02x "
                 "got %02x%02x%02x%02x want %02x%02x%02x%02x\n",
                 index, length,
                 length > 0 ? data[0] : 0, length > 1 ? data[1] : 0,
                 length > 2 ? data[2] : 0, length > 3 ? data[3] : 0,
                 length > 4 ? data[4] : 0, length > 5 ? data[5] : 0,
                 length > 6 ? data[6] : 0, length > 7 ? data[7] : 0,
                 actual[0], actual[1], actual[2], actual[3],
                 view->hashes[index * 32 + 0], view->hashes[index * 32 + 1],
                 view->hashes[index * 32 + 2], view->hashes[index * 32 + 3]);
        }
        return kIOReturnNotPrivileged;
      }
      __atomic_fetch_add (&native_msdc_verified_blocks, 1, __ATOMIC_RELAXED);
      return kIOReturnSuccess;
    }
    uint64_t copy (uint64_t offset, const uint8_t *data, uint64_t length)
    {
      return buffer->writeBytes (offset, data, length);
    }
  } operations = { verifiedLock, buffer, &verified, firstSector, this, &cacheKey, configuration, windowBytes };
  VerifiedReadResult outcome = verifiedReadWindow (operations, windowBytes, block, count, verifiedBounce,
                                                   kTransferBytes, &verifiedCache, &cacheKey);
  if (outcome.status == kIOReturnNotPrivileged)
    IOLog ("NativeMsdc: %s: read of sectors %llu+%llu refused, a block failed its signed hash\n", label, block, count);
  completion->action (completion->target, completion->parameter, outcome.status, outcome.done);
  return kIOReturnSuccess;
}

// True when uuid is the volume UUID signed into this window's policy. digest (48 bytes, optional) receives the SHA-384
// of the authenticated envelope (header, signature and hash table), as NativeUfs reports it.
bool NativeMsdcBlock::authenticatesVolume (const char *uuid, uint8_t *manifestDigest) const
{
  if (!uuid || !online || !verifiedActive || !envelope || !verified.hashes) return false;
  static const char hex[] = "0123456789ABCDEF";
  char expected[37];
  unsigned at = 0;
  for (unsigned i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) expected[at++] = '-';
    expected[at++] = hex[verified.volume_uuid[i] >> 4];
    expected[at++] = hex[verified.volume_uuid[i] & 15];
  }
  expected[at] = 0;
  if (strcmp (uuid, expected)) return false;
  if (manifestDigest) {
    const uint64_t envelopeBytes = VR_HEADER_BYTES + VR_SIGNATURE_BYTES + verified.block_count * 32;
    SHA384_CTX context;
    SHA384_Init (&context);
    SHA384_Update (&context, envelope, envelopeBytes);
    SHA384_Final (manifestDigest, &context);
  }
  return true;
}

void NativeMsdcBlock::free ()
{
  if (verifiedBounce) {
    IOFree (verifiedBounce, kTransferBytes);
    verifiedBounce = nullptr;
  }
  if (verifiedLock) {
    IOLockFree (verifiedLock);
    verifiedLock = nullptr;
  }
  IOBlockStorageDevice::free ();
}

// The root authorization of NativeVirtioBlock (native_verified_block_root_authorized) asks every provider of the root
// media through this weak import: -1 when provider is not an eMMC window, else 1 if its signed volume UUID is uuid and
// 0 if not. NativeUfs defines the same symbol for its windows; a kernel links one of the two.
extern "C" int native_ufs_block_authenticates (IOService *provider, const char *uuid, uint8_t *digest)
{
  NativeMsdcBlock *block = OSDynamicCast (NativeMsdcBlock, provider);
  if (!block) return -1;
  return block->authenticatesVolume (uuid, digest) ? 1 : 0;
}


#endif // NATIVE_MSDC_VERIFIED
#endif // NATIVE_MSDC_BLOCK_DEVICES

// ---- Controller service ----------------------------------------------------------------------------------------------

class NativeMsdc : public IOService {
  OSDeclareDefaultStructors (NativeMsdc)
#if NATIVE_MSDC_BLOCK_DEVICES
  bool startRoot (bool allowWrites, const char **why);
#endif
public:
  IOService *probe (IOService *provider, SInt32 *score) override;
  bool start (IOService *provider) override;
  void stop (IOService *provider) override;
};
OSDefineMetaClassAndStructors (NativeMsdc, IOService)

#if NATIVE_MSDC_BLOCK_DEVICES
namespace {

NativeMsdcBlock *gPending[kMaxWindows];
RootPlan gPlan;
NativeMsdc *gPendingProvider;
bool gVerifyWindows;

// The window's image as flashed must hash to the manifest entry. Reads the whole window.
bool windowMatchesManifest (NativeMsdcBlock *device)
{
  const RootWindow &entry = gHost.manifest.window[device->windowIndex ()];
  uint64_t first = gAreaBaseSector + entry.offset / kSector, count = entry.size / kSector;
  SHA256_CTX context;
  SHA256_Init (&context);
  for (uint64_t done = 0; done < count;) {
    uint32_t blocks = count - done > kMaxBlocks ? kMaxBlocks : uint32_t (count - done);
    HostSession session;
    if (transfer (first + done, blocks, gHost.bounce, false) != kIOReturnSuccess) return false;
    SHA256_Update (&context, gHost.bounce, size_t (blocks) * kSector);
    done += blocks;
  }
  uint8_t digest[32];
  SHA256_Final (digest, &context);
  return !memcmp (digest, entry.sha256, 32);
}

// An APFS volume whose provider chain reaches this window: the window's container has been opened.
bool volumeBelow (IOService *window)
{
  OSDictionary *matching = IOService::serviceMatching ("IOMedia");
  OSIterator *iterator = matching ? IOService::getMatchingServices (matching) : nullptr;
  bool found = false;
  for (unsigned count = 0; iterator && !found && count < 256; ++count) {
    IORegistryEntry *media = OSDynamicCast (IORegistryEntry, iterator->getNextObject ());
    if (!media) break;
    if (strcmp (media->getMetaClass ()->getClassName (), "AppleAPFSVolume")) continue;
    IORegistryIterator *parents =
      IORegistryIterator::iterateOver (media, gIOServicePlane, kIORegistryIterateRecursively | kIORegistryIterateParents);
    for (unsigned steps = 0; parents && steps < 256; ++steps) {
      IORegistryEntry *entry = parents->getNextObject ();
      if (!entry) break;
      if (entry == window) {
        found = true;
        break;
      }
    }
    OSSafeReleaseNULL (parents);
  }
  OSSafeReleaseNULL (iterator);
  OSSafeReleaseNULL (matching);
  return found;
}

void discardPending (unsigned index)
{
  if (NativeMsdcBlock *device = gPending[index]) {
    device->detach (gPendingProvider);
    device->release ();
    gPending[index] = nullptr;
  }
}

// Ordered publication: each window before OS waits (up to 30 s) until its APFS volume appears, so SystemOS is
// resolvable when the root mounts. A window that fails its hash or its start is not published; the rest continue.
void publishThread (void *, wait_result_t)
{
#if NATIVE_MSDC_VERIFIED
  // Every window needs its signed policy before it is published; the region is read here, off the matching thread,
  // since it is tens of MiB. A window without an accepted policy is discarded and never published.
  const char *policyFailure = loadPolicy ();
  if (policyFailure) IOLog ("NativeMsdc: signed root policy unusable: %s; no window is published\n", policyFailure);
  unsigned verifiedWindows = 0;
  for (unsigned index = 0; index < gHost.manifest.count; ++index) {
    NativeMsdcBlock *device = gPending[index];
    if (!device) continue;
    int status = -1;
    const char *why = policyFailure ? policyFailure : device->configureVerification (&status);
    if (why) {
      IOLog ("NativeMsdc: %s not published: %s (vr_open %d)\n", device->windowLabel (), why, status);
      native_msdc_failure = why;
      discardPending (index);
      continue;
    }
    IOLog ("NativeMsdc: %s verified against its signed policy\n", device->windowLabel ());
    ++verifiedWindows;
  }
  native_msdc_verified_windows = verifiedWindows;
  if (!verifiedWindows) freePolicy ();
#endif
  for (unsigned position = 0; position < gPlan.count; ++position) {
    unsigned index = gPlan.order[position];
    NativeMsdcBlock *device = gPending[index];
    if (!device) continue;
    if (gVerifyWindows && !windowMatchesManifest (device)) {
      IOLog ("NativeMsdc: %s does not match its manifest SHA-256; not published\n", device->windowLabel ());
      native_msdc_failure = "window SHA-256 mismatch";
      discardPending (index);
      continue;
    }
    if (!device->start (gPendingProvider)) {
      IOLog ("NativeMsdc: %s did not start; not published\n", device->windowLabel ());
      native_msdc_failure = "window start failed";
      discardPending (index);
      continue;
    }
    gPending[index] = nullptr;          // owned by the registry from here on
    native_msdc_blocks_published = native_msdc_blocks_published + 1;
    IOLog ("NativeMsdc: published %s\n", device->windowLabel ());
    if (position + 1 == gPlan.count) {
      device->release ();
      break;
    }
    bool ready = false;
    for (unsigned tick = 0; tick < 300 && !(ready = volumeBelow (device)); ++tick) IOSleep (100);
    IOLog ("NativeMsdc: %s volume %s\n", device->windowLabel (), ready ? "ready" : "not seen after 30 s");
    device->release ();
  }
  native_msdc_step = 6;
  gPendingProvider->release ();
}

} // namespace
#endif // NATIVE_MSDC_BLOCK_DEVICES

IOService *NativeMsdc::probe (IOService *provider, SInt32 *score)
{
  if (!IOService::probe (provider, score) || !listHas (provider->getProperty ("compatible"), "mediatek,mt6765-msdc"))
    return nullptr;
  if (score) *score = 1000;
  return this;
}

bool NativeMsdc::start (IOService *provider)
{
#if NATIVE_MSDC_VERIFIED
  gRootTimingEnabled = bootArgument ("native-root-timing", 1, 0) == 1;
#endif
  unsigned root = bootArgument ("native-msdc-root", 2, 0);
  unsigned persist = bootArgument ("native-msdc-persist", 2, 0);
  unsigned dma = bootArgument ("native-msdc-dma", 1, 0);
  if (!root && !persist) return false;   // matched only; the controller is not touched
  gModeTelemetryEnabled = bootArgument ("native-msdc-mode", 1, 0) == 1;
  if (gHost.base) {
    IOLog ("NativeMsdc: one controller only; %s ignored\n", provider->getName ());
    return false;
  }
  native_msdc_step = 1;
  if (!IOService::start (provider)) return false;
  const char *why = "";
  if (!hostBringUp (provider, dma == 1, &why) || !hostFindLayout (&why)) {
    fail (why);
    IOService::stop (provider);
    return false;
  }
  if (!gHost.manifestValid) fail (manifestReason (native_msdc_manifest_status));
#if NATIVE_MSDC_BLOCK_DEVICES
  else if (root) {
    gVerifyWindows = bootArgument ("native-msdc-verify", 1, 0) == 1;
    if (!startRoot (root == 2, &why)) fail (why);
  }
#else
  else if (root) fail ("native-msdc-root: built without the window block devices (NATIVE_MSDC_BLOCK_DEVICES=0)");
#endif
  return true;
}

#if NATIVE_MSDC_BLOCK_DEVICES
bool NativeMsdc::startRoot (bool allowWrites, const char **why)
{
  if (!buildRootPlan (gHost.manifest, gPlan)) {
    *why = "manifest has no OS window";
    return false;
  }
  gPendingProvider = this;
  for (unsigned index = 0; index < gHost.manifest.count; ++index) {
    const RootWindow &entry = gHost.manifest.window[index];
    NativeMsdcBlock *device = OSTypeAlloc (NativeMsdcBlock);
    gPending[index] = device;
    if (!device || !device->init (static_cast<OSDictionary *> (nullptr)) ||
        !device->configure (index, entry, gAreaBaseSector + entry.offset / kSector, allowWrites)) {
      if (device) device->release ();
      gPending[index] = nullptr;
      while (index) discardPending (--index);
      *why = "window preparation failed";
      return false;
    }
    if (!device->attach (this)) {
      device->release ();
      gPending[index] = nullptr;
      while (index) discardPending (--index);
      *why = "window attach failed";
      return false;
    }
  }
  retain ();
  thread_t thread = nullptr;
  if (kernel_thread_start (publishThread, nullptr, &thread) != KERN_SUCCESS) {
    release ();
    for (unsigned index = gHost.manifest.count; index;) discardPending (--index);
    *why = "publisher thread";
    return false;
  }
  thread_deallocate (thread);
  native_msdc_step = 5;
  return true;
}
#endif // NATIVE_MSDC_BLOCK_DEVICES

void NativeMsdc::stop (IOService *provider)
{
  // Published windows may still have clients; the host stays as it is.
  IOService::stop (provider);
}

// ---- Persistent store window (NativeRamBlock's overlay, see PersistOverlay.h) --------------------------------------
// 4096-byte blocks, relative to the window. Mode 1 only reads; mode 2 also writes. Writes return when the blocks are
// on the medium (written, then the card cache flushed), so the overlay needs no separate flush.
namespace {
uint64_t gStoreFirstSector, gStoreBlocks;

unsigned storeMode (void)
{
  static int cached = -1;
  if (cached < 0) cached = int (bootArgument ("native-msdc-persist", 2, 0));
  return unsigned (cached);
}

uint64_t reserveBlocks (void)
{
  // The store grows down from the end of userdata, so the iOS area at the end has to be
  // reserved in addition to the operator's own reserve.
  return uint64_t (bootArgument ("native-msdc-reserve", 16384, 1024)) * (1024 * 1024 / kStoreBlock);
}

bool storeTransfer (uint64_t relative, uint32_t blocks, uint8_t *data, bool write)
{
  if (!gStoreBlocks || !data || !blocks || relative >= gStoreBlocks || blocks > gStoreBlocks - relative) return false;
  constexpr uint32_t perTransfer = uint32_t (kMaxBlocks / kSectorsPerStoreBlock);
  while (blocks) {
    uint32_t chunk = blocks > perTransfer ? perTransfer : blocks;
    HostSession session;
    if (transfer (gStoreFirstSector + relative * kSectorsPerStoreBlock, uint32_t (chunk * kSectorsPerStoreBlock), data,
                  write) != kIOReturnSuccess)
      return false;
    data += chunk * kStoreBlock;
    relative += chunk;
    blocks -= chunk;
  }
  return true;
}
} // namespace

extern "C" unsigned native_msdc_store_mode (void) { return storeMode (); }

// Returns 1 and the first absolute 512-byte sector of the window, 0 on refusal (reason in *why, may be null).
extern "C" int native_msdc_store_open (uint64_t blocks, uint64_t *firstSector, const char **why)
{
  const char *reason = "";
  int ok = 0;
  if (!storeMode ()) reason = "off";
  else if (!gHost.ready || !gHost.manifestValid || gHost.poisoned) reason = "no root layout";
  else {
    HostSession session;
    uint64_t first = 0;
    // The store also stays clear of the space the signed root policy region may use after the windows.
    uint64_t protectedBytes = gHost.manifest.imageBytes;
#if NATIVE_MSDC_VERIFIED
    if (policyRegionOffset (gHost.manifest) + kPolicyMaxBytes > protectedBytes)
      protectedBytes = policyRegionOffset (gHost.manifest) + kPolicyMaxBytes;
#endif
    if (gStoreBlocks) reason = "already open";
    else if (const char *refused = storeWindow (gHost.gpt.userdataLast - gHost.gpt.userdataFirst + 1,
                                                protectedBytes, reserveBlocks (), blocks, first))
      reason = refused;
    else {
      gStoreFirstSector = gHost.gpt.userdataFirst + first * kSectorsPerStoreBlock;
      gStoreBlocks = blocks;
      if (firstSector) *firstSector = gStoreFirstSector;
      ok = 1;
    }
  }
  if (why) *why = reason;
  if (ok) IOLog ("NativeMsdc: store at sector %llu, %llu blocks, mode %u\n", gStoreFirstSector, gStoreBlocks, storeMode ());
  else IOLog ("NativeMsdc: store refused: %s\n", reason);
  return ok;
}

extern "C" int native_msdc_store_read (uint64_t relative, uint32_t blocks, void *destination)
{
  return storeTransfer (relative, blocks, static_cast<uint8_t *> (destination), false);
}

extern "C" int native_msdc_store_write (uint64_t relative, uint32_t blocks, const void *source)
{
  if (storeMode () != 2) return 0;
  // transfer() only reads from the buffer on a write.
  if (!storeTransfer (relative, blocks, static_cast<uint8_t *> (const_cast<void *> (source)), true)) return 0;
  HostSession session;
  return hostFlush () == kIOReturnSuccess;
}

extern "C" int native_msdc_store_sync (void)
{
  if (storeMode () != 2 || !gStoreBlocks || gHost.poisoned) return 0;
  HostSession session;
  return hostFlush () == kIOReturnSuccess;
}

// Re-read the GPT header and entry array and compare their CRCs with those recorded at start. 0 = unchanged.
extern "C" unsigned native_msdc_store_gpt_check (void)
{
  if (!gHost.ready || !gHost.layoutValid || gHost.poisoned) return 1;
  HostSession session;
  if (transfer (kGptHeaderLba, 1, gHost.bounce, false) != kIOReturnSuccess) return 2;
  GptLayout now;
  if (gptCheckHeader (gHost.bounce, gHost.diskSectors, now) || now.headerCrc != gHost.gpt.headerCrc) return 3;
  if (transfer (gHost.gpt.entriesLba, gHost.gpt.entrySectors, gHost.bounce, false) != kIOReturnSuccess) return 4;
  return gptCrc32 (gHost.bounce, size_t (gHost.gpt.entryCount) * kGptEntryBytes) == gHost.gpt.entriesCrc ? 0 : 5;
}

#if defined(NATIVE_MSDC_UFS_STORE_ABI) && NATIVE_MSDC_UFS_STORE_ABI
// NativeRamBlock imports the store under the native_ufs_store_* names (weak). A board without NativeUfs selects this
// to give it the eMMC store instead; linking both drivers with it fails on the duplicate symbols.
extern "C" unsigned native_ufs_store_mode (void) { return native_msdc_store_mode (); }
extern "C" int native_ufs_store_open (uint64_t blocks, uint64_t *first, const char **why)
{
  return native_msdc_store_open (blocks, first, why);
}
extern "C" int native_ufs_store_read (uint64_t relative, uint32_t blocks, void *destination)
{
  return native_msdc_store_read (relative, blocks, destination);
}
extern "C" int native_ufs_store_write (uint64_t relative, uint32_t blocks, const void *source)
{
  return native_msdc_store_write (relative, blocks, source);
}
extern "C" int native_ufs_store_sync (void) { return native_msdc_store_sync (); }
extern "C" unsigned native_ufs_store_gpt_check (void) { return native_msdc_store_gpt_check (); }
#endif

// ---------------------------------------------------------------------------
// dandelion bring-up hook.
//
// Upstream is loaded as a kext, so its Info.plist personality does the matching.
// Here the driver is linked into the kernel, so nothing would ever instantiate
// it: this installs the same match as a notification.  addMatchingNotification
// also runs the handler for platform devices that already exist, so calling it
// from bsd_init works whether or not the nub has been published yet.
//
// The driver itself stays default-off: without native-msdc-root or
// native-msdc-persist it matches and returns without touching the controller.
// ---------------------------------------------------------------------------
static bool dandelion_msdc_matched (void *, void *, IOService *service, IONotifier *)
{
  SInt32 score = 0;
  NativeMsdc *driver = new NativeMsdc;
  if (!driver) return true;
  if (!driver->init ()) {
    driver->release ();
    return true;
  }
  if (!driver->probe (service, &score)) {
    driver->release ();
    return true;
  }
  if (!driver->attach (service)) {
    driver->release ();
    return true;
  }
  if (!driver->start (service)) {
    driver->detach (service);
    driver->release ();
  }
  return true;
}

extern "C" void dandelion_msdc_install (void)
{
  OSDictionary *matching = IOService::serviceMatching ("IOPlatformDevice");
  if (!matching) {
    IOLog ("NativeMsdc: no IOPlatformDevice matching\n");
    return;
  }
  if (!IOService::addMatchingNotification (gIOMatchedNotification, matching,
                                           dandelion_msdc_matched, nullptr, nullptr, 0)) {
    IOLog ("NativeMsdc: matching notification not installed\n");
  }
}
