#!/usr/bin/env python3
"""Recompute a Mach-O's CodeDirectory page hashes after the file was edited.

Dropping LC_LOAD_DYLINKER / LC_DYLD_CHAINED_FIXUPS / LC_DYLD_EXPORTS_TRIE (see
strip_dynamic_load.py) changes page 0, which invalidates the CodeDirectory ld64.lld
wrote.  A trust-cache entry only says "this CDHash may run"; the kernel still
verifies every page against the CodeDirectory, so the edit alone turns the image
into OS_REASON_EXEC / EXEC_EXIT_REASON_BAD_MACHO.

This rewrites the page hashes in place (the CodeDirectory is big-endian, the slots
start at hashOffset and cover codeLimit in pageSize units, the last page
zero-padded), then prints the new CDHash so the trust cache can be regenerated.

    resign_pages.py in.macho out.macho [out.trustcache]
"""
import hashlib
import struct
import sys
import uuid
import pathlib

CSMAGIC_EMBEDDED_SIGNATURE = 0xFADE0CC0
CSMAGIC_CODEDIRECTORY = 0xFADE0C02
LC_CODE_SIGNATURE = 0x1D

data = bytearray(pathlib.Path(sys.argv[1]).read_bytes())
(magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, _) = struct.unpack_from('<IIIIIIII', data, 0)
assert magic == 0xFEEDFACF, 'not a 64-bit Mach-O'

signature = None
offset = 32
for _ in range(ncmds):
    cmd, cmdsize = struct.unpack_from('<II', data, offset)
    if cmd == LC_CODE_SIGNATURE:
        _, _, dataoff, datasize = struct.unpack_from('<4I', data, offset)
        signature = (dataoff, datasize)
    offset += cmdsize
if signature is None:
    raise SystemExit('no LC_CODE_SIGNATURE to refresh')

dataoff, datasize = signature
blob = bytearray(data[dataoff:dataoff + datasize])
(sign_magic, sign_length, count) = struct.unpack_from('>3I', blob, 0)
assert sign_magic == CSMAGIC_EMBEDDED_SIGNATURE, hex(sign_magic)

cd_slot = None
for index in range(count):
    slot_type, slot_offset = struct.unpack_from('>II', blob, 12 + 8 * index)
    if slot_type == 0:
        cd_slot = slot_offset
assert cd_slot is not None, 'no CodeDirectory'

(cd_magic, cd_length, version, cd_flags, hash_offset, ident_offset, n_special, n_code,
 code_limit, hash_size, hash_type, platform, page_size) = struct.unpack_from('>9I4B', blob, cd_slot)
assert cd_magic == CSMAGIC_CODEDIRECTORY
assert hash_type in (1, 2), hash_type
assert page_size >= 9, 'unsupported page size'

block = 1 << page_size
hasher = hashlib.sha256 if hash_type == 2 else hashlib.sha1

refreshed = 0
for slot in range(n_code):
    start = slot * block
    page = bytes(data[start:start + block])
    if len(page) < block:
        page += b'\x00' * (block - len(page))
    digest = hasher(page).digest()
    at = cd_slot + hash_offset + slot * hash_size
    blob[at:at + hash_size] = digest
    refreshed += 1

data[dataoff:dataoff + datasize] = blob
pathlib.Path(sys.argv[2]).write_bytes(bytes(data))

cd = bytes(blob[cd_slot:cd_slot + cd_length])
cdhash = hasher(cd).digest()[:20]
print(f'{sys.argv[2]}: refreshed {refreshed} page hashes '
      f'(CD v{version:#x}, hashType {hash_type}, page {block}, codeLimit {code_limit:#x})')
print(f'  cdhash {cdhash.hex()}')

if len(sys.argv) > 3:
    identifier = cd[ident_offset:cd.index(b'\x00', ident_offset)].decode('latin-1')
    module = (struct.pack('<I', 2) + uuid.uuid5(uuid.NAMESPACE_URL, identifier).bytes +
              struct.pack('<I', 1) + cdhash + bytes([hash_type, 0, 0, 0]))
    bundle = struct.pack('<2I', 1, 8) + module
    pathlib.Path(sys.argv[3]).write_bytes(bundle)
    print(f'  {sys.argv[3]}: bundle {len(bundle)} B, module v2, 1 entry')
