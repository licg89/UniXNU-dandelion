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
// Transfers that stalled once and then succeeded on a whole-transfer retry.
volatile uint64_t native_msdc_retries = 0;
// MSDC_CFG.CKDIV and MSDC_IOCON read-data sampling delay in effect after bring-up.
volatile uint32_t native_msdc_clock_divider = 0;
volatile uint32_t native_msdc_sample_delay = 0;
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

// One command and its data phase.  A 64 KiB PIO block takes about 0.2 s at the measured
// A 64 KiB PIO block takes about 0.2 s at the measured 320 KB/s.  Ten seconds leaves
// generous room for a slow card under APFS load without hiding a real hardware fault.
constexpr uint64_t kCommandTimeoutNs = 10000000000ULL;
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
// Where the time actually goes on a read.  A 64 KiB transfer at this bus rate should take about
// 0.65 ms; the policy read measures about 190 ms per 64 KiB, so 99.7% of it is not the transfer.
// These accumulate the three phases and a summary is printed every 256 reads, which says whether
// the cost is the command, the data phase setup, or the FIFO drain -- and each of those is a
// different fix.  Pure PIO, no register is written that the working path does not already write.
static uint64_t readPhaseTicks[4];
static unsigned readPhaseCount, readPhaseSamples;
// Two counters that separate "the card is slow" from "our drain loop is slow": spins where the FIFO
// reported nothing and the loop went round again, and words actually read out of the FIFO.  A 64 KiB
// transfer needs exactly 16384 word reads, so if wordsRead matches that and the drain still takes
// ~98 ms, each MMIO read costs about 6 us and the loop is the problem; if the spins dominate, the
// transfer is waiting for the card.
static uint64_t readFifoSpins, readFifoWords;

static void readPhaseSummary (void)
{
  if (++readPhaseCount % 256) return;
  const uint64_t n = readPhaseSamples ? readPhaseSamples : 1;
  const uint64_t per = readPhaseTicks[3] / n;
  IOLog ("NativeMsdc: read timing setblk=%llu prep=%llu cmd=%llu drain=%llu total=%llu (%llu us each)\n",
         readPhaseTicks[0] / n, readPhaseTicks[1] / n, readPhaseTicks[2] / n,
         (readPhaseTicks[3] - readPhaseTicks[0] - readPhaseTicks[1] - readPhaseTicks[2]) / n, per,
         (per * 1000) / 24000);
  IOLog ("NativeMsdc: read drain spins=%llu words=%llu per-read (spins %llu words %llu)\n",
         readFifoSpins, readFifoWords, readFifoSpins / n, readFifoWords / n);
  for (unsigned i = 0; i < 4; ++i) readPhaseTicks[i] = 0;
  readPhaseSamples = 0;
  readFifoSpins = 0;
  readFifoWords = 0;
}

IOReturn hostReadPio (unsigned opcode, uint32_t argument, uint8_t *buffer, uint32_t blocks)
{
  if (!blocks || blocks > kMaxBlocks) return kIOReturnBadArgument;
  uint32_t response = 0;
  bool multi = blocks > 1;
  IOReturn result;
  uint64_t phase0 = mach_absolute_time ();
  if (multi) {
    result = hostCommand (kCmdSetBlockCount, blocks, Data::None, &response);
    if (result != kIOReturnSuccess) return result;
    opcode = kCmdReadMulti;
  }
  uint64_t phase1 = mach_absolute_time ();
  result = hostPrepareData (blocks);
  if (result != kIOReturnSuccess) return result;
  uint64_t phase2 = mach_absolute_time ();
  result = hostCommand (opcode, argument, multi ? Data::ReadMulti : Data::ReadSingle, &response);
  uint64_t phase3 = mach_absolute_time ();
  readPhaseTicks[0] += phase1 - phase0;
  readPhaseTicks[1] += phase2 - phase1;
  readPhaseTicks[2] += phase3 - phase2;
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
      // MSDC_FIFOCS reports the receive level in bytes here: this must stay a byte count, because
      // treating it as words stopped the root volume from mounting at all when it was tried.
      unsigned available = rd (kMsdcFifoCs) & 255u;
      if (!available && offset < total) ++readFifoSpins;
      while (available && offset < total) {
        if (available >= 4 && total - offset >= 4) {
          uint32_t word = rd (kMsdcRxData);
          ++readFifoWords;
          memcpy (buffer + offset, &word, 4);
          offset += 4;
          available -= 4;
        } else {
          buffer[offset++] = *reinterpret_cast<volatile uint8_t *> (gHost.base + kMsdcRxData);
          available--;
        }
      }
      if (done && offset == total) {
        readPhaseTicks[3] += mach_absolute_time () - phase0;
        ++readPhaseSamples;
        readPhaseSummary ();
        return kIOReturnSuccess;
      }
      // All bytes are in the buffer and the controller is idle: the XferDone interrupt may
      // have been consumed by the same status read, so do not wait for a bit that will never
      // come.  This is what used to turn a completed read into ETIMEDOUT (errno 60).
      if (offset == total && !(rd (kSdcSts) & kStsSdcBusy) &&
          !(rd (kMsdcFifoCs) & 255u)) {
        readPhaseTicks[3] += mach_absolute_time () - phase0;
        ++readPhaseSamples;
        readPhaseSummary ();
        return kIOReturnSuccess;
      }
    } while (mach_absolute_time () < end);
  }
  if (multi) (void) hostCommand (kCmdStop, 0, Data::None, &response);
  return result;
}

// Defined further down.  The DMA failure paths below have to stop the card and leave it in a state
// where the next read works, which is what this does.
void hostRecoverAfterError (void);

