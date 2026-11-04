// SPDX-License-Identifier: MIT
//
// MediaTek MSDC0 eMMC host, polled PIO, for dandelion (MT6762G).
//
// The card is already initialised and running when the kernel starts: LK brings
// it up, the firmware (DandelionEmmcDxe) inherits that state -- its own header
// says "CMD0, no CMD1, no re-tuning" -- and this driver inherits it in turn.
// So there is no CMD0/CMD1/CMD2/CMD3/CMD9/CMD7 sequence here and no RCA search:
// RCA is 1, the bus is already as LK left it, and all this driver does is
// program a divider, keep PIO mode on, and issue CMD17/CMD18 (and CMD24/CMD25).
//
// The register map, the command encodings and the FIFO discipline are taken from
// uefi-dandelion's DandelionEmmcDxe.c, which is the same hardware running the
// same way; bootshim/msdc.c is the origin of both.
//
// First goal (this version): publish the medium and read block 0, so the kernel
// log can prove the eMMC is readable.  Writes are implemented but only exercised
// once reads are known good.

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/storage/IOMedia.h>
#include <pexpert/pexpert.h>

extern "C" void dandelion_emmc_probe(void);

#define kMsdcBaseDefault 0x11230000ULL
#define kMsdcSize        0x00010000ULL

// Register map (DandelionEmmcDxe.c:56-68).
enum {
	kRegCfg      = 0x000,
	kRegPs       = 0x008,
	kRegInt      = 0x00C,
	kRegIntEn    = 0x010,
	kRegFifocs   = 0x014,
	kRegTxData   = 0x018,
	kRegRxData   = 0x01C,
	kRegSdcCfg   = 0x030,
	kRegSdcCmd   = 0x034,
	kRegSdcArg   = 0x038,
	kRegSdcSts   = 0x03C,
	kRegResp0    = 0x040,
	kRegBlkNum   = 0x050,
};

enum {
	kCfgMode     = 1u << 0,
	kCfgPio      = 1u << 3,
	kCfgCkstb    = 1u << 7,
	kCfgCkdiv    = 0xFFFu << 8,
};

enum {
	kStsSdcBusy  = 1u << 0,
	kStsCmdBusy  = 1u << 1,
};

enum {
	kIntCmdRdy   = 1u << 8,
	kIntCmdTmo   = 1u << 9,
	kIntRspCrc   = 1u << 10,
	kIntXferDone = 1u << 12,
	kIntDatTmo   = 1u << 14,
	kIntDatCrc   = 1u << 15,
	kIntCmdAll   = kIntCmdRdy | kIntCmdTmo | kIntRspCrc,
	kIntDatAll   = kIntXferDone | kIntDatTmo | kIntDatCrc,
};

enum {
	kFifocsClr   = 1u << 31,
	kFifoDepth   = 128,
	kBlockSize   = 512,
	kChunkBlocks = 128,          // 64 KiB per CMD18/CMD25, as the firmware does
	kRca         = 1,
	kExtCsdSector = 0,           // CMD8 with arg 0 reads the EXT_CSD area
};

#define CMDW(opc, rsp, dtype, rw, blk) \
	((((uint32_t)(opc) & 0x3F) << 24) | (((uint32_t)(rsp) & 0x7) << 16) | \
	 (((uint32_t)(dtype) & 0x3) << 14) | (((uint32_t)(rw) & 0x1) << 13) | \
	 (((uint32_t)(blk) & 0x1FF) << 0))

static const uint32_t kCmd8Read   = CMDW(8, 1, 1, 0, 512);
static const uint32_t kCmd12Stop  = CMDW(12, 7, 0, 0, 0);
static const uint32_t kCmd17Read  = CMDW(17, 1, 1, 0, 512);
static const uint32_t kCmd18Read  = CMDW(18, 1, 2, 0, 512);
static const uint32_t kCmd24Write = CMDW(24, 1, 1, 1, 512);
static const uint32_t kCmd25Write = CMDW(25, 1, 2, 1, 512);

static const uint32_t kR1Fatal        = 0xFFFFA080u;

class DandelionEmmc : public IOBlockStorageDevice
{
	OSDeclareDefaultStructors(DandelionEmmc)

	volatile uint32_t *regs = nullptr;
	uint64_t base = kMsdcBaseDefault;
	uint64_t blockCount = 0;
	bool   medium = false;
	IOSimpleLock *lock = nullptr;

	uint32_t read32(uint32_t offset) { return regs[offset / 4]; }
	void   write32(uint32_t offset, uint32_t value) { regs[offset / 4] = value; }

