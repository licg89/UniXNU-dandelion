// SPDX-License-Identifier: BSD-2-Clause-Patent
// Bounded offline original lifecycle instrument; no desktop claim.
#include <IOKit/IOPlatformExpert.h>
#include "DandelionFont6x10.h"
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOUserClient.h>
#include <libkern/c++/OSMetaClass.h>
#include <libkern/c++/OSSerialize.h>
#include <libkern/c++/OSData.h>
#include <kern/task.h>
#include <kern/clock.h>
#include <kern/thread_call.h>
#include <sys/proc.h>
#include <pexpert/pexpert.h>
#include <mach/kmod.h>
#include <machine/machine_routines.h>
#include <stdarg.h>

extern "C" void native_gop_text(unsigned line, const char *text) __attribute__((weak));
extern "C" void native_gop_redraw_ring(void) __attribute__((weak));
extern "C" void native_diag_process_summary(void) __attribute__((weak));
static void dline(unsigned line, const char *format, ...) {
    char text[96];
    va_list list;
    if (!native_gop_text) return;
    va_start(list, format);
    vsnprintf(text, sizeof(text), format, list);
    va_end(list);
    native_gop_text(line, text);
}

/*
 * Reaches the kernel console (and therefore the log block this port reads back), unlike dline(): the
 * on-screen text hook native_gop_text is stubbed while the provider is being brought up, so dline()
 * writes nothing at all.  Stage markers only; no new state.
 */
static void slog(const char *format, ...) {
    char text[160];
    va_list list;
    va_start(list, format);
    vsnprintf(text, sizeof(text), format, list);
    va_end(list);
    IOLog("DANDELION-DISP %s\n", text);
}

/* Last-resort visual path: prove the boot GOP still scans out even when the
 * original framebuffer provider is not accepted by userland. */
static void gopPaint(const char *why, bool full) {
    PE_Video video = {};
    if (PE_current_console(&video) || !video.v_baseAddr || !video.v_width ||
        !video.v_height || video.v_depth != 32 || video.v_rotate ||
        !video.v_rowBytes || video.v_width > 16384 || video.v_height > 16384) {
        dline(21, "GOP NOVID %s", why);
        return;
    }
    IODeviceMemory *memory = IODeviceMemory::withRange(video.v_baseAddr,
        (IOByteCount)video.v_rowBytes * video.v_height);
    IOMemoryMap *mapping = memory ? memory->map(kIOMapAnywhere | kIOMapInhibitCache) : nullptr;
    if (memory) memory->release();
    if (!mapping || !mapping->getVirtualAddress()) {
        dline(21, "GOP NOMAP %s", why);
        OSSafeReleaseNULL(mapping);
        return;
    }
    static const uint32_t colors[8] = {
        0xFFFFFFFFU, 0xFFFFFF00U, 0xFF00FFFFU, 0xFF00FF00U,
        0xFFFF00FFU, 0xFFFF0000U, 0xFF0000FFU, 0xFF000000U,
    };
    volatile uint8_t *base = reinterpret_cast<volatile uint8_t *>(mapping->getVirtualAddress());
    const uint32_t first = full ? 0 : (video.v_height > 320 ? video.v_height - 320 : 0);
    for (uint32_t y = first; y < video.v_height; ++y) {
        volatile uint32_t *row = reinterpret_cast<volatile uint32_t *>(base + (uint64_t)y * video.v_rowBytes);
        for (uint32_t x = 0; x < video.v_width; ++x)
            row[x] = colors[(uint64_t)x * 8 / video.v_width];
    }
    __asm__ volatile("dsb sy" ::: "memory");
    dline(21, "GOP FALLBACK %s", why);
    mapping->release();
}