// Descriptor DMA read into the private bounce buffer, then a copy out.  A failed transfer no longer
// quarantines anything: it puts the controller back in PIO, stops the card and returns an error, so
// the caller falls back to the path that works and the boot continues.
IOReturn hostReadDma (uint32_t argument, uint8_t *buffer, uint32_t blocks)
{
  if (!gHost.dmaVirt || !blocks || blocks > kMaxBlocks) return kIOReturnBadArgument;
  // The busy guard that used to be here (holding native_msdc_busy so the console would decline) is
  // gone: when the DMA transfer hangs, the console -- which is written through this same card --
  // stays blocked for as long as the hang lasts, and the boot went silent after 121 lines with the
  // controller holding the card.  A failed experiment must not be able to silence the machine.
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
  // Order follows the vendor's msdc_dma_setup() and msdc_start_data(): MSDC_DMA_CFG.DECSEN, then
  // MSDC_DMA_CTRL, and the descriptor address (MSDC_DMA_SA) written last --- the vendor writes
  // gpd_addr after both.  This path used to write SA first, which is the one ordering difference
  // left from a setup that otherwise matches register for register.  The command is issued with
  // Data::None so the data phase belongs to the engine, and START follows the response.
  wr (kDmaCfg, rd (kDmaCfg) | kDmaCfgDescEn);
  wr (kDmaCtrl, (rd (kDmaCtrl) & ~(kDmaCtrlStart | kDmaCtrlStop | kDmaCtrlLastBuffer | kDmaCtrlBurstMask)) |
                kDmaCtrlDescriptor | kDmaCtrlBurst64);
  wr (kDmaSaHigh, 0);
  wr (kDmaSa, uint32_t (gHost.dmaPhys));
  // 0xa8 is not written any more: see the note in NativeMsdc.h.  The vendor programs the descriptor
  // address and nothing else in that gap.
  /* Command only: Data::None leaves the data phase to the engine, which is started below. */
  result = hostCommand (multi ? kCmdReadMulti : kCmdReadSingle, argument, Data::None, &response);
  // hostCommand runs its transaction with PIO selected and restores the configuration afterwards,
  // so the DMA mode set above is gone by the time it returns.  The trace proved it: at the point
  // where the engine should have been running, MSDC_CFG still had PIO set (cfg=02210499), DMA_CTRL
  // had no START bit and the GPD's HWO was still 1 -- the engine had never been asked to do
  // anything.  Clear PIO again here, after the command path has finished with the controller, and
  // only then start the engine.
  wr (kMsdcCfg, rd (kMsdcCfg) & ~kCfgPio);
  // Two readings of the same register, one line apart: if PIO is already back at the first one the
  // write did not stick (wrong bit, or a read-modify-write race), and if it comes back at the second
  // something between the arm and the start put it back.  Everything else in the trace is the same
  // for both cases, which is why this could not be settled from the one line.
  IOLog ("NativeMsdc: dma arm cfg=%08x pio=%u dma=%08x\n", rd (kMsdcCfg),
         unsigned ((rd (kMsdcCfg) & kCfgPio) != 0), rd (kDmaCfg));
  {
    const IOReturn commandResult = result;
    wr (kDmaCtrl, rd (kDmaCtrl) | kDmaCtrlStart);
    IOLog ("NativeMsdc: dma start cfg=%08x pio=%u ctrl=%08x\n", rd (kMsdcCfg),
           unsigned ((rd (kMsdcCfg) & kCfgPio) != 0), rd (kDmaCtrl));
    const uint64_t start = mach_absolute_time ();
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
      // The interrupt *status* is looked at rather than waited for, so a masked INTEN (HostSession
      // clears it for the duration of a request) only hides the IRQ, not the event.  Belt and
      // braces on top of that: an engine that has gone idle while the card is not busy has finished,
      // and treating that as done is what the PIO path already does.
      if (mach_absolute_time () - start > 1000000 &&
          !(rd (kDmaCfg) & kDmaCfgStatus) && !(rd (kSdcSts) & kStsSdcBusy)) {
        break;
      }
      if (mach_absolute_time () >= end) {
        result = kIOReturnTimeout;
        break;
      }
    }
    // The engine was started even when the command path reported a failure, so that the trace below
    // always describes a machine that was actually asked to transfer.  The command's error is still
    // what the caller gets when the transfer itself looked fine.
    if (result == kIOReturnSuccess && commandResult != kIOReturnSuccess) result = commandResult;
  }
  // What the engine did, in one line: the GPD's HWO bit is cleared by the hardware when it takes
  // the descriptor, so reading it back says whether the engine ever fetched one; DMA_CTRL says
  // whether START survived; MSDC_CFG says whether the controller was still in DMA mode.
  cacheCleanInvalidate (gHost.dmaVirt, kDmaDescriptorBytes);
  IOLog ("NativeMsdc: dma trace gpd_info=%08x gpd_len=%08x gpd_blk=%u bd_info=%08x bd_len=%08x "
         "intr=%08x inten=%08x sts=%08x dmactrl=%08x cfg=%08x\n",
         gpd[0].info, gpd[0].length, gpd[0].blocks, bd->info, bd->length,
         rd (kMsdcIntr), rd (kMsdcIntEn), rd (kSdcSts), rd (kDmaCtrl), rd (kMsdcCfg));
  wr (kDmaCtrl, rd (kDmaCtrl) | kDmaCtrlStop);
  // The vendor polls two places after a stop, and both are watched here: MSDC_DMA_CTRL.STOP
  // clearing itself (mtk-sd.c:1601-1605) and MSDC_DMA_CFG.STS clearing (1609-1610).  The earlier
  // change to watch only the first was wrong about the second being the wrong register -- the port
  // was already watching a register the vendor watches.  Either clearing means the engine is idle.
  bool stopped = false;
  for (unsigned poll = 0; poll < 2000; ++poll) {
    if (!(rd (kDmaCtrl) & kDmaCtrlStop) || !(rd (kDmaCfg) & kDmaCfgStatus)) {
      stopped = true;
      break;
    }
    IODelay (10);
  }
  if (!stopped) {
    // A DMA experiment must not decide whether the machine boots.  This used to poison the
    // transport here, which left the controller unable to read at all: the root volume never
    // appeared ("Still waiting for root device") and the boot was over.  Instead put the
    // controller back in PIO, forget the DMA buffer, and let the caller fall back to the path that
    // is known to work.  Same for every other DMA failure below.
    wr (kDmaCfg, rd (kDmaCfg) & ~kDmaCfgDescEn);
    wr (kMsdcCfg, rd (kMsdcCfg) | kCfgPio);
    gHost.dmaEnabled = false;
    native_msdc_dma = 2;
    native_msdc_failure = "DMA engine did not stop; PIO kept";
    IOLog ("NativeMsdc: DMA engine did not stop; PIO kept\n");
    return kIOReturnNotReady;
  }
  wr (kDmaCfg, rd (kDmaCfg) & ~kDmaCfgDescEn);
  wr (kMsdcCfg, rd (kMsdcCfg) | kCfgPio);
  if (result != kIOReturnSuccess) {
    // A failed engine leaves the card in the middle of a data phase, and every later read then
    // fails -- that is what took the root volume away after the earlier attempts.  Stop the
    // transfer and let the card return to transfer state before giving up.
    if (multi) (void) hostCommand (kCmdStop, 0, Data::None, &response);
    hostRecoverAfterError ();
    return result;
  }
  result = waitClear (kSdcSts, kStsSdcBusy);
  if (result != kIOReturnSuccess) {
    hostRecoverAfterError ();
    return result;
  }
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
  // A stall is retried as a whole transfer, never resumed.  hostReadPio clears the FIFO
  // (hostPrepareData) and refills the buffer from offset zero on every attempt, so a retry
  // cannot leave stale bytes behind and the verified reader still hashes a complete, freshly
  // read block.  An earlier attempt resumed a partially filled buffer instead, which is what
  // turned a stall into a window hash mismatch ("offline owner SystemOS mount failed: 80").
  // Reads are retried; a write is left alone because its data path is not the bottleneck.
  IOReturn result = kIOReturnTimeout;
  const unsigned attempts = write ? 1u : 6u;
  // What the user space actually reads decides why a boot takes so long: a sequential sweep of the
  // cryptex window would mean the whole shared cache is being hashed, while scattered small reads
  // mean plist and metadata traffic.  One line every 400 reads is cheap on the console.
  if (!write) {
    static volatile uint64_t traceCounter = 0;
    if ((__atomic_fetch_add (&traceCounter, 1, __ATOMIC_RELAXED) % 400) == 0) {
      IOLog ("NativeMsdc: read trace sector=%llu blocks=%u\n",
             (unsigned long long)sector, blocks);
    }
  }
  for (unsigned attempt = 0; attempt < attempts; attempt++) {
    if (write) result = hostWrite (uint32_t (sector), buffer, blocks);
    else if (gHost.dmaEnabled) result = hostReadDma (uint32_t (sector), buffer, blocks);
    else result = hostReadPio (kCmdReadSingle, uint32_t (sector), buffer, blocks);
    if (result == kIOReturnSuccess) break;
    // A rejected argument or a poisoned engine will not improve with another attempt.
    if (result == kIOReturnBadArgument || gHost.poisoned) break;
    if (attempt + 1 < attempts) {
      __atomic_fetch_add (&native_msdc_retries, 1, __ATOMIC_RELAXED);
      // Give the card a moment to leave the failed data state before the next command.
      IODelay (1000);
    }
  }
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

// The bootloader leaves MSDC_CFG.CKDIV at the value it used for slow enumeration (measured 260) and
// MSDC_IOCON's read-data sampling delay at 0.  That pair only works at the slow rate: raising the
// divider alone made "Failed to mount root device", because the sampling point has to move with the
// bus rate.  Sweep both and keep the first pair whose reads still match references taken at the
// inherited settings; anything that does not verify restores the inherited registers, so a wrong
// guess costs nothing.  A bad pair normally fails fast with a CRC error rather than timing out.
// Verify a divider at several places on the card, not one: under load at a wrong rate, signal
// integrity varies across the die, and a single sector at 0x40 passed while the policy region was
// 73 MB spread over the user area.  All of these have to match before a divider is kept.
static const uint32_t kSweepSectors[] = { 0x40, 0x445d000, 0x5000000, 0x6000000 };
constexpr unsigned kSweepCount = sizeof (kSweepSectors) / sizeof (kSweepSectors[0]);

// A failed single-block read can leave the card mid-transfer; SEND_STATUS until it reports the
// transfer state again, otherwise every later command in the sweep fails for the wrong reason.
void hostRecoverAfterError (void)
{
  for (unsigned i = 0; i < 50; i++) {
    uint32_t response = 0;
    if (hostCommand (kCmdStatus, kStatusArgument, Data::None, &response) == kIOReturnSuccess &&
        cardInTransfer (response)) {
      return;
    }
    IODelay (200);
  }
}

