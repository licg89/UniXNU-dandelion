#!/usr/bin/env python3
"""Turn the linked fixture into a static executable the kernel will run directly.

`ld64.lld -static` is not implemented ("Option `-static' is not yet implemented"),
so the linker still writes a dynamic image: LC_LOAD_DYLINKER /usr/lib/dyld and
MH_DYLDLINK.  The kernel then wants dyld, mockfs has none, and exec fails as
OS_REASON_EXEC / EXEC_EXIT_REASON_BAD_MACHO -- which is exactly the SIGKILL the
phone showed ("namespace 9 code 0x1"; namespace 9 is OS_REASON_EXEC, not
codesigning).

Dropping LC_LOAD_DYLINKER and clearing MH_DYLDLINK leaves a plain static
MH_EXECUTE with LC_MAIN, which XNU maps and enters at the entry point.

The signature is left untouched, so the CDHash -- and therefore the trust cache
entry -- stays the same; page hashes no longer cover these bytes, which is what the
trust cache is for.

    make_static_exec.py in.macho out.macho
"""
import struct
import sys
import pathlib

LC_LOAD_DYLINKER = 0x0E
MH_DYLDLINK = 0x4
MH_TWOLEVEL = 0x80

data = bytearray(pathlib.Path(sys.argv[1]).read_bytes())
(magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, _) = struct.unpack_from('<IIIIIIII', data, 0)
assert magic == 0xFEEDFACF, 'not a 64-bit Mach-O'

start = 32
kept, dropped = bytearray(), []
offset = start
for _ in range(ncmds):
    cmd, cmdsize = struct.unpack_from('<II', data, offset)
    if cmd == LC_LOAD_DYLINKER:
        dropped.append(cmdsize)
    else:
        kept += data[offset:offset + cmdsize]
    offset += cmdsize
assert offset == start + sizeofcmds, 'load command area does not add up'

if not dropped:
    pathlib.Path(sys.argv[2]).write_bytes(bytes(data))
    print(f'{sys.argv[1]}: already static (no LC_LOAD_DYLINKER)')
    sys.exit(0)

region = len(kept)
if region > sizeofcmds:
    raise SystemExit('load commands grew; refusing')

data[start:start + sizeofcmds] = bytes(kept) + b'\x00' * (sizeofcmds - region)
struct.pack_into('<II', data, 16, ncmds - len(dropped), region)
new_flags = flags & ~MH_DYLDLINK
struct.pack_into('<I', data, 24, new_flags)
pathlib.Path(sys.argv[2]).write_bytes(bytes(data))

print(f'{sys.argv[1]} -> {sys.argv[2]}: dropped LC_LOAD_DYLINKER ({dropped}), '
      f'ncmds {ncmds} -> {ncmds - len(dropped)}, flags {flags:#x} -> {new_flags:#x}')
