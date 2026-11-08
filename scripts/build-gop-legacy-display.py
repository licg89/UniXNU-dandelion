#!/usr/bin/env python3
"""Build the merlinx GOP legacy display archive from the fixed iOS 17.7.11 ABI.

This is the public sheng recipe with the sheng-only probes removed. The
original IOMobileFramebufferLegacy layout and method addresses come from
scripts/data/merlinx-legacy-display-abi.json, extracted from the working
prelinked kernel, so no sheng candidate or private kit is needed.
"""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CLANG = '/opt/unixnu-linux/llvm/bin/clang'
AR = '/opt/unixnu-linux/CommandLineTools/usr/bin/ar'
OUT = ROOT / 'build/native-legacy-display'
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT / 'kits/XnuPrelink'))
from native_prelink import macho


def run(command, **kwargs):
    return subprocess.check_output([str(x) for x in command], timeout=120, **kwargs)


def require(value, why):
    if not value:
        raise SystemExit(why)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


abi = json.loads((ROOT / 'scripts/data/merlinx-legacy-display-abi.json').read_text())
pure = abi['pure']
methods = [int(value, 16) for value in abi['methods']]
legacy_vptr = int(abi['legacy_vptr'], 16)
require(len(methods) == 358, 'merlinx legacy ABI needs exactly 358 methods')

header = '''// SPDX-License-Identifier: BSD-2-Clause-Patent
// Opaque ABI storage for the exact original class, not source-level access
// to its private fields. The actual original metadata supplies its full
// Framebuffer/Service/IOService ancestry. Only qualified base prototypes may
// be called from source; opaque entries are original dispatch targets only.
#pragma once
#include <IOKit/IOService.h>
#include <libkern/libkern.h>
struct OriginalLegacySwapRequest;
struct OriginalLegacyStateRequest {uint32_t skipRequested,requested,skipPowered,powered;uint8_t changed;uint8_t pad[11];};
static constexpr unsigned kLegacyVsyncClients=5592;
static constexpr unsigned kRequestRect0=17,kRequestRect1=81,kRequestMasks=792,kRequestOptions=804,kRequestDims=816,kRequestSurfaces=2624;
#define NATIVE_LEGACY_LEAVES(LEAF) LEAF(211) LEAF(245) LEAF(246) LEAF(306) LEAF(307) LEAF(338) LEAF(345)
class IOMobileFramebufferLegacy : public IOService {
 OSDeclareAbstractStructors(IOMobileFramebufferLegacy)
 unsigned char originalPrivate[0x18a0-sizeof(IOService)];
public:
 void installBootDimensions(uint32_t width,uint32_t height) {
  *reinterpret_cast<uint32_t *>(reinterpret_cast<unsigned char *>(this)+5808)=width;
  *reinterpret_cast<uint32_t *>(reinterpret_cast<unsigned char *>(this)+5812)=height;
 }
 const OriginalLegacySwapRequest *pendingSwapRequest() const {
  const OriginalLegacySwapRequest *request=nullptr;
  memcpy(&request,reinterpret_cast<const unsigned char *>(this)+5480,sizeof(request));return request;
 }
 bool activateOnlyPendingSwap(const OriginalLegacySwapRequest *request) {
  auto self=reinterpret_cast<unsigned char *>(this);
  auto bytes=reinterpret_cast<const unsigned char *>(request);
  const void *current=nullptr,*head=nullptr,*tail=nullptr,*next=nullptr,*previous=nullptr;
  memcpy(&current,self+5472,8);memcpy(&head,self+5480,8);memcpy(&tail,self+5488,8);
  if(!request || current || head!=request)return false;
  memcpy(&next,bytes+2960,8);memcpy(&previous,bytes+2968,8);
  if(previous!=self+5480)return false;
  if(next) {
   const void *link=self+5480;
   memcpy(const_cast<unsigned char *>(reinterpret_cast<const unsigned char *>(next))+2968,&link,8);
  } else if(tail!=bytes+2960)return false;
  memcpy(self+5472,&request,8);
  if(next) {memcpy(self+5480,&next,8);}
  else {const void *empty=nullptr,*emptyTail=self+5480;memcpy(self+5480,&empty,8);memcpy(self+5488,&emptyTail,8);}
  return true;
 }
 void free() override;
 bool init(OSDictionary *properties=nullptr) override;
 IOReturn setProperties(OSObject *) override;
 void systemWillShutdown(IOOptionBits) override;
 IOService *probe(IOService *,SInt32 *) override;
 bool start(IOService *) override;
 void stop(IOService *) override;
 IOWorkLoop *getWorkLoop() const override;
 IOReturn newUserClient(task_t,void *,UInt32,IOUserClient **) override;
 IOReturn setPowerState(unsigned long,IOService *) override;
'''
for i in range(168, 358):
    if i == 184:
        header += ' virtual bool isMainDisplay();\n'
    elif i == 188:
        header += ' virtual IOReturn getDotPitch(uint32_t *);\n'
    elif i == 213:
        header += ' virtual IOReturn supportedFrameInfo(uint32_t,uint32_t *,char *,uint32_t *)=0;\n'
    elif i == 249:
        header += ' virtual void getDisplayDimensions(uint32_t *,uint32_t *);\n'
    elif i == 290:
        header += ' virtual void kickSwapQueue();\n'
    elif i == 296:
        header += ' virtual bool validateSwap(const OriginalLegacySwapRequest *);\n'
    elif i == 295:
        header += ' virtual void completeCurrentSwap();\n'
    elif i == 337:
        header += ' virtual IOReturn collectCompletionReports(uint32_t,void *,uint32_t *,uint32_t)=0;\n'
    elif i == 288:
        header += ' virtual bool startHardware();\n'
    elif i == 354:
        header += ' virtual int applyStateRequest(OriginalLegacyStateRequest *,bool);\n'
    elif i == 301:
        header += ' virtual bool getRefreshAttributes(uint64_t *,bool);\n'
    elif i in pure:
        header += f' virtual __attribute__((noreturn)) void opaque{i}()=0;\n'
    else:
        header += f' virtual __attribute__((deprecated("Unclassified ABI: never call from source"))) void opaque{i}();\n'