bool hostSweepMatches (const uint8_t *reference)
{
  uint8_t *candidate = gHost.bounce + kSweepCount * kSector;
  for (unsigned i = 0; i < kSweepCount; i++) {
    if (hostReadPio (kCmdReadSingle, kSweepSectors[i], candidate, 1) != kIOReturnSuccess) {
      hostRecoverAfterError ();
      return false;
    }
    if (memcmp (reference + i * kSector, candidate, kSector) != 0) {
      hostRecoverAfterError ();
      return false;
    }
  }
  return true;
}

// Changing MSDC_CFG.CKDIV with the card clock running glitches the bus, so the vendor mtk-sd.c
// stops the clock, sets the divider, and starts it again.  Two details matter and both were wrong
// here before:
//   * MSDC_CFG_CKPDN reads as "clock running" when set -- the vendor clears it to switch the clock
//     off (see its hz == 0 path) and sets it again afterwards.  Setting it first and clearing it at
//     the end, as this function used to, leaves the card clock stopped and every read fails.
//   * SDC_CFG.DTOC counts units of 2^20 card clocks, so it has to be rescaled with the divider or
//     the card times out immediately at the new rate.
void hostSetClockDivider (unsigned divider, unsigned previous)
{
  const uint32_t cfg = rd (kMsdcCfg);
  // CKDIV is bits [15:8] and CKMOD is bits [17:16]; the vendor writes them as separate fields
  // (mtk-sd.c: MSDC_CFG_CKDIV GENMASK(15,8), MSDC_CFG_CKMOD GENMASK(17,16), mode 2 = DDR with
  // divisor).  This code used to write "divider << 8" through a 12-bit mask, which rewrote CKMOD
  // as well: divider 4 became 0x400, clearing the DDR clock mode the firmware had established, so
  // the bus ran in the wrong mode.  That is the difference between the boots that only logged
  // "verify ... got ... want" mismatches and the boots where every read answered EIO and the
  // console died with it (it is written through this same card).  Write CKDIV only and leave the
  // mode the firmware left.
  const uint32_t stopped = (cfg & ~(0xffu << 8)) & ~kCfgCkPdn;
  wr (kMsdcCfg, stopped);                                  // clock off
  IODelay (200);
  const uint32_t retimed = stopped | ((divider & 0xffu) << 8);
  wr (kMsdcCfg, retimed);                                  // divider set while stopped
  IODelay (200);
  wr (kMsdcCfg, retimed | kCfgCkPdn);                      // clock on
  for (unsigned i = 0; i < 100; i++) {
    if (rd (kMsdcCfg) & kCfgCkStb) break;
    IODelay (100);
  }
  // sclk scales as 1/(CKDIV+1), so the timeout register scales the other way.
  const uint32_t sdc = rd (kSdcCfg);
  unsigned dtoc = (sdc & kSdcCfgDtoc) >> 24;
  uint64_t scaled = (uint64_t)dtoc * (previous + 1) / (divider + 1);
  if (dtoc == 0) scaled = 0;
  else if (scaled == 0) scaled = 1;
  if (scaled > 255) scaled = 255;
  wr (kSdcCfg, (sdc & ~kSdcCfgDtoc) | ((uint32_t)scaled << 24));
  // The first transfer right after a switch failed in the self test; give the bus time to settle.
  IODelay (20000);
  IOLog ("NativeMsdc: ckdiv %u (from %u), dtoc %u -> %u, cfg %08x\n",
         divider, previous, dtoc, (unsigned)scaled, rd (kMsdcCfg));
}

// Card speed-mode switch: CMD6 (SWITCH) writing one EXT_CSD byte, then wait for the card to leave the
// programming state.  The argument shape is the one kFlushCacheArgument already uses -- access 1 in
// [31:26], "write byte" 3 in [25:24], the index in [23:16] and the value in [15:8].
bool hostSwitchExtCsd (unsigned index, unsigned value)
{
  const uint32_t argument = (1u << 26) | (3u << 24) | ((index & 0xffu) << 16) | ((value & 0xffu) << 8);
  uint32_t response = 0;
  IOReturn result = hostCommand (kCmdSwitch, argument, Data::None, &response);
  // Say why, not just "refused": the R1 response carries the card's status, whose low 16 bits are
  // the error flags and [12:9] the current state.  Without them a failed switch is indistinguishable
  // from a command that never completed.
  IOLog ("NativeMsdc: CMD6 %s arg=%08x rsp=%08x state=%u errors=%04x\n",
         result == kIOReturnSuccess ? "ok" : "failed", argument, response,
         unsigned ((response >> 9) & 0xfu), unsigned (response & 0xffffu));
  if (result != kIOReturnSuccess) return false;
  // The card is busy while it programs; SEND_STATUS until it reports the transfer state again.
  for (unsigned i = 0; i < 100; ++i) {
    uint32_t status = 0;
    if (hostCommand (kCmdStatus, kStatusArgument, Data::None, &status) != kIOReturnSuccess) return false;
    if (i == 0) {
      IOLog ("NativeMsdc: CMD13 rsp=%08x state=%u errors=%04x\n", status,
             unsigned ((status >> 9) & 0xfu), unsigned (status & 0xffffu));
    }
    if (cardInTransfer (status)) return true;
    IODelay (1000);
  }
  IOLog ("NativeMsdc: card never left the programming state\n");
  return false;
}

// The Android kernel on this same phone says what the hardware should be doing:
//
//   msdc0 -> Set<400KHz>    Source<400000KHz> -> sclk<400KHz>   timing<0>  div<250>
//   msdc0 -> Set<200000KHz> Source<400000KHz> -> sclk<200000KHz> timing<9>  mode<0> div<0>
//   [AUTOK] eMMC HS200 Tune
//   msdc0 -> Set<200000KHz> Source<400000KHz> -> sclk<200000KHz> timing<10> mode<3> div<0>
//   [AUTOK] eMMC HS400 Tune   ->  "new HS400 MMC card"
//
// So the card is meant to run in HS200/HS400 at 200 MHz off the 400 MHz MSDCPLL, and the boot
// firmware leaves it in plain HS at the bring-up clock.  That is why every divider this driver
// tried changed nothing: the card never left HS, and the host clock never left the bring-up rate.
// HS200 is the mode this driver can reach with what it already has (8-bit bus, a working sampling
// sweep and CMD6), so it is tried first, with the card put back to HS if anything fails to verify.
// The card's bus width.  This is the one part of the speed problem that needs no 1.8 V: eight data
// lines work at 3.3 V and in High Speed, and the firmware's own storage diagnostics on this board
// record the card coming back as 1-bit ("inherits from LK ... i.e. 1-bit bus") after their CMD6
// experiments, which is what this kernel then inherits.  One bit instead of eight is a factor of
// eight on every transfer, on top of whatever the clock is doing.
bool hostSetBusWidth (unsigned bits)
{
  const unsigned extCsd = bits == 8 ? 2u : bits == 4 ? 1u : 0u;   // EXT_CSD[183]: 0 1-bit, 1 4-bit, 2 8-bit
  if (!hostSwitchExtCsd (183, extCsd)) {
    IOLog ("NativeMsdc: BUS_WIDTH=%u refused\n", extCsd);
    return false;
  }
  // Host side: SDC_CFG[17:16] is the bus width, 0 = 1-bit, 1 = 4-bit, 2 = 8-bit.
  const uint32_t sdc = rd (kSdcCfg);
  wr (kSdcCfg, (sdc & ~(0x3u << 16)) | ((extCsd & 0x3u) << 16));
  IODelay (20000);
  IOLog ("NativeMsdc: bus width %u written (card %u, sdc %08x -> %08x)\n", bits, extCsd, sdc, rd (kSdcCfg));
  return true;
}

// Sweep the read sampling delay at whatever clock and bus width are in force now, keep the centre of
// the widest passing run, and report whether the reference sectors still read back identical.
bool hostRetune (const uint8_t *reference, int *windowFirst, int *windowLast)
{
  const uint32_t iocon = rd (kMsdcIoCon);
  int first = -1, last = -1;
  for (unsigned sample = 0; sample < 64; ++sample) {
    wr (kMsdcIoCon, (iocon & ~(0x3fu << 16)) | ((sample & 0x3fu) << 16));
    IODelay (200);
    if (hostSweepMatches (reference)) {
      if (first < 0) first = (int)sample;
      last = (int)sample;
    }
  }
  if (windowFirst) *windowFirst = first;
  if (windowLast) *windowLast = last;
  if (first < 0) return false;
  wr (kMsdcIoCon, (iocon & ~(0x3fu << 16)) | (((unsigned)((first + last) / 2) & 0x3fu) << 16));
  IODelay (200);
  return hostSweepMatches (reference);
}