extern "C" kmod_info_t g_kernel_kmod_info;
extern "C" void native_legacy_display_static_init(void) __asm__("__GLOBAL__sub_I_NativeLegacyDisplay.cpp");
extern "C" IOService *native_legacy_display_allocate(void);
extern "C" void native_gop_presenter_static_init(void) __asm__("__GLOBAL__sub_I_NativeGOPPresenter.cpp");
extern "C" IOService *native_gop_presenter_create(void);
static IOService *pendingLegacyDisplay;
static IOService *activeGOPPresenter,*activeGOPNub;
// GPU hand-off presenter (sheng-fb-gpu=1, phase 4): lives beside the display driver, which offers it frames instead of copying them.
static IOService *gpuHandoffPresenter;
extern "C" bool native_gop_gpu_offer(IOService *,void *display,IOMemoryDescriptor *,uint64_t stride,uint32_t width,uint32_t height);
extern "C" void native_gop_presenter_set_source(IOService *presenter,IOService *nub);
volatile uint32_t native_legacy_gpu_fallbacks,native_legacy_gpu_last_status;
static IOService *activeLegacyDisplay;
static IOService *activeKeyStore;
extern "C" IOService *native_keyevents_allocate(void);
volatile uint32_t native_keyevents_refused_opens,native_aks_opens,native_aks_calls;
extern "C" void native_keyevents_open_refused(void) {__atomic_fetch_add(&native_keyevents_refused_opens,1,__ATOMIC_RELAXED);}
extern "C" bool native_aks_state_enabled(void) {
    unsigned v=0;return PE_parse_boot_argn("sheng-aks",&v,sizeof(v)) && v==1;
}
extern "C" uint32_t native_aks_lock_state(void) {
    unsigned v=0;return PE_parse_boot_argn("sheng-aks-lock",&v,sizeof(v))?v:0;
}
extern "C" void native_aks_opened(void) {
    unsigned n=__atomic_fetch_add(&native_aks_opens,1,__ATOMIC_RELAXED)+1;
    if(n<=4)dline(100,"AKS OPEN #%u",n);
}
extern "C" void native_aks_call(uint32_t selector,int32_t rc) {
    unsigned n=__atomic_fetch_add(&native_aks_calls,1,__ATOMIC_RELAXED)+1;
    if(n==1 || (rc!=0 && n<=6))dline(100,"AKS S%u %x",selector,(unsigned)rc);
}
// Keep one diagnostic mapping alive until reboot, even after backend stop.
// The pulse is permanently closed before any system-frame copy.
static IOMemoryMap *diagnosticAnchorMapping;
extern "C" int boot_progress_pulse_anchor(uintptr_t,uint64_t,unsigned,unsigned,unsigned) { return 0; }
extern "C" uint32_t native_ufs_dpu_map(unsigned,const uint64_t *,unsigned) { return 0; }
extern "C" volatile uint32_t native_ufs_dpu_state = 0;
static IORegistryEntry *unitScaleChosen;
static bool unitScaleAdded,unitRotationAdded;
static void finishUnitScalePolicy(bool keep) {
    if(unitScaleChosen && !keep) {
        if(unitScaleAdded)unitScaleChosen->removeProperty("display-scale");
        if(unitRotationAdded)unitScaleChosen->removeProperty("display-rotation");
    }
    OSSafeReleaseNULL(unitScaleChosen);unitScaleAdded=false;unitRotationAdded=false;
}
static bool publishOrMatchWord(IORegistryEntry *entry,const char *key,uint32_t value,bool &added) {
    OSObject *previous=entry->getProperty(key);
    if(previous) {
        OSData *data=OSDynamicCast(OSData,previous);uint32_t word=0;
        if(!data || data->getLength()!=sizeof(word))return false;
        memcpy(&word,data->getBytesNoCopy(),sizeof(word));return word==value;
    }
    OSData *data=OSData::withBytes(&value,sizeof(value));
    bool result=data && entry->setProperty(key,data);OSSafeReleaseNULL(data);added=result;return result;
}
extern "C" {
volatile uint32_t native_legacy_stage;
volatile int native_legacy_meta_status=-1;
volatile uint32_t native_legacy_init_result,native_legacy_start_result,native_legacy_probe_match;
volatile uint32_t native_legacy_parent_match,native_legacy_object_size,native_legacy_released;
volatile uint64_t native_legacy_object,native_legacy_nub,native_legacy_workloop;
volatile uint32_t native_legacy_guard_slot;
volatile uint64_t native_legacy_guard_object,native_legacy_guard_caller;
volatile int native_legacy_surface_status=-1;
volatile uint32_t native_legacy_surface_found;
volatile uint64_t native_legacy_video_base,native_legacy_video_bytes;
volatile uint64_t native_legacy_gop_map,native_legacy_gop_virtual,native_legacy_gop_physical,native_legacy_gop_bytes;
volatile uint32_t native_legacy_gop_width,native_legacy_gop_height;
volatile uint32_t native_legacy_registration_calls,native_legacy_properties_length;
volatile uint32_t native_legacy_dt_attached;
// 1 when the display's provider is a real AppleARMIODevice (allocated from the prelinked class),
// 0 when the class was not available and the nub fell back to a plain IOService.
volatile uint32_t native_legacy_armio_device;
volatile int native_legacy_unit_scale_status=-1;
volatile uint32_t native_legacy_unit_scale_calls,native_legacy_chosen_scale,native_legacy_chosen_rotation;
volatile uint32_t native_legacy_pm_stage,native_legacy_pm_calls,native_legacy_pm_callbacks,native_legacy_pm_ordinal;
volatile int native_legacy_pm_status=-1;
volatile uint32_t native_legacy_swap_validation_calls,native_legacy_swap_validation_result,native_legacy_swap_validation_reason;
volatile uint64_t native_legacy_swap_request,native_legacy_swap_surface;
volatile uint32_t native_legacy_transfer_calls;
volatile int native_legacy_transfer_status=-1;
volatile uint64_t native_legacy_transfer_bytes,native_legacy_transfer_request;
volatile uint32_t native_legacy_retirement_calls,native_legacy_retired_generation,native_legacy_presented_generation,native_legacy_retired_frames;
volatile uint64_t native_legacy_retired_current,native_legacy_retired_pending;
char native_legacy_properties[4096];
volatile uint32_t native_legacy_client_calls,native_legacy_client_type;
volatile int native_legacy_client_pid,native_legacy_client_status=-1;
volatile int native_legacy_swap_pid;
volatile uint64_t native_legacy_client_task,native_legacy_client_object;
char native_legacy_client_class[64];
static char native_legacy_client_name[32];
static char native_legacy_swap_name[32];
extern kern_return_t OSKextLoadKextWithIdentifier(const char *);
int boot_progress_prepare_scanout(void) { return 1; }
void boot_progress_cancel_scanout(void) {}
void boot_progress_stage(const char *) {}
volatile uint32_t native_legacy_prepare_calls,native_legacy_prepare_reason,native_legacy_prepare_attempts;
volatile uint32_t native_legacy_prepare_failures,native_legacy_prepare_retried,native_legacy_prepare_max_attempts;
volatile uint64_t native_legacy_prepare_wait_us;
static uint64_t probeNowUs(void) {uint64_t ns;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;}
bool native_legacy_prepare_scanout(void) {
    native_legacy_prepare_calls=native_legacy_prepare_calls+1;native_legacy_prepare_reason=0;native_legacy_prepare_attempts=0;
    dline(29,"DISP PREP %u",native_legacy_prepare_calls);
    if (ml_at_interrupt_context()) {native_legacy_prepare_reason=1;return false;}
    if (!ml_get_interrupts_enabled()) {native_legacy_prepare_reason=2;return false;}
    // Yield to any in-flight diagnostic writer; never spin on a preempted
    // holder or claim a frame that the console can still overwrite.
    const uint64_t began=probeNowUs();
    for (unsigned attempt=0;attempt<100;++attempt) {
        native_legacy_prepare_attempts=attempt+1;
        if (boot_progress_prepare_scanout()) {
            if(attempt) {
                native_legacy_prepare_retried=native_legacy_prepare_retried+1;native_legacy_prepare_wait_us=native_legacy_prepare_wait_us+(probeNowUs()-began);
                if(attempt+1>native_legacy_prepare_max_attempts)native_legacy_prepare_max_attempts=attempt+1;
            }
            return true;
        }
        IOSleep(1);
    }
    native_legacy_prepare_failures=native_legacy_prepare_failures+1;native_legacy_prepare_wait_us=native_legacy_prepare_wait_us+(probeNowUs()-began);
    native_legacy_prepare_reason=3;
    boot_progress_cancel_scanout();
    return false;
}
__attribute__((noinline)) void native_legacy_display_checkpoint(void) {__asm__ volatile("" ::: "memory");}
__attribute__((noinline)) void native_legacy_gop_mapping_checkpoint(void) {__asm__ volatile("" ::: "memory");}
__attribute__((noinline)) void native_legacy_client_checkpoint(void) {__asm__ volatile("" ::: "memory");}
void native_legacy_pm_observed(unsigned stage,IOReturn status,unsigned long ordinal) {
    native_legacy_pm_stage=stage;native_legacy_pm_status=status;native_legacy_pm_ordinal=(uint32_t)ordinal;
    native_legacy_pm_calls=native_legacy_pm_calls+1;
    if(stage==3)native_legacy_pm_callbacks=native_legacy_pm_callbacks+1;
    dline(29,"DISP PM %u %x",stage,(unsigned)status);
}
__attribute__((noinline)) void native_legacy_swap_validation_checkpoint(bool,unsigned,const void *,const void *) {__asm__ volatile("" ::: "memory");}
// Last rejected swap request, copied from the original request layout the validator reads (offsets
// qualified in NativeLegacyDisplay::validateSwap). Observation only; nothing here changes the verdict.
volatile uint32_t native_legacy_swap_accepted_count,native_legacy_swap_rejected_count,native_legacy_rej_reason;
volatile uint64_t native_legacy_rej_surfaces[4];
volatile uint32_t native_legacy_rej_state[3],native_legacy_rej_options[3],native_legacy_rej_size[2],native_legacy_rej_rects[2][4];
char native_legacy_rej_class[32];
volatile uint64_t native_legacy_rej_surface[7];   // width, height, format, planes, stride, allocation, descriptor bytes of the last rejected surface
void native_legacy_surface_rejected(uint64_t width,uint64_t height,uint32_t format,uint32_t planes,uint64_t stride,uint64_t allocation,uint64_t bytes) {
    native_legacy_rej_surface[0]=width;native_legacy_rej_surface[1]=height;native_legacy_rej_surface[2]=format;native_legacy_rej_surface[3]=planes;
    native_legacy_rej_surface[4]=stride;native_legacy_rej_surface[5]=allocation;native_legacy_rej_surface[6]=bytes;
    dline(30,"DISP REJ %llux%llu %x",(unsigned long long)width,(unsigned long long)height,format);
}
static void recordRejectedSwap(unsigned reason,const void *request,const void *surface) {
    const unsigned char *bytes=reinterpret_cast<const unsigned char *>(request);
    if(!bytes)return;
    native_legacy_rej_reason=reason;
    uint64_t surfaces[4]={};memcpy(surfaces,bytes+2624,sizeof(surfaces));
    for(unsigned i=0;i<4;++i)native_legacy_rej_surfaces[i]=surfaces[i];
    const unsigned stateOffsets[3]={792,796,800},optionOffsets[3]={804,808,812};
    for(unsigned i=0;i<3;++i){uint32_t a=0,b=0;memcpy(&a,bytes+stateOffsets[i],4);memcpy(&b,bytes+optionOffsets[i],4);
        native_legacy_rej_state[i]=a;native_legacy_rej_options[i]=b;}
    uint32_t w=0,h=0;memcpy(&w,bytes+816,4);memcpy(&h,bytes+820,4);native_legacy_rej_size[0]=w;native_legacy_rej_size[1]=h;
    const unsigned rectOffsets[2]={17,81};
    for(unsigned r=0;r<2;++r){uint32_t rect[4]={};memcpy(rect,bytes+rectOffsets[r],sizeof(rect));
        for(unsigned i=0;i<4;++i)native_legacy_rej_rects[r][i]=rect[i];}
    char name[32]={};
    if(surface){const OSObject *object=reinterpret_cast<const OSObject *>(surface);
        const OSMetaClass *meta=object->getMetaClass();const char *cls=meta?meta->getClassName():nullptr;
        if(cls)strlcpy(name,cls,sizeof(name));}
    memcpy(native_legacy_rej_class,name,sizeof(name));
}
// Content statistics of presented frames (copy = kind 0, blank fill = kind 1): the first 32 are kept, with the time
// in seconds since the first frame. A per-frame record shows the animation cadence and when the OS blanks the panel.
#define NATIVE_LEGACY_FRAMES 32
volatile uint32_t native_legacy_frame_count;
volatile uint32_t native_legacy_frame_kind[NATIVE_LEGACY_FRAMES];
volatile uint32_t native_legacy_frame_time[NATIVE_LEGACY_FRAMES];
volatile uint64_t native_legacy_frame_stats[NATIVE_LEGACY_FRAMES][5];
static uint64_t frameEpoch;
void native_legacy_frame_observed(unsigned kind,uint64_t count,uint64_t minx,uint64_t miny,uint64_t maxx,uint64_t maxy) {
    uint32_t n=native_legacy_frame_count;
    uint64_t now,frequency;
    __asm__ volatile("isb\n\tmrs %0, cntpct_el0":"=r"(now));__asm__ volatile("mrs %0, cntfrq_el0":"=r"(frequency));
    if(!frameEpoch)frameEpoch=now;
    if(n<NATIVE_LEGACY_FRAMES) {
        native_legacy_frame_kind[n]=kind;
        native_legacy_frame_time[n]=frequency?(uint32_t)((now-frameEpoch)/frequency):0;
        native_legacy_frame_stats[n][0]=count;native_legacy_frame_stats[n][1]=minx;native_legacy_frame_stats[n][2]=miny;
        native_legacy_frame_stats[n][3]=maxx;native_legacy_frame_stats[n][4]=maxy;
    }
    native_legacy_frame_count=n+1;
    dline(31,"DISP FRAME %u K%u",n+1,kind);
    if(n<NATIVE_LEGACY_FRAMES)
        printf("sheng-fb: frame %u kind %u: %llu lit pixels in (%llu,%llu)-(%llu,%llu)\n",n,kind,count,minx,miny,maxx,maxy);
}
volatile uint32_t native_legacy_frame_colors[8][4];
volatile uint64_t native_legacy_frame_color_counts[8][4],native_legacy_frame_color_other[8];
// Called right after the matching native_legacy_frame_observed(); fills slot count-1 (or the blank slot).
void native_legacy_frame_colors_observed(const uint32_t *colors,const uint64_t *counts,uint64_t other) {
    uint32_t n=native_legacy_frame_count;
    if(!n || n>8)return;   // dominant colours are kept for the first eight frames only
    n-=1;
    for(unsigned i=0;i<4;++i){native_legacy_frame_colors[n][i]=colors[i];native_legacy_frame_color_counts[n][i]=counts[i];}
    native_legacy_frame_color_other[n]=other;
}
// The life of every accepted swap, from the kernel's side: validated (the client submitted it), offered to the GPU copy (or started on the CPU),
// and retired. Times are microseconds since boot; the last 64 are kept.
struct SwapEvent {uint64_t submitted,offered,done;uint32_t number;};
static SwapEvent swapEvents[64];static uint32_t swapEventCount;
extern "C" void native_legacy_swap_stage(unsigned stage) {
    const uint64_t now=probeNowUs();
    uint32_t total=swapEventCount;
    if(stage==0) {
        SwapEvent &e=swapEvents[total%64];e={now,0,0,total+1};swapEventCount=total+1;return;
    }
    uint32_t first=total>64?total-64:0;
    for(uint32_t i=first;i<total;++i) {
        SwapEvent &e=swapEvents[i%64];
        if(stage==1 && !e.offered && !e.done){e.offered=now;return;}
        if(stage==2 && !e.done){e.done=now;if(!e.offered)e.offered=now;return;}
    }
}
extern "C" unsigned native_legacy_swap_timeline(char *out,unsigned cap) {
    unsigned at=0;out[0]=0;
    uint32_t total=swapEventCount,first=total>40?total-40:0;
    for(uint32_t i=first;i<total && at+90<cap;++i) {
        const SwapEvent &e=swapEvents[i%64];
        uint64_t previous=i>first?swapEvents[(i-1)%64].submitted:e.submitted;
        at+=snprintf(out+at,cap-at,"W%u t=%llu.%03llus +%llums offer=%lldms done=%lldms\n",e.number,(unsigned long long)(e.submitted/1000000),
            (unsigned long long)((e.submitted/1000)%1000),(unsigned long long)((e.submitted-previous)/1000),
            e.offered?(long long)((e.offered-e.submitted)/1000):-1LL,e.done?(long long)((e.done-e.submitted)/1000):-1LL);
    }
    return at;
}
// The display refresh tick (NativeLegacyDisplay, sheng-vsync): vsync messages sent, arms, idle stops, late ticks, starts, refresh
// properties reported at that rate, and the rate.
//
// Whether a stopped tick is stuck inside the original sender or simply has no client left cannot be told from the counters alone:
// both leave "sent" frozen. native_legacy_vsync_notify below is the original function under the alias the build script binds, with a
// flag held for exactly the time it runs, and a thread call of its own prints line 34 every two seconds. That thread is independent
// of the family work loop, so it still reports when the work loop is the thing that stopped.
extern "C" void native_legacy_vsync_notify_original(void *display,uint64_t timestamp,uint64_t continuousTime,uint64_t interval);
static volatile uint32_t vsyncInNotify;
extern "C" void native_legacy_vsync_notify(void *display,uint64_t timestamp,uint64_t continuousTime,uint64_t interval) {
    vsyncInNotify=1;
    native_legacy_vsync_notify_original(display,timestamp,continuousTime,interval);
    vsyncInNotify=0;
}
static volatile uint64_t vsyncEvents[6];
static volatile unsigned vsyncHz;
static thread_call_t vsyncWatch;
static void vsyncWatchFn(thread_call_param_t,thread_call_param_t) {
    dline(32,"VS s%llu a%llu i%llu l%llu n%u",
        (unsigned long long)vsyncEvents[1],(unsigned long long)vsyncEvents[0],
        (unsigned long long)vsyncEvents[2],(unsigned long long)vsyncEvents[3],(unsigned)vsyncInNotify);
    uint64_t deadline;clock_interval_to_deadline(2000,kMillisecondScale,&deadline);
    thread_call_enter_delayed(vsyncWatch,deadline);
}
void native_legacy_vsync_observed(unsigned event,unsigned hz) {
    if(event<6)vsyncEvents[event]=vsyncEvents[event]+1;
    vsyncHz=hz;
    dline(31,"DISP VSYNC %u %u",event,hz);
    if(event==4 && !vsyncWatch) {
        vsyncWatch=thread_call_allocate(vsyncWatchFn,nullptr);
        if(vsyncWatch)vsyncWatchFn(nullptr,nullptr);
    }
}
extern "C" unsigned native_legacy_prepare_report(char *out,unsigned cap) {
    return snprintf(out,cap,"prepare calls=%u failures=%u retried=%u max_attempts=%u waited_ms=%llu vsync hz=%u sent=%llu arms=%llu idle=%llu late=%llu start=%llu refresh=%llu",
        native_legacy_prepare_calls,native_legacy_prepare_failures,native_legacy_prepare_retried,native_legacy_prepare_max_attempts,
        (unsigned long long)(native_legacy_prepare_wait_us/1000),vsyncHz,(unsigned long long)vsyncEvents[1],(unsigned long long)vsyncEvents[0],
        (unsigned long long)vsyncEvents[2],(unsigned long long)vsyncEvents[3],(unsigned long long)vsyncEvents[4],(unsigned long long)vsyncEvents[5]);
}
void native_legacy_swap_validation_observed(bool accepted,unsigned reason,const void *request,const void *surface) {
    if(accepted) {
        native_legacy_swap_stage(0);
        native_legacy_swap_accepted_count=native_legacy_swap_accepted_count+1;
        task_t stask=current_task();
        int spid=stask?proc_pid((proc_t)get_bsdtask_info(stask)):0;
        if(spid>0) {
            native_legacy_swap_pid=spid;
            native_legacy_swap_name[0]=0;
            proc_name(spid,native_legacy_swap_name,sizeof(native_legacy_swap_name));
        }
    }
    else {native_legacy_swap_rejected_count=native_legacy_swap_rejected_count+1;recordRejectedSwap(reason,request,surface);}
    native_legacy_swap_validation_calls=native_legacy_swap_validation_calls+1;
    native_legacy_swap_validation_result=accepted;native_legacy_swap_validation_reason=reason;
    dline(26,"DISP VAL %d R%u",accepted,reason);
    native_legacy_swap_request=(uintptr_t)request;native_legacy_swap_surface=(uintptr_t)surface;
    native_legacy_swap_validation_checkpoint(accepted,reason,request,surface);
}
// Time of the pixel copy into the scanout buffer (counter ticks): swaps completed, last/maximum/sum, for the banner.
volatile uint64_t native_legacy_copy_n,native_legacy_copy_last,native_legacy_copy_max,native_legacy_copy_sum,native_legacy_copy_bytes_last,native_legacy_copy_bytes_sum;
void native_legacy_copy_timing_observed(uint64_t ticks,uint64_t bytes) {
    native_legacy_copy_bytes_last=bytes;native_legacy_copy_bytes_sum=native_legacy_copy_bytes_sum+bytes;
    native_legacy_copy_n=native_legacy_copy_n+1;native_legacy_copy_last=ticks;native_legacy_copy_sum=native_legacy_copy_sum+ticks;
    if(ticks>native_legacy_copy_max)native_legacy_copy_max=ticks;
    if(native_legacy_copy_n<=32) {
        uint64_t frequency;__asm__ volatile("mrs %0, cntfrq_el0":"=r"(frequency));
        printf("sheng-fb: copy %u: %llu bytes in %llu us\n",(unsigned)native_legacy_copy_n,bytes,frequency?ticks*1000000/frequency:0);
    }
}
__attribute__((noinline)) void native_legacy_transfer_observed(IOReturn status,const void *request,uint64_t bytes) {
    native_legacy_transfer_calls=native_legacy_transfer_calls+1;
    if(status==kIOReturnSuccess)native_legacy_swap_stage(2);
    native_legacy_transfer_status=status;native_legacy_transfer_request=(uintptr_t)request;native_legacy_transfer_bytes=bytes;
    dline(30,"DISP COPY %x %llu",(unsigned)status,(unsigned long long)bytes);
    if(status!=kIOReturnSuccess) {
        char message[120];snprintf(message,sizeof(message),"DISPLAY COPY FAILED status=%08x prepare=%u tries=%u",
          (unsigned)status,native_legacy_prepare_reason,native_legacy_prepare_attempts);
        boot_progress_stage(message);
    }
}
bool native_legacy_gpu_offer(void *display,IOMemoryDescriptor *memory,uint64_t stride,uint32_t width,uint32_t height,uint32_t format) {
    (void)format;
    return gpuHandoffPresenter && native_gop_gpu_offer(gpuHandoffPresenter,display,memory,stride,width,height);
}
void native_legacy_gpu_fallback_observed(int status) {
    native_legacy_gpu_last_status=(uint32_t)status;native_legacy_gpu_fallbacks=native_legacy_gpu_fallbacks+1;
}
__attribute__((noinline)) void native_legacy_retirement_observed(IOService *display) {
    const unsigned char *bytes=reinterpret_cast<const unsigned char *>(display);
    uint64_t current=0,pending=0;uint32_t completed=0,presented=0,frames=0;
    memcpy(&current,bytes+5472,8);memcpy(&pending,bytes+5480,8);
    memcpy(&completed,bytes+5820,4);memcpy(&presented,bytes+5824,4);memcpy(&frames,bytes+252,4);
    native_legacy_retired_current=current;native_legacy_retired_pending=pending;
    native_legacy_retired_generation=completed;native_legacy_presented_generation=presented;native_legacy_retired_frames=frames;
    native_legacy_retirement_calls=native_legacy_retirement_calls+1;
    dline(30,"DISP RET %u/%u F%u S%d:%.6s C%u/%d:%.6s",completed,presented,frames,
        native_legacy_swap_pid,native_legacy_swap_name[0]?native_legacy_swap_name:"-",
        native_legacy_client_calls,native_legacy_client_pid,native_legacy_client_name[0]?native_legacy_client_name:"-");
    if(native_diag_process_summary)native_diag_process_summary();
    if(native_gop_redraw_ring)native_gop_redraw_ring();
}
// The display object's properties as they are now (the registration copy above predates setDisplayRefreshProperties and the rest): for the
// UFS log. Returns the bytes written (NUL-terminated).
extern "C" unsigned native_legacy_properties_now(char *out,unsigned cap) {
    if(!out || !cap)return 0;
    out[0]=0;
    IOService *display=reinterpret_cast<IOService *>(native_legacy_object);
    if(!display)return 0;
    OSSerialize *xml=OSSerialize::withCapacity(8192);
    unsigned size=0;
    if(xml && display->serializeProperties(xml)) {
        size=xml->getLength();if(size>=cap)size=cap-1;
        memcpy(out,xml->text(),size);out[size]=0;
    }
    OSSafeReleaseNULL(xml);
    return size;
}
void native_legacy_registration_observed(IOService *display) {
    native_legacy_registration_calls=native_legacy_registration_calls+1;
    dline(27,"DISP REG %u",native_legacy_registration_calls);
    OSSerialize *xml=OSSerialize::withCapacity(2048);
    if(xml && display->serializeProperties(xml)) {
        unsigned size=xml->getLength();if(size>=sizeof(native_legacy_properties))size=sizeof(native_legacy_properties)-1;
        memcpy(native_legacy_properties,xml->text(),size);native_legacy_properties[size]=0;native_legacy_properties_length=size;
    }
    OSSafeReleaseNULL(xml);
}
void native_legacy_client_enter(task_t task,uint32_t type) {
    native_legacy_client_calls=native_legacy_client_calls+1;native_legacy_client_task=(uintptr_t)task;
    native_legacy_client_type=type;native_legacy_client_pid=task?proc_pid((proc_t)get_bsdtask_info(task)):0;
    native_legacy_client_name[0]=0;
    if(native_legacy_client_pid)proc_name(native_legacy_client_pid,native_legacy_client_name,sizeof(native_legacy_client_name));
    dline(28,"DISP CLIENT %d %s T%u",native_legacy_client_pid,native_legacy_client_name,type);
    dline(100,"CL#%u %d %.12s T%u",native_legacy_client_calls,native_legacy_client_pid,
        native_legacy_client_name[0]?native_legacy_client_name:"-",type);
}
// Every framebuffer user client seen (object, pid), with what it does: the original client's vtable is copied and two entries (externalMethod, slot 168,
// and registerNotificationPort(port,type,refCon), slot 175) are pointed at wrappers that count and forward to the original function. The original code
// is not touched; only this object's vptr names the copy. XNU's ARM64 vptr points at slot 0 (the original code dispatches with ldr x8,[vptr,#slot*8]).
enum {kSeenClients=8,kSelectors=96,kRegEvents=8,kVtableSlots=256};
struct ClientStat {
    uintptr_t object;int pid;uint32_t calls,fails;uint32_t selector_calls[kSelectors],selector_fails[kSelectors],selector_status[kSelectors];
    unsigned regs;struct {uint32_t type;uint64_t port;uint32_t ref,status;} reg[kRegEvents];
    uint64_t sel6_first[3],sel6_last[3];uint32_t sel6_distinct;uint64_t sel6_seen[8][3];uint32_t sel6_calls[8],sel6_fails[8],sel6_status[8];
    uint32_t seq_count;struct {uint32_t a,b,c,status,done,presented;} seq[24];
    uintptr_t original_external,original_register;
};
static ClientStat seenClients[kSeenClients];static unsigned seenClientCount;
typedef IOReturn (*ExternalFn)(IOUserClient *,uint32_t,IOExternalMethodArguments *,IOExternalMethodDispatch *,OSObject *,void *);
typedef IOReturn (*RegisterFn)(IOUserClient *,mach_port_t,UInt32,UInt32);
static ClientStat *statFor(IOUserClient *client) {
    for(unsigned i=0;i<seenClientCount;++i)if(seenClients[i].object==(uintptr_t)client)return &seenClients[i];
    return nullptr;
}
static IOReturn wrappedExternal(IOUserClient *client,uint32_t selector,IOExternalMethodArguments *args,IOExternalMethodDispatch *dispatch,OSObject *target,void *ref) {
    ClientStat *stat=statFor(client);
    IOReturn status=stat?reinterpret_cast<ExternalFn>(stat->original_external)(client,selector,args,dispatch,target,ref):kIOReturnNotAttached;
    if(stat) {
        if(selector==6 && args && args->scalarInput && args->scalarInputCount>=3) {
            const uint64_t *in=args->scalarInput;
            if(!stat->sel6_distinct && !stat->sel6_first[0] && !stat->sel6_first[1])memcpy(stat->sel6_first,in,sizeof stat->sel6_first);
            memcpy(stat->sel6_last,in,sizeof stat->sel6_last);
            int at=-1;
            for(unsigned k=0;k<stat->sel6_distinct && k<8;++k)if(!memcmp(stat->sel6_seen[k],in,24))at=(int)k;
            if(at<0 && stat->sel6_distinct<8){at=(int)stat->sel6_distinct++;memcpy(stat->sel6_seen[at],in,24);}
            if(stat->seq_count<24){
                auto &e=stat->seq[stat->seq_count++];e.a=(uint32_t)in[0];e.b=(uint32_t)in[1];e.c=(uint32_t)in[2];e.status=(uint32_t)status;
                const unsigned char *object=reinterpret_cast<const unsigned char *>(native_legacy_object);
                e.done=e.presented=0;if(object){memcpy(&e.done,object+5820,4);memcpy(&e.presented,object+5824,4);}
            }
            if(at>=0){++stat->sel6_calls[at];if(status)++stat->sel6_fails[at];stat->sel6_status[at]=(uint32_t)status;}
        }
        ++stat->calls;if(status)++stat->fails;
        if(selector<kSelectors){++stat->selector_calls[selector];if(status)++stat->selector_fails[selector];stat->selector_status[selector]=(uint32_t)status;}
    }
    return status;
}
static IOReturn wrappedRegister(IOUserClient *client,mach_port_t port,UInt32 type,UInt32 refCon) {
    ClientStat *stat=statFor(client);
    IOReturn status=stat?reinterpret_cast<RegisterFn>(stat->original_register)(client,port,type,refCon):kIOReturnNotAttached;
    if(stat && stat->regs<kRegEvents){auto &e=stat->reg[stat->regs++];e.type=type;e.port=(uint64_t)(uintptr_t)port;e.ref=refCon;e.status=(uint32_t)status;}
    return status;
}
static void interpose(IOUserClient *client,int pid) {
#if __has_feature(ptrauth_calls)
    // The vtable pointer and its entries are signed: a copied vtable would not authenticate. Clients are not interposed.
    (void)client;(void)pid;return;
#endif
    if(seenClientCount>=kSeenClients)return;
    ClientStat &stat=seenClients[seenClientCount];
    memset(&stat,0,sizeof stat);
    uintptr_t *vtable=*reinterpret_cast<uintptr_t **>(client);
    uintptr_t *copy=static_cast<uintptr_t *>(IOMalloc(kVtableSlots*sizeof(uintptr_t)));
    if(!copy)return;
    memcpy(copy,vtable,kVtableSlots*sizeof(uintptr_t));
    stat.object=(uintptr_t)client;stat.pid=pid;stat.original_external=copy[168];stat.original_register=copy[175];
    copy[168]=(uintptr_t)&wrappedExternal;copy[175]=(uintptr_t)&wrappedRegister;
    ++seenClientCount;
    *reinterpret_cast<uintptr_t **>(client)=copy;
}
// The display object's state words the original wait selector tests (offsets 5832..5864, as dwords), for the log.
extern "C" unsigned native_legacy_state_words(char *out,unsigned cap) {
    out[0]=0;
    const unsigned char *object=reinterpret_cast<const unsigned char *>(native_legacy_object);
    if(!object)return 0;
    unsigned at=snprintf(out,cap,"d+5808:");
    for(unsigned o=5808;o<5872 && at+16<cap;o+=4){uint32_t v;memcpy(&v,object+o,4);at+=snprintf(out+at,cap-at," %x",v);}
    return at;
}
extern "C" unsigned native_legacy_clients_dump(char *out,unsigned cap) {
    unsigned at=0;out[0]=0;
    for(unsigned c=0;c<seenClientCount && at+120<cap;++c) {
        const ClientStat &s=seenClients[c];
        at+=snprintf(out+at,cap-at,"C%u pid=%d calls=%u fails=%u regs=%u",c,s.pid,s.calls,s.fails,s.regs);
        for(unsigned r=0;r<s.regs && r<kRegEvents && at+60<cap;++r)
            at+=snprintf(out+at,cap-at," reg(t%u,p%llx,r%x)=%x",s.reg[r].type,(unsigned long long)s.reg[r].port,s.reg[r].ref,s.reg[r].status);
        at+=snprintf(out+at,cap-at,"\n");
        for(unsigned k=0;k<s.sel6_distinct && k<8 && at+100<cap;++k)
            at+=snprintf(out+at,cap-at,"  sel6 args %llx %llx %llx: %u calls %u fails last %x\n",(unsigned long long)s.sel6_seen[k][0],(unsigned long long)s.sel6_seen[k][1],(unsigned long long)s.sel6_seen[k][2],s.sel6_calls[k],s.sel6_fails[k],s.sel6_status[k]);
        for(unsigned k=0;k<s.seq_count && at+90<cap;++k)
            at+=snprintf(out+at,cap-at,"  s6#%u %x %x %x -> %x [5820]=%x [5824]=%x\n",k,s.seq[k].a,s.seq[k].b,s.seq[k].c,s.seq[k].status,s.seq[k].done,s.seq[k].presented);
        unsigned n=0;
        for(unsigned sel=0;sel<kSelectors && at+40<cap;++sel) {
            if(!s.selector_calls[sel])continue;
            if(!n)at+=snprintf(out+at,cap-at,"  sel");
            at+=snprintf(out+at,cap-at," %u:%u/%u/%x",sel,s.selector_calls[sel],s.selector_fails[sel],s.selector_status[sel]);
            if(++n%6==0)at+=snprintf(out+at,cap-at,"\n  sel");
        }
        if(n)at+=snprintf(out+at,cap-at,"\n");
    }
    return at;
}
void native_legacy_client_observed(IOReturn status,IOUserClient *client) {
    if(client && !status)interpose(client,native_legacy_client_pid);
    native_legacy_client_status=status;native_legacy_client_object=(uintptr_t)client;
    if(client)strlcpy(native_legacy_client_class,client->getMetaClass()->getClassName(),sizeof(native_legacy_client_class));
    dline(28,"DISP CLIENT %d %s %x",native_legacy_client_pid,native_legacy_client_name,(unsigned)status);
    dline(100,"CL %d %.16s %x",native_legacy_client_pid,native_legacy_client_name,(unsigned)status);
    native_legacy_client_checkpoint();
}
__attribute__((noinline)) bool native_legacy_gop_mapping_observed(IOService *object,IOMemoryMap *mapping,uint32_t width,uint32_t height) {
    if((uintptr_t)object!=native_legacy_object)panic("NativeLegacyDisplay: wrong mapping owner");
    native_legacy_gop_map=(uintptr_t)mapping;native_legacy_gop_virtual=mapping->getVirtualAddress();
    native_legacy_gop_physical=mapping->getPhysicalAddress();native_legacy_gop_bytes=mapping->getLength();
    native_legacy_gop_width=width;native_legacy_gop_height=height;
    dline(24,"DISP MAP %ux%u %llx",width,height,(unsigned long long)mapping->getPhysicalAddress());
    if(!diagnosticAnchorMapping) {
        PE_Video anchorVideo={};
        if(!PE_current_console(&anchorVideo) && anchorVideo.v_depth==32 &&
           anchorVideo.v_rowBytes<=65536 &&
           anchorVideo.v_width==width && anchorVideo.v_height==height &&
           anchorVideo.v_baseAddr==mapping->getPhysicalAddress() && !anchorVideo.v_rotate) {
            mapping->retain();
            if(boot_progress_pulse_anchor(mapping->getVirtualAddress(),mapping->getLength(),width,height,
                (unsigned)anchorVideo.v_rowBytes))diagnosticAnchorMapping=mapping;
            else mapping->release();
        }
    }
    native_legacy_gop_mapping_checkpoint();
    unsigned enabled=0;PE_parse_boot_argn("offline-legacy-unit-scale",&enabled,sizeof(enabled));
    if(!enabled)return true;
    native_legacy_unit_scale_calls=native_legacy_unit_scale_calls+1;native_legacy_unit_scale_status=1;
    unsigned phase=0;PE_parse_boot_argn("offline-legacy-phase",&phase,sizeof(phase));
    PE_Video video={};
    // The value is the UI scale: logical points = actual scanout pixels / scale (1..3). It changes
    // only the published display-scale; the scanout copy still moves one actual pixel per pixel.
    if(enabled<1 || enabled>3 || phase!=4 || unitScaleChosen || PE_current_console(&video) ||
       video.v_width!=width || video.v_height!=height || video.v_rotate ||
       mapping->getPhysicalAddress()!=video.v_baseAddr)return false;
    // Explicit offline UI policy: `enabled` actual GOP scanout pixels per logical
    // point in each direction (1 = native pixels). This is not physical pixel density,
    // panel timing, an Apple device identity or a synthesized mode list.
    unitScaleChosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
    if(!unitScaleChosen || !publishOrMatchWord(unitScaleChosen,"display-scale",enabled,unitScaleAdded) ||
       !publishOrMatchWord(unitScaleChosen,"display-rotation",video.v_rotate,unitRotationAdded)) {
        finishUnitScalePolicy(false);return false;
    }
    OSData *scale=OSDynamicCast(OSData,unitScaleChosen->getProperty("display-scale"));
    OSData *rotation=OSDynamicCast(OSData,unitScaleChosen->getProperty("display-rotation"));
    uint32_t actualScale=0,actualRotation=0;
    if(!scale || !rotation || scale->getLength()!=4 || rotation->getLength()!=4) {
        finishUnitScalePolicy(false);return false;
    }
    memcpy(&actualScale,scale->getBytesNoCopy(),4);memcpy(&actualRotation,rotation->getBytesNoCopy(),4);
    native_legacy_chosen_scale=actualScale;native_legacy_chosen_rotation=actualRotation;
    native_legacy_unit_scale_status=actualScale==enabled && actualRotation==video.v_rotate?0:2;
    if(native_legacy_unit_scale_status){finishUnitScalePolicy(false);return false;}
    return true;
}
__attribute__((noreturn,noinline)) void native_legacy_backend_guard(unsigned slot,void *object,void *caller) {
    native_legacy_guard_slot=slot;native_legacy_guard_object=(uintptr_t)object;native_legacy_guard_caller=(uintptr_t)caller;
    panic("NativeLegacyDisplay: leaf=%u caller=%llx stage=%u PM=%u swap=%u/%u copy=%08x bytes=%llu retire=%u handoff=%u/%u",
      slot,(uint64_t)(uintptr_t)caller,native_legacy_stage,native_legacy_pm_stage,
      native_legacy_swap_validation_result,native_legacy_swap_validation_reason,
      (unsigned)native_legacy_transfer_status,native_legacy_transfer_bytes,native_legacy_retirement_calls,
      native_legacy_prepare_reason,native_legacy_prepare_attempts);
}
void native_legacy_display_probe(void);
void native_legacy_display_probe(void) {
    // The registration path and optional original-userclient inspection both
    // reach this boot-thread entry. Run class constructors only once; a second
    // postModLoad would attempt to register the same metaclasses again.
    if(native_legacy_stage)return;
    native_legacy_stage=1;dline(21,"DISP PROBE 1");void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);
    if(!handle){native_legacy_display_checkpoint();return;}
    native_legacy_display_static_init();native_gop_presenter_static_init();native_legacy_meta_status=OSMetaClass::postModLoad(handle);
    if(native_legacy_meta_status){native_legacy_display_checkpoint();return;}
    PE_Video video={};native_legacy_stage=2;dline(21,"DISP PROBE 2");
    if(PE_current_console(&video) || !video.v_baseAddr || !video.v_width || !video.v_height || video.v_depth!=32
        || video.v_rotate || video.v_width>16384 || video.v_height>16384 || video.v_rowBytes<video.v_width*4
        || video.v_rowBytes>UINT64_MAX/video.v_height){dline(22,"DISP NOGOP");native_legacy_display_checkpoint();return;}
    IOService *display=native_legacy_display_allocate();native_legacy_object=(uintptr_t)display;
    if(!display){native_legacy_display_checkpoint();return;}
    native_legacy_object_size=display->getMetaClass()->getClassSize();
    native_legacy_parent_match=(display->metaCast("IOMobileFramebufferLegacy") && display->metaCast("IOMobileFramebuffer") && display->metaCast("IOService"));
    native_legacy_stage=3;native_legacy_init_result=display->init();dline(22,"DISP INIT %d",native_legacy_init_result);
    unsigned phase=1;PE_parse_boot_argn("offline-legacy-phase",&phase,sizeof(phase));
    if(native_legacy_init_result && (phase==2 || phase==3 || phase==4)) {
        // IOSurface matches the real IOBSD resource, published by bsd_autoconf.
        // Retain this actual object until that normal lifecycle boundary.
        pendingLegacyDisplay=display;return;
    }
    native_legacy_stage=7;gopPaint("EARLY", true);display->release();native_legacy_released=1;native_legacy_stage=8;
    native_legacy_display_checkpoint();
}
void native_legacy_display_late_probe(void);
void native_legacy_display_late_probe(void) {
    IOService *display=pendingLegacyDisplay;if(!display)return;pendingLegacyDisplay=nullptr;dline(21,"DISP LATE");
    native_legacy_surface_status=OSKextLoadKextWithIdentifier("com.apple.iokit.IOSurface");
    OSDictionary *matching=IOService::nameMatching("IOCoreSurfaceRoot");
    IOService *surface=matching?IOService::waitForMatchingService(matching,5000000000ULL):nullptr;
    native_legacy_surface_found=surface!=nullptr;dline(23,"DISP SURF %d F%d",native_legacy_surface_status,native_legacy_surface_found);OSSafeReleaseNULL(surface);OSSafeReleaseNULL(matching);
    PE_Video video={};
    if(!native_legacy_surface_status && native_legacy_surface_found && !PE_current_console(&video)
        && video.v_baseAddr && video.v_width && video.v_height && video.v_depth==32
        && !video.v_rotate && video.v_width<=16384 && video.v_height<=16384
        && video.v_rowBytes>=video.v_width*4 && video.v_rowBytes<=UINT64_MAX/video.v_height) {
        native_legacy_video_base=video.v_baseAddr;native_legacy_video_bytes=video.v_rowBytes*video.v_height;dline(23,"DISP VID %llx %ux%u",(unsigned long long)video.v_baseAddr,(unsigned)video.v_width,(unsigned)video.v_height);
        {unsigned phase=0;PE_parse_boot_argn("offline-legacy-phase",&phase,sizeof(phase));
         slog("late surface=%d found=%d phase=%u video=%llx %ux%u",native_legacy_surface_status,native_legacy_surface_found,phase,
              (unsigned long long)video.v_baseAddr,(unsigned)video.v_width,(unsigned)video.v_height);}
        // A real firmware framebuffer resource, with addresses and extent
        // obtained from the actual GOP handoff. It has no invented compatible,
        // display list, panel timing, GPU/fence completion or vendor registers.
        //
        // The provider must be an AppleARMIODevice, and that is not a guess: the original
        // IOMobileFramebufferLegacy::start() bound by build-gop-legacy-display.py walks the provider's
        // metaclass chain against AppleARMIODevice's own metaclass (the cast helper at
        // 0xfffffff0078cfd50 compares the metaclass, then follows superclass) and its failure branch
        // logs "%s: provider was not AppleARMIODevice." against __PRETTY_FUNCTION__. A plain IOService
        // can never pass that test. The class itself is present in this kernel - the original display
        // kext references its metaclass - so allocate the genuine class rather than a stand-in: a local
        // class that merely shares the name fails the pointer comparison.
        native_legacy_stage=4;OSObject *allocated=OSMetaClass::allocClassWithName("AppleARMIODevice");
        IOService *nub=OSDynamicCast(IOService,allocated);
        native_legacy_armio_device=nub?1:0;dline(27,"DISP ARMIO %u",native_legacy_armio_device);
        slog("armio=%u class=%s",native_legacy_armio_device,
             nub?nub->getMetaClass()->getClassName():"none");
        if(!nub){if(allocated)allocated->release();nub=new IOService;}
        native_legacy_nub=(uintptr_t)nub;bool nubAttached=false,nubStarted=false,attached=false,started=false;
        IORegistryEntry *displayTreeParent=nullptr;bool dtAttached=false;
        IOService *parent=IOService::getResourceService();
        // An IOPlatformDevice (which AppleARMIODevice is) expects the platform expert as its provider;
        // the resource service is the fallback for the plain-service case.
        IOService *nubParent=parent;
        if(native_legacy_armio_device) {
            IOService *platform=OSDynamicCast(IOService,static_cast<IOService *>(IOService::getPlatform()));
            if(platform)nubParent=platform;
        }
        if(!activeKeyStore && parent) {
            IOService *aks=native_keyevents_allocate();
            if(aks && aks->init() && aks->attach(parent) && aks->start(parent)) {
                activeKeyStore=aks;dline(100,"AKS UP");
            } else if(aks) {
                aks->release();
            }
        }
        IODeviceMemory *memory=IODeviceMemory::withRange(video.v_baseAddr,video.v_rowBytes*video.v_height);
        OSArray *ranges=OSArray::withCapacity(1);
        if(nub && memory && ranges && nubParent && nub->init()) {
            ranges->setObject(memory);nub->setDeviceMemory(ranges);nub->setName("NativeGOPFramebuffer");
            nubAttached=nub->attach(nubParent);if(nubAttached)nubStarted=nub->start(nubParent);dline(25,"DISP NUB A%d S%d",nubAttached,nubStarted);
            slog("nub parent=%s attach=%d start=%d",nubParent->getMetaClass()->getClassName(),nubAttached,nubStarted);
            // The attach is what the display needs from its provider; the provider's own start can
            // still refuse (an IOPlatformDevice wants device-tree properties this nub does not
            // carry), and that must not hide the display attempt behind a silent exit.
            if(nubAttached) {
                unsigned phase=0;PE_parse_boot_argn("offline-legacy-phase",&phase,sizeof(phase));
                if(phase==4) {
                    // This is the actually implemented GOP display subsystem,
                    // attached to its real firmware framebuffer memory resource.
                    // No Apple/vendor compatible or panel timing is supplied.
                    displayTreeParent=IORegistryEntry::fromPath("/arm-io",gIODTPlane);
                    const char type[]="display-subsystem";OSData *data=OSData::withBytes(type,sizeof(type));
                    if(displayTreeParent && data && nub->setProperty("device_type",data)) {
                        nub->setName("gop-display",gIODTPlane);dtAttached=nub->attachToParent(displayTreeParent,gIODTPlane);
                    }
                    OSSafeReleaseNULL(data);native_legacy_dt_attached=dtAttached;
                    slog("dtattach=%d",dtAttached);
                }
                if(phase==3) {
                    IOService *presenter=native_gop_presenter_create();bool presenterAttached=false;
                    if(presenter && presenter->init() && (presenterAttached=presenter->attach(nub)) && presenter->start(nub)) {
                        // Explicit own transport service only. The incomplete
                        // original framebuffer backend is never published.
                        activeGOPPresenter=presenter;activeGOPNub=nub;nub=nullptr;nubAttached=false;nubStarted=false;
                    } else {if(presenterAttached)presenter->detach(nub);OSSafeReleaseNULL(presenter);}
                } else {
                    SInt32 score=0;native_legacy_probe_match=(phase!=4 || dtAttached) && display->probe(nub,&score)==display;
                    if(native_legacy_probe_match)attached=display->attach(nub);
                    if(attached){native_legacy_stage=5;started=display->start(nub);native_legacy_start_result=started;dline(26,"DISP P%d A%d S%d",native_legacy_probe_match,attached,started);}
                    slog("disp probe=%d attach=%d start=%d",native_legacy_probe_match,attached,started);
                    if(started && phase==4) {
                        activeLegacyDisplay=display;activeGOPNub=nub;native_legacy_stage=10;dline(31,"DISP UP");
                        slog("UP");
                        unsigned gopTest=0;PE_parse_boot_argn("dandelion-gop-test",&gopTest,sizeof(gopTest));
                        if(gopTest==1)gopPaint("TEST", false);
                        {
                            unsigned gpu=0;PE_parse_boot_argn("sheng-fb-gpu",&gpu,sizeof gpu);
                            if(gpu==1) {
                                IOService *handoff=native_gop_presenter_create();
                                // Not a child of the display subsystem's node: the original display code enumerates that node's children (int-075/077:
                                // SpringBoard found no display and crashed with the presenter attached there, registered or not).
                                IOService *home=IOService::getResourceService();
                                bool handoffAttached=false;
                                if(handoff && home)native_gop_presenter_set_source(handoff,nub);
                                if(handoff && home && handoff->init() && (handoffAttached=handoff->attach(home)) && handoff->start(home))gpuHandoffPresenter=handoff;
                                else {if(handoffAttached)handoff->detach(home);OSSafeReleaseNULL(handoff);}
                            }
                        }
                        finishUnitScalePolicy(true);
                        OSSafeReleaseNULL(displayTreeParent);
                        native_legacy_display_checkpoint();return;
                    }
                }
            }
        }
        native_legacy_stage=6;dline(31,"DISP FAIL");finishUnitScalePolicy(false);
        if(started)display->stop(nub);if(attached)display->detach(nub);
        if(dtAttached)nub->detachFromParent(displayTreeParent,gIODTPlane);
        if(nubStarted)nub->stop(nubParent);if(nubAttached)nub->detach(nubParent);
        OSSafeReleaseNULL(ranges);OSSafeReleaseNULL(memory);OSSafeReleaseNULL(nub);
        OSSafeReleaseNULL(displayTreeParent);
    }
    native_legacy_stage=7;gopPaint("FALLBACK", true);display->release();native_legacy_released=1;native_legacy_stage=8;
    native_legacy_display_checkpoint();
}
}

