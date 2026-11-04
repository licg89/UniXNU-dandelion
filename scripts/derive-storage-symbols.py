#!/usr/bin/env python3
"""Derive IOStorageFamily's absolute addresses from our own prelinked kernel.

The records for other boards (docs/devices/sirius-*.md) state the method and why it
works: IOStorageFamily's kext image carries no symbol table after prelink, so its
addresses are derived "from the dispatch table itself" --

  * 176 of the 190 pinned slot names are kernel exports, and exactly one __DATA_CONST
    table matches all of them; the other 14 slots point at the kext's own BRK traps;
  * the single constructor that stores table+16 and forwards to the actual IOService
    constructor is the protected base constructor;
  * the base destructor resolves to IOService's;
  * slot 7 (getMetaClass) is `adrp/add/ret`, which yields gMetaClass directly.

The slot names come from the public IOStorageFamily source compiled against our own
kernel's flags -- the same reference object the framework's audit builds, which this
tree already has at build/storage-abi/IOBlockStorageDevice-reference.o.

    derive-storage-symbols.py [--out build/native-msdc-bindings]
"""
import argparse
import json
import pathlib
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'kits/XnuPrelink'))
from native_prelink import macho, exports, backed_offset  # noqa: E402

MODULE = 'com.apple.iokit.IOStorageFamily'
SLOTS = 190
PURE = 14
PURE_NAME = '___cxa_pure_virtual'
IOSERVICE_CTOR = '__ZN9IOServiceC2EPK11OSMetaClass'
IOSERVICE_DTOR = '__ZN9IOServiceD2Ev'
IOSERVICE_META = '__ZN9IOService10gMetaClassE'

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--plan', type=pathlib.Path, default=ROOT.parent / 'kernel/kernel.json')
parser.add_argument('--kernel', type=pathlib.Path, default=ROOT.parent / 'kernel/kernel.macho')
parser.add_argument('--reference', type=pathlib.Path,
                    default=ROOT / 'build/storage-abi/IOBlockStorageDevice-reference.o')
parser.add_argument('--object', type=pathlib.Path, help='driver object to archive with the bindings')
parser.add_argument('--out', type=pathlib.Path, default=ROOT / 'build/native-msdc-bindings')
args = parser.parse_args()

out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)

plan = json.loads(args.plan.read_text())
extents = {e['segment']: e for e in plan['extents'] if e['module'] == MODULE}
kernel = args.kernel.read_bytes()
image = macho(kernel)
by_name = {name: va for va, names in exports(kernel, image).items() for name in names}


def read(va, length):
    at = backed_offset(image['segments'], va, length, len(kernel))
    return kernel[at:at + length]


# --- slot names from the reference object -------------------------------------
obj = args.reference.read_bytes()
om = macho(obj)
symbols = []
table_at = next(at for cmd, at, size in om['commands'] if cmd == 2)
symoff, count, stroff, _ = struct.unpack_from('<4I', obj, table_at + 8)
for index in range(count):
    strx, kind, sect, _, value = struct.unpack_from('<IBBHQ', obj, symoff + index * 16)
    end = obj.find(b'\0', stroff + strx)
    symbols.append((obj[stroff + strx:end].decode(), value, kind))
vtable_va = next(value for name, value, kind in symbols if name == '__ZTV20IOBlockStorageDevice')
relocations = {}
for segment in om['segments']:
    for section in segment['sections']:
        if not (section['va'] <= vtable_va < section['va'] + section['size']):
            continue
        reloc_at, reloc_count = struct.unpack_from('<II', obj, section['command_offset'] + 56)
        for index in range(reloc_count):
            address, word = struct.unpack_from('<II', obj, reloc_at + index * 8)
            if word >> 27 & 1 and word >> 28 == 0 and word >> 25 & 3 == 3:
                relocations[section['va'] + address] = symbols[word & 0xffffff][0]
methods = []
for at in range(vtable_va + 16, vtable_va + 16 + 8 * SLOTS, 8):
    if at not in relocations:
        break
    methods.append(relocations[at])
if len(methods) != SLOTS:
    sys.exit(f'expected {SLOTS} slots in the reference object, found {len(methods)}')
if methods.count(PURE_NAME) != PURE:
    sys.exit(f'expected {PURE} pure-virtual slots, found {methods.count(PURE_NAME)}')

# --- the dispatch table in our kernel -----------------------------------------
const = extents['__DATA_CONST']
text = extents['__TEXT_EXEC']
blob = read(const['va'], const['size'])
found = []
for at in range(0, len(blob) - (SLOTS + 2) * 8 + 1, 8):
    if blob[at:at + 16] != bytes(16):
        continue
    pointers = struct.unpack_from('<' + 'Q' * SLOTS, blob, at + 16)
    if all(pointers[i] == by_name[name] for i, name in enumerate(methods) if name in by_name):
        found.append((const['va'] + at, pointers))
if len(found) != 1:
    sys.exit(f'cannot uniquely identify the dispatch table (found {len(found)})')
table, pointers = found[0]
matched = sum(name in by_name for name in methods)
if matched != 176:
    sys.exit(f'expected 176 kernel-bound slots, matched {matched}')
for index, name in enumerate(methods):
    if name not in by_name and not (text['va'] <= pointers[index] < text['va'] + text['size']):
        sys.exit(f'slot {index} ({name}) is neither a kernel export nor kext code')


def relative_branch(address, instruction):
    immediate = instruction & 0x3ffffff
    if immediate & 0x2000000:
        immediate -= 0x4000000
    return address + immediate * 4


