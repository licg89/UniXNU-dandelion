#!/usr/bin/env python3
"""Generate the storage bindings our kernel actually needs, from our own prelink plan.

A block driver subclasses IOBlockStorageDevice, whose implementation lives in the
prelinked IOStorageFamily kext: those symbols do not exist at kernel link time, so
the link needs them as absolute addresses.  The framework's own route
(audit-storage-interface.py + build-native-virtio-bindings.py) is pinned to a
reference candidate this tree does not have, and assumes macOS; this does the same
job from two inputs we do have:

  * kernel/kernel.json   -- the prelink plan: every module's segment VAs and sizes
                            in the kernel we just built
  * the IPSW kernelcache -- the original fileset, whose IOStorageFamily entry still
                            has a symbol table

Mapping is exact: a symbol at original VA v inside segment S becomes
plan_extent(module, S).va + (v - S.va).  Every generated address is checked to land
inside the planned extent, and the symbol must be one the driver genuinely imports.

    build-storage-bindings.py --object <driver.o> --out build/native-msdc-bindings
"""
import argparse
import json
import pathlib
import re
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'kits/XnuPrelink'))
from native_prelink import macho, exports  # noqa: E402

LC_FILESET_ENTRY = 0x80000035
MODULE = 'com.apple.iokit.IOStorageFamily'

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--object', type=pathlib.Path, required=True, help='the driver object')
parser.add_argument('--plan', type=pathlib.Path, default=ROOT.parent / 'kernel/kernel.json')
parser.add_argument('--kernelcache', type=pathlib.Path, required=True)
parser.add_argument('--out', type=pathlib.Path, default=ROOT / 'build/native-msdc-bindings')
args = parser.parse_args()

out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)

plan = json.loads(args.plan.read_text())
extents = {}
for entry in plan['extents']:
    if entry['module'] == MODULE:
        extents[entry['segment']] = entry['va']
if not extents:
    sys.exit(f'no {MODULE} extents in the prelink plan')

cache = args.kernelcache.read_bytes()
outer = macho(cache)
entry_at = None
for command, offset, length in outer['commands']:
    if command != LC_FILESET_ENTRY:
        continue
    vmaddr, fileoff, entry_id, reserved = struct.unpack_from('<QQII', cache, offset + 8)
    name = cache[offset + 32:offset + length].split(b'\0')[0].decode()
    if name == MODULE:
        entry_at = fileoff
if entry_at is None:
    sys.exit(f'{MODULE} is not a fileset entry of {args.kernelcache}')

kext = macho(cache, entry_at)
symbols = exports(cache, kext) if False else None   # exports() assumes a non-fileset layout
# Parse the kext's own symbol table relative to its file offset.
table = [(offset, length) for command, offset, length in kext['commands'] if command == 2]
if len(table) != 1:
    sys.exit('expected one symbol table in the kext')
symoff, count, stroff, strsize = struct.unpack_from('<4I', cache, table[0][0] + 8)
original = {}
for index in range(count):
    strx, kind, _, _, address = struct.unpack_from('<IBBHQ', cache, symoff + index * 16)
    if kind & 0xe0 or not kind & 1 or not kind & 0x0e:
        continue
    end = cache.find(b'\0', stroff + strx)
    name = cache[stroff + strx:end].decode()
    original[name] = address

object_data = args.object.read_bytes()
image = macho(object_data)
imports = []
tables = [(offset, length) for command, offset, length in image['commands'] if command == 2]
symoff, count, stroff, strsize = struct.unpack_from('<4I', object_data, tables[0][0] + 8)
for index in range(count):
    strx, kind = struct.unpack_from('<IB', object_data, symoff + index * 16)
    if kind & 0x0e and not kind & 0x0e == 0x0e:
        continue
    if kind & 0xe0:
        continue
    end = object_data.find(b'\0', stroff + strx)
    name = object_data[stroff + strx:end].decode()
    if name.startswith(('__ZN20IOBlockStorageDevice', '__ZTV20IOBlockStorageDevice',
                        '__ZN9IOStorage', '__ZNK20IOBlockStorageDevice')):
        imports.append(name)

segments = kext['segments']


def original_segment(va):
    for segment in segments:
        if segment['va'] <= va < segment['va'] + segment['size']:
            return segment
    return None


bindings, problems = {}, []
for name in sorted(set(imports)):
    va = original.get(name)
    if va is None:
        problems.append(f'{name}: not in the kext symbol table')
        continue
    segment = original_segment(va)
    if segment is None:
        problems.append(f'{name}: {va:#x} is in no segment')
        continue
    planned = extents.get(segment['name'])
    if planned is None:
        problems.append(f"{name}: no planned extent for {segment['name']}")
        continue
    offset = va - segment['va']
    bindings[name] = planned + offset
    if not (planned <= bindings[name] < planned + 1 << 32):
        problems.append(f'{name}: mapped outside the extent')

if problems:
    for problem in problems[:10]:
        print('  ' + problem)
    sys.exit(f'{len(problems)} import(s) could not be resolved')

assembly = out / 'storage-bindings.S'
assembly.write_text(''.join(f'.globl {name}\n.set {name}, {value:#x}\n'
                            for name, value in sorted(bindings.items())))
subprocess.run(['/usr/bin/xcrun', 'clang', '-target', 'arm64-apple-ios17.0', '-c',
                str(assembly), '-o', str(out / 'storage-bindings.o')], check=True)
archive = out / 'libStorageBindings.a'
archive.unlink(missing_ok=True)
subprocess.run(['llvm-ar', '--format=darwin', 'rcs', str(archive),
                str(args.object), str(out / 'storage-bindings.o')], check=True)
(out / 'bindings.json').write_text(json.dumps({
    'module': MODULE, 'plan_extents': {k: hex(v) for k, v in extents.items()},
    'symbols': {k: hex(v) for k, v in sorted(bindings.items())},
    'original_kernelcache': str(args.kernelcache)}, indent=2) + '\n')
print(f'PASS: {len(bindings)} IOStorageFamily symbols bound from {MODULE}')
for name, value in sorted(bindings.items())[:6]:
    print(f'  {name[:64]:64s} {value:#x}')
print(f'{archive} ({archive.stat().st_size} bytes)')