bool hostEnterHs200 (const uint8_t *reference)
{
  uint32_t cfg = rd (kMsdcCfg);
  // HS200 needs a 1.8 V IO.  The vendor switches that through a PMIC regulator this port has no
  // driver for, so the interesting question is whether the firmware already left it at 1.8 V --
  // Android runs this card at HS400, which needs it.  MSDC_CFG bit 5 is the 1.8 V control and bit 6
  // the power status (mtk-sd.c: MSDC_CFG_BV18SDT / MSDC_CFG_BV18PSS).
  IOLog ("NativeMsdc: before HS200 cfg=%08x bv18ctl=%u bv18status=%u\n", cfg,
         unsigned ((cfg >> 5) & 1u), unsigned ((cfg >> 6) & 1u));
  if (!hostSwitchExtCsd (185, 2)) {                    // EXT_CSD[185] HS_TIMING = 2 (HS200)
    IOLog ("NativeMsdc: HS_TIMING=2 refused; staying in HS\n");
    return false;
  }
  IODelay (20000);
  // Host side: the vendor's HS200 line is mode 0 (SDR with the divider) and div 0, which its own
  // formula turns into source/2.  Bits [17:16] are CKMOD and [15:8] CKDIV.  The data timeout counter
  // was scaled for the old, much slower clock, so it goes to its maximum here -- the vendor calls
  // msdc_set_timeout() after every clock change for exactly this reason, and a counter sized for
  // 400 kHz would expire long before a 200 MHz block finishes arriving from the card.
  wr (kMsdcCfg, (cfg & ~((0x3u << 16) | (0xffu << 8))) | (0u << 16) | (0u << 8));
  wr (kSdcCfg, (rd (kSdcCfg) & ~kSdcCfgDtoc) | (0xffu << 24));
  IODelay (20000);
  // Retune the sampling point at the new rate; the old one belongs to the old clock.
  const uint32_t iocon = rd (kMsdcIoCon);
  int firstPass = -1, lastPass = -1;
  for (unsigned sample = 0; sample < 64; ++sample) {
    wr (kMsdcIoCon, (iocon & ~(0x3fu << 16)) | ((sample & 0x3fu) << 16));
    IODelay (200);
    if (hostSweepMatches (reference)) {
      if (firstPass < 0) firstPass = (int)sample;
      lastPass = (int)sample;
    }
  }
  const unsigned centre = firstPass >= 0 ? (unsigned)((firstPass + lastPass) / 2) : 0;
  if (firstPass >= 0) {
    wr (kMsdcIoCon, (iocon & ~(0x3fu << 16)) | ((centre & 0x3fu) << 16));
    IODelay (200);
  }
  IOLog ("NativeMsdc: HS200 tuning window %d..%d centre %u, cfg %08x\n", firstPass, lastPass, centre,
         rd (kMsdcCfg));
  if (firstPass < 0 || !hostSweepMatches (reference)) {
    // Put everything back the way it was: HS mode, the inherited clock and sampling point.
    (void)hostSwitchExtCsd (185, 1);
    wr (kMsdcCfg, cfg);
    wr (kMsdcIoCon, iocon);
    IODelay (20000);
    IOLog ("NativeMsdc: HS200 did not verify; back in HS\n");
    return false;
  }
  return true;
}

