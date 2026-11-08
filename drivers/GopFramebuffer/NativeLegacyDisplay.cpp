#include "NativeLegacyRequest.h"
// SPDX-License-Identifier: BSD-2-Clause-Patent
// Reuse the unchanged original generic framebuffer implementation. Unknown
// backend leaves stop; this class is not yet a published system display.
#include "OriginalLegacyFramebufferABI.h"
#include "NativeGOPWire.h"
#include "NativeHalfCodes.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IODeviceMemory.h>
#include <kern/clock.h>
#include <IOKit/pwr_mgt/IOPM.h>
#include <IOKit/pwr_mgt/IOPMpowerState.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <pexpert/pexpert.h>
extern "C" __attribute__((noreturn)) void native_legacy_backend_guard(unsigned,void *,void *);
extern "C" IOService *native_legacy_display_allocate(void);
extern "C" bool native_legacy_gop_mapping_observed(IOService *,IOMemoryMap *,uint32_t,uint32_t);
extern "C" void native_legacy_registration_observed(IOService *);
extern "C" void native_legacy_swap_stage(unsigned);
extern "C" void native_legacy_client_enter(task_t,uint32_t);
extern "C" void native_legacy_client_observed(IOReturn,IOUserClient *);
extern "C" void native_legacy_pm_observed(unsigned,IOReturn,unsigned long);
extern "C" IOMemoryDescriptor *native_legacy_surface_descriptor(OSObject *);
extern "C" uint64_t native_legacy_surface_width(OSObject *),native_legacy_surface_height(OSObject *);
extern "C" uint64_t native_legacy_surface_stride(OSObject *),native_legacy_surface_allocation(OSObject *);
extern "C" uint32_t native_legacy_surface_format(OSObject *),native_legacy_surface_planes(OSObject *);
extern "C" void native_legacy_frame_observed(unsigned kind,uint64_t count,uint64_t minx,uint64_t miny,uint64_t maxx,uint64_t maxy);
extern "C" void native_legacy_frame_colors_observed(const uint32_t *colors,const uint64_t *counts,uint64_t other);
extern "C" void native_legacy_swap_validation_observed(bool,unsigned,const void *,const void *);
// The surface of a rejected swap, as the validator read it: width, height, format, planes, stride, allocation, descriptor length.
extern "C" void native_legacy_surface_rejected(uint64_t,uint64_t,uint32_t,uint32_t,uint64_t,uint64_t,uint64_t);
extern "C" bool native_legacy_prepare_scanout(void);
extern "C" void native_legacy_transfer_observed(IOReturn,const void *,uint64_t);
extern "C" void native_legacy_copy_timing_observed(uint64_t ticks,uint64_t bytes);
extern "C" void native_legacy_retirement_observed(IOService *);
// GPU hand-off (boot-arg sheng-fb-gpu=1): the frame is offered to the privileged GPU client instead of being copied; the swap retires when the
// client reports the GPU copy done (native_legacy_gpu_complete). A refused offer or a failed copy uses the CPU copy below and is counted.
// format: NGP_BGRX8888, or for a half-float surface NGP_RGBA16F_LINEAR / NGP_RGBA16F_ENCODED (the client converts it on the GPU).
extern "C" bool native_legacy_gpu_offer(void *display,IOMemoryDescriptor *,uint64_t stride,uint32_t width,uint32_t height,uint32_t format);
// The compositor's swapped surfaces, for a display that fetches them itself: [0] distinct surfaces seen, [1] swaps checked; for the first four, at
// [2 + 3 * k]: allocation bytes, physical segments, largest segment (KiB). The pages are wired for the count and released.
extern "C" {volatile uint32_t native_legacy_surfaces[14];}
// Direct scanout (sheng-dpu-scanout=2): the display's two pipes fetch the compositor's surface through the display bank's window
// (NativeUfs native_ufs_dpu_map). [0] flips, [1] surfaces mapped, [2] map failures, [3] latches that took over 50 ms, [4] longest latch (us),
// [5] latch total (us, for the mean), [6] surfaces let go, [7] swaps that fell back to the copy, [8] flips back to the boot memory,
// [9] swaps held back until the GPU had written the frame (the compositor submits the swap when the command buffer is scheduled), [10] total
// and [11] longest wait (us), [12] waits that gave up after 250 ms, [13] waits of 8 ms or more (longer than a refresh at 120 Hz: the frame missed a flip).
extern "C" {volatile uint32_t native_legacy_direct[14];}
extern "C" bool native_adreno_fences(uint32_t *submitted,uint32_t *retired) __attribute__((weak_import));
// The swap lifecycle: native_legacy_ring holds the newest native_legacy_ring_cap events (allocated at start: a static array of this size no longer fits the kernel's data segment), {[0] microseconds since boot (64 bit), [1] event << 56 | slot + 1 << 48 |
// swaps validated and not retired << 40 | GPU fences outstanding << 24 | the request's tag (24 bits)}. Events: 1 swap validated, 2 fence wait
// begun, 3 fence passed, 4 flipped to a slot, 5 latched, 6 retired, 7 offered to the GPU copy, 8 blank frame. native_legacy_snap is the 64
// events before the first swap whose validation to flip took over 100 ms (native_legacy_snap_count 1 once taken). native_legacy_swapstat:
// [0] swaps validated, [1] kicks, [2] kicks refused for a swap in flight (transferred), [3] refused while the display is not ready, [4] with no
// validated head, [5] direct flips refused for a latch in flight, [6] fence waits begun, [7] swaps that took the copy path, [8] blank frames,
// [9] flips, [10] latches, [11] retires, [12] validation to flip over 100 ms, [13] the longest (us), [14] their total (us), [15] most swaps
// validated and not yet retired.
extern "C" {volatile uint64_t (*native_legacy_ring)[2]=nullptr;volatile uint32_t native_legacy_ring_cap=0;volatile uint32_t native_legacy_ring_count;volatile uint64_t native_legacy_snap[64][2];volatile uint32_t native_legacy_snap_count;volatile uint32_t native_legacy_swapstat[16];}
extern "C" uint32_t native_ufs_dpu_map(unsigned slot,const uint64_t *pages,unsigned count) __attribute__((weak_import));
extern "C" volatile uint32_t native_ufs_dpu_state __attribute__((weak_import));
static void noteSurface(OSObject *surface,IOMemoryDescriptor *memory)
{
    static OSObject *seen[4];
    native_legacy_surfaces[1]=native_legacy_surfaces[1]+1;
    for(OSObject *known:seen)if(known==surface)return;
    const unsigned k=native_legacy_surfaces[0];
    native_legacy_surfaces[0]=k+1;
    if(k>=4)return;
    seen[k]=surface;
    if(memory->prepare()!=kIOReturnSuccess)return;
    uint32_t segments=0;uint64_t largest=0;
    for(IOByteCount offset=0,length=0;offset<memory->getLength() && segments<100000;offset+=length,++segments) {
        if(!memory->getPhysicalSegment(offset,&length,kIOMemoryMapperNone) || !length)break;
        if(length>largest)largest=length;
    }
    memory->complete();
    native_legacy_surfaces[2+3*k]=uint32_t(memory->getLength());native_legacy_surfaces[3+3*k]=segments;native_legacy_surfaces[4+3*k]=uint32_t(largest>>10);
}
extern "C" void native_legacy_gpu_fallback_observed(int status);
// Swaps retired when the GPU client took the frame (before its copy finished), and copies that then failed after such a retirement.
extern "C" {volatile uint64_t native_legacy_early_retires,native_legacy_early_failures;}
// Swaps the family gave up on after waiting 5 s for them (cancelSwapsAfterTimeout).
extern "C" {volatile uint64_t native_legacy_swap_timeouts;}
// Display refresh tick (boot-arg sheng-vsync=<Hz>, 0 or absent: off). The original family sends the compositor's vsync message (type 5)
// through native_legacy_vsync_notify, the unchanged original sender, but only a platform subclass calls it from the panel's vblank and
// that subclass is not part of this port: without it the render server never saw a vsync. The GOP scanout has no vblank interrupt, so a
// work-loop timer stands in for it while at least one client has vsync notifications enabled. Events: 0 armed, 1 sent, 2 idle (no client),
// 3 late (deadline passed, cadence restarted), 4 started, 5 refresh properties reported at that rate.
extern "C" void native_legacy_vsync_notify(void *display,uint64_t timestamp,uint64_t continuousTime,uint64_t interval);
// The panel's refresh rate in Hz for the tick and the refresh properties below (0: none, the render server's own 1/60 s). By default
// boot-arg sheng-vsync; a platform may answer otherwise (its display device's mode).
extern "C" __attribute__((weak)) unsigned native_legacy_refresh_hz(void) {
    unsigned hz=0;PE_parse_boot_argn("sheng-vsync",&hz,sizeof(hz));return hz;
}
// The unchanged original setDisplayRefreshProperties: publishes IOMFBDisplayRefresh from slot 301 (getRefreshAttributes below).
extern "C" bool native_legacy_set_refresh_properties(void *display);
extern "C" void native_legacy_vsync_observed(unsigned event,unsigned hz);
extern "C" uint64_t mach_continuous_time(void);
extern "C" void flush_dcache64(addr64_t addr,unsigned count,int phys);
// 32-bit BGRA, plain ('BGRA') or in Apple's lossless-compressed spelling ('&BGA', one plane): the GPU that renders the surface here writes plain rows
// either way (no compression is implemented), so the bytes are the same.
static inline bool nativeBgra(uint32_t format,uint32_t planes) {
    return (format==0x42475241 && !planes) || (format==0x26424741 && planes<=1);
}
// 64-bit RGBA half float ('RGhA', one plane): iPadOS 27's render server picks it for this panel's framebuffer once a Metal device renders
// (every Metal frame was refused while only BGRA was taken). The copy converts it to the scanout's BGRA (convertHalfRows below).
static inline bool nativeHalf(uint32_t format,uint32_t planes) {return format==0x52476841 && !planes;}
static inline bool nativeScanoutFormat(uint32_t format,uint32_t planes) {return nativeBgra(format,planes) || nativeHalf(format,planes);}
// One byte per positive half bit pattern (32768); negative values and NaNs give 0, values of 1.0 and above 255.
static uint8_t *halfTable() {
    static uint8_t *table;
    if(table)return table;
    uint8_t *made=static_cast<uint8_t *>(IOMalloc(32768));
    if(!made)return nullptr;
    unsigned mode=1;PE_parse_boot_argn("sheng-fb-half",&mode,sizeof(mode));
    const uint16_t *thresholds=mode==2?native_half::kEncoded:native_half::kLinear;
    unsigned code=0;
    for(unsigned bits=0;bits<32768;++bits) {
        while(code<255 && bits>=thresholds[code])++code;
        made[bits]=(bits>=0x7c00)?(bits==0x7c00?255:0):uint8_t(code);
    }
    if(!__sync_bool_compare_and_swap(&table,(uint8_t *)nullptr,made))IOFree(made,32768);
    return table;
}
// Rows of RGBA half floats to the scanout's BGRA rows (alpha opaque). Returns false when the table cannot be made or a row runs past either end.
static bool convertHalfRows(volatile void *dst,uint64_t dstLength,uint64_t dstStride,const void *src,uint64_t srcLength,uint64_t srcStride,
                            uint32_t width,uint32_t height) {
    const uint8_t *table=halfTable();
    if(!table || dstStride<uint64_t(width)*4 || srcStride<uint64_t(width)*8 || !height ||
       dstStride*(height-1)+uint64_t(width)*4>dstLength || srcStride*(height-1)+uint64_t(width)*8>srcLength)return false;
    for(uint32_t y=0;y<height;++y) {
        const uint64_t *in=reinterpret_cast<const uint64_t *>(static_cast<const uint8_t *>(src)+srcStride*y);
        volatile uint32_t *out=reinterpret_cast<volatile uint32_t *>(static_cast<volatile uint8_t *>(dst)+dstStride*y);
        for(uint32_t x=0;x<width;++x) {
            const uint64_t p=in[x];
            const uint32_t r=uint32_t(p)&0xffff,g=uint32_t(p>>16)&0xffff,b=uint32_t(p>>32)&0xffff;
            const uint32_t rv=(r&0x8000)?0:table[r],gv=(g&0x8000)?0:table[g],bv=(b&0x8000)?0:table[b];
            out[x]=0xff000000u|(rv<<16)|(gv<<8)|bv;
        }
    }
    return true;
}
class NativeLegacyDisplay;
static NativeLegacyDisplay *gDirectDisplay=nullptr;
extern "C" void native_legacy_direct_snapshot(void);
class NativeLegacyDisplay : public IOMobileFramebufferLegacy {
    OSDeclareDefaultStructors(NativeLegacyDisplay)
    IOMemoryMap *bootMapping=nullptr;
    uint32_t scanBudget=32;
    void *shadow=nullptr;uint64_t shadowBytes=0;bool shadowValid=false;
    bool primary=false;
    bool logicalPM=false;
    bool transferred=false;
    // Every request the validator accepted and the family has not retired yet. A client may submit swap N+1 while swap N is still on the GPU
    // (int-131: swap 3 was validated during swap 2's GPU copy and one remembered pointer lost swap 2, which then never retired and left
    // every wait on it busy), so more than one can be pending.
    const OriginalLegacySwapRequest *validated[8]={};
    bool isValidated(const OriginalLegacySwapRequest *request) const {
        if(!request)return false;
        for(auto entry: validated)if(entry==request)return true;
        return false;
    }
    void removeValidated(const OriginalLegacySwapRequest *request) {for(unsigned i=0;i<8;++i)if(validated[i]==request){validated[i]=nullptr;validatedAt[i]=0;}}
    uint64_t validatedAt[8];   // no initializer: IOKit objects are allocated zeroed, and a member memset would push the constructor's call to the original C2 out of the binding check's window
    static uint64_t nowUs() {uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;}
    unsigned validatedCount() const {unsigned n=0;for(auto entry: validated)if(entry)++n;return n;}
    void stat(unsigned index) {native_legacy_swapstat[index]=native_legacy_swapstat[index]+1;}
    void note(unsigned event,int slot,const OriginalLegacySwapRequest *request) {
        uint32_t submitted=0,retired=0;uint32_t outstanding=0;
        if(&native_adreno_fences && native_adreno_fences(&submitted,&retired)){const int32_t d=int32_t(submitted-retired);outstanding=d>0?(d>255?255:uint32_t(d)):0;}
        if(!native_legacy_ring_cap)return;
        const uint32_t n=native_legacy_ring_count;volatile uint64_t *at=native_legacy_ring[n&(native_legacy_ring_cap-1)];
        at[0]=nowUs();
        at[1]=uint64_t(event)<<56|uint64_t(slot+1)<<48|uint64_t(validatedCount()&255)<<40|uint64_t(outstanding)<<24|((uintptr_t(request)>>4)&0xffffff);
        native_legacy_ring_count=n+1;
    }
    // Microseconds since this request was validated, 0 when it is not tracked.
    uint64_t sinceValidated(const OriginalLegacySwapRequest *request) const {
        for(unsigned i=0;i<8;++i)if(validated[i]==request && validatedAt[i])return nowUs()-validatedAt[i];
        return 0;
    }
    void noteFlipDelay(const OriginalLegacySwapRequest *request) {
        const uint64_t us=sinceValidated(request);
        if(!us)return;
        native_legacy_swapstat[14]=native_legacy_swapstat[14]+uint32_t(us);
        if(us>native_legacy_swapstat[13])native_legacy_swapstat[13]=uint32_t(us>0xffffffffull?0xffffffffull:us);
        if(us>100000) {
            stat(12);
            if(!native_legacy_snap_count && native_legacy_ring_cap) {
                const uint32_t n=native_legacy_ring_count;
                for(unsigned i=0;i<64;++i) {
                    const uint32_t from=n>=64?n-64+i:i;
                    const uint32_t mask=native_legacy_ring_cap-1;native_legacy_snap[i][0]=from<n?native_legacy_ring[from&mask][0]:0;native_legacy_snap[i][1]=from<n?native_legacy_ring[from&mask][1]:0;
                }
                native_legacy_snap_count=1;
            }
        }
    }
    void addValidated(const OriginalLegacySwapRequest *request) {
        if(isValidated(request))return;
        for(unsigned i=0;i<8;++i)if(!validated[i]){validated[i]=request;validatedAt[i]=nowUs();return;}
        validated[0]=request;validatedAt[0]=nowUs();
    }
    const OriginalLegacySwapRequest *gpuRequest=nullptr;
    uint64_t gpuLength=0;
    bool gpuBypass=false;
    // The swap of the frame the GPU client is copying already retired (boot-arg sheng-early-retire, default 1). Retiring the offered
    // swap releases the frame shown before it, whose copy has finished (transferred held this one back); the offered frame itself stays
    // in use until the next swap retires, so the compositor never writes a frame the GPU is still reading.
    bool gpuRetired=false;
    // How a half-float surface is encoded (boot-arg sheng-fb-half=2: values already sRGB-encoded), as the GPU client converts it.
    static uint32_t halfOfferFormat() {
        unsigned mode=1;PE_parse_boot_argn("sheng-fb-half",&mode,sizeof(mode));
        return mode==2?NGP_RGBA16F_ENCODED:NGP_RGBA16F_LINEAR;
    }
    // The refresh tick runs on the family's work loop, the loop of the command gate that registers and removes the vsync clients, so the
    // client queue (kLegacyVsyncClients) never changes while the sender walks it.
    IOTimerEventSource *vsyncTimer=nullptr;
    uint64_t vsyncInterval=0,vsyncNext=0;
    unsigned vsyncHz=0;
    bool vsyncArmed=false;
    // Direct scanout state, used on the work loop only. DPU 9.2 inside the MDP block (0xae01000): VIG 0 and 1 at 0x4000 and 0x6000 (source
    // address 0 at +0x14, stride 0 at +0x24; VIG 1 takes the right half through its own source x), CTL 0 at 0x15000 (flush at +0x18, VIG 0
    // and 1 are bits 0 and 1). i562: a flush of the two pipes latched within 5.4 ms with no start (the panel refreshes by itself).
    struct DirectSlot {OSObject *surface;IOMemoryDescriptor *memory;uint32_t iova;uint64_t used;};
    static constexpr unsigned kDirectSlots=4;
    DirectSlot directSlots[kDirectSlots]={};
    IOMemoryMap *mdpMap=nullptr;
    volatile uint32_t *mdp=nullptr;
    uint64_t *directPages=nullptr;
    IOTimerEventSource *latchTimer=nullptr;
    const OriginalLegacySwapRequest *latchRequest=nullptr;
    uint64_t latchStart=0,directClock=0;
    int shownSlot=-1,pendingSlot=-1;
    uint32_t shownStride=0;
    // The flip waits for the GPU: fenceFor is the request it waits for, fenceTarget the newest fence handed out when it arrived.
    const OriginalLegacySwapRequest *fenceFor=nullptr;
    uint32_t fenceTarget=0;
    uint64_t fenceSince=0;
    IOTimerEventSource *fenceTimer=nullptr;
    bool directOn=false,directBoot=true;
    static constexpr uint32_t kVig[2]={0x4000,0x6000},kCtlFlush=0x15018,kBootIova=0xb8000000U,kBootStride=0x2fa0;
    void startDirect() {
        unsigned mode=0;PE_parse_boot_argn("sheng-dpu-scanout",&mode,sizeof(mode));
        if(mode<2 || !&native_ufs_dpu_map || !&native_ufs_dpu_state || !(native_ufs_dpu_state>>31) || !primary)return;
        IODeviceMemory *memory=IODeviceMemory::withRange(0xae01000,0x8f000);
        mdpMap=memory?memory->map(kIOMapAnywhere|kIOMapInhibitCache):nullptr;
        if(memory)memory->release();
        directPages=static_cast<uint64_t *>(IOMallocData(8192*sizeof(uint64_t)));
        IOWorkLoop *loop=getWorkLoop();
        latchTimer=loop?IOTimerEventSource::timerEventSource(this,&NativeLegacyDisplay::latchFired):nullptr;
        fenceTimer=loop?IOTimerEventSource::timerEventSource(this,&NativeLegacyDisplay::fenceFired):nullptr;
        if(!mdpMap || !directPages || !latchTimer || !fenceTimer || loop->addEventSource(fenceTimer)!=kIOReturnSuccess || loop->addEventSource(latchTimer)!=kIOReturnSuccess) {
            OSSafeReleaseNULL(mdpMap);OSSafeReleaseNULL(latchTimer);OSSafeReleaseNULL(fenceTimer);
            if(directPages){IOFreeData(directPages,8192*sizeof(uint64_t));directPages=nullptr;}
            return;
        }
        mdp=reinterpret_cast<volatile uint32_t *>(mdpMap->getVirtualAddress());
        directOn=true;gDirectDisplay=this;
    }
    // The diagnostic screenshot reads the boot memory, which the display no longer scans: draw the surface on screen into it (rows of the
    // visible width) so the picture shows what the panel shows.
    friend void native_legacy_direct_snapshot(void);
    void snapshot() {
        if(!directOn || directBoot || shownSlot<0 || !bootMapping || !shownStride)return;
        IOMemoryDescriptor *source=directSlots[shownSlot].memory;
        PE_Video video={};uint32_t width=0,height=0;getDisplayDimensions(&width,&height);
        if(!source || PE_current_console(&video) || video.v_baseAddr!=bootMapping->getPhysicalAddress() || video.v_width!=width || video.v_height!=height)return;
        uint8_t *out=reinterpret_cast<uint8_t *>(bootMapping->getVirtualAddress());
        for(uint32_t y=0;y<height && uint64_t(y+1)*video.v_rowBytes<=bootMapping->getLength();++y)
            if(source->readBytes(uint64_t(y)*shownStride,out+uint64_t(y)*video.v_rowBytes,uint64_t(width)*4)!=uint64_t(width)*4)return;
    }
    void flip(uint32_t iova,uint32_t stride) {
        shownStride=stride;
        for(uint32_t vig:kVig){mdp[(vig+0x14)/4]=iova;mdp[(vig+0x24)/4]=stride&0xffff;}
        __asm__ volatile("dsb sy" ::: "memory");
        mdp[kCtlFlush/4]=0x3;
        __asm__ volatile("dsb sy" ::: "memory");
    }
    // The slot that holds this surface, mapping it into a free one (never the one on screen or the one waiting to latch) the first time.
    int directSlot(OSObject *surface,IOMemoryDescriptor *memory) {
        ++directClock;
        int pick=-1;
        for(unsigned k=0;k<kDirectSlots;++k)if(directSlots[k].surface==surface && directSlots[k].memory==memory){directSlots[k].used=directClock;return int(k);}
        for(unsigned k=0;k<kDirectSlots;++k) {
            if(int(k)==shownSlot || int(k)==pendingSlot)continue;
            if(pick<0 || !directSlots[k].surface || (directSlots[pick].surface && directSlots[k].used<directSlots[pick].used))pick=int(k);
        }
        if(pick<0)return -1;
        DirectSlot &slot=directSlots[pick];
        if(slot.surface) {
            slot.memory->complete();OSSafeReleaseNULL(slot.memory);OSSafeReleaseNULL(slot.surface);
            native_legacy_direct[6]=native_legacy_direct[6]+1;
        }
        if(memory->getLength()>8192ull*4096 || memory->prepare()!=kIOReturnSuccess){native_legacy_direct[2]=native_legacy_direct[2]+1;return -1;}
        unsigned count=0;bool ok=true;
        for(IOByteCount offset=0;offset<memory->getLength() && ok;) {
            IOByteCount length=0;
            const addr64_t pa=memory->getPhysicalSegment(offset,&length,kIOMemoryMapperNone);
            if(!pa || (pa&0xfff) || !length){ok=false;break;}
            for(IOByteCount at=0;at<length && offset+at<memory->getLength();at+=4096)directPages[count++]=pa+at;
            offset+=length;
        }
        const uint32_t iova=ok?native_ufs_dpu_map(unsigned(pick),directPages,count):0;
        if(!iova){memory->complete();native_legacy_direct[2]=native_legacy_direct[2]+1;return -1;}
        surface->retain();memory->retain();
        slot={surface,memory,iova,directClock};
        native_legacy_direct[1]=native_legacy_direct[1]+1;
        return pick;
    }
    // True once the GPU wrote everything handed to it before this request arrived (or gave up after 250 ms); otherwise arms the poll.
    bool gpuWrote(const OriginalLegacySwapRequest *request) {
        uint32_t submitted=0,retired=0;
        if(!&native_adreno_fences || !native_adreno_fences(&submitted,&retired))return true;
        if(fenceFor!=request) {
            fenceFor=request;fenceTarget=submitted;fenceSince=0;
        }
        if(int32_t(retired-fenceTarget)>=0) {
            if(fenceSince)note(3,-1,request);
            if(fenceSince) {
                uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time()-fenceSince,&ns);
                const uint32_t us=uint32_t(ns/1000);
                native_legacy_direct[10]=native_legacy_direct[10]+us;
                if(us>native_legacy_direct[11])native_legacy_direct[11]=us;
                if(us>=8000)native_legacy_direct[13]=native_legacy_direct[13]+1;
            }
            fenceFor=nullptr;return true;
        }
        if(!fenceSince) {fenceSince=mach_absolute_time();native_legacy_direct[9]=native_legacy_direct[9]+1;stat(6);note(2,-1,request);}
        uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time()-fenceSince,&ns);
        if(ns>=250000000ull) {
            native_legacy_direct[12]=native_legacy_direct[12]+1;
            fenceFor=nullptr;return true;
        }
        fenceTimer->setTimeoutUS(200);
        return false;
    }
    static void fenceFired(OSObject *owner,IOTimerEventSource *) {static_cast<NativeLegacyDisplay *>(owner)->fenceTick();}
    void fenceTick() {transferred=false;kickSwapQueue();}
    static void latchFired(OSObject *owner,IOTimerEventSource *) {static_cast<NativeLegacyDisplay *>(owner)->latchTick();}
    void latchTick() {
        if(!latchRequest)return;
        uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time()-latchStart,&ns);
        if((mdp[kCtlFlush/4]&0x3) && ns<50000000ull){latchTimer->setTimeoutUS(500);return;}
        if(ns>=50000000ull)native_legacy_direct[3]=native_legacy_direct[3]+1;
        const uint32_t us=uint32_t(ns/1000);
        if(us>native_legacy_direct[4])native_legacy_direct[4]=us;
        native_legacy_direct[5]=native_legacy_direct[5]+us;
        shownSlot=pendingSlot;pendingSlot=-1;
        stat(10);note(5,shownSlot,latchRequest);
        const OriginalLegacySwapRequest *request=latchRequest;latchRequest=nullptr;
        finishTransfer(request,true);
    }
    // Back to the boot loader's memory (the blank frame and the copy are drawn there).
    void directToBoot() {
        if(!directOn || directBoot)return;
        flip(kBootIova,kBootStride);directBoot=true;shownSlot=-1;
        native_legacy_direct[8]=native_legacy_direct[8]+1;
    }
    bool vsyncWanted() const {
        const void *head=nullptr;memcpy(&head,reinterpret_cast<const unsigned char *>(this)+kLegacyVsyncClients,sizeof(head));return head;
    }
    // Selector 72's registration of a vsync client ends in kickSwapQueue (slot 290), which arms the tick; the tick stops by itself once
    // the last client is removed.
    void armVsync() {
        if(!vsyncTimer || vsyncArmed || !vsyncWanted())return;
        vsyncArmed=true;vsyncNext=mach_absolute_time()+vsyncInterval;
        vsyncTimer->wakeAtTime(vsyncNext);native_legacy_vsync_observed(0,vsyncHz);
    }
    static void vsyncFired(OSObject *owner,IOTimerEventSource *) {static_cast<NativeLegacyDisplay *>(owner)->vsyncTick();}
    void vsyncTick() {
        vsyncArmed=false;
        if(!vsyncTimer)return;
        if(!vsyncWanted()){native_legacy_vsync_observed(2,vsyncHz);return;}
        const uint64_t now=mach_absolute_time();
        native_legacy_vsync_notify(this,now,mach_continuous_time(),vsyncInterval);
        native_legacy_vsync_observed(1,vsyncHz);
        vsyncNext+=vsyncInterval;
        if(vsyncNext<=now){vsyncNext=now+vsyncInterval;native_legacy_vsync_observed(3,vsyncHz);}
        vsyncArmed=true;vsyncTimer->wakeAtTime(vsyncNext);
    }
    void startVsync() {
        unsigned hz=native_legacy_refresh_hz();
        if(!hz || hz>240 || !primary || !bootMapping || vsyncTimer)return;
        IOWorkLoop *loop=getWorkLoop();
        if(!loop)return;
        IOTimerEventSource *timer=IOTimerEventSource::timerEventSource(this,&NativeLegacyDisplay::vsyncFired);
        if(!timer)return;
        if(loop->addEventSource(timer)!=kIOReturnSuccess){timer->release();return;}
        nanoseconds_to_absolutetime(1000000000ull/hz,&vsyncInterval);
        vsyncHz=hz;vsyncTimer=timer;native_legacy_vsync_observed(4,hz);
        // A client may already have registered: arm under the gate, like the registration does.
        loop->runAction([](OSObject *owner,void *,void *,void *,void *)->IOReturn {
            static_cast<NativeLegacyDisplay *>(owner)->armVsync();return kIOReturnSuccess;
        },this);
    }
    void stopVsync() {
        if(!vsyncTimer)return;
        vsyncTimer->cancelTimeout();
        if(IOWorkLoop *loop=getWorkLoop())loop->removeEventSource(vsyncTimer);
        OSSafeReleaseNULL(vsyncTimer);vsyncArmed=false;
    }