	bool waitStatus(uint32_t mask, unsigned milliseconds);
	bool fifoClear(void);
	bool setClock(uint32_t divider);
	bool command(uint32_t cmdw, uint32_t arg, uint32_t *response);
	bool transfer(uint32_t cmdw, uint32_t arg, void *buffer, uint32_t blocks, bool write);
	bool readExtCsd(uint8_t *sector);
	bool mapController(void);

public:
	bool start(IOService *provider) override;
	void stop(IOService *provider) override;
	void free() override;

	IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblocks,
	                          IOStorageAttributes *attributes, IOStorageCompletion *completion) override;
	IOReturn doSyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblocks);
	IOReturn doEjectMedia(void) override { return kIOReturnUnsupported; }
	IOReturn doFormatMedia(UInt64 byteCapacity) override { return kIOReturnUnsupported; }
	UInt32   doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const override { return 0; }

	char *getVendorString(void) override { return const_cast<char *>("MediaTek"); }
	char *getProductString(void) override { return const_cast<char *>("MSDC0 eMMC"); }
	char *getRevisionString(void) override { return const_cast<char *>("dandelion"); }
	char *getAdditionalDeviceInfoString(void) override { return const_cast<char *>("mt6762g"); }

	IOReturn reportBlockSize(UInt64 *blockSize) override
	{
		*blockSize = kBlockSize;
		return kIOReturnSuccess;
	}
	IOReturn reportEjectability(bool *ejectable) override
	{
		*ejectable = false;
		return kIOReturnSuccess;
	}
	IOReturn reportRemovability(bool *removable) override
	{
		*removable = false;
		return kIOReturnSuccess;
	}
	IOReturn reportWriteProtection(bool *writeProtected) override
	{
		*writeProtected = false;
		return kIOReturnSuccess;
	}
	IOReturn reportMaxValidBlock(UInt64 *maxBlock) override
	{
		if (!medium) { return kIOReturnNotReady; }
		*maxBlock = blockCount ? blockCount - 1 : 0;
		return kIOReturnSuccess;
	}
	IOReturn reportMediaState(bool *mediaPresent, bool *changedState) override
	{
		*mediaPresent = medium;
		if (changedState != nullptr) { *changedState = false; }
		return kIOReturnSuccess;
	}
};

OSDefineMetaClassAndStructors(DandelionEmmc, IOBlockStorageDevice)

bool DandelionEmmc::mapController(void)
{
	// The address comes from the ADT when the board exports one; the register
	// block is fixed in silicon either way.
	IORegistryEntry *node = IORegistryEntry::fromPath("/arm-io/emmc", gIODTPlane);
	if (node != nullptr) {
		OSData *reg = OSDynamicCast(OSData, node->getProperty("reg"));
		if (reg != nullptr && reg->getLength() >= 8) {
			uint32_t cells[2] = { 0, 0 };
			memcpy(cells, reg->getBytesNoCopy(), 8);
			uint64_t offset = cells[0];
			IORegistryEntry *soc = IORegistryEntry::fromPath("/arm-io", gIODTPlane);
			if (soc != nullptr) {
				OSData *ranges = OSDynamicCast(OSData, soc->getProperty("ranges"));
				if (ranges != nullptr && ranges->getLength() >= 16) {
					uint32_t window[4] = { 0, 0, 0, 0 };
					memcpy(window, ranges->getBytesNoCopy(), 16);
					offset += window[1];   // child base 0 -> parent base window[1]
				}
				soc->release();
			}
			base = offset;
		}
		node->release();
	}
	IOMemoryDescriptor *descriptor = IOMemoryDescriptor::withPhysicalAddress(
	    base, kMsdcSize, kIODirectionNone);
	if (descriptor == nullptr) {
		kprintf("DANDELION-EMMC: no descriptor for %#llx\n", base);
		return false;
	}
	IOMemoryMap *mapping = descriptor->map();
	if (mapping != nullptr) {
		regs = reinterpret_cast<volatile uint32_t *>(mapping->getVirtualAddress());
	}
	descriptor->release();
	if (regs == nullptr) {
		kprintf("DANDELION-EMMC: cannot map %#llx\n", base);
		return false;
	}
	kprintf("DANDELION-EMMC: controller at %#llx mapped\n", base);
	return true;
}

bool DandelionEmmc::waitStatus(uint32_t mask, unsigned milliseconds)
{
	for (unsigned i = 0; i < milliseconds; i++) {
		if ((read32(kRegSdcSts) & mask) == 0) { return true; }
		IOSleep(1);
	}
	return (read32(kRegSdcSts) & mask) == 0;
}

bool DandelionEmmc::fifoClear(void)
{
	write32(kRegFifocs, read32(kRegFifocs) | kFifocsClr);
	for (unsigned i = 0; i < 20; i++) {
		if ((read32(kRegFifocs) & kFifocsClr) == 0) { return true; }
		IOSleep(1);
	}
	return false;
}