void hostRaiseClock (void)
{
  if (!gHost.bounce) return;
  uint8_t *reference = gHost.bounce;
  // Calibration escape hatch.  The reference reads below cannot predict what the boot needs: they
  // rejected divider 64, which then ran a whole boot correctly, and they accepted divider 4, which
  // is marginal -- the boots that used it logged dozens of "verify ... got ... want" mismatches and
  // some of them ended with the controller wedged.  native-msdc-clkforce=<N> sets a divider and
  // trusts it, so the safe rate can be measured instead of guessed: run a boot at each value and
  // see whether it finishes and how many reads it does.
  const unsigned forced = bootArgument ("native-msdc-clkforce", 0, 0);
  if (forced != 0) {
    const unsigned current = (rd (kMsdcCfg) >> 8) & 0xfffu;
    hostSetClockDivider (forced, current);
    native_msdc_clock_divider = forced;
    native_msdc_sample_delay = 0;
    IOLog ("NativeMsdc: clock forced to divider %u (was %u, no verification)\n", forced, current);
    return;
  }
  for (unsigned i = 0; i < kSweepCount; i++) {
    if (hostReadPio (kCmdReadSingle, kSweepSectors[i], reference + i * kSector, 1) != kIOReturnSuccess) {
      IOLog ("NativeMsdc: clock tuning skipped; a reference read failed\n");
      return;
    }
  }
  const uint32_t cfg0 = rd (kMsdcCfg), iocon0 = rd (kMsdcIoCon);
  // CKDIV is 8 bits wide; reading 12 here also picked up CKMOD and HS400_CK_MODE, which are not
  // divider bits at all.
  const unsigned inherited = (cfg0 >> 8) & 0xffu;
  // Why divider 4 is unreliable, in one sentence: the sampling delay in MSDC_IOCON was trained by
  // the boot firmware for the inherited divider, and this code never retrains it.  At a shorter
  // period the same delay lands at a different point of the read eye, so reads sit on the edge of
  // it -- which shows up exactly as observed: dozens of "verify ... got ... want" mismatches even
  // on the boots that "worked", and, when the phase lands outside the eye, every read failing and
  // the controller left unusable (the console is written through this card, so the log dies with
  // it, the SystemOS mount answers EIO and exec of launchd answers the same EIO).
  //
  // So tune the sampling point for each candidate divider instead of trusting the inherited one:
  // read the reference sectors at every one of the 64 delays, keep the middle of the widest
  // passing run, and require that centre to pass a final check.  A centre sample has margin on
  // both sides, which is what a whole boot needs; "the first delay that happened to pass" does
  // not.  A divider with no passing delay is genuinely too fast for this card, and the previous
  // divider is put back before leaving.
  const unsigned wanted = bootArgument ("native-msdc-clk", 0xfffu, 0);
  // The boot firmware already runs this card at CKDIV 4 (the old 12-bit read reported 260, which
  // is CKDIV 4 plus CKMOD 1 -- it was never a divider).  In the vendor's DDR formula CKDIV 0 is
  // the fastest setting (src >> 2), so the values that actually raise the rate are 0..3, and the
  // old candidate list {4, 8, 16, 32, 64} could only ever slow it down.  Try the fast end first;
  // the sampling-point tuning below decides whether a rate is usable, and divider 4 (the inherited
  // one) is what is kept when none of them is.
  static const unsigned dividers[] = { 0, 1, 2, 3 };
  unsigned best = inherited, bestSample = 0;
  for (unsigned d = 0; d < sizeof (dividers) / sizeof (dividers[0]); d++) {
    if (wanted != 0 && dividers[d] != wanted) continue;
    if (dividers[d] >= best) continue;
    hostSetClockDivider (dividers[d], best);
    int firstPass = -1, lastPass = -1;
    for (unsigned sample = 0; sample < 64; sample++) {
      wr (kMsdcIoCon, (iocon0 & ~(0x3fu << 16)) | ((sample & 0x3fu) << 16));
      IODelay (200);
      if (hostSweepMatches (reference)) {
        if (firstPass < 0) firstPass = (int)sample;
        lastPass = (int)sample;
      }
    }
    if (firstPass >= 0) {
      const unsigned centre = (unsigned)((firstPass + lastPass) / 2);
      wr (kMsdcIoCon, (iocon0 & ~(0x3fu << 16)) | ((centre & 0x3fu) << 16));
      IODelay (200);
      if (hostSweepMatches (reference)) {
        best = dividers[d];
        bestSample = centre;
        IOLog ("NativeMsdc: clock divider %u sample %u verified (window %d..%d)\n",
               dividers[d], centre, firstPass, lastPass);
        if (wanted != 0) break;
        continue;
      }
    }
    IOLog ("NativeMsdc: divider %u has no usable sampling window (passing %d..%d); keeping %u\n",
           dividers[d], firstPass, lastPass, best);
    hostSetClockDivider (best, dividers[d]);
    wr (kMsdcIoCon, (iocon0 & ~(0x3fu << 16)) | ((bestSample & 0x3fu) << 16));
    break;
  }
  wr (kMsdcIoCon, (iocon0 & ~(0x3fu << 16)) | ((bestSample & 0x3fu) << 16));
  // Eight data lines before anything that needs a voltage change.  The firmware's storage
  // diagnostics on this board leave the card in 1-bit (their own notes record reading it back that
  // way), and one bit instead of eight is a factor of eight on every transfer with no PMIC work at
  // all.  The reference sectors decide, and the sampling point is retuned because a wider bus has a
  // different eye.
  if (bootArgument ("native-msdc-buswidth", 1, 0)) {
    uint8_t *scratch = gHost.bounce + kSweepCount * kSector;
    const uint64_t before = mach_absolute_time ();
    const IOReturn beforeResult = hostReadPio (kCmdReadMulti, 0, scratch, 8);
    const uint64_t beforeTicks = mach_absolute_time () - before;
    const uint32_t sdcBefore = rd (kSdcCfg);
    if (hostSetBusWidth (8)) {
      int first = -1, last = -1;
      const bool ok = hostRetune (reference, &first, &last);
      const uint64_t after = mach_absolute_time ();
      const IOReturn afterResult = hostReadPio (kCmdReadMulti, 0, scratch, 8);
      const uint64_t afterTicks = mach_absolute_time () - after;
      IOLog ("NativeMsdc: bus width 8 window %d..%d, read %llu us -> %llu us (%s/%s)\n", first, last,
             (beforeTicks * 1000) / 24000, (afterTicks * 1000) / 24000,
             beforeResult == kIOReturnSuccess ? "ok" : "failed",
             afterResult == kIOReturnSuccess ? "ok" : "failed");
      if (!ok) {
        // Back to whatever the firmware left: no point keeping a width that cannot be read.
        (void)hostSetBusWidth (1);
        wr (kSdcCfg, sdcBefore);
        IODelay (20000);
        IOLog ("NativeMsdc: 8-bit did not verify; back to 1-bit\n");
      }
    }
  }
  // Card speed mode.  Everything above only divides whatever clock the firmware left in place; the
  // card itself is still in plain HS, which is not what this hardware does -- the Android kernel on
  // this phone runs it in HS400 at 200 MHz.  HS200 is the fast mode this driver can reach with the
  // pieces it already has, and it is kept only if the reference sectors still read back identical.
  // The timing read is eight blocks at the spare offset so it stays inside the bounce buffer.
  if (bootArgument ("native-msdc-hs200", 1, 0)) {
    uint8_t *scratch = gHost.bounce + kSweepCount * kSector;
    const uint64_t before = mach_absolute_time ();
    const IOReturn beforeResult = hostReadPio (kCmdReadMulti, 0, scratch, 8);
    const uint64_t beforeTicks = mach_absolute_time () - before;
    if (hostEnterHs200 (reference)) {
      const uint64_t after = mach_absolute_time ();
      const IOReturn afterResult = hostReadPio (kCmdReadMulti, 0, scratch, 8);
      const uint64_t afterTicks = mach_absolute_time () - after;
      IOLog ("NativeMsdc: HS200 read %llu us -> %llu us (%s/%s)\n", (beforeTicks * 1000) / 24000,
             (afterTicks * 1000) / 24000, beforeResult == kIOReturnSuccess ? "ok" : "failed",
             afterResult == kIOReturnSuccess ? "ok" : "failed");
    }
  }
  // Rate curve at the divider and sampling point that just verified.  The clock tree says MSDCPLL is
  // running and CLK_CFG_3 already selects it, yet the card delivers about 600 KB/s -- a factor of
  // roughly 170 away from what that path should give.  Timing the same read at several dividers
  // separates the two explanations: if the time scales with the divider then this division works and
  // the limit is the source clock; if it does not, there is a fixed division upstream that CKDIV
  // cannot see.  Eight blocks fits in the bounce buffer past the reference copy.
  {
    static const unsigned curve[] = { 0, 1, 2, 4, 8, 16 };
    constexpr unsigned kCurveBlocks = 8;
    uint8_t *scratch = gHost.bounce + kSweepCount * kSector;
    for (unsigned i = 0; i < sizeof (curve) / sizeof (curve[0]); ++i) {
      hostSetClockDivider (curve[i], best);
      wr (kMsdcIoCon, (iocon0 & ~(0x3fu << 16)) | ((bestSample & 0x3fu) << 16));
      IODelay (200);
      const uint64_t before = mach_absolute_time ();
      const IOReturn got = hostReadPio (kCmdReadMulti, 0, scratch, kCurveBlocks);
      const uint64_t ticks = mach_absolute_time () - before;
      IOLog ("NativeMsdc: divider %u timed %llu us (%s)\n", curve[i], (ticks * 1000) / 24000,
             got == kIOReturnSuccess ? "ok" : "failed");
    }
    hostSetClockDivider (best, 16);
    wr (kMsdcIoCon, (iocon0 & ~(0x3fu << 16)) | ((bestSample & 0x3fu) << 16));
    IODelay (200);
  }
  native_msdc_clock_divider = best;
  native_msdc_sample_delay = bestSample;
  IOLog ("NativeMsdc: clock settled at divider %u sample %u (inherited %u)\n",
         best, bestSample, inherited);
  // The DMA line hostEnableDma() prints during bring-up comes before this console can be written,
  // so nothing in any collected log has ever shown whether the fast path is in use.  That matters
  // more than the clock: a 64 KiB PIO block costs about 0.2 s here, which by itself caps reads at
  // the 320 KB/s the policy read measures, while a DMA transfer should cost a fraction of it.
  IOLog ("NativeMsdc: dma state %u enabled %u phys 0x%llx\n", native_msdc_dma,
         unsigned (gHost.dmaEnabled), gHost.dmaPhys);
  // The bus looks slow in the wrong place.  A 64 KiB read spends 99.4% of its time in the FIFO
  // drain, and the drain loop spins about 738000 times per transfer waiting for data: the loop is
  // not the cost, the card's delivery rate is, and that lands at roughly 640 KB/s.  MSDC0's source
  // clock is selected by TOPCKGEN CLK_CFG_0 bits [13:11] (the driver already reads that register
  // for telemetry) and it reads 0 -- the slowest parent.  Raising the divider was never going to
  // help beyond the fastest division of a slow parent.
  //
  // So sweep the parent selectors with the same discipline as the divider sweep: keep the fastest
  // one whose reads still match the data the current parent returns, restore the original when
  // nothing beats it, and gate the whole thing on a boot argument so a selector that wedges the bus
  // costs one experiment rather than every boot.
  if (bootArgument ("native-msdc-srcclk", 1, 0)) {
    // The timing read has to fit where it is aimed.  gHost.bounce is exactly kTransferBytes
    // (kMaxBlocks * kSector = 64 KiB) and `candidate` starts kSweepCount sectors into it, so asking
    // for kMaxBlocks there reads 2 KiB past the end -- which is what panicked with a data abort
    // inside hostReadPio: far - x21 was 0xF800, the 64 KiB the call asked for.  Eight blocks (4 KiB)
    // is enough to time the bus and leaves the allocation intact.
    constexpr unsigned kSrcClkBlocks = 8;
    // The eMMC clock's parent is not in CLK_CFG_0.  The firmware's own clock library says where it
    // is: TOPCKGEN CLK_CFG_3 (0x70) with CLK_TOP_MSDC50_0_SEL at bits [10:8], update bit 13, and the
    // parent tables put MSDCPLL in slot 1 (Silicon/MediaTek/MT6765Pkg/Library/ClockImplLib).
    // CLK_CFG_0 [13:11] is what the earlier sweep changed, which is why seven selectors all measured
    // the same speed: they were not this clock at all.
    constexpr unsigned kTopCfg3Offset = 0x70;
    constexpr unsigned kMsdc50SelShift = 8;
    constexpr uint32_t kMsdc50SelMask = 7u << kMsdc50SelShift;
    constexpr uint32_t kTopCfgUpdate = 1u << 13;    // commit the field
    constexpr uint32_t kTopCfgGate = 1u << 15;      // CLK_CFG_3 [15] is the gate bit for the same field
    IODeviceMemory *clockMemory = IODeviceMemory::withRange (0x10000000ULL, 0x1000);
    IOMemoryMap *clockMap = clockMemory ? clockMemory->map (kIOMapAnywhere | kIOMapInhibitCache) : nullptr;
    if (clockMap && clockMap->getVirtualAddress () && clockMap->getLength () >= kTopCfg3Offset + sizeof (uint32_t)) {
      volatile uint32_t *cfg3 = reinterpret_cast<volatile uint32_t *> (uintptr_t (clockMap->getVirtualAddress ()) + kTopCfg3Offset);
      const uint32_t original = *cfg3;
      const unsigned inheritedSel = (original >> kMsdc50SelShift) & 7u;
      IOLog ("NativeMsdc: source CLK_CFG_3=%08x msdc50_0_sel=%u\n", original, inheritedSel);
      // The selector already points at MSDCPLL, so the question moves one block up: is that PLL
      // running, and at what rate?  The firmware's clock library gives the MT6765 map (APMIXED
      // 0x1000C000; MSDCPLL CON0 0x27c, CON1 0x280 with a 22-bit pcw at shift 8, CON3 0x288 with the
      // power/ISO enable) and clk-mt6765's formula, pcw = rate * 2^8 / 26 MHz.  Read only.
      IODeviceMemory *apmixedMemory = IODeviceMemory::withRange (0x1000C000ULL, 0x1000);
      IOMemoryMap *apmixedMap = apmixedMemory ? apmixedMemory->map (kIOMapAnywhere | kIOMapInhibitCache) : nullptr;
      if (apmixedMap && apmixedMap->getVirtualAddress () && apmixedMap->getLength () >= 0x28c) {
        volatile uint32_t *base = reinterpret_cast<volatile uint32_t *> (uintptr_t (apmixedMap->getVirtualAddress ()));
        const uint32_t con0 = base[0x27c / 4], con1 = base[0x280 / 4], con3 = base[0x288 / 4];
        const uint32_t pcw = (con1 >> 8) & 0x3fffffu;
        const uint32_t postdiv = con1 & 7u;
        // pcw = rate * 256 / 26 MHz  ->  rate = pcw * 26 MHz / 256, divided by postdiv + 1.
        const uint64_t mhz = pcw ? (uint64_t (pcw) * 26000000ull) / 256ull / 1000000ull : 0;
        IOLog ("NativeMsdc: MSDCPLL con0=%08x con1=%08x con3=%08x pcw=%u postdiv=%u ~%llu MHz\n",
               con0, con1, con3, pcw, postdiv, mhz / (postdiv + 1));
        OSSafeReleaseNULL (apmixedMap);
      }
      OSSafeReleaseNULL (apmixedMemory);
      // CKDIV turned out to have no effect at all: the same 4 KiB read takes 7.5 ms at divider 0 and
      // 8.0 ms at divider 16, so the controller is not dividing the parent this driver thinks it is.
      // The firmware's clock library names two gates on this path, INFRA_AO ifr3 bit 2 (MSDC0) and
      // ifr5 bit 9 (MSDC0_SRC) with CLR/SET/STA at 0x88/0x8c/0x94 and 0xc0/0xc4/0xc8.  A closed
      // source gate is exactly what would leave the controller on a fallback clock that no divider
      // reaches.  Read the status, and if either gate is closed, open it the way the firmware's
      // SdUngateClock does, then time a read again so the effect is visible in the same log.
      IODeviceMemory *infraMemory = IODeviceMemory::withRange (0x10001000ULL, 0x1000);
      IOMemoryMap *infraMap = infraMemory ? infraMemory->map (kIOMapAnywhere | kIOMapInhibitCache) : nullptr;
      if (infraMap && infraMap->getVirtualAddress () && infraMap->getLength () >= 0xcc) {
        volatile uint32_t *ao = reinterpret_cast<volatile uint32_t *> (uintptr_t (infraMap->getVirtualAddress ()));
        const uint32_t sta3 = ao[0x94 / 4], sta5 = ao[0xc8 / 4];
        const bool msdc0Gated = (sta3 & (1u << 2)) == 0, srcGated = (sta5 & (1u << 9)) == 0;
        IOLog ("NativeMsdc: infra_ao ifr3=%08x ifr5=%08x msdc0=%s src=%s\n", sta3, sta5,
               msdc0Gated ? "gated" : "on", srcGated ? "gated" : "on");
        if (msdc0Gated || srcGated) {
          // SET is at 0x88 / 0xc0 and CLR at 0x8c / 0xc4 (the firmware's clock library, verified
          // against clk-mt6765.c).  The first attempt wrote 0x8c / 0xc4 -- the clear registers -- which
          // is why nothing changed and why the status still read back with both gates shut.
          if (msdc0Gated) ao[0x88 / 4] = 1u << 2;
          if (srcGated) ao[0xc0 / 4] = 1u << 9;
          __asm__ volatile ("dsb sy" ::: "memory");
          IODelay (1000);
          IOLog ("NativeMsdc: infra_ao ungated, ifr3=%08x ifr5=%08x\n", ao[0x94 / 4], ao[0xc8 / 4]);
          // With the source gate open the divider should finally do something: run the curve again so
          // this same log says whether CKDIV has an effect now, and what the read costs.
          const uint64_t before = mach_absolute_time ();
          const IOReturn got = hostReadPio (kCmdReadMulti, 0, gHost.bounce + kSweepCount * kSector, 8);
          const uint64_t ticks = mach_absolute_time () - before;
          IOLog ("NativeMsdc: ungated read timed %llu us (%s)\n", (ticks * 1000) / 24000,
                 got == kIOReturnSuccess ? "ok" : "failed");
          // With the gate open the clock is faster, and the fastest division of it is more than the
          // card's current speed mode can take: restoring divider 0 here hung the machine -- the log
          // ended on the restore line and never came back, with the console blocked because the card
          // had stopped answering.  So walk it the other way: slowest first, then step towards fast,
          // and stop at the last setting that still reads back correctly.  Slower is always safe.
          static const unsigned climb[] = { 16, 8, 4, 2, 1, 0 };
          unsigned kept = 16;
          uint64_t keptTicks = 0;
          bool anyOk = false;
          for (unsigned i = 0; i < sizeof (climb) / sizeof (climb[0]); ++i) {
            hostSetClockDivider (climb[i], kept);
            const uint64_t t0 = mach_absolute_time ();
            const IOReturn r2 = hostReadPio (kCmdReadMulti, 0, gHost.bounce + kSweepCount * kSector, 8);
            const uint64_t ticks = mach_absolute_time () - t0;
            IOLog ("NativeMsdc: ungated divider %u timed %llu us (%s)\n", climb[i], (ticks * 1000) / 24000,
                   r2 == kIOReturnSuccess ? "ok" : "failed");
            if (r2 != kIOReturnSuccess) break;
            anyOk = true;
            kept = climb[i];
            keptTicks = ticks;
          }
          if (anyOk) {
            hostSetClockDivider (kept, 16);
            IOLog ("NativeMsdc: ungated clock kept at divider %u (%llu us per 4 KiB)\n", kept,
                   (keptTicks * 1000) / 24000);
          } else {
            // Not even the slowest division of the ungated clock reads: shut the gates again and put
            // the divider back where it was, so the boot keeps its storage.
            if (srcGated) ao[0xc4 / 4] = 1u << 9;
            if (msdc0Gated) ao[0x8c / 4] = 1u << 2;
            __asm__ volatile ("dsb sy" ::: "memory");
            IODelay (1000);
            hostSetClockDivider (best, 16);
            IOLog ("NativeMsdc: ungated clock unreadable, gates closed again (ifr3=%08x ifr5=%08x)\n",
                   ao[0x94 / 4], ao[0xc8 / 4]);
          }
        }
        OSSafeReleaseNULL (infraMap);
      }
      OSSafeReleaseNULL (infraMemory);
      uint8_t *reference = gHost.bounce;
      uint8_t *candidate = gHost.bounce + kSweepCount * kSector;
      bool haveReference = hostReadPio (kCmdReadSingle, kSweepSectors[0], reference, 1) == kIOReturnSuccess;
      uint64_t baseline = ~0ULL;
      if (haveReference) {
        const uint64_t before = mach_absolute_time ();
        if (hostReadPio (kCmdReadMulti, 0, candidate, kSrcClkBlocks) != kIOReturnSuccess) haveReference = false;
        else baseline = mach_absolute_time () - before;
      }
      unsigned bestSel = inheritedSel;
      uint64_t bestTicks = baseline;
      if (haveReference) {
        for (unsigned sel = 0; sel < 8; ++sel) {
          if (sel == inheritedSel) continue;
          *cfg3 = (original & ~kMsdc50SelMask) | ((sel & 7u) << kMsdc50SelShift) | kTopCfgGate | kTopCfgUpdate;
          __asm__ volatile ("dsb sy" ::: "memory");
          IODelay (1000);
          const uint64_t before = mach_absolute_time ();
          IOReturn got = hostReadPio (kCmdReadMulti, 0, candidate, kSrcClkBlocks);
          const uint64_t ticks = mach_absolute_time () - before;
          bool same = got == kIOReturnSuccess;
          if (same) {
            got = hostReadPio (kCmdReadSingle, kSweepSectors[0], candidate, 1);
            same = got == kIOReturnSuccess && memcmp (candidate, reference, kSector) == 0;
          }
          IOLog ("NativeMsdc: source selector %u reads %s in %llu ticks%s\n", sel,
                 (got == kIOReturnSuccess) ? "ok" : "failed", ticks, same ? "" : " (data differs)");
          if (same && ticks < bestTicks) {
            bestTicks = ticks;
            bestSel = sel;
          }
        }
        // When nothing beat the inherited selector, put the register back exactly as it was: the gate
        // bit is what the sweep added to every candidate, and it must not be left set on a clock the
        // card has not been proven to read through.
        *cfg3 = bestSel == inheritedSel ? original
                                        : ((original & ~kMsdc50SelMask) |
                                           ((bestSel & 7u) << kMsdc50SelShift) | kTopCfgGate | kTopCfgUpdate);
        __asm__ volatile ("dsb sy" ::: "memory");
        IODelay (1000);
        // Only keep the new parent if the card still reads correctly through it.
        if (bestSel != inheritedSel &&
            (hostReadPio (kCmdReadSingle, kSweepSectors[0], candidate, 1) != kIOReturnSuccess ||
             memcmp (candidate, reference, kSector) != 0)) {
          bestSel = inheritedSel;
          *cfg3 = original | kTopCfgUpdate;
          __asm__ volatile ("dsb sy" ::: "memory");
          IODelay (1000);
        }
        IOLog ("NativeMsdc: source clock selector %u (inherited %u, was %llu ticks, now %llu)\n",
               bestSel, inheritedSel, baseline / 1000, bestTicks / 1000);
      } else {
        IOLog ("NativeMsdc: source clock sweep skipped (no reference read)\n");
      }
      OSSafeReleaseNULL (clockMap);
    }
    OSSafeReleaseNULL (clockMemory);
  }
  // Read only: dump the two clock blocks the firmware's own notes point at, so the next step is
  // based on what is actually programmed rather than on which register the vendor driver happens to
  // name.  TOPCKGEN holds the MSDC50_0 mux (CLK_CFG_3 at 0x70) and its misc registers; msdc0_top at
  // 0x11CD0000 is "the HS200/HS400 delay-line tuning block" per MT6765Pkg's MsdcImplLib, and on
  // several MTK parts that block also carries the eMMC clock control.  26 MHz / 64 is 406 kHz, which
  // is the rate the reads measure, so something is dividing the crystal down and it is not CKDIV.
  if (bootArgument ("native-msdc-regdump", 1, 0)) {
    struct { uint64_t base; unsigned words; const char *name; } blocks[] = {
      // msdc0_top at 0x11CD0000 is deliberately absent: reading a block whose clock is not running
      // stalls the bus and hangs the CPU, which is exactly what happened when it was included -- the
      // log ended at the line before this dump and never came back.
      { 0x10000000ULL, 64, "topckgen" },
    };
    for (unsigned b = 0; b < sizeof (blocks) / sizeof (blocks[0]); ++b) {
      IODeviceMemory *memory = IODeviceMemory::withRange (blocks[b].base, 0x100);
      IOMemoryMap *map = memory ? memory->map (kIOMapAnywhere | kIOMapInhibitCache) : nullptr;
      if (map && map->getVirtualAddress () && map->getLength () >= blocks[b].words * 4) {
        volatile uint32_t *r = reinterpret_cast<volatile uint32_t *> (uintptr_t (map->getVirtualAddress ()));
        for (unsigned i = 0; i < blocks[b].words; i += 4) {
          IOLog ("NativeMsdc: %s +%02x: %08x %08x %08x %08x\n", blocks[b].name, i * 4,
                 r[i], r[i + 1], r[i + 2], r[i + 3]);
        }
        OSSafeReleaseNULL (map);
      }
      OSSafeReleaseNULL (memory);
    }
  }
  // The console is written through this same card, so report the outcome where storage cannot hide
  // it: the panic screen is drawn from the framebuffer.
  if (bootArgument ("native-msdc-clkreport", 1, 0)) {
    panic("D4K clock: div=%u smp=%u verified=%d cfg=%08x sdc=%08x",
          best, bestSample, best == inherited ? 0 : 1, rd (kMsdcCfg), rd (kSdcCfg));
  }
  // Repeat-read self test.  Reads are supposed to be deterministic, so any difference between two
  // reads of the same sector is corruption, and where it lands says whether the FIFO drain is
  // losing bytes (fixed offset) or the transfer itself is unreliable (scattered).
  if (bootArgument ("native-msdc-iotest", 1, 0)) {
    uint8_t *a = gHost.bounce;
    uint8_t *b = gHost.bounce + kSector;
    IOReturn first = hostReadPio (kCmdReadSingle, kSweepSectors[0], a, 1);
    unsigned mismatches = 0, firstOffset = 0xffffffffu, differing = 0;
    for (unsigned round = 0; round < 8 && first == kIOReturnSuccess; round++) {
      if (hostReadPio (kCmdReadSingle, kSweepSectors[0], b, 1) != kIOReturnSuccess) {
        mismatches = 0xffffffffu;
        break;
      }
      for (unsigned i = 0; i < kSector; i++) {
        if (a[i] != b[i]) {
          if (firstOffset == 0xffffffffu) firstOffset = i;
          differing++;
        }
      }
      if (differing) mismatches++;
    }
    panic("D4K iotest: first=%d rounds_with_diff=%u bytes_diff=%u at=%u a=%02x%02x b=%02x%02x",
          (int)first, mismatches, differing, firstOffset,
          a[firstOffset == 0xffffffffu ? 0 : firstOffset], a[firstOffset == 0xffffffffu ? 1 : firstOffset + 1],
          b[firstOffset == 0xffffffffu ? 0 : firstOffset], b[firstOffset == 0xffffffffu ? 1 : firstOffset + 1]);
  }
}

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
  // The clock is raised later, at the end of publishThread, once every window is published: changing
  // it here broke the root mount, because the console runs through this same card.
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
  IOLog ("NativeMsdc: signed root policy read in %llu ms, %u entries, %llu reads, %llu retries, %llu failures, ckdiv %u/%u\n",
      elapsed / 1000000ULL, gPolicy.header.count,
      (unsigned long long)native_msdc_reads, (unsigned long long)native_msdc_retries,
      (unsigned long long)native_msdc_failures, native_msdc_clock_divider, native_msdc_sample_delay);
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
// The eMMC console's own polled-PIO sequence stops working once this driver has touched the card,
// so the console patch hands a failed block here.  This is the same single-block write the whole
// port already uses, so it works wherever the driver does.

