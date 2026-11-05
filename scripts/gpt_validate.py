#!/usr/bin/env python3
"""Validate the GPT the editor produced (paths match make_ios_partition.py)."""
import pathlib
import struct
import zlib

SECTOR = 512
ENTRY_BYTES = 128
COUNT = 128
work = pathlib.Path('/tmp/gpt-dry')

for name in ('primary', 'backup'):
    blob = (work / f'out-{name}.bin').read_bytes()
    hdr_at = SECTOR if name == 'primary' else 32 * SECTOR
    hdr = bytearray(blob[hdr_at:hdr_at + SECTOR])
    stored = struct.unpack_from('<I', hdr, 16)[0]
    struct.pack_into('<I', hdr, 16, 0)
    crc_ok = (zlib.crc32(bytes(hdr[:92])) & 0xFFFFFFFF) == stored
    ent_lba = struct.unpack_from('<Q', hdr, 72)[0]
    ent_crc = struct.unpack_from('<I', hdr, 88)[0]
    base = 0 if name == 'primary' else 0
    table = blob[(ent_lba * SECTOR) - (hdr_at - SECTOR) if name == 'primary' else 0:0]
    print(f'{name}: header CRC ok {crc_ok}, header says entries at LBA {ent_lba}, entries CRC {ent_crc:#x}')

primary = (work / 'out-primary.bin').read_bytes()
hdr = primary[SECTOR:2 * SECTOR]
ent_lba = struct.unpack_from('<Q', hdr, 72)[0]
ent_crc = struct.unpack_from('<I', hdr, 88)[0]
table = primary[ent_lba * SECTOR:ent_lba * SECTOR + COUNT * ENTRY_BYTES]
print('primary entries CRC ok:', (zlib.crc32(table) & 0xFFFFFFFF) == ent_crc)

backup = (work / 'out-backup.bin').read_bytes()
btable = backup[0:COUNT * ENTRY_BYTES]
bhdr = backup[32 * SECTOR:33 * SECTOR]
b_ent_crc = struct.unpack_from('<I', bhdr, 88)[0]
print('backup entries CRC ok:', (zlib.crc32(btable) & 0xFFFFFFFF) == b_ent_crc)
print('primary and backup tables identical:', table == btable)

for index in range(3):
    at = index * ENTRY_BYTES
    label = table[at + 56:at + 56 + 72].decode('utf-16-le').split('\0')[0]
    if not label:
        continue
    first, last = struct.unpack_from('<QQ', table, at + 32)
    print(f'  [{index}] {label:10s} {first}..{last}  {(last - first + 1) * SECTOR / 2**30:.1f} GiB')