/*
 * Hooks the kernel's display path calls that no component of this port provides yet.
 *
 * native_adreno_fences belongs to the Adreno GPU family; this board is Mali-G52, so there is nothing
 * to fence and the display path gets a no-op.  native_diag_process_summary only feeds a report.
 * Adreno740 was declared to satisfy these, but it is the wrong GPU and it made clang 24 crash, so
 * stubs are the honest answer until the Mali side exists.
 */
extern "C" void
native_adreno_fences (void)
{
}

/* This file declares it as void(void); the display path only calls it to report. */
extern "C" void
native_diag_process_summary (void)
{
}

/*
 * Boot-progress text hooks of the display path.  Stubbed while the provider is exercised: losing the
 * on-screen progress messages is acceptable, losing the provider is not.
 */
/*
 * On-screen boot progress.  The kernel console goes to the eMMC log block this port reads back, not
 * to the panel, so a boot that is going well looks exactly like a boot that has stopped -- which is
 * what makes hours-long bring-up runs hard to watch.  This draws text into the boot GOP framebuffer
 * instead, using the same assumption gopPaint() already relies on: the boot framebuffer is still
 * scanning out even after the original provider is released.  The glyphs come from a 6x10 table
 * generated on the host from a monospace font (package/DandelionFont6x10.h), so nothing here depends
 * on the layout of another project's font file.
 */
