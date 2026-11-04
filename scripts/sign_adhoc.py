#!/usr/bin/env python3
"""Ad-hoc sign an arm64 Mach-O and emit the trust cache entry for it.

The kernel killed our fixture with "unexpected SIGKILL of init with reason --
namespace 9 code 0x1", and the two boot arguments that looked like escapes are
dead ends: `cs_enforcement_disable=1` is itself refused ("can't has
cs_enforcement_disable" @AppleMobileFileIntegrity.cpp:5261 -- it panics), and
`amfi_get_out_of_my_way=1` alone did not help.  What AMFI wants is a code
directory it can hash, plus (for a binary it has never seen) an entry in the trust
cache it is handed at boot.

The framework's own flow gets both from macOS codesign
(build-native-user-fixture.py calls codesign and writes trustcache.bundle).  This
host is Linux, so this script builds the same two artifacts:

  * LC_CODE_SIGNATURE + a CodeDirectory (version 0x20100, SHA-256, 4 KiB pages,
    CS_ADHOC) appended to the image, with every page hash filled in;
  * the CDHash the kernel will compute (first 20 bytes of SHA-256 over the
    CodeDirectory blob) and a `struct trust_cache_module1` containing it, matching
    osfmk/kern/trustcache.h:

        struct trust_cache_entry1 { uint8_t cdhash[20]; uint8_t hash_type; uint8_t flags; };
        struct trust_cache_module1 { uint32_t version; uuid_t uuid; uint32_t num_entries;
                                     struct trust_cache_entry1 entries[]; };

    hash_type 2 is CS_HASHTYPE_SHA256.

    sign_adhoc.py in.macho out.macho out.trustcache
"""
import hashlib
import struct
import sys
import uuid
import pathlib

CSMAGIC_EMBEDDED_SIGNATURE = 0xFADE0CC0
CSMAGIC_CODEDIRECTORY = 0xFADE0C02
CSSLOT_CODEDIRECTION = 0
CS_ADHOC = 0x00000002
CS_HASHTYPE_SHA256 = 2
LC_CODE_SIGNATURE = 0x1D
PAGE = 4096
IDENTIFIER = b'org.unixnu.dandelion.mockfs-fixture'


def load_commands(data):
    (magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags,
     reserved) = struct.unpack_from('<IIIIIIII', data, 0)
    assert magic == 0xFEEDFACF, 'not a 64-bit Mach-O'
    commands, offset = [], 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from('<II', data, offset)
        commands.append((cmd, offset, cmdsize))
        offset += cmdsize
    return (ncmds, sizeofcmds, flags, commands, offset)


def main(source, target, trust_out):
    data = bytearray(pathlib.Path(source).read_bytes())
    ncmds, sizeofcmds, flags, commands, end = load_commands(data)

    # The signature goes after everything already in the file, and the hashed
    # region ends where the signature starts.
    signature_offset = (len(data) + PAGE - 1) & ~(PAGE - 1)
    data.extend(b'\x00' * (signature_offset - len(data)))
    code_limit = signature_offset

    pages = [data[i:i + PAGE] for i in range(0, code_limit, PAGE)]
    page_hashes = [hashlib.sha256(page).digest() for page in pages]

    identifier = IDENTIFIER + b'\x00'
    header_size = 44                      # 0x20100 layout
    hash_offset = header_size + len(identifier)
    length = hash_offset + 32 * len(page_hashes)
    # 0x20100 header: 9 words, 4 bytes, one spare word = 44 bytes.
    directory = struct.pack('<9I4BI', CSMAGIC_CODEDIRECTORY, length, 0x20100, CS_ADHOC,
                            hash_offset, header_size, 0, len(page_hashes), code_limit,
                            32, CS_HASHTYPE_SHA256, 0, 12, 0)
    assert len(directory) == header_size, len(directory)
    code_directory = directory + identifier + b''.join(page_hashes)

    # SuperBlob: magic, length, count, then one {type, offset} index entry.
    index_offset = 12 + 8
    super_blob = (struct.pack('<3I', CSMAGIC_EMBEDDED_SIGNATURE,
                              12 + 8 + len(code_directory), 1) +
                  struct.pack('<II', CSSLOT_CODEDIRECTION, index_offset) + code_directory)

    # LC_CODE_SIGNATURE has to fit in the padding before the first section.
    if 32 + sizeofcmds + 16 > min(off for _, off, _ in commands if off > 32):
        raise SystemExit('no room for LC_CODE_SIGNATURE in the load command area')

    data.extend(super_blob)
    command = struct.pack('<4I', LC_CODE_SIGNATURE, 16, signature_offset, len(super_blob))
    # insert the command right after the last existing one
    at = 32 + sizeofcmds
    data[at:at + 16] = command
    struct.pack_into('<II', data, 16, ncmds + 1, sizeofcmds + 16)

    pathlib.Path(target).write_bytes(bytes(data))

    cdhash = hashlib.sha256(code_directory).digest()[:20]
    module = struct.pack('<I16sI', 1, uuid.uuid5(uuid.NAMESPACE_URL, 'dandelion-mockfs-fixture').bytes,
                         1) + cdhash + bytes([CS_HASHTYPE_SHA256, 0])
    pathlib.Path(trust_out).write_bytes(module)

    print(f'{target}: CodeDirectory {len(code_directory)} B, {len(page_hashes)} pages, '
          f'codeLimit {code_limit:#x}, signature at {signature_offset:#x}')
    print(f'  cdhash {cdhash.hex()}')
    print(f'{trust_out}: trust_cache_module1, version 1, 1 entry, hash_type '
          f'{CS_HASHTYPE_SHA256}, {len(module)} bytes')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3])
