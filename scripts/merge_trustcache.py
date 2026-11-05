#!/usr/bin/env python3
"""Merge Apple .trustcache files into the bundle DarwinBoot validates.

Each file is an IM4P DER wrapper whose payload is already one trust-cache module
(version 2, uuid, entry count, 24-byte entries).  The loader wants

    uint32 count; uint32 offset[count]; modules...

with the offsets ascending and 4-byte aligned, so this strips the DER header and
concatenates the payloads.  Both the OS and the SystemOS cache have to be present: the dyld
shared cache lives on the SystemOS volume, so its cdhash is only in that cache, and AMFI's
shared-cache registration refuses an image it cannot find there.

    merge_trustcache.py out.bundle in1.trustcache [in2.trustcache ...]
"""
import struct
import sys
import pathlib

out = pathlib.Path(sys.argv[1])
sources = [pathlib.Path(name) for name in sys.argv[2:]]
if not sources:
    sys.exit('give at least one .trustcache file')

modules = []
for source in sources:
    data = source.read_bytes()
    # DER: SEQUENCE { UTF8String "IM4P", UTF8String type, UTF8String "1", OCTET STRING }
    marker = data.find(b'IM4P')
    if marker < 0 or marker > 16:
        sys.exit(f'{source} is not an IM4P container')
    at = data.find(b'\x04\x82', marker)
    if at < 0:
        sys.exit(f'{source}: no long-form OCTET STRING after the IM4P header')
    length = int.from_bytes(data[at + 2:at + 4], 'big')
    payload = data[at + 4:at + 4 + length]
    if len(payload) != length:
        sys.exit(f'{source}: payload truncated')
    version, = struct.unpack_from('<I', payload, 0)
    count, = struct.unpack_from('<I', payload, 20)
    entries = (len(payload) - 24) // 24
    if version != 2 or count != entries:
        sys.exit(f'{source}: module v{version} with {count} entries in {len(payload)} bytes')
    print(f'{source.name}: module v2, {count} entries')
    modules.append(payload)

count = len(modules)
head = 4 + 4 * count
offsets, position = [], head
for module in modules:
    offsets.append(position)
    position += len(module)
    position = (position + 3) & ~3

blob = bytearray(position)
struct.pack_into('<I', blob, 0, count)
for index, offset in enumerate(offsets):
    struct.pack_into('<I', blob, 4 + 4 * index, offset)
cursor = head
for module in modules:
    blob[cursor:cursor + len(module)] = module
    cursor = (cursor + len(module) + 3) & ~3
out.write_bytes(blob)
print(f'{out}: {len(blob)} bytes, {count} modules at {[hex(o) for o in offsets]}')