header += '};\nstatic_assert(sizeof(IOMobileFramebufferLegacy)==0x18a0,"Actual original instance extent");\n'
(OUT / 'OriginalLegacyFramebufferABI.h').write_text(header)

run(['python3', ROOT / 'scripts/build-arm-driver.py', 'NativeLegacyDisplay'])
run(['python3', ROOT / 'scripts/build-arm-driver.py', 'NativeGOPPresenter'])

obj = OUT / 'NativeLegacyDisplay.o'
raw = obj.read_bytes()
image = macho(raw)
st = next(p for c, p, _ in image['commands'] if c == 2)
sy, count, strings, _ = struct.unpack_from('<4I', raw, st + 8)
symbols = []
for i in range(count):
    ix, kind, section, _, va = struct.unpack_from('<IBBHQ', raw, sy + i * 16)
    symbols.append((raw[strings + ix:raw.index(b'\0', strings + ix)].decode(), va, kind))
vtable = next(va for name, va, _ in symbols if name == '__ZTV19NativeLegacyDisplay')
relocations = {}
for segment in image['segments']:
    for section in segment['sections']:
        if section['va'] <= vtable < section['va'] + section['size']:
            ro, rc = struct.unpack_from('<2I', raw, section['command_offset'] + 56)
            for i in range(rc):
                offset, bits = struct.unpack_from('<2I', raw, ro + i * 8)
                if bits >> 27 & 1 and bits >> 28 == 0 and bits >> 25 & 3 == 3:
                    relocations[section['va'] + offset] = symbols[bits & 0xffffff][0]
compiled = [relocations.get(vtable + 16 + i * 8) for i in range(358)]
require(all(compiled), 'Exact owned 358-entry vtable')

bindings = {
    '__ZN25IOMobileFramebufferLegacyC2EPK11OSMetaClass': 0xfffffff0057865cc,
    '__ZN25IOMobileFramebufferLegacyD2Ev': 0xfffffff005786618,
    '__ZN25IOMobileFramebufferLegacy10gMetaClassE': 0xfffffff006b35940,
    '__ZTV25IOMobileFramebufferLegacy': legacy_vptr - 16,
    '_native_legacy_surface_descriptor': 0xfffffff004b9ead0,
    '_native_legacy_surface_width': 0xfffffff004b9ebbc,
    '_native_legacy_surface_height': 0xfffffff004b9ebc4,
    '_native_legacy_surface_stride': 0xfffffff004b9ebd4,
    '_native_legacy_surface_allocation': 0xfffffff004b9ebfc,
    '_native_legacy_surface_format': 0xfffffff004b9ebac,
    '_native_legacy_surface_planes': 0xfffffff004b9ebf4,
    '_native_legacy_vsync_notify_original': 0xfffffff005796720,
    '_native_legacy_set_refresh_properties': 0xfffffff00579ebb8,
}
for i, name in enumerate(compiled):
    if name and (name.startswith('__ZN25IOMobileFramebufferLegacy') or
                 name.startswith('__ZNK25IOMobileFramebufferLegacy')):
        bindings[name] = methods[i]