// ---- Framebuffer text console ---------------------------------------------------------------------------------------
// The kernel console is an eMMC block, so the screen shows nothing while the system runs and the log
// dies with the card.  Drawing the same characters into the firmware framebuffer needs no display
// driver: the geometry comes from the DarwinBoot handoff and the glyphs are the 5x12 font the panic
// screen uses.  Addresses are written directly, the way the panic screen does.
static uint64_t gFbFont[95] = {
    0x0000000000000000, 0x0842108000020084,
    0x0005294000000000, 0x15F528000000295F,
    0x1C52F88000023E94, 0x088556400004D542,
    0x04528800000B2725, 0x0002108000000000,
    0x0421108800821042, 0x1084108200221108,
    0x0957548000000000, 0x3E42000000000084,
    0x0000000000223000, 0x3E00000000000000,
    0x0000000000471000, 0x0884420000008442,
    0x2318A88000022A31, 0x08429880000F9084,
    0x1108C5C0000F8444, 0x1C4443E000074610,
    0x14A62100000423E9, 0x26D087E000074610,
    0x1E10C5C000074631, 0x088443E000010844,
    0x1D18C5C000074631, 0x3D18C5C000074610,
    0x08E2000000471000, 0x08E2000000223000,
    0x0222220000082082, 0x01F00000000003E0,
    0x2082082000008888, 0x1108C5C000020084,
    0x2B98C5C0000F05B5, 0x2318A8800008C63F,
    0x1D2949E00007CA52, 0x0210C5C000074421,
    0x252949E00007CA52, 0x1E1087E0000F8421,
    0x1E1087E000008421, 0x0210C5C000074639,
    0x3F18C6200008C631, 0x084211C000071084,
    0x1084238000032508, 0x0654C6200008C525,
    0x02108420000F8421, 0x2B5DC6200008C631,
    0x2B59CE200008C739, 0x2318C5C000074631,
    0x1F18C5E000008421, 0x2318C5C001075631,
    0x1F18C5E00008C525, 0x1C10C5C000074610,
    0x084213E000021084, 0x2318C62000074631,
    0x1518C6200002114A, 0x2B18C620000556B5,
    0x08A546200008C54A, 0x08A5462000021084,
    0x088443E0000F8442, 0x0421084E00E10842,
    0x0821042000084108, 0x1084210E00E42108,
    0x0008A88000000000, 0x0000000001F00000,
    0x0000010400000000, 0x20E00000000B663E,
    0x22F084200007C631, 0x22E0000000074421,
    0x23E84200000F4631, 0x22E000000007443F,
    0x1E21498000010842, 0x22E000001D187A31,
    0x26D084200008C631, 0x0860100000071084,
    0x10C020000C94A108, 0x0A9084200008A4A3,
    0x084210C000071084, 0x2AB000000008D6B5,
    0x26D000000008C631, 0x22E0000000074631,
    0x22F000000210BE31, 0x23E0000021087A31,
    0x26D0000000008421, 0x22E0000000074506,
    0x04F1080000064842, 0x23100000000B6631,
    0x2310000000022951, 0x23100000000556B5,
    0x151000000008A884, 0x231000001D185B31,
    0x11F00000000F8444, 0x0642109801821084,
    0x0842108000021084, 0x3042108300321084,
    0x0004D64000000000,
};
static volatile uint32_t *gFbBase = nullptr;
static IOMemoryMap *gFbMap = nullptr;
static uint32_t gFbRow = 0, gFbWidth = 0, gFbHeight = 0;
static uint32_t gFbX = 0, gFbY = 0;

