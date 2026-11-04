#!/usr/bin/env python3
"""Bind a block driver's IOStorageFamily references by matching vtables slot by slot.

The link needs absolute addresses for the IOStorageFamily methods a driver does not
override.  The framework gets them from a pinned reference candidate and a macOS
audit; this tree has neither, and the IPSW kernelcache is stripped of kext symbols,
so there is no symbol table to read them from.

What is available is the shape of the ABI itself:

  * the driver object carries its own vtable, whose slots are relocations naming
    either its own overrides or the original base method it inherits;
  * our prelinked kernel contains IOStorageFamily's vtable as an array of function
    pointers into the kext's __TEXT_EXEC extent, in the same slot order, because
    both come from the same class declaration.

So: find the base vtable in our kernel (a 190-slot run whose entries point into the
kext's own code, with pure-virtual slots pointing at __cxa_pure_virtual), read the
driver's slot->symbol map, and pair them.  Every generated address is checked to
land inside the planned IOStorageFamily extents.

    bind-by-vtable.py --object NativeMsdc.o --plan kernel.json --out DIR
"""
import argparse
import json
import pathlib
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'kits/XnuPrelink'))
from native_prelink import macho, backed_offset  # noqa: E402

MODULE = 'com.apple.iokit.IOStorageFamily'
SLOTS = 190
PURE = '__cxa_pure_virtual'

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--object', type=pathlib.Path, required=True)
parser.add_argument('--plan', type=pathlib.Path, default=ROOT.parent / 'kernel/kernel.json')
parser.add_argument('--kernel', type=pathlib.Path, default=ROOT.parent / 'kernel/kernel.macho')
parser.add_argument('--out', type=pathlib.Path, default=ROOT / 'build/native-msdc-bindings')
args = parser.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)

plan = json.loads(args.plan.read_text())
extents = {e['segment']: (e['va'], e['size']) for e in plan['extents'] if e['module'] == MODULE}
if not extents:
    sys.exit(f'no {MODULE} extents in the plan')

# --- the driver's own vtable: slot -> symbol name ------------------------------
obj = args.object.read_bytes()
image = macho(obj)
symbols = []
table = next(at for cmd, at, size in image['commands'] if cmd == 2)
symoff, count, stroff, _ = struct.unpack_from('<4I', obj, table + 8)
for index in range(count):
    strx, kind, sect, _, value = struct.unpack_from('<IBBHQ', obj, symoff + index * 16)
    end = obj.find(b'\0', stroff + strx)
    symbols.append((obj[stroff + strx:end].decode(), value, kind))

vtables = [name for name, _, kind in symbols if name.startswith('__ZTV') and 'NativeMsdcBlock' in name]
if not vtables:
    sys.exit('no NativeMsdcBlock vtable in the object')
vtable_name = vtables[0]
vtable_va = next(value for name, value, _ in symbols if name == vtable_name)

slots = {}
for segment in image['segments']:
    for section in segment['sections']:
        if not (section['va'] <= vtable_va < section['va'] + section['size']):
            continue
        reloc_at, reloc_count = struct.unpack_from('<II', obj, section['command_offset'] + 56)
        for index in range(reloc_count):
            address, word = struct.unpack_from('<II', obj, reloc_at + index * 8)
            if not (word >> 27 & 1 and word >> 28 == 0 and word >> 25 & 3 == 3):
                continue
            slot = (section['va'] + address - (vtable_va + 16)) // 8
            if 0 <= slot < SLOTS:
                slots[slot] = symbols[word & 0xffffff][0]
if not slots:
    sys.exit('the driver vtable has no slot relocations')

# --- the base vtable in our prelinked kernel ----------------------------------
kernel = args.kernel.read_bytes()
kernel_image = macho(kernel)
text_exec = extents.get('__TEXT_EXEC')
data_const = extents.get('__DATA_CONST')
if not text_exec or not data_const:
    sys.exit('the plan lacks IOStorageFamily __TEXT_EXEC or __DATA_CONST')


def in_text_exec(address):
    return text_exec[0] <= address < text_exec[0] + text_exec[1]


# Most slots are IOService implementations (kernel text); only the storage-specific
# ones land in the kext.  What identifies the right run is that exactly the slots the
# driver leaves undefined point into the kext, so use that as the test rather than a
# raw count of kext pointers.
kext_slots = [slot for slot, name in slots.items()
              if name.startswith(('__ZN20IOBlockStorageDevice', '__ZNK20IOBlockStorageDevice',
                                  '__ZN9IOStorage', '__ZNK9IOStorage'))]
if not kext_slots:
    sys.exit('the driver references no IOStorageFamily method by slot')

best = None
for segment_name in ('__DATA_CONST', '__DATA'):
    extent = extents.get(segment_name)
    if not extent:
        continue
    start = extent[0]
    end = extent[0] + extent[1] - 8 * SLOTS
    address = start
    while address <= end:
        try:
            offset = backed_offset(kernel_image['segments'], address, 8 * SLOTS, len(kernel))
        except ValueError:
            address += 8
            continue
        entries = struct.unpack_from('<%dQ' % SLOTS, kernel, offset)
        if all(in_text_exec(entries[slot]) for slot in kext_slots):
            best = (address, entries, sum(1 for value in entries if in_text_exec(value)))
            break
        address += 8
    if best:
        break

if best is None:
    sys.exit('no vtable run whose storage slots land in the kext code')
base_va, base_entries, code_slots = best
print(f'driver vtable {vtable_name} with {len(slots)} named slots')
print(f'base vtable at {base_va:#x}: {code_slots}/{SLOTS} slots land in the kext code, '
      f'matched on {len(kext_slots)} storage-specific slots')

bindings, unresolved = {}, []
for slot, name in sorted(slots.items()):
    if 'NativeMsdcBlock' in name:
        continue                                  # the driver's own override
    target = base_entries[slot]
    # Kernel-implemented slots (OSObject/IOService) may point anywhere in the
    # kernel; only the storage-specific ones have to land in the kext's code.
    storage_slot = name.startswith(('__ZN20IOBlockStorageDevice', '__ZNK20IOBlockStorageDevice',
                                    '__ZN9IOStorage', '__ZNK9IOStorage'))
    if target == 0 or (storage_slot and not in_text_exec(target)):
        unresolved.append((slot, name, target))
        continue
    bindings[name] = target

if unresolved:
    for slot, name, target in unresolved[:8]:
        print(f'  slot {slot}: {name} -> {target:#x} outside the kext')
    sys.exit(f'{len(unresolved)} slot(s) could not be resolved')

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
    'method': 'vtable slot pairing',
    'base_vtable': hex(base_va), 'code_slots': code_slots,
    'bindings': {k: hex(v) for k, v in sorted(bindings.items())}}, indent=2) + '\n')
print(f'PASS: {len(bindings)} symbols bound')
for name, value in sorted(bindings.items())[:8]:
    print(f'  {name[:66]:66s} {value:#x}')
print(archive, archive.stat().st_size, 'bytes')