static IOMemoryMap *textMapping;
static PE_Video textVideo;
static bool textReady;

static bool gopTextOpen(void) {
    if (textReady) return true;
    PE_Video video = {};
    if (PE_current_console(&video) || !video.v_baseAddr || !video.v_width || !video.v_height ||
        video.v_depth != 32 || video.v_rotate || !video.v_rowBytes ||
        video.v_width > 16384 || video.v_height > 16384) return false;
    IODeviceMemory *memory = IODeviceMemory::withRange(video.v_baseAddr,
        (IOByteCount)video.v_rowBytes * video.v_height);
    if (!memory) return false;
    textMapping = memory->map(kIOMapAnywhere | kIOMapInhibitCache);
    memory->release();
    if (!textMapping || !textMapping->getVirtualAddress()) {
        OSSafeReleaseNULL(textMapping);
        return false;
    }
    textVideo = video;
    textReady = true;
    return true;
}

extern "C" void
native_gop_text (unsigned line, const char *text)
{
  if (!text || !gopTextOpen()) return;
  volatile uint8_t *base = reinterpret_cast<volatile uint8_t *>(textMapping->getVirtualAddress());
  const unsigned scale = textVideo.v_width >= 600 ? 2u : 1u;
  const unsigned cellW = 6u * scale, cellH = 10u * scale, gap = 2u;
  const unsigned cols = textVideo.v_width / cellW;
  if (!cols) return;
  const unsigned y0 = 4u + line * (cellH + gap);
  if (y0 + cellH > textVideo.v_height) return;
  for (unsigned y = y0; y < y0 + cellH; ++y) {
    volatile uint32_t *row = reinterpret_cast<volatile uint32_t *>(base + (uint64_t)y * textVideo.v_rowBytes);
    for (unsigned x = 0; x < cols * cellW && x + 4 < textVideo.v_width; ++x) row[x + 4] = 0xFF000000u;
  }
  unsigned cx = 4;
  for (const char *p = text; *p; ++p) {
    const unsigned code = static_cast<unsigned char>(*p);
    const unsigned char *glyph = (code >= 32 && code <= 95) ? kFont6x10[code - 32] : nullptr;
    if (glyph && cx + cellW <= textVideo.v_width) {
      for (unsigned gy = 0; gy < 10; ++gy) {
        const unsigned bits = glyph[gy];
        for (unsigned gx = 0; gx < 6; ++gx) {
          if (!(bits & (1u << (5 - gx)))) continue;
          for (unsigned sy = 0; sy < scale; ++sy) {
            volatile uint32_t *row = reinterpret_cast<volatile uint32_t *>(
                base + (uint64_t)(y0 + gy * scale + sy) * textVideo.v_rowBytes);
            for (unsigned sx = 0; sx < scale; ++sx) row[cx + gx * scale + sx] = 0xFFFFFFFFu;
          }
        }
      }
    }
    cx += cellW;
  }
  __asm__ volatile("dsb sy" ::: "memory");
}

/* Called from other port drivers that want a line on the panel; a no-op when there is no framebuffer. */
extern "C" void native_boot_progress (unsigned line, const char *text) { native_gop_text(line, text); }

extern "C" void
native_gop_redraw_ring (void)
{
}

/*
 * Provided by the AppleKeyStoreState component.  Selecting that component is prepared but does not
 * link yet: its object defines this function and its archive builds, but the framework's link does
 * not name libAppleKeyStoreState.a, so the symbol stays undefined (HANDOFF 0.115).  The stub keeps
 * the link honest and returns nothing; the caller above treats that as "no key store yet".
 */
class IOService;
extern "C" IOService *
native_keyevents_allocate (void)
{
  return nullptr;
}
