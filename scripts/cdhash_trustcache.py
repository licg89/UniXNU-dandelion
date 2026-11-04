#!/usr/bin/env python3
"""Read the CodeDirectory a Mach-O already carries, and emit the trust-cache bundle
the loader accepts.

ld64.lld signs arm64 output ad-hoc by default, so the fixture the linker produced
already has a valid CodeDirectory: there is no need to hand-build a signature, only
to compute the CDHash the kernel will compute and put it in the bundle DarwinBoot
validates (DarwinBootPkg/Driver/DarwinBoot.c:ValidTrustCaches):

    uint32 count;
    uint32 offset[count];              // >= 4 + count*4, 4-byte aligned, ascending
    per module at Start..End:
        uint32 version == 2
        uuid_t uuid
        uint32 num_entries             // at Start + 20
        num_entries * 24 bytes:        // End - Start == 24 + entries*24
            uint8 cdhash[20]; uint8 hash_type (1..4); uint8 flags[3]
        entries sorted ascending by cdhash

The kernel-side `struct trust_cache_module1` in osfmk/kern/trustcache.h has its own
(packed, version 1, 22-byte entry) layout; that is not what crosses the loader
boundary, which is why a version-1 file was refused with "boot refused:
Compromised Data".

    cdhash_trustcache.py in.macho out.trustcache
"""
import hashlib
import struct
import sys
import uuid
import pathlib

CSMAGIC_EMBEDDED_SIGNATURE = 0xFADE0CC0
CSMAGIC_CODEDIRECTORY = 0xFADE0C02
CS_HASHTYPE_SHA1 = 1
CS_HASHTYPE_SHA256 = 2
LC_CODE_SIGNATURE = 0x1D
MODULE_VERSION = 2
ENTRY_SIZE = 24

data = pathlib.Path(sys.argv[1]).read_bytes()
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
    raise SystemExit(f'{sys.argv[1]}: no LC_CODE_SIGNATURE; link with -adhoc_codesign')

dataoff, datasize = signature
blob = data[dataoff:dataoff + datasize]
# Code-signing blobs are big-endian; the Mach-O header is not.
(sign_magic, sign_length, count) = struct.unpack_from('>3I', blob, 0)
assert sign_magic == CSMAGIC_EMBEDDED_SIGNATURE, hex(sign_magic)

best = None
for index in range(count):
    slot_type, slot_offset = struct.unpack_from('>II', blob, 12 + 8 * index)
    if slot_type != 0:
        continue
    at = slot_offset
    (cd_magic, cd_length, version, cd_flags, hash_offset, ident_offset, n_special, n_code,
     code_limit, hash_size, hash_type, platform, page_size) = struct.unpack_from('>9I4B', blob, at)
    assert cd_magic == CSMAGIC_CODEDIRECTORY, hex(cd_magic)
    if best is None or version > best[0]:
        best = (version, blob[at:at + cd_length], hash_type, page_size, n_code, cd_flags, ident_offset)

version, cd, hash_type, page_size, n_code, cd_flags, ident_offset = best
identifier = cd[ident_offset:cd.index(b'\x00', ident_offset)].decode('latin-1')
cdhash = (hashlib.sha256(cd).digest() if hash_type == CS_HASHTYPE_SHA256
          else hashlib.sha1(cd).digest())[:20]

module_uuid = uuid.uuid5(uuid.NAMESPACE_URL, identifier).bytes
module = (struct.pack('<I', MODULE_VERSION) + module_uuid + struct.pack('<I', 1) +
          cdhash + bytes([hash_type, 0, 0, 0]))
assert len(module) == 24 + ENTRY_SIZE, len(module)

offset_table = 4 + 4
bundle = struct.pack('<2I', 1, offset_table) + module
pathlib.Path(sys.argv[2]).write_bytes(bundle)

print(f'{sys.argv[1]}: CodeDirectory v{version:#x}, {len(cd)} B, hashType {hash_type}, '
      f'page {1 << page_size}, {n_code} slots, flags {cd_flags:#x}')
print(f'  identifier {identifier}')
print(f'  cdhash {cdhash.hex()}')
print(f'{sys.argv[2]}: bundle {len(bundle)} B: count 1, module v{MODULE_VERSION} at '
      f'{offset_table}, 1 entry')
