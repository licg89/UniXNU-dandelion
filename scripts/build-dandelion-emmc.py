#!/usr/bin/env python3
"""Build the dandelion eMMC host driver archive.

Same shape as build-platform-power.py: reuse the kernel's own compile record for
IOPlatformExpert.cpp so the object is built with exactly the flags the kernel
uses, then archive it and let the kernel link it with -force_load.

Two additions this board needs:
  * IOStorageFamily is a prelinked kext, so its headers are not in the kernel SDK;
    they are taken from the IOStorageFamily feed and exposed as <IOKit/storage/...>.
  * clang 24 is stricter than the toolchain that produced the record, so the
    Apple-header warnings the kernel build already silences are silenced here too.
"""
import json
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/dandelion-emmc'
OUT.mkdir(parents=True, exist_ok=True)

sources = sorted(ROOT.glob('kits/*/DandelionEmmc.cpp'))
if len(sources) != 1:
    sys.exit('Expected one board kit with DandelionEmmc.cpp')
source = sources[0]

record_path = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'
if not record_path.is_file():
    sys.exit(f'Missing compile record {record_path}; build the kernel once first')
record = json.loads(record_path.read_text().strip().rstrip(','))
obj = OUT / 'DandelionEmmc.o'

# <IOKit/storage/*.h> from the IOStorageFamily feed.
feeds_dir = next(path / 'feeds' for path in ROOT.parents if (path / 'feeds').is_dir())
feeds = sorted(feeds_dir.glob('IOStorageFamily-*'))
if not feeds:
    sys.exit('IOStorageFamily feed is missing; run the kernel step once so feeds resolve')
include = OUT / 'include/IOKit/storage'
include.mkdir(parents=True, exist_ok=True)
for header in feeds[-1].glob('*.h'):
    shutil.copyfile(header, include / header.name)

# IOStorageDeviceCharacteristics.h asks for TargetConditionals.h, which lives in the
# macOS SDK and not in the kernel SDK this build uses.  The kernel only needs the
# platform answers, so provide them.
(OUT / 'include/TargetConditionals.h').write_text(
    '/* Minimal TargetConditionals for the kernel build: no macOS SDK here. */\n'
    '#pragma once\n'
    '#define TARGET_OS_MAC 1\n'
    '#define TARGET_OS_OSX 0\n'
    '#define TARGET_OS_IPHONE 1\n'
    '#define TARGET_OS_IOS 1\n'
    '#define TARGET_OS_TV 0\n'
    '#define TARGET_OS_WATCH 0\n'
    '#define TARGET_OS_SIMULATOR 0\n'
    '#define TARGET_OS_EMBEDDED 1\n'
    '#define TARGET_CPU_ARM64 1\n'
    '#define TARGET_RT_64_BIT 1\n')

command, index = [], 0
arguments = record['arguments']
while index < len(arguments):
    arg = arguments[index]
    if arg == record['file']:
        command.append(str(source))
    elif arg == '-o':
        command += ['-o', str(obj)]
        index += 1
    elif arg == '-Xclang' and arguments[index + 1].startswith('-W'):
        index += 1
    elif not arg.startswith('-W'):
        command.append(arg)
    index += 1
command += [
    '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
    '-Wno-missing-designated-field-initializers', '-Wno-unknown-warning-option',
    '-Wno-deprecated-enum-enum-conversion', '-Wno-c99-designator',
    '-Wno-unnecessary-virtual-specifier', '-Wno-reorder-init-list',
    '-I', str(OUT / 'include'), '-I', str(OUT),
]
result = subprocess.run(command, cwd=record['directory'], stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, text=True)
if result.returncode != 0:
    sys.stderr.write(result.stdout)
    sys.exit('DandelionEmmc.cpp did not compile')

archive = OUT / 'libDandelionEmmc.a'
archive.unlink(missing_ok=True)
subprocess.run(['llvm-ar', '--format=darwin', 'rcs', str(archive), str(obj)], check=True)
print(f'PASS: {archive} ({archive.stat().st_size} bytes)')
