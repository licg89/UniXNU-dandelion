#!/usr/bin/env python3
"""Strip the dynamic-loader commands from the fixture so the kernel loads it statically.

ld64.lld cannot produce a static image ("Option `-static' is not yet implemented"),
so besides LC_LOAD_DYLINKER it also writes LC_DYLD_CHAINED_FIXUPS and
LC_DYLD_EXPORTS_TRIE.  A static MH_EXECUTE has no dyld to apply those fixups, and
the kernel's loader reports the image as unusable -- which reaches userspace as
OS_REASON_EXEC / EXEC_EXIT_REASON_BAD_MACHO, the SIGKILL the phone showed.

Dropping the two fixup commands as well as the dylinker leaves segments, LC_MAIN
and the symbol table: an image the kernel can map and enter directly.
"""
import struct
import sys
import pathlib

DROP = {
    0x0E: 'LC_LOAD_DYLINKER',
    0x80000034: 'LC_DYLD_CHAINED_FIXUPS',
    0x80000033: 'LC_DYLD_EXPORTS_TRIE',
    0x80000035: 'LC_DYLD_INFO_ONLY',
    0x22: 'LC_DYLD_INFO',
}
MH_DYLDLINK = 0x4

data = bytearray(pathlib.Path(sys.argv[1]).read_bytes())
(magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, _) = struct.unpack_from('<IIIIIIII', data, 0)
assert magic == 0xFEEDFACF, 'not a 64-bit Mach-O'

kept, dropped = bytearray(), []
offset = 32
for _ in range(ncmds):
    cmd, cmdsize = struct.unpack_from('<II', data, offset)
    if cmd in DROP:
        dropped.append((DROP[cmd], cmdsize))
    else:
        kept += data[offset:offset + cmdsize]
    offset += cmdsize
assert offset == 32 + sizeofcmds, 'load command area does not add up'

if not dropped:
    pathlib.Path(sys.argv[2]).write_bytes(bytes(data))
    print(f'{sys.argv[1]}: nothing to strip')
    sys.exit(0)

region = len(kept)
if region > sizeofcmds:
    raise SystemExit('load commands grew; refusing')
data[32:32 + sizeofcmds] = bytes(kept) + b'\x00' * (sizeofcmds - region)
struct.pack_into('<II', data, 16, ncmds - len(dropped), region)
new_flags = flags & ~MH_DYLDLINK
struct.pack_into('<I', data, 24, new_flags)
pathlib.Path(sys.argv[2]).write_bytes(bytes(data))
print(f'{sys.argv[1]} -> {sys.argv[2]}: dropped {dropped}, ncmds {ncmds} -> {ncmds - len(dropped)}, '
      f'flags {flags:#x} -> {new_flags:#x}')
