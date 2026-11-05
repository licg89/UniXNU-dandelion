#!/usr/bin/env python3
"""Carve a dedicated `ios` partition out of the tail of `userdata`.

Why: the SHNGROOT manifest has to sit at a fixed offset, and at the start of userdata it
collides with the Android F2FS superblock -- every iOS test therefore needed a userdata
flash and an Android re-setup.  With its own GPT partition the iOS data area is separate:
Android keeps its filesystem, and the driver reads `ios` in preference to `userdata`
(NativeMsdc.h gptFindData).

    make_ios_partition.py <gpt-dump> <output-prefix> [--ios-gib 30]

The dump is sectors 0..33 of the disk (primary GPT).  The script writes
<prefix>-primary.bin (34 sectors from LBA 0) and <prefix>-backup.bin (33 sectors that go
to last_lba-32..last_lba), so both copies stay consistent.
"""
import argparse
import pathlib
import struct
import sys
import zlib

SECTOR = 512
ENTRY_BYTES = 128
SECTORS = 34

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('dump', type=pathlib.Path, help='sectors 0..33 of the disk')
parser.add_argument('prefix', type=pathlib.Path)
parser.add_argument('--ios-gib', type=float, default=30.0)
parser.add_argument('--name', default='ios')
args = parser.parse_args()

data = args.dump.read_bytes()
if len(data) < SECTORS * SECTOR:
    sys.exit(f'dump is {len(data)} bytes, expected at least {SECTORS * SECTOR}')
if data[SECTOR:SECTOR + 8] != b'EFI PART':
    sys.exit('no GPT header at LBA 1')

header = bytearray(data[SECTOR:2 * SECTOR])
disk_lba = struct.unpack_from('<Q', header, 32)[0]
last_usable = struct.unpack_from('<Q', header, 48)[0]
entries_lba = struct.unpack_from('<Q', header, 72)[0]
entry_count = struct.unpack_from('<I', header, 80)[0]
entry_bytes = struct.unpack_from('<I', header, 84)[0]
entries_crc = struct.unpack_from('<I', header, 88)[0]
print(f'disk {disk_lba} sectors, usable {last_usable}, entries at {entries_lba} '
      f'({entry_count} x {entry_bytes}), entries CRC {entries_crc:#x}')

entries = bytearray(data[entries_lba * SECTOR:(entries_lba + 32) * SECTOR])
if zlib.crc32(bytes(entries[:entry_count * entry_bytes])) & 0xFFFFFFFF != entries_crc:
    sys.exit('the dump is not a consistent primary GPT (entries CRC mismatch)')

def find(name):
    for index in range(entry_count):
        at = index * entry_bytes
        if not any(entries[at:at + 16]):
            continue
        label = bytes(entries[at + 56:at + 56 + 72]).decode('utf-16-le').split('\0')[0]
        if label == name:
            return index, struct.unpack_from('<QQ', entries, at + 32)
    return None, None

index, bounds = find('userdata')
if index is None:
    sys.exit('no userdata partition in the dump')
first, last = bounds
print(f'userdata: entries[{index}] sectors {first}..{last} ({(last - first + 1) * SECTOR / 2**30:.1f} GiB)')

ios_sectors = int(args.ios_gib * 2**30 / SECTOR)
if ios_sectors >= last - first + 1:
    sys.exit('the requested ios size does not fit')
new_last = last - ios_sectors
if find(args.name)[0] is not None:
    sys.exit(f'a partition named {args.name} already exists')

# shrink userdata
struct.pack_into('<QQ', entries, index * entry_bytes + 32, first, new_last)

# add the ios entry in the first free slot
free = next(i for i in range(entry_count) if not any(entries[i * entry_bytes:(i + 1) * entry_bytes]))
at = free * entry_bytes
entries[at:at + 16] = bytes.fromhex('0fc63daf848347728e793d69d8477de4')   # Linux filesystem data
entries[at + 16:at + 32] = bytes(range(1, 17))                             # unique GUID (fixed, documented)
struct.pack_into('<QQ', entries, at + 32, new_last + 1, last)
struct.pack_into('<Q', entries, at + 48, 0)
label = args.name.encode('utf-16-le')
entries[at + 56:at + 56 + len(label)] = label
print(f'{args.name}: entries[{free}] sectors {new_last + 1}..{last} '
      f'({(last - new_last) * SECTOR / 2**30:.1f} GiB)')

new_entries_crc = zlib.crc32(bytes(entries[:entry_count * entry_bytes])) & 0xFFFFFFFF
struct.pack_into('<I', header, 88, new_entries_crc)

# primary header: itself at LBA 1, backup at last_lba, entries right after it
struct.pack_into('<Q', header, 24, 1)
struct.pack_into('<Q', header, 32, disk_lba)
struct.pack_into('<Q', header, 72, entries_lba)
struct.pack_into('<I', header, 16, 0)
header_crc = zlib.crc32(bytes(header[:struct.unpack_from('<I', header, 12)[0]])) & 0xFFFFFFFF
struct.pack_into('<I', header, 16, header_crc)

primary = bytearray(SECTORS * SECTOR)
primary[SECTOR:2 * SECTOR] = header
primary[entries_lba * SECTOR:entries_lba * SECTOR + len(entries)] = entries
(args.prefix.parent / f'{args.prefix.name}-primary.bin').write_bytes(primary)

# backup: entries at disk_lba-32, header at disk_lba
backup = bytearray(SECTORS * SECTOR)
backup_header = bytearray(header)
struct.pack_into('<Q', backup_header, 24, disk_lba)
struct.pack_into('<Q', backup_header, 72, disk_lba - 32)
struct.pack_into('<I', backup_header, 16, 0)
backup_crc = zlib.crc32(bytes(backup_header[:struct.unpack_from('<I', backup_header, 12)[0]])) & 0xFFFFFFFF
struct.pack_into('<I', backup_header, 16, backup_crc)
backup[0:ENTRY_BYTES * entry_count] = entries[:ENTRY_BYTES * entry_count]
backup[32 * SECTOR:33 * SECTOR] = backup_header
(args.prefix.parent / f'{args.prefix.name}-backup.bin').write_bytes(backup)

print(f'wrote {args.prefix}-primary.bin ({SECTORS} sectors from LBA 0)')
print(f'wrote {args.prefix}-backup.bin  (sectors {disk_lba - 32}..{disk_lba})')