public:
    // Called by the GPU client's completion (any thread): the swap is finished on the framebuffer's work loop.
    void gpuComplete(int status) {
        struct Args {NativeLegacyDisplay *display;int status;} args={this,status};
        IOWorkLoop *loop=getWorkLoop();
        auto action=[](OSObject *,void *a0,void *,void *,void *)->IOReturn {
            Args *a=static_cast<Args *>(a0);a->display->finishGpuSwap(a->status);return kIOReturnSuccess;
        };
        if(loop)loop->runAction(action,this,&args);else finishGpuSwap(status);
    }
    void free() override {stopVsync();if(logicalPM){PMstop();logicalPM=false;}primary=false;OSSafeReleaseNULL(bootMapping);IOMobileFramebufferLegacy::free();}
    void stop(IOService *provider) override {
        stopVsync();
        transferred=false;for(auto &entry: validated)entry=nullptr;gpuRequest=nullptr;gpuRetired=false;
        directToBoot();latchRequest=nullptr;pendingSlot=-1;
        if(fenceTimer){fenceTimer->cancelTimeout();if(IOWorkLoop *loop=getWorkLoop())loop->removeEventSource(fenceTimer);OSSafeReleaseNULL(fenceTimer);}
        if(latchTimer){latchTimer->cancelTimeout();if(IOWorkLoop *loop=getWorkLoop())loop->removeEventSource(latchTimer);OSSafeReleaseNULL(latchTimer);}
        directOn=false;
        if(logicalPM){PMstop();logicalPM=false;}
        primary=false;IOMobileFramebufferLegacy::stop(provider);OSSafeReleaseNULL(bootMapping);
    }
    // The panel is already lit by the firmware's GOP framebuffer, which is the one thing the original code's power-state word (the "powered" word
    // of the state pair) is meant to say; the platform subclass that normally writes it is not part of this
    // port, so without it the user client's wait (selector 6) answers kIOReturnBusy forever and the compositor never presents. This is the
    // original state setter (the virtual the original code calls with this same request shape) with only the powered word named.
    void announcePoweredByFirmware() {
        OriginalLegacyStateRequest request={1,0,0,1,1,{}};
        applyStateRequest(&request,false);
    }
    bool start(IOService *provider) override {
        if(!startDisplay(provider))return false;
        publishOneLayer();
        startVsync();startDirect();return true;
    }
    // CoreAnimation reads how many layers the display scans out from IOMFBNumLayers (MaxNumLayers). Without the property it may hand a
    // full-screen layer (an app's Metal layer) to the display as a detached layer of its own, copied through the IOSurfaceAccelerator
    // scaler, which does not exist here (backboardd aborts: "Failed to create IOSurfaceAccelerator"). This display scans out one layer.
    void publishOneLayer() {
        OSDictionary *layers=OSDictionary::withCapacity(1);
        OSNumber *count=OSNumber::withNumber(1ULL,32);
        if(layers && count && layers->setObject("MaxNumLayers",count))setProperty("IOMFBNumLayers",layers);
        OSSafeReleaseNULL(count);OSSafeReleaseNULL(layers);
    }
    bool startDisplay(IOService *provider) {
        if(!IOMobileFramebufferLegacy::start(provider))return false;
        if(!native_legacy_ring) {
            const uint32_t cap=65536;
            volatile uint64_t (*memory)[2]=(volatile uint64_t (*)[2])IOMallocZero(sizeof(uint64_t)*2*cap);
            if(memory) {native_legacy_ring=memory;native_legacy_ring_cap=cap;}
        }
        if(primary && bootMapping)announcePoweredByFirmware();
        unsigned enabled=0;PE_parse_boot_argn("offline-legacy-pm",&enabled,sizeof(enabled));
        if(!enabled)return true;
        unsigned phase=0;PE_parse_boot_argn("offline-legacy-phase",&phase,sizeof(phase));
        if(enabled!=1 || phase!=4 || !provider || !bootMapping || !primary) {
            native_legacy_pm_observed(1,kIOReturnNotReady,0);IOMobileFramebufferLegacy::stop(provider);return false;
        }
        // Actual IOKit PM lifecycle for logical scanout scheduling only. GOP
        // provides no panel power switch: neither state advertises physical
        // power output, power control or a hardware power transition.
        static IOPMPowerState states[2]={};
        states[0].version=states[1].version=1;
        states[1].capabilityFlags=kIOPMDeviceUsable;
        PMinit();logicalPM=initialized;
        if(!logicalPM){native_legacy_pm_observed(1,kIOReturnNotReady,0);IOMobileFramebufferLegacy::stop(provider);return false;}
        provider->joinPMtree(this);
        IOReturn status=registerPowerDriver(this,states,2);native_legacy_pm_observed(1,status,0);
        if(status==kIOReturnSuccess){status=changePowerStateToPriv(1);native_legacy_pm_observed(2,status,1);}
        if(status!=kIOReturnSuccess){PMstop();logicalPM=false;IOMobileFramebufferLegacy::stop(provider);return false;}
        return true;
    }
    IOReturn setPowerState(unsigned long ordinal,IOService *device) override {
        if(!logicalPM)return IOMobileFramebufferLegacy::setPowerState(ordinal,device);
        if(ordinal>1 || !bootMapping){native_legacy_pm_observed(3,kIOReturnUnsupported,ordinal);return kIOReturnUnsupported;}
        native_legacy_pm_observed(3,kIOReturnSuccess,ordinal);
        IOReturn status=IOMobileFramebufferLegacy::setPowerState(ordinal,device);
        native_legacy_pm_observed(4,status,ordinal);return status;
    }
    // The explicit offline system-client experiment publishes only the real
    // GOP mode. Unknown backend calls retain their stop-with-caller guards.
    void registerService(IOOptionBits options=0) override {
        unsigned enabled=0;PE_parse_boot_argn("offline-legacy-register",&enabled,sizeof(enabled));
        if(enabled!=1)native_legacy_backend_guard(84,this,__builtin_return_address(0));
        // Nothing in the family publishes IOMFBDisplayRefresh (its platform subclass does), and without it the render server keeps its own
        // 1/60 s heartbeat (int-210: refresh=0). Publish it before any client can find the display, only when sheng-vsync asks for more.
        unsigned hz=native_legacy_refresh_hz();
        if(primary && hz>60 && hz<=240)native_legacy_set_refresh_properties(this);
        IOService::registerService(options);native_legacy_registration_observed(this);
    }
    bool isMainDisplay() override {return primary;}
    // The render server paces its display timer by the minimum refresh interval in IOMFBDisplayRefresh (int-209: no vsync client ever
    // registered, frames came every 16.7 ms), and the generic slot 301 fixes it at 1/60 s. With sheng-vsync=<Hz> it reports the panel's
    // own rate instead; the maximum interval and the step keep the original values. Words in 32.32 seconds, then their mach times.
    // The original maximum interval is 1/24 s (int-240), which offers the render server a 24 to sheng-vsync Hz range. The panel has no
    // variable refresh, so sheng-vsync-fixed=1 reports the maximum interval equal to the minimum: one fixed rate.
    bool getRefreshAttributes(uint64_t *words,bool fixed) override {
        if(!IOMobileFramebufferLegacy::getRefreshAttributes(words,fixed) || !words)return false;
        unsigned hz=native_legacy_refresh_hz();
        if(hz<=60 || hz>240)return true;
        unsigned locked=0;PE_parse_boot_argn("sheng-vsync-fixed",&locked,sizeof(locked));
        const uint64_t interval=(1ull<<32)/hz;
        uint64_t machTime=0;nanoseconds_to_absolutetime(interval*1000000000ull>>32,&machTime);
        words[0]=interval;words[1]=machTime;
        if(fixed || locked==1){words[2]=interval;words[3]=machTime;}
        native_legacy_vsync_observed(5,hz);
        return true;
    }
    // A swap the client submitted while the previous one was still on its way (transferred) is not kicked by the family again: without the
    // re-kick after each retirement it waited for the next submission (int-143: every offer came one frame late, a still screen showed the
    // previous frame until the user touched it again). A retirement inside a kick only asks the running kick to loop.
    bool kicking=false,rekick=false;
    void kickSwapQueue() override {
        armVsync();
        if(kicking){rekick=true;return;}
        kicking=true;
        do {
            rekick=false;
            kickOnce();
            gpuBypass=false;
        } while(rekick && !transferred);
        kicking=false;
    }
    void kickOnce() {
        unsigned enabled=0;PE_parse_boot_argn("offline-legacy-transfer",&enabled,sizeof(enabled));
        stat(1);
        if(enabled!=1 || !logicalPM || !bootMapping || !primary){stat(3);return;}
        if(transferred){stat(2);return;}
        const OriginalLegacySwapRequest *request=pendingSwapRequest();
        if(!request || !isValidated(request)){stat(4);return;}
        // This bounded first-frame transfer preserves the original queue's
        // resource ownership. Separately gated retirement follows the copy;
        // neither path signals a hardware fence, announces a vblank or
        // claims general producer synchronization.
        // The owned GPU instrument completes its work before submission.
        // The separate original software-renderer experiment uses completed
        // synchronous CPU surfaces, with no GPU or display fence fabricated.
        OSObject *surface=nullptr;
        memcpy(&surface,reinterpret_cast<const unsigned char *>(request)+kRequestSurfaces,sizeof(surface));
        if(!surface) {
            // Validated blank swap (all layers disabled over black): solid-fill the actual GOP.
            PE_Video blank={};
            uint32_t bw=0,bh=0;getDisplayDimensions(&bw,&bh);
            uint64_t blankLength=(uint64_t)bw*bh*4;
            if(PE_current_console(&blank) || blank.v_baseAddr!=bootMapping->getPhysicalAddress() ||
               blank.v_width!=bw || blank.v_height!=bh || blank.v_depth!=32 || blank.v_rotate ||
               !native_legacy_surface_bounds(bw,bh,blank.v_rowBytes,bootMapping->getLength(),bootMapping->getLength())) {
                native_legacy_transfer_observed(kIOReturnNotReady,request,0);return;
            }
            if(!native_legacy_prepare_scanout()) {native_legacy_transfer_observed(kIOReturnNotReady,request,0);return;}
            directToBoot();
            if(!native_legacy_fill_pixels(reinterpret_cast<volatile void *>(bootMapping->getVirtualAddress()),
                bootMapping->getLength(),blank.v_rowBytes,bw,bh,0xff000000U)) {
                native_legacy_transfer_observed(kIOReturnBadArgument,request,0);return;
            }
            __asm__ volatile("dsb sy" ::: "memory");
            shadowValid=false;
            transferred=true;
            native_legacy_frame_observed(1,0,0,0,0,0);
            stat(8);note(8,-1,request);
            {const uint32_t colors[4]={0xff000000U,0,0,0};const uint64_t counts[4]={(uint64_t)bw*bh,0,0,0};native_legacy_frame_colors_observed(colors,counts,0);}
            native_legacy_transfer_observed(kIOReturnSuccess,request,blankLength);
            finishTransfer(request);
            return;
        }
        if(!surface->metaCast("IOSurface"))return;
        IOMemoryDescriptor *memory=native_legacy_surface_descriptor(surface);
        PE_Video video={};
        uint32_t width=0,height=0;getDisplayDimensions(&width,&height);
        uint64_t length=(uint64_t)width*height*4;
        uint64_t sourceStride=native_legacy_surface_stride(surface);
        uint64_t allocation=native_legacy_surface_allocation(surface);
        if(!memory || !memory->metaCast("IOMemoryDescriptor") || !native_legacy_surface_bounds(width,height,sourceStride,allocation,memory->getLength()) ||
           native_legacy_surface_width(surface)<width || native_legacy_surface_height(surface)<height ||
           !nativeScanoutFormat(native_legacy_surface_format(surface),native_legacy_surface_planes(surface)) ||
           PE_current_console(&video) || video.v_baseAddr!=bootMapping->getPhysicalAddress() ||
           video.v_width!=width || video.v_height!=height || video.v_depth!=32 || video.v_rotate ||
           !native_legacy_surface_bounds(width,height,video.v_rowBytes,bootMapping->getLength(),bootMapping->getLength())) {
            native_legacy_transfer_observed(kIOReturnNotReady,request,0);return;
        }
        // The diagnostic renderer repaints the page over the scanout until it is told the system owns the display; the GPU path needs that as much
        // as the CPU copy (int-093: accepted swaps were GPU-copied under a page that kept drawing).
        if(!gpuBypass) {
            if(!native_legacy_prepare_scanout()) {native_legacy_transfer_observed(kIOReturnNotReady,request,0);return;}
        }
        noteSurface(surface,memory);
        const bool half=nativeHalf(native_legacy_surface_format(surface),native_legacy_surface_planes(surface));
        if(directOn && !half && !gpuBypass && latchRequest)stat(5);
        if(directOn && !half && !gpuBypass && !latchRequest) {
            if(!gpuWrote(request)) {transferred=true;return;}
            const int slot=directSlot(surface,memory);
            if(slot>=0) {
                native_legacy_swap_stage(1);
                flip(directSlots[slot].iova,uint32_t(sourceStride));directBoot=false;
                native_legacy_direct[0]=native_legacy_direct[0]+1;
                stat(9);note(4,slot,request);noteFlipDelay(request);
                shadowValid=false;pendingSlot=slot;latchRequest=request;transferred=true;
                latchStart=mach_absolute_time();latchTimer->setTimeoutUS(500);
                return;
            }
            native_legacy_direct[7]=native_legacy_direct[7]+1;
        }
        directToBoot();
        stat(7);note(7,-1,request);
        if(!gpuBypass && native_legacy_gpu_offer(this,memory,sourceStride,width,height,half?halfOfferFormat():NGP_BGRX8888)) {
            // The GPU writes the scanout memory behind the CPU's back: the differential copy's picture of it is stale from now on.
            native_legacy_swap_stage(1);
            shadowValid=false;gpuRequest=request;gpuLength=length;transferred=true;
            // The compositor renders the next frame only after this swap retires, so retiring at the end of the copy serialized render and
            // copy (int-243: about 4.4 ms render then about 3 ms copy per frame). Retire now: the presenter holds its own reference to this
            // frame's memory, the previous frame's copy has finished (transferred held this one back), and the next swap still waits for
            // the copy because transferred stays set until finishGpuSwap.
            unsigned early=1;PE_parse_boot_argn("sheng-early-retire",&early,sizeof(early));
            if(early==1 && retireSwap(request,true)) {
                gpuRetired=true;native_legacy_early_retires=native_legacy_early_retires+1;
            }
            return;
        }
        // Surfaces may be write-combined (iPadOS 27 display surfaces are): reading them uncached costs seconds per frame. Read through
        // a cacheable view, after cleaning and invalidating it so lines from the previous frame are not reused.
        IOMemoryMap *source=memory->map(kIOMapAnywhere|kIOMapReadOnly|kIOMapCopybackCache);
        if(!source || !source->getVirtualAddress() || source->getLength()<allocation) {
            OSSafeReleaseNULL(source);native_legacy_transfer_observed(kIOReturnNoMemory,request,0);return;
        }
        flush_dcache64((addr64_t)source->getVirtualAddress(),(unsigned)source->getLength(),false);
        if(half) {
            if(!native_legacy_prepare_scanout()) {OSSafeReleaseNULL(source);native_legacy_transfer_observed(kIOReturnNotReady,request,0);return;}
            uint64_t copyStart;__asm__ volatile("isb\n\tmrs %0, cntpct_el0":"=r"(copyStart));
            if(!convertHalfRows(reinterpret_cast<volatile void *>(bootMapping->getVirtualAddress()),bootMapping->getLength(),video.v_rowBytes,
                reinterpret_cast<const void *>(source->getVirtualAddress()),source->getLength(),sourceStride,width,height)) {
                OSSafeReleaseNULL(source);native_legacy_transfer_observed(kIOReturnBadArgument,request,0);return;
            }
            __asm__ volatile("dsb sy" ::: "memory");
            {uint64_t copyEnd;__asm__ volatile("isb\n\tmrs %0, cntpct_el0":"=r"(copyEnd));native_legacy_copy_timing_observed(copyEnd-copyStart,length);}
            shadowValid=false;OSSafeReleaseNULL(source);transferred=true;
            native_legacy_transfer_observed(kIOReturnSuccess,request,length);
            finishTransfer(request);
            return;
        }
        if(!native_legacy_copy_layout(reinterpret_cast<volatile void *>(bootMapping->getVirtualAddress()),
            bootMapping->getLength(),video.v_rowBytes,reinterpret_cast<const void *>(source->getVirtualAddress()),
            source->getLength(),sourceStride,width,height)) {
            OSSafeReleaseNULL(source);native_legacy_transfer_observed(kIOReturnBadArgument,request,0);return;
        }
        // The two content scans read every pixel again; they only describe the first frames (the probe keeps 32).
        if(scanBudget) {
            --scanBudget;
            uint64_t scan[5];
            if(native_legacy_frame_scan(reinterpret_cast<const void *>(source->getVirtualAddress()),source->getLength(),sourceStride,width,height,scan))
                native_legacy_frame_observed(0,scan[0],scan[1],scan[2],scan[3],scan[4]);
            uint32_t colors[4];uint64_t counts[4],other=0;
            if(native_legacy_frame_colors(reinterpret_cast<const void *>(source->getVirtualAddress()),source->getLength(),sourceStride,width,height,colors,counts,&other))
                native_legacy_frame_colors_observed(colors,counts,other);
        }
        if(!native_legacy_prepare_scanout()) {
            OSSafeReleaseNULL(source);native_legacy_transfer_observed(kIOReturnNotReady,request,0);return;
        }
        uint64_t copyStart;__asm__ volatile("isb\n\tmrs %0, cntpct_el0":"=r"(copyStart));
        uint64_t stored=UINT64_MAX;
        {
            // Store only what changed since the previous frame (the scanout memory is slow device memory).
            uint64_t need=(uint64_t)width*height*4;
            // Differential copy is opt-in (sheng-fb-diff=1): on hardware it reads the whole source every frame and was not faster
            // than the plain copy (143-285 ms per swap either way), and the first boots with it were followed by an unresponsive
            // lock screen that disappeared with the plain copy (int-005).
            static int diffOff=-1;
            if(diffOff<0){unsigned v=0;PE_parse_boot_argn("sheng-fb-diff",&v,sizeof v);diffOff=(v!=1);}
            if(!diffOff && !shadow && need<=(64ull<<20)){shadow=IOMallocAligned((vm_size_t)need,16384);if(shadow){shadowBytes=need;shadowValid=false;}}
            if(!diffOff && shadow && shadowBytes>=need)
                stored=native_legacy_copy_diff(reinterpret_cast<volatile void *>(bootMapping->getVirtualAddress()),
                    bootMapping->getLength(),video.v_rowBytes,reinterpret_cast<const void *>(source->getVirtualAddress()),
                    source->getLength(),sourceStride,width,height,shadow,shadowValid);
            if(stored!=UINT64_MAX)shadowValid=true;
        }
        if(stored==UINT64_MAX) {
            shadowValid=false;
            if(!native_legacy_copy_pixels(reinterpret_cast<volatile void *>(bootMapping->getVirtualAddress()),
                bootMapping->getLength(),video.v_rowBytes,reinterpret_cast<const void *>(source->getVirtualAddress()),
                source->getLength(),sourceStride,width,height)) {
                OSSafeReleaseNULL(source);native_legacy_transfer_observed(kIOReturnBadArgument,request,0);return;
            }
            stored=length;
        }
        __asm__ volatile("dsb sy" ::: "memory");
        {uint64_t copyEnd;__asm__ volatile("isb\n\tmrs %0, cntpct_el0":"=r"(copyEnd));native_legacy_copy_timing_observed(copyEnd-copyStart,stored);}
        OSSafeReleaseNULL(source);transferred=true;
        native_legacy_transfer_observed(kIOReturnSuccess,request,length);
        finishTransfer(request);
    }
    // The GPU client reported its copy (status 0) or lost it. Success retires the swap like a finished copy; anything else uses the CPU copy.
    void finishGpuSwap(int status) {
        const OriginalLegacySwapRequest *request=gpuRequest;
        if(!request)return;
        gpuRequest=nullptr;
        if(gpuRetired) {
            // The swap retired when the client took it; its copy ending only releases the next frame. A failed copy loses this frame
            // (the next one takes the CPU copy).
            gpuRetired=false;
            if(status==0)native_legacy_transfer_observed(kIOReturnSuccess,request,gpuLength);
            else {native_legacy_early_failures=native_legacy_early_failures+1;native_legacy_gpu_fallback_observed(status);gpuBypass=true;}
            transferred=false;kickSwapQueue();return;
        }
        if(status==0) {
            native_legacy_transfer_observed(kIOReturnSuccess,request,gpuLength);
            finishTransfer(request,true);return;
        }
        native_legacy_gpu_fallback_observed(status);
        transferred=false;gpuBypass=true;kickSwapQueue();
    }
    // Hands the pending head to the unchanged family's completion. Leaves transferred alone: the callers decide when the next frame may go.
    bool retireSwap(const OriginalLegacySwapRequest *request,bool force) {
        unsigned retire=0,cpuPresent=0;
        PE_parse_boot_argn("offline-legacy-retire",&retire,sizeof(retire));
        PE_parse_boot_argn("offline-legacy-cpu-present",&cpuPresent,sizeof(cpuPresent));
        if(!(retire==1 || cpuPresent==1 || force))return false;
        if(!activateOnlyPendingSwap(request))return false;
        stat(11);note(6,-1,request);
        removeValidated(request);
        IOMobileFramebufferLegacy::completeCurrentSwap();
        native_legacy_retirement_observed(this);
        return true;
    }
    // Separately gated retirement after a real scanout store, shared by copied and blank frames.
    void finishTransfer(const OriginalLegacySwapRequest *request,bool force=false) {
        // Only after the real copy and store barrier: select the actual pending head and let the unchanged family release it and wake
        // waiters. This reports memory scanout completion, not a panel vblank or hardware fence.
        if(!retireSwap(request,force))return;
        // The unchanged family has retired this generation and released its current reference. Permit the next independently validated
        // single pending frame. Keep the latch set while completion can reenter this method, and whenever retirement remains disabled.
        transferred=false;
        kickSwapQueue();
    }
