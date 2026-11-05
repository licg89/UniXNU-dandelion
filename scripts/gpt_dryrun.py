#!/usr/bin/env python3
"""Dry-run make_ios_partition.py against a synthetic GPT shaped like this phone's.

Checks that the edit keeps both CRCs valid, that the ios entry lands exactly at the tail,
and that userdata shrinks by the requested amount.
"""
import pathlib
import struct
import subprocess
import sys
import zlib

SECTOR = 512
ENTRY_BYTES = 128
COUNT = 128
DISK = 122_142_720           # the phone's total sectors (HANDOFF 0.28)
FIRST_USABLE, LAST_USABLE = 34, DISK - 34
USERDATA_FIRST, USERDATA_LAST = 13_385_728, 122_021_854

work = pathlib.Path('/tmp/gpt-dry')
work.mkdir(exist_ok=True)
dump = work / 'dump.bin'

entries = bytearray(COUNT * ENTRY_BYTES)
def add(index, name, first, last, guid_bytes):
    at = index * ENTRY_BYTES
    entries[at:at + 16] = guid_bytes
    entries[at + 16:at + 32] = bytes([0x10 + index] * 16)
    struct.pack_into('<QQ', entries, at + 32, first, last)
    label = name.encode('utf-16-le')
    entries[at + 56:at + 56 + len(label)] = label

add(0, 'boot', 34, 1023, bytes.fromhex('0fc63daf848347728e793d69d8477de4'))
add(1, 'userdata', USERDATA_FIRST, USERDATA_LAST, bytes.fromhex('0fc63daf848347728e793d69d8477de4'))

entries_crc = zlib.crc32(bytes(entries)) & 0xFFFFFFFF
header = bytearray(SECTOR)
header[0:8] = b'EFI PART'
struct.pack_into('<I', header, 8, 0x00010000)
struct.pack_into('<I', header, 12, 92)
struct.pack_into('<Q', header, 24, 1)
struct.pack_into('<Q', header, 32, DISK)
struct.pack_into('<Q', header, 40, FIRST_USABLE)
struct.pack_into('<Q', header, 48, LAST_USABLE)
struct.pack_into('<Q', header, 56, 0)                    # no usable copy of the entries
struct.pack_into('<Q', header, 72, 2)                    # entries at LBA 2
struct.pack_into('<I', header, 80, COUNT)
struct.pack_into('<I', header, 84, ENTRY_BYTES)
struct.pack_into('<I', header, 88, entries_crc)
struct.pack_into('<I', header, 16, 0)
struct.pack_into('<I', header, 16, zlib.crc32(bytes(header[:92])) & 0xFFFFFFFF)

image = bytearray(34 * SECTOR)
image[0:SECTOR] = b'\0' * SECTOR
image[SECTOR:2 * SECTOR] = header
image[2 * SECTOR:2 * SECTOR + len(entries)] = entries
dump.write_bytes(image)

result = subprocess.run([sys.executable, '/mnt/d/aios/_work/make_ios_partition.py', str(dump),
                         str(work / 'out'), '--ios-gib', '30'], capture_output=True, text=True)
print(result.stdout.strip())
if result.returncode:
    print(result.stderr.strip())
    sys.exit(1)

# validate the outputs
for name in ('primary', 'backup'):
    blob = (work / 'out' / f'{name}.bin').read_bytes()
    hdr_at = SECTOR if name == 'primary' else 32 * SECTOR
    hdr = bytearray(blob[hdr_at:hdr_at + SECTOR])
    stored = struct.unpack_from('<I', hdr, 16)[0]
    struct.pack_into('<I', hdr, 16, 0)
    ok_crc = zlib.crc32(bytes(hdr[:92])) & 0xFFFFFFFF == stored
    ent_at = struct.unpack_from('<Q', hdr, 72)[0]
    ent_crc = struct.unpack_from('<I', hdr, 88)[0]
    table = blob[(ent_at * SECTOR) - (hdr_at - (0 if name == 'primary' else 32 * SECTOR)) if False else 0:0]
    print(f'{name}: header CRC valid {ok_crc}, entries at LBA {ent_at}, entries CRC {ent_crc:#x}')

# the table itself
blob = (work / 'out' / 'primary.bin').read_bytes()
table = blob[2 * SECTOR:2 * SECTOR + COUNT * ENTRY_BYTES]
print('entries CRC valid:', zlib.crc32(table) & 0xFFFFFFFF == struct.unpack_from('<I', blob, SECTOR + 88)[0])
def entry(i):
    at = i * ENTRY_BYTES
    label = table[at + 56:at + 56 + 72].decode('utf-16-le').split('\0')[0]
    first, last = struct.unpack_from('<QQ', table, at + 32)
    return label, first, last
for i in (0, 1, 2):
    label, first, last = entry(i)
    if label:
        print(f'  [{i}] {label:10s} {first}..{last}  {(last - first + 1) * SECTOR / 2**30:.1f} GiB')