bool DandelionEmmc::setClock(uint32_t divider)
{
	// Keep everything else in MSDC_CFG: CKMOD/CCKPD belong to the bootloader.
	write32(kRegCfg, (read32(kRegCfg) & ~kCfgCkdiv) | kCfgMode | kCfgPio | (divider << 8));
	for (unsigned i = 0; i < 20; i++) {
		if ((read32(kRegCfg) & kCfgCkstb) != 0) { return true; }
		IOSleep(1);
	}
	return false;
}

bool DandelionEmmc::command(uint32_t cmdw, uint32_t arg, uint32_t *response)
{
	uint32_t opcode = cmdw & 0x3F;

	// CMD12/CMD13 are allowed while DAT0 is busy; everything else needs an idle bus.
	uint32_t mask = (opcode == 12 || opcode == 13) ? kStsCmdBusy : kStsSdcBusy;
	if (!waitStatus(mask, 100)) { return false; }

	write32(kRegInt, 0xFFFFFFFFu);        // W1C
	write32(kRegSdcArg, arg);
	write32(kRegSdcCmd, cmdw);

	for (unsigned i = 0; i < 200; i++) {
		uint32_t status = read32(kRegInt) & kIntCmdAll;
		if (status != 0) {
			write32(kRegInt, status);
			if ((status & (kIntCmdTmo | kIntRspCrc)) != 0) { return false; }
			if (response != nullptr) { *response = read32(kRegResp0); }
			return true;
		}
		IOSleep(1);
	}
	return false;
}

bool DandelionEmmc::transfer(uint32_t cmdw, uint32_t arg, void *buffer, uint32_t blocks, bool write)
{
	uint8_t *bytes = static_cast<uint8_t *>(buffer);
	uint32_t r1 = 0, left = blocks * kBlockSize, offset = 0;
	bool done = false;

	if (!waitStatus(kStsSdcBusy, 100)) { return false; }
	if (!fifoClear()) { return false; }

	write32(kRegBlkNum, blocks);
	write32(kRegInt, 0xFFFFFFFFu);
	if ((read32(kRegCfg) & kCfgPio) == 0) { write32(kRegCfg, read32(kRegCfg) | kCfgPio); }

	if (!command(cmdw, arg, &r1)) { goto stop; }
	if ((r1 & kR1Fatal) != 0) { goto stop; }

	for (unsigned ms = 0; ms < 3000; ms++) {
		uint32_t status = read32(kRegInt) & kIntDatAll;
		if (status != 0) {
			write32(kRegInt, status);
			if ((status & (kIntDatTmo | kIntDatCrc)) != 0) { goto stop; }
			if ((status & kIntXferDone) != 0) { done = true; }
		}
		if (done && left == 0 && !write) { goto stop; }   // read finished cleanly

		if (write) {
			if (left != 0 && ((read32(kRegFifocs) >> 16) & 0xFF) == 0) {
				uint32_t burst = left < (uint32_t)kFifoDepth ? left : (uint32_t)kFifoDepth;
				for (uint32_t step = 0; step < burst; step += 4) {
					uint32_t word;
					memcpy(&word, bytes + offset, 4);
					write32(kRegTxData, word);
					offset += 4;
				}
				left -= burst;
			}
			if (done && left == 0 && (((read32(kRegFifocs) >> 16) & 0xFF) == 0)) { goto stop; }
		} else {
			for (;;) {
				uint32_t count = read32(kRegFifocs) & 0xFF;
				if (count == 0 || left == 0) { break; }
				if (count > left) { count = left; }
				count &= ~3u;
				for (uint32_t step = 0; step < count; step += 4) {
					uint32_t word = read32(kRegRxData);
					memcpy(bytes + offset, &word, 4);
					offset += 4;
				}
				left -= count;
			}
		}
		IOSleep(1);
	}

stop:
	if (blocks > 1) {
		uint32_t ignore = 0;
		command(kCmd12Stop, 0, &ignore);
	}
	return (left == 0);
}

bool DandelionEmmc::readExtCsd(uint8_t *sector)
{
	return transfer(kCmd8Read, kExtCsdSector, sector, 1, false);
}

