#!/usr/bin/env python3
"""Validate a trust-cache bundle's structure before it costs a boot.

DarwinBoot's ValidTrustCaches wants count, offsets, then per module version==2, uuid[16],
num_entries and that many 24-byte entries, with the module length exactly 24 + 24*count.  The
previous bundle failed all of that by three bytes and the firmware answered "Compromised Data",
which is expensive to discover from the menu screen.
"""
import pathlib
import struct
import sys

path = pathlib.Path(sys.argv[1])
data = path.read_bytes()
count, = struct.unpack_from('<I', data, 0)
print(f'{path.name}: {len(data)} bytes, {count} modules')
if not 1 <= count <= 64:
    sys.exit('impossible module count')
head = 4 + 4 * count
offsets = list(struct.unpack_from('<%dI' % count, data, 4))
print('  offsets:', [hex(o) for o in offsets])
if offsets != sorted(offsets) or offsets[0] < head or any(o % 4 for o in offsets):
    sys.exit('offsets are not ascending, aligned, or past the header')

ends = offsets[1:] + [len(data)]
for index, (start, end) in enumerate(zip(offsets, ends)):
    if end - start < 24:
        sys.exit(f'module {index}: {end - start} bytes is shorter than a module header')
    version, = struct.unpack_from('<I', data, start)
    uuid = data[start + 4:start + 20]
    entries, = struct.unpack_from('<I', data, start + 20)
    expected = 24 + 24 * entries
    status = 'ok' if version == 2 and end - start == expected else 'BAD'
    print(f'  module {index}: v{version} uuid {uuid.hex()[:16]}… entries {entries} '
          f'length {end - start} (expected {expected}) -> {status}')
    if status == 'BAD':
        sys.exit('module layout mismatch')
print('PASS: the bundle matches the layout DarwinBoot validates')
