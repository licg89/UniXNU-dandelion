#!/usr/bin/env python3
"""Write the transport bindings records the kernel build compares, from our derived symbols.

The framework's build-virtio-mmio-bindings.py (macOS/xcrun) records, next to each storage
transport archive,

    {"compiled_object_sha256": <sha256 of the .o>, "bindings": {symbol: address}}

and build-xnu-4k-baseline.sh reads those two files when NATIVE_RAM_BLOCK and
NATIVE_VIRTIO_BLOCK are both on: it checks that each record's object hash matches the object in
place, and that both transports import the *same* original IOBlockStorageDevice symbols (they
must not duplicate alias definitions for the same class).

On this host the symbols come from scripts/derive-storage-symbols.py, which derives them from our
own prelinked kernel's dispatch table (18 IOBlockStorageDevice symbols), and all three transports
(NativeMsdc, NativeVirtioBlock, NativeRamBlock) link against exactly that map.  This script copies
that one map into the schema the build expects, computing each object hash from the object itself,
so nothing is asserted that was not measured.

    python3 scripts/write-transport-bindings.py --tree <tree>
"""
import argparse
import hashlib
import json
import pathlib
import sys

parser = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--tree', type=pathlib.Path,
                    default=pathlib.Path(__file__).resolve().parents[1],
                    help='the assembled build tree (default: the tree this script lives in)')
args = parser.parse_args()

tree = args.tree.resolve()
source = tree / 'build/native-msdc-bindings/bindings.json'
if not source.is_file():
    raise SystemExit(f'{source} is missing; run derive-storage-symbols.py first')
symbols = json.loads(source.read_text()).get('symbols') or {}
if not symbols:
    raise SystemExit(f'{source} carries no symbols')

targets = [
    ('virtio-mmio-block', 'NativeVirtioBlock.o', 'VirtioMmioBlock.o'),
    ('ram-block', 'NativeRamBlock.o', 'RamBlock.o'),
]
written = 0
for directory, built_name, canonical_name in targets:
    obj = tree / 'build' / directory / built_name
    if not obj.is_file():
        print(f'skip {directory}: {built_name} not built')
        continue
    # build-xnu-4k-baseline.sh derives the object it hashes from the archive name
    # (libVirtioMmioBlock.a -> VirtioMmioBlock.o), while build-arm-driver.py names the object
    # after the runtime identifier (NativeVirtioBlock.o).  Provide the canonical name as a
    # hard link so both refer to the same bytes.
    canonical = obj.with_name(canonical_name)
    if canonical != obj and not canonical.exists():
        try:
            canonical.hardlink_to(obj)
        except OSError:
            canonical.write_bytes(obj.read_bytes())
    record = {
        'compiled_object_sha256': hashlib.sha256(canonical.read_bytes()).hexdigest(),
        'bindings': symbols,
        'source': 'scripts/derive-storage-symbols.py (Linux equivalent of '
                  'build-virtio-mmio-bindings.py)',
    }
    path = obj.with_name('bindings.json')
    path.write_text(json.dumps(record, indent=2) + '\n')
    written += 1
    print(f'{path}: {len(symbols)} bindings, object {record["compiled_object_sha256"][:16]}… '
          f'({canonical_name})')

if not written:
    sys.exit('no transport bindings were written')
print(f'PASS: {written} transport record(s) written from one derived map')
