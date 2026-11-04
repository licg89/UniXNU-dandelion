#!/usr/bin/env python3
"""Give the fixture the classic static-entry form: LC_UNIXTHREAD, not LC_MAIN.

XNU's Mach-O loader keeps `result->using_lcmain` for the dyld path: a static image
that only carries LC_MAIN has no thread state to enter with, and load_machfile
ends as LOAD_FAILURE (measured on the phone: "DANDELION-EXEC: load_machfile lret=4
errno=85", errno 85 = EBADEXEC).  A static MH_EXECUTE is supposed to carry
LC_UNIXTHREAD with ARM_THREAD_STATE64 instead: that is what ld64 emits for
`-static`, and what load_unixthread() consumes.

So: drop LC_MAIN, append LC_UNIXTHREAD whose pc is __TEXT.vmaddr + entryoff, and
clear MH_PIE so the entry needs no slide.  The load commands grow into the padding
between the header and the first section's file contents.

    make_unixthread_exec.py in.macho out.macho
"""
import struct
import sys
import pathlib

LC_SEGMENT_64 = 0x19
LC_MAIN = 0x80000028
LC_UNIXTHREAD = 0x5
ARM_THREAD_STATE64 = 6
MH_PIE = 0x200000
ARM64_THREAD_WORDS = 68          # count is in 32-bit words: 34 uint64_t
ARM64_THREAD_SLOTS = 34          # x0-x28, fp, lr, sp, pc, cpsr
SECTION_ENTRY = 80               # sizeof(struct section_64)

data = bytearray(pathlib.Path(sys.argv[1]).read_bytes())
(magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, _) = struct.unpack_from('<IIIIIIII', data, 0)
assert magic == 0xFEEDFACF, 'not a 64-bit Mach-O'

text_vmaddr = None
entry_off = None
first_content = 1 << 62
kept = bytearray()
offset = 32
for _ in range(ncmds):
    cmd, cmdsize = struct.unpack_from('<II', data, offset)
    command = data[offset:offset + cmdsize]
    if cmd == LC_SEGMENT_64:
        segname = bytes(command[8:24]).rstrip(b'\0')
        vmaddr = struct.unpack_from('<Q', command, 24)[0]
        if segname == b'__TEXT':
            text_vmaddr = vmaddr
        nsects = struct.unpack_from('<I', command, 64)[0]
        for index in range(nsects):
            (section_offset,) = struct.unpack_from('<I', command, 72 + index * SECTION_ENTRY + 48)
            if section_offset:
                first_content = min(first_content, section_offset)
    if cmd == LC_MAIN:
        entry_off = struct.unpack_from('<Q', command, 8)[0]
        offset += cmdsize
        continue
    kept += command
    offset += cmdsize
assert offset == 32 + sizeofcmds, 'load command area does not add up'
assert entry_off is not None, 'no LC_MAIN to convert'
assert text_vmaddr is not None, 'no __TEXT segment'

entry = text_vmaddr + entry_off
state = [0] * ARM64_THREAD_SLOTS
state[32] = entry            # pc
state[33] = 0                # cpsr: EL0t, interrupts enabled
body = struct.pack('<%dQ' % len(state), *state)
thread = struct.pack('<IIII', LC_UNIXTHREAD, 16 + len(body),
                     ARM_THREAD_STATE64, ARM64_THREAD_WORDS) + body

region = len(kept) + len(thread)
if 32 + region > first_content:
    raise SystemExit(f'load commands would reach the section data: 32 + {region} > {first_content:#x}')

span = max(sizeofcmds, region)
data[32:32 + span] = bytes(kept) + thread + b'\x00' * (span - region)
struct.pack_into('<I', data, 16, ncmds)
struct.pack_into('<I', data, 20, region)
struct.pack_into('<I', data, 24, flags & ~MH_PIE)
pathlib.Path(sys.argv[2]).write_bytes(bytes(data))

print(f'{sys.argv[1]} -> {sys.argv[2]}: LC_MAIN entryoff {entry_off:#x} -> '
      f'LC_UNIXTHREAD pc {entry:#x}; sizeofcmds {sizeofcmds} -> {region} '
      f'(content starts {first_content:#x}); flags {flags:#x} -> {flags & ~MH_PIE:#x}')