#if defined(NATIVE_LEGACY_CANCEL_SWAPS) && NATIVE_LEGACY_CANCEL_SWAPS
    // swap_wait_gated gives up on a swap that has not completed in 5 s ("CommandWake didn't happen even after 5 seconds ... cancelling
    // swaps") and calls this first, then cancels its own queue. Forget the swaps this class tracks: a GPU copy that ends later finds no
    // request (finishGpuSwap), a request the family cancelled is no longer its queue's head, so nothing completes twice and the next
    // frame is not held behind the cancelled one. The family only logs the result; the slot used to be a guard that panicked.
    bool cancelSwapsAfterTimeout() override {
        native_legacy_swap_timeouts=native_legacy_swap_timeouts+1;
        for(auto &entry: validated)entry=nullptr;
        gpuRequest=nullptr;gpuRetired=false;transferred=false;
        return true;
    }
#endif
    IOReturn collectCompletionReports(uint32_t,void *,uint32_t *count,uint32_t) override {
        // The original completion helper passes null optional outputs and
        // ignores this IOReturn. GOP exposes no hardware completion telemetry.
        if(count)*count=0;
        return kIOReturnUnsupported;
    }
    bool validateSwap(const OriginalLegacySwapRequest *request) override {
        // Qualified original request fields are packed; use memcpy for each
        // access. Accept only one full-frame, untransformed, nonplanar BGRA
        // surface with real original backing memory. No generic capability
        // or success is supplied for other layer/format/geometry contracts.
        auto bytes=reinterpret_cast<const unsigned char *>(request);
        auto word=[&](unsigned offset){uint32_t value=0;memcpy(&value,bytes+offset,4);return value;};
        OSObject *surface=nullptr;unsigned reason=1;bool accepted=false;
        unsigned enabled=0;PE_parse_boot_argn("offline-legacy-validate",&enabled,sizeof(enabled));
        if(enabled==1 && logicalPM && primary && bootMapping && request) {
            reason=2;OSObject *surfaces[4]={};memcpy(surfaces,bytes+kRequestSurfaces,sizeof(surfaces));surface=surfaces[0];
            uint32_t width=0,height=0;getDisplayDimensions(&width,&height);
            if(surface && surface->metaCast("IOSurface") && !surfaces[1] && !surfaces[2] && !surfaces[3] &&
               native_legacy_single_layer_state(word(kRequestMasks),word(kRequestMasks+4),word(kRequestMasks+8))) {
                reason=3;bool rectangles=true;
                const unsigned rectOffsets[]={kRequestRect0,kRequestRect1};
                for(unsigned offset: rectOffsets) {
                    uint32_t rect[4]={};memcpy(rect,bytes+offset,sizeof(rect));
                    rectangles=rectangles && rect[0]==0 && rect[1]==0 && rect[2]==width && rect[3]==height;
                }
                bool options=true;for(unsigned offset=kRequestOptions;offset<=kRequestOptions+8;offset+=4)options=options && word(offset)==0;
                if(width && height && rectangles && options && word(kRequestDims)==width && word(kRequestDims+4)==height) {
                    reason=4;
                    IOMemoryDescriptor *memory=native_legacy_surface_descriptor(surface);
                    // A surface larger than the display (rows or lines padded to the GPU's alignment) is accepted: only the visible rectangle,
                    // which the request's rectangles already name as the whole display, is copied.
                    if(native_legacy_surface_width(surface)>=width && native_legacy_surface_height(surface)>=height &&
                       nativeScanoutFormat(native_legacy_surface_format(surface),native_legacy_surface_planes(surface)) &&
                       memory && memory->metaCast("IOMemoryDescriptor") &&
                       native_legacy_surface_bounds(width,height,native_legacy_surface_stride(surface),
                           native_legacy_surface_allocation(surface),memory->getLength())) {
                        reason=0;accepted=true;
                    } else {
                        native_legacy_surface_rejected(native_legacy_surface_width(surface),native_legacy_surface_height(surface),
                            native_legacy_surface_format(surface),native_legacy_surface_planes(surface),native_legacy_surface_stride(surface),
                            native_legacy_surface_allocation(surface),memory?memory->getLength():0);
                    }
                }
            }
        }
        if(!accepted && reason==2 && !surface && enabled==1) {
            // No surface in any slot and every updated layer disabled over black: present a blank screen.
            OSObject *none[4]={};memcpy(none,bytes+kRequestSurfaces,sizeof(none));
            uint32_t width=0,height=0;getDisplayDimensions(&width,&height);
            bool options=true;for(unsigned offset=kRequestOptions;offset<=kRequestOptions+8;offset+=4)options=options && word(offset)==0;
            if(!none[0] && !none[1] && !none[2] && !none[3] && width && height && options &&
               word(kRequestDims)==width && word(kRequestDims+4)==height && native_legacy_blank_state(word(kRequestMasks),word(kRequestMasks+4),word(kRequestMasks+8))) {
                reason=5;accepted=true;
            }
        }
        if(accepted){addValidated(request);stat(0);note(1,-1,request);const unsigned depth=validatedCount();if(depth>native_legacy_swapstat[15])native_legacy_swapstat[15]=depth;}
        else removeValidated(request);
        native_legacy_swap_validation_observed(accepted,reason,request,surface);return accepted;
    }
    // The original slot-188 caller passes a uint32_t output and consumes an
    // IOReturn. Its implementation assumes an AppleARMIODevice provider.
    // Query our actual generic provider instead; GOP supplies no dot pitch.
    IOReturn getDotPitch(uint32_t *pitch) override {
        if(!pitch)return kIOReturnBadArgument;
        *pitch=0;IOService *provider=getProvider();if(!provider)return kIOReturnNotReady;
        OSObject *property=provider->getProperty("dot-pitch");
        if(!property)return kIOReturnNotFound;
        OSData *data=OSDynamicCast(OSData,property);
        if(!data || data->getLength()!=sizeof(*pitch))return kIOReturnBadArgument;
        memcpy(pitch,data->getBytesNoCopy(),sizeof(*pitch));return kIOReturnSuccess;
    }
    // Selector 74 enumerates optional frame-info trace keys, not display
    // modes. This backend implements no trace-key subscription contract.
    IOReturn supportedFrameInfo(uint32_t,uint32_t *count,char *key,uint32_t *mask) override {
        if(!count || !key || !mask)return kIOReturnBadArgument;
        *count=0;*mask=0;bzero(key,64);return kIOReturnUnsupported;
    }
    IOReturn newUserClient(task_t task,void *security,UInt32 type,IOUserClient **client) override {
        native_legacy_client_enter(task,type);
        IOReturn status=IOMobileFramebufferLegacy::newUserClient(task,security,type,client);
        native_legacy_client_observed(status,status==kIOReturnSuccess && client?*client:nullptr);return status;
    }
    bool startHardware() override {
        unsigned enabled=0;
        PE_parse_boot_argn("offline-legacy-gop",&enabled,sizeof(enabled));
        if(enabled!=1)return false;
        PE_Video video={};IOService *provider=getProvider();
        if(!provider || PE_current_console(&video) || !video.v_baseAddr ||
           !video.v_width || !video.v_height || video.v_depth!=32 || video.v_rotate ||
           video.v_width>16384 || video.v_height>16384 || video.v_rowBytes<video.v_width*4 ||
           video.v_rowBytes>UINT64_MAX/video.v_height)return false;
        IOMemoryMap *mapping=provider->mapDeviceMemoryWithIndex(0,kIOMapInhibitCache);
        if(!mapping)return false;
        bool valid=mapping->getVirtualAddress() && mapping->getPhysicalAddress()==video.v_baseAddr &&
                   mapping->getLength()==video.v_rowBytes*video.v_height;
        if(valid) {
            installBootDimensions((uint32_t)video.v_width,(uint32_t)video.v_height);
            uint32_t width=0,height=0;getDisplayDimensions(&width,&height);
            valid=width==video.v_width && height==video.v_height;
            if(valid) {
                OSSafeReleaseNULL(bootMapping);bootMapping=mapping;mapping=nullptr;primary=video.v_display==1;
                valid=native_legacy_gop_mapping_observed(this,bootMapping,width,height);
#if defined(NATIVE_LEGACY_DEFAULT_SURFACE) && NATIVE_LEGACY_DEFAULT_SURFACE
                // The family calls its void slot 316 after startHardware. Our backend supplies the client's default surface.
                if(valid)valid=installDefaultSurface(width,height);
#endif
            }
        }
        OSSafeReleaseNULL(mapping);return valid;
    }
#define LEAF(n) __attribute__((noreturn,noinline)) void opaque##n() override { native_legacy_backend_guard(n,this,__builtin_return_address(0)); }
    NATIVE_LEGACY_LEAVES(LEAF)
#undef LEAF
};
OSDefineMetaClassAndStructors(NativeLegacyDisplay,IOMobileFramebufferLegacy)
extern "C" void native_legacy_direct_snapshot(void) {if(gDirectDisplay)gDirectDisplay->snapshot();}
IOService *native_legacy_display_allocate(void) {return new NativeLegacyDisplay;}
extern "C" void native_legacy_gpu_complete(void *display,int status) {if(display)static_cast<NativeLegacyDisplay *>(display)->gpuComplete(status);}