def thunk_names(address):
    a, load, branch = struct.unpack('<3I', read(address, 12))
    if (a & 0x9f00001f, load & 0xffc003ff, branch) != (0x90000010, 0xf9400210, 0xd61f0200):
        return []
    immediate = ((a >> 5 & 0x7ffff) << 2) | (a >> 29 & 3)
    if immediate & 0x100000:
        immediate -= 0x200000
    slot = (address & ~4095) + immediate * 4096 + (load >> 10 & 4095) * 8
    target = struct.unpack('<Q', read(slot, 8))[0]
    return exports(kernel, image).get(target, [])


# --- protected constructor ----------------------------------------------------
constructors = []
for address in range(text['va'], text['va'] + text['size'] - 32, 4):
    words = struct.unpack('<8I', read(address, 32))
    if (words[:2] != (0xa9bf7bfd, 0x910003fd) or words[2] & 0xfc000000 != 0x94000000 or
            words[3] & 0x9f00001f != 0x90000008 or words[4] & 0xffc003ff != 0x91000108 or
            words[5:] != (0xf9000008, 0xa8c17bfd, 0xd65f03c0)):
        continue
    immediate = ((words[3] >> 5 & 0x7ffff) << 2) | (words[3] >> 29 & 3)
    if immediate & 0x100000:
        immediate -= 0x200000
    stored = ((address + 12) & ~4095) + immediate * 4096 + (words[4] >> 10 & 4095)
    if stored == table + 16:
        constructors.append((address, relative_branch(address + 8, words[2])))
if len(constructors) != 1:
    sys.exit(f'expected one protected constructor, found {len(constructors)}')
ctor, ctor_thunk = constructors[0]
if IOSERVICE_CTOR not in thunk_names(ctor_thunk):
    sys.exit('the constructor does not forward to IOService')

# --- base destructor ---------------------------------------------------------
dtor = pointers[0] - 4
word = struct.unpack('<I', read(dtor, 4))[0]
if word & 0xfc000000 != 0x14000000 or IOSERVICE_DTOR not in thunk_names(relative_branch(dtor, word)):
    sys.exit('the base destructor does not resolve to IOService')

# --- metaclass from slot 7 ---------------------------------------------------
function = pointers[7]
a, add, ret = struct.unpack('<3I', read(function, 12))
if (a & 0x9f00001f, add & 0xffc003ff, ret) != (0x90000000, 0x91000000, 0xd65f03c0):
    sys.exit('slot 7 is not the metaclass getter')
immediate = ((a >> 5 & 0x7ffff) << 2) | (a >> 29 & 3)
if immediate & 0x100000:
    immediate -= 0x200000
meta = (function & ~4095) + immediate * 4096 + (add >> 10 & 4095)

# --- bindings ----------------------------------------------------------------
bindings = {
    '__ZTV20IOBlockStorageDevice': table,
    '__ZN20IOBlockStorageDeviceC2EPK11OSMetaClass': ctor,
    '__ZN20IOBlockStorageDeviceD2Ev': dtor,
    '__ZN20IOBlockStorageDevice10gMetaClassE': meta,
}
for index, name in enumerate(methods):
    if name in (PURE_NAME,) or name in bindings:
        continue
    # Only the kext-owned slots need an absolute address: the other 176 names are
    # kernel exports (IOService, IORegistryEntry, ...) that the kernel already
    # defines, and binding them again produces "162 duplicate symbols".
    if name in by_name:
        continue
    bindings.setdefault(name, pointers[index])

assembly = out / 'storage-bindings.S'
assembly.write_text(''.join(f'.globl {name}\n.set {name}, {value:#x}\n'
                            for name, value in sorted(bindings.items())))
subprocess.run(['/usr/bin/xcrun', 'clang', '-target', 'arm64-apple-ios17.0', '-c',
                str(assembly), '-o', str(out / 'storage-bindings.o')], check=True)
if args.object:
    archive = out / 'libStorageBindings.a'
    archive.unlink(missing_ok=True)
    subprocess.run(['llvm-ar', '--format=darwin', 'rcs', str(archive),
                    str(args.object), str(out / 'storage-bindings.o')], check=True)
    print(f'{archive} ({archive.stat().st_size} bytes): driver object plus {len(bindings)} bindings')
    # The kernel link picks up build/native-msdc/libNativeMsdc.a by name, so the
    # archive it finds there has to be the bound one; otherwise the same object
    # arrives without its bindings and the whole IOBlockStorageDevice vtable is
    # undefined again.
    plain = args.object.parent / 'libNativeMsdc.a'
    if plain.is_file():
        plain.write_bytes(archive.read_bytes())
        print(f'{plain}: replaced with the bound archive')

(out / 'bindings.json').write_text(json.dumps({
    'dispatch_table': hex(table), 'kernel_bound_slots': matched,
    'constructor': hex(ctor), 'destructor': hex(dtor), 'metaclass': hex(meta),
    'ioservice_metaclass': hex(by_name[IOSERVICE_META]),
    'symbols': {k: hex(v) for k, v in sorted(bindings.items())}}, indent=2) + '\n')
print(f'PASS: table {table:#x}, {matched} kernel-bound slots')
print(f'  constructor {ctor:#x} (forwards to IOService)')
print(f'  destructor  {dtor:#x} (resolves to IOService)')
print(f'  metaclass   {meta:#x}')
print(f'  {len(bindings)} symbols written to {assembly}')