void fbConsoleWrite (const void *data, size_t length);
void fbConsolePut (char c);

void fbConsoleInit (void)
{
  if (bootArgument ("native-fb", 1, 0) != 1) return;
  const unsigned base = bootArgument ("native-fb-base", 0xffffffffu, 0x7ec50000u);
  gFbRow = bootArgument ("native-fb-row", 0xffffu, 2944);
  gFbWidth = bootArgument ("native-fb-w", 0xffffu, 720);
  gFbHeight = bootArgument ("native-fb-h", 0xffffu, 1600);
  if (!base || !gFbRow || !gFbWidth || !gFbHeight) return;
  // The framebuffer is physical memory the kernel has not mapped for us: writing the address
  // directly faults with a data abort at exactly that address.  Map it the way the controller
  // window is mapped, then write through the mapping.
  const uint64_t bytes = uint64_t (gFbRow) * gFbHeight + 4096;
  IODeviceMemory *memory = IODeviceMemory::withRange (base, bytes);
  gFbMap = memory ? memory->map (kIOMapAnywhere) : nullptr;
  OSSafeReleaseNULL (memory);
  if (!gFbMap) {
    IOLog ("NativeMsdc: framebuffer at 0x%x could not be mapped\n", base);
    return;
  }
  gFbBase = reinterpret_cast<volatile uint32_t *> (gFbMap->getVirtualAddress ());
  if (!gFbBase) return;
  gFbX = 0;
  gFbY = 0;
  for (uint32_t y = 0; y < gFbHeight; y++)
    for (uint32_t x = 0; x < gFbWidth; x++)
      gFbBase[(uint64_t)y * gFbRow / 4 + x] = 0x00000000u;      // clear once so the menu is gone
}

