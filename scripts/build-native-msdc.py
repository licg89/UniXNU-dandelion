#!/usr/bin/env python3
"""Build the dandelion MSDC eMMC driver archive (vendored NativeMsdc).

Compiles the kernel's own IOPlatformExpert.cpp flags against the vendored source,
like build-platform-power.py and build-dandelion-emmc.py do.

NATIVE_MSDC_BLOCK_DEVICES selects what is built:
  1 (upstream default)  the window block devices too, which subclass the original
                        IOStorageFamily IOBlockStorageDevice -- that build needs the
                        storage bindings and cannot link on its own
  0                     the controller, manifest and store only: this links without
                        bindings, which is what the first bring-up step checks

The driver also needs the personality hook (patches/xnu/dandelion-msdc.patch) to be
instantiated on the board's IOPlatformDevice, because it is linked into the kernel
rather than loaded as a kext.

    build-native-msdc.py [--block-devices 0|1]
"""
import argparse
import json
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--block-devices', type=int, choices=(0, 1), default=0,
                    help='build the window block devices (needs the storage bindings)')
parser.add_argument('--verified-root', action='store_true',
                    help='serve the windows only through owner-signed VerifiedRoot policies')
parser.add_argument('--ufs-store-abi', action='store_true',
                    help="also export native_ufs_store_* aliases so NativeRamBlock can back its "
                         "seed with this board's eMMC store (do not combine with NativeUfs)")
args = parser.parse_args()

OUT = ROOT / 'build/native-msdc'
OUT.mkdir(parents=True, exist_ok=True)

sources = sorted(ROOT.glob('kits/*/NativeMsdc.cpp'))
if len(sources) != 1:
    sys.exit('Expected one board kit with NativeMsdc.cpp')
source = sources[0]

record_path = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'
if not record_path.is_file():
    sys.exit(f'Missing compile record {record_path}; build the kernel once first')
record = json.loads(record_path.read_text().strip().rstrip(','))
obj = OUT / 'NativeMsdc.o'

# IOStorageFamily headers, as the framework exposes them.
feeds_dir = next(path / 'feeds' for path in ROOT.parents if (path / 'feeds').is_dir())
feeds = sorted(feeds_dir.glob('IOStorageFamily-*'))
storage_include = ROOT / 'build/storage-abi/include'
if not storage_include.is_dir():
    sys.exit('build/storage-abi/include is missing; run prepare-storage-headers.py first')

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
    f'-DNATIVE_MSDC_BLOCK_DEVICES={args.block_devices}',
    '-I', str(storage_include), '-I', str(OUT), '-I', str(source.parent),
]
if args.verified_root:
    command += ['-DNATIVE_VERIFIED_BLOCK=1',
                '-I', str(ROOT / 'kits/VerifiedRoot/include')]
if args.ufs_store_abi:
    command += ['-DNATIVE_MSDC_UFS_STORE_ABI=1']
result = subprocess.run(command, cwd=record['directory'], stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, text=True)
(OUT / 'compile.log').write_text(result.stdout)
if result.returncode != 0:
    sys.stderr.write(result.stdout[-8000:])
    sys.exit('NativeMsdc.cpp did not compile')

archive = OUT / 'libNativeMsdc.a'
archive.unlink(missing_ok=True)
subprocess.run(['llvm-ar', '--format=darwin', 'rcs', str(archive), str(obj)], check=True)
print(f'PASS: {archive} ({archive.stat().st_size} bytes), block devices '
      f'{args.block_devices}')