undefined = {n for n, _, kind in symbols if kind == 1 and 'IOMobileFramebufferLegacy' in n}
missing = undefined - set(bindings)
require(not missing, 'Unbound original Legacy symbols: ' + ', '.join(sorted(missing)))

asm = OUT / 'original-bindings.S'
asm.write_text(''.join(f'.globl {name}\n.set {name}, {value:#x}\n' for name, value in sorted(bindings.items())))
sdk = ''   # Linux: the kernel compiles against in-tree headers, with no -isysroot
run([CLANG, '-target', 'arm64-apple-ios17.0', '-Werror',
     '-c', asm, '-o', OUT / 'original-bindings.o'])

# The backend is the public sheng GOP backend with the sheng probes removed.
record = json.loads((ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))
args = record['arguments']
command = []
index = 0
source = ROOT / 'package/DandelionLegacyDisplay.cpp'
while index < len(args):
    value = args[index]
    if value == record['file']:
        command.append(str(source))
    elif value == '-o':
        command += ['-o', str(OUT / 'DandelionLegacyDisplay.o')]
        index += 1
    elif value == '-Xclang' and args[index + 1].startswith('-W'):
        index += 1
    elif not value.startswith('-W'):
        command.append(value)
    index += 1
command += ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
            '-Wno-unnecessary-virtual-specifier', '-Wno-c99-designator',
            '-Wno-reorder-init-list', '-Wno-deprecated-enum-enum-conversion',
            '-I', str(ROOT / '.cache/xnu-work/osfmk')]
run(command, cwd=record['directory'])

llvm = Path('/opt/homebrew/opt/llvm/bin')
if not (llvm / 'llvm-objcopy').is_file():
    llvm = Path(shutil.which('llvm-objcopy')).parent
late = OUT / 'NativeLegacyDisplay.late.o'
run([llvm / 'llvm-objcopy', '--remove-section=__DATA,__mod_init_func',
     '--globalize-symbol=__GLOBAL__sub_I_NativeLegacyDisplay.cpp', obj, late])
gop_obj = ROOT / 'build/native-gop-presenter/NativeGOPPresenter.o'
gop_late = OUT / 'NativeGOPPresenter.late.o'
run([llvm / 'llvm-objcopy', '--remove-section=__DATA,__mod_init_func',
     '--globalize-symbol=__GLOBAL__sub_I_NativeGOPPresenter.cpp', gop_obj, gop_late])

archive = OUT / 'libNativeLegacyDisplay.late.a'
archive.unlink(missing_ok=True)
run([AR, 'rcs', archive, late, OUT / 'original-bindings.o',
     OUT / 'DandelionLegacyDisplay.o', gop_late])
sources = [ROOT / 'drivers/GopFramebuffer/NativeLegacyDisplay.cpp',
           ROOT / 'drivers/GopFramebuffer/NativeLegacyRequest.h',
           ROOT / 'drivers/GopFramebuffer/NativeGOPPresenter.cpp',
           ROOT / 'drivers/GopFramebuffer/NativeGOPWire.h',
           ROOT / 'package/DandelionLegacyDisplay.cpp',
           ROOT / 'scripts/data/merlinx-legacy-display-abi.json',
           ROOT / 'scripts/build-gop-legacy-display.py']
(OUT / 'build-report.json').write_text(json.dumps(dict(
    source_sha256={str(p.relative_to(ROOT)): sha(p) for p in sources},
    generated_abi_sha256=sha(OUT / 'OriginalLegacyFramebufferABI.h'),
    compiled_object_sha256=sha(obj), late_object_sha256=sha(late),
    archive_sha256=sha(archive), virtual_entries=358,
    owned_entries=[i for i, name in enumerate(compiled) if name and 'NativeLegacyDisplay' in name],
    original_instructions_modified=False, display_presented=False, hardware_tested=False), indent=2) + '\n')
print(f'PASS: merlinx GOP legacy display archive ({len(bindings)} original bindings)')