void fbConsolePut (char c)
{
  const uint32_t scale = 2, cw = 5 * scale, ch = 12 * scale;
  if (!gFbBase) return;
  if (c == '\n' || gFbX + cw > gFbWidth) {
    gFbX = 0;
    gFbY += ch;
    if (c == '\n') return;
  }
  if (c == '\r') { gFbX = 0; return; }
  if (gFbY + ch > gFbHeight) {                              // wrap to the top once the screen is full
    gFbY = 0;
    gFbX = 0;
  }
  if (c < 32 || c > 126) return;
  const uint64_t glyph = gFbFont[c - 32];
  const uint32_t top = uint32_t (glyph >> 32), bottom = uint32_t (glyph);
  for (uint32_t row = 0; row < 12; row++) {
    const uint32_t bits = (row < 6) ? (top >> (row * 5)) : (bottom >> ((row - 6) * 5));
    for (uint32_t col = 0; col < 5; col++) {
      if (!((bits >> col) & 1)) continue;
      for (uint32_t dy = 0; dy < scale; dy++)
        for (uint32_t dx = 0; dx < scale; dx++)
          gFbBase[(uint64_t)(gFbY + row * scale + dy) * gFbRow / 4 + gFbX + col * scale + dx] = 0xFFFFFFFFu;
    }
  }
  gFbX += cw;
}

void fbConsoleWrite (const void *data, size_t length)
{
  if (!gFbBase || !data) return;
  const char *text = static_cast<const char *> (data);
  for (size_t i = 0; i < length; i++) {
    if (!text[i]) break;
    fbConsolePut (text[i]);
  }
}

extern "C" int native_msdc_console_sink (uint32_t lba, const void *data)
{
  if (!data) return -1;
  // Mirror the text to the framebuffer before any of the storage guards: it touches only mapped
  // video memory, and putting it after the "busy" test meant it almost never ran, because the
  // driver is busy for most of the boot.
  fbConsoleWrite (data, kSector);
  if (!gHost.layoutValid) return -1;
  // This driver logs through IOLog inside its own transfers, and IOLog reaches the console, which
  // calls back here.  Entering transfer() again would take the host lock it already holds, so a
  // busy driver declines and the console keeps the block for its next flush.
  if (__atomic_load_n (&native_msdc_busy, __ATOMIC_ACQUIRE) != 0) return -1;
  HostSession session;
  return transfer (lba, 1, const_cast<uint8_t *> (static_cast<const uint8_t *> (data)), true) ==
             kIOReturnSuccess ? 0 : -1;
}

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
    // Every window gets the readiness wait, including the last one.  Skipping it for the final
    // window meant the OS window -- the one the root volume lives on -- was never confirmed, and
    // the root mount raced against its APFS volume appearing.
    bool ready = false;
    // Two minutes, not thirty seconds: on this board the APFS volume below a window needs a while
    // to appear at the bootloader's bus rate, and the root mount gives up if the media is not there
    // by then.  The log line records which window made it.
    for (unsigned tick = 0; tick < 1200 && !(ready = volumeBelow (device)); ++tick) IOSleep (100);
    IOLog ("NativeMsdc: %s volume %s\n", device->windowLabel (), ready ? "ready" : "not seen after 120 s");
    device->release ();
  }
  // Raise the card clock only now.  Doing it during bring-up broke the root mount: the console is
  // written through this same card, so a clock change there also silenced the log and the windows
  // were never published.  By this point the policy read and every window are done, and what still
  // needs speed is the dyld cache read that follows the root mount.
  if (bootArgument ("native-msdc-clock", 1, 0)) hostRaiseClock ();
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
  // Clear the firmware menu and mirror the console to the framebuffer, before the first log line.
  fbConsoleInit ();
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