bool DandelionEmmc::start(IOService *provider)
{
	if (!IOBlockStorageDevice::start(provider)) { return false; }
	if (!mapController()) { return false; }

	lock = IOSimpleLockAlloc();
	if (lock == nullptr) { return false; }

	// The bootloader left a divider behind; keep the card clock conservative
	// until the read path is proven.
	if (!setClock(26)) {
		kprintf("DANDELION-EMMC: clock did not stabilise\n");
		return false;
	}

	uint8_t extCsd[kBlockSize];
	memset(extCsd, 0, sizeof(extCsd));
	if (!readExtCsd(extCsd)) {
		kprintf("DANDELION-EMMC: CMD8 (EXT_CSD) failed\n");
		return false;
	}
	// EXT_CSD[212..215] is SEC_COUNT in 512-byte units (JEDEC).
	uint32_t secCount = (uint32_t)extCsd[212] | ((uint32_t)extCsd[213] << 8) |
	                  ((uint32_t)extCsd[214] << 16) | ((uint32_t)extCsd[215] << 24);
	kprintf("DANDELION-EMMC: EXT_CSD rev %u sec_count %u (cap %u MiB)\n",
	        extCsd[192], secCount, (secCount / 2048));
	if (secCount == 0 || secCount == 0xFFFFFFFFu) {
		kprintf("DANDELION-EMMC: implausible SEC_COUNT, not publishing\n");
		return false;
	}
	blockCount = secCount;
	medium = true;

	// Read block 0 and print the signature: proof the path works end to end.
	uint8_t block0[kBlockSize];
	memset(block0, 0, sizeof(block0));
	if (transfer(kCmd17Read, 0, block0, 1, false)) {
		kprintf("DANDELION-EMMC: block 0 %02x%02x%02x%02x %02x%02x%02x%02x\n",
		        block0[0], block0[1], block0[2], block0[3],
		        block0[4], block0[5], block0[6], block0[7]);
	} else {
		kprintf("DANDELION-EMMC: block 0 read failed\n");
	}

	registerService();
	kprintf("DANDELION-EMMC: published, %llu blocks of %u bytes\n", blockCount, kBlockSize);
	return true;
}

void DandelionEmmc::stop(IOService *provider)
{
	IOBlockStorageDevice::stop(provider);
}

void DandelionEmmc::free(void)
{
	if (lock != nullptr) { IOSimpleLockFree(lock); lock = nullptr; }
	IOBlockStorageDevice::free();
}

IOReturn DandelionEmmc::doSyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblocks)
{
	if (buffer == nullptr || !medium) { return kIOReturnNotReady; }
	if (block + nblocks > blockCount) { return kIOReturnBadArgument; }

	IOReturn status = buffer->prepare(kIODirectionOutIn);
	if (status != kIOReturnSuccess) { return status; }

	IOMemoryMap *map = buffer->createMappingInTask(kernel_task, 0, kIODirectionOutIn,
	                                              0, buffer->getLength());
	if (map == nullptr) {
		buffer->complete(kIODirectionOutIn);
		return kIOReturnNoMemory;
	}
	uint8_t *bytes = reinterpret_cast<uint8_t *>(map->getAddress());
	bool write = (buffer->getDirection() == kIODirectionIn);
	bool ok = true;

	IOSimpleLockLock(lock);
	for (uint64_t done = 0; done < nblocks && ok; ) {
		uint64_t remaining = nblocks - done;
		uint32_t chunk = remaining > kChunkBlocks ? kChunkBlocks : (uint32_t)remaining;
		uint32_t cmd = write ? (chunk > 1 ? kCmd25Write : kCmd24Write)
		                   : (chunk > 1 ? kCmd18Read : kCmd17Read);
		ok = transfer(cmd, (uint32_t)(block + done), bytes + done * kBlockSize, chunk, write);
		done += chunk;
	}
	IOSimpleLockUnlock(lock);

	map->release();
	buffer->complete(kIODirectionOutIn);
	return ok ? kIOReturnSuccess : kIOReturnIOError;
}

IOReturn DandelionEmmc::doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblocks,
                                         IOStorageAttributes *attributes, IOStorageCompletion *completion)
{
	IOReturn status = doSyncReadWrite(buffer, block, nblocks);
	if (completion != nullptr) {
		IOStorageCompletion done = *completion;
		IOStorage::complete(&done, status, buffer->getLength());
	}
	return status;
}

// Called from bsd_init, before IOKit has matched anything: attach the device to
// the platform expert so IOBlockStorageDriver can find it.
extern "C" void dandelion_emmc_probe(void)
{
	DandelionEmmc *device = new DandelionEmmc;
	if (device == nullptr) { return; }
	if (!device->init(nullptr)) {
		device->release();
		return;
	}
	IOService *provider = IOService::getPlatform();
	if (provider == nullptr || !device->attach(provider)) {
		kprintf("DANDELION-EMMC: no platform to attach to\n");
		device->release();
		return;
	}
	if (!device->start(provider)) {
		kprintf("DANDELION-EMMC: start failed\n");
		device->detach(provider);
		device->release();
	}
}
