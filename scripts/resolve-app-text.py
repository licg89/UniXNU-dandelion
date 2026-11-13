#!/usr/bin/env python3
"""Resolve a trap address that lies outside the dyld shared cache, by finding its Mach-O.

A trap pc below the shared region lives in the process's own text.  `brk #imm` encodes the comment
(0xD4200000 | imm << 5), so the image that owns the pc must have such an opcode at
`pc - __TEXT.vmaddr` inside its __TEXT.  Searching the local iOS image for Mach-O files that satisfy
that condition names the binary without needing a loader base at all -- each candidate is tested
against its own vmaddr, which is exactly the base the kernel maps it at.

    python3 scripts/resolve-app-text.py --image <userdata-root.img> --pc 0x1005f8b0c --comment 1

`--log` instead walks a console text file and resolves every DANDELION-TEXT / DANDELION-BRK record
whose pc is below the shared region.  `--base` and `--path`, when the kernel printed them, narrow the
answer to the image the kernel actually found.
"""
import argparse
import pathlib
import re
import struct
import subprocess

MAGIC64 = b'\xcf\xfa\xed\xfe'
LC_SEGMENT_64 = 0x19
LC_UUID = 0x1b
LC_LOAD_DYLIB = 0x0c
LC_ID_DYLIB = 0x0d
TEXT_PC = re.compile(r'DANDELION-TEXT: pid=(-?\d+) comm=(\S+) pc=(0x[0-9a-fA-F]+) '
                     r'base=(0x[0-9a-fA-F]+) off=(0x[0-9a-fA-F]+) path=(.*)')
BRK_PC = re.compile(r'DANDELION-BRK: pid=(-?\d+) comm=(\S+) pc=(0x[0-9a-fA-F]+)'
                    r'(?:[^\n]*comment=(0x[0-9a-fA-F]+))?')
REGION_TOKENS = ('0x18', '0x19', '0x1a', '0x1b', '0x1c', '0x1d', '0x1e', '0x1f',
                 '0x20', '0x21', '0x22', '0x23', '0x24')


def brk_word(imm):
    return struct.pack('<I', 0xD4200000 | (imm << 5))


def scan_machos(image):
    """Yield file offsets of 64-bit Mach-O headers."""
    with open(image, 'rb') as fh:
        pos = 0
        overlap = b''
        while True:
            chunk = fh.read(1 << 24)
            if not chunk:
                return
            blob = overlap + chunk
            at = blob.find(MAGIC64)
            while at >= 0:
                yield pos - len(overlap) + at
                at = blob.find(MAGIC64, at + 1)
            overlap = blob[-(len(MAGIC64) - 1):]
            pos += len(chunk)


def parse_macho(fh, file_off, size):
    """Return a dict describing this Mach-O, or None if it does not look genuine."""
    fh.seek(file_off)
    head = fh.read(32)
    if len(head) < 32 or head[:4] != MAGIC64:
        return None
    _magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, _flags, _res = \
        struct.unpack('<IiiIIIII', head)
    if not (0 < ncmds < 4000) or not (24 < sizeofcmds < (8 << 20)):
        return None
    fh.seek(file_off + 32)
    cmds = fh.read(sizeofcmds)
    if len(cmds) < sizeofcmds:
        return None
    info = {'cputype': cputype, 'cpusubtype': cpusubtype & 0xffffff, 'filetype': filetype,
            'text': None, 'uuid': None, 'dylibs': []}
    off = 0
    count = 0
    while off + 8 <= len(cmds) and count < ncmds:
        cmd, cmdsize = struct.unpack_from('<II', cmds, off)
        if cmdsize < 8 or off + cmdsize > len(cmds):
            return None
        count += 1
        if cmd == LC_SEGMENT_64 and cmdsize >= 72:
            name = cmds[off + 8:off + 24].split(b'\0')[0].decode('latin-1', 'replace')
            vmaddr, vmsize, fileoff, filesize, _mp, _ip, _ns, _fl = \
                struct.unpack_from('<QQQQIIII', cmds, off + 24)
            if name == '__TEXT':
                info['text'] = (vmaddr, vmsize, fileoff, filesize)
        elif cmd == LC_UUID and cmdsize >= 24:
            info['uuid'] = cmds[off + 8:off + 24].hex()
        elif cmd in (LC_LOAD_DYLIB, LC_ID_DYLIB) and cmdsize >= 24:
            name_off = struct.unpack_from('<I', cmds, off + 8)[0]
            raw = cmds[off + name_off:off + cmdsize].split(b'\0')[0]
            info['dylibs'].append(raw.decode('latin-1', 'replace'))
        off += cmdsize
    if info['text'] is None:
        return None
    vmaddr, vmsize, fileoff, filesize = info['text']
    if not (0 <= fileoff < size) or filesize == 0:
        return None
    info['file_off'] = file_off
    return info


def find_at_pc(image, pc, comment, base_hint, path_hint, limit=8):
    """Mach-O files that carry a matching brk at pc - __TEXT.vmaddr inside __TEXT."""
    size = pathlib.Path(image).stat().st_size
    hits = []
    with open(image, 'rb') as fh:
        for file_off in scan_machos(image):
            if file_off + 32 > size:
                continue
            info = parse_macho(fh, file_off, size)
            if info is None:
                continue
            vmaddr, vmsize, text_fileoff, filesize = info['text']
            if base_hint is not None and vmaddr != base_hint:
                continue
            delta = pc - vmaddr
            if delta < 0 or delta + 4 > vmsize or delta + 4 > filesize:
                continue
            fh.seek(file_off + text_fileoff + delta)
            word = fh.read(4)
            if len(word) < 4:
                continue
            value = struct.unpack('<I', word)[0]
            if value >> 20 != 0xD42:
                continue                     # not a brk at all
            imm = (value >> 5) & 0xffff
            if comment is not None and imm != comment:
                continue
            info['imm'] = imm
            info['delta'] = delta
            if path_hint and any(path_hint in d for d in info['dylibs']):
                info['note'] = 'loads ' + path_hint
            hits.append(info)
            if len(hits) >= limit:
                break
    return hits


def disasm(image, info, span=0x40):
    vmaddr, _vmsize, text_fileoff, _fs = info['text']
    start = info['delta'] - span
    if start < 0:
        start = 0
    with open(image, 'rb') as fh:
        fh.seek(info['file_off'] + text_fileoff + start)
        code = fh.read(span * 2)
    tmp = pathlib.Path('/tmp/apptext.bin')
    tmp.write_bytes(code)
    out = subprocess.run(['aarch64-linux-gnu-objdump', '-D', '-b', 'binary', '-m', 'aarch64',
                          '--adjust-vma=%#x' % (vmaddr + start), str(tmp)],
                         capture_output=True, text=True).stdout
    return [l.rstrip() for l in out.splitlines() if ':\t' in l]


def report(image, pc, comment, base_hint, path_hint):
    print('\n=== pc %#x (comment %s) %s' % (pc, comment, 'base hint %#x' % base_hint if base_hint is not None else ''))
    hits = find_at_pc(image, pc, comment, base_hint, path_hint)
    if not hits:
        print('  no Mach-O in this image carries a matching brk at pc - __TEXT.vmaddr')
        return
    for info in hits:
        vmaddr, vmsize, _tf, filesize = info['text']
        print('  file offset %#x  cputype %d/%d filetype %d  __TEXT vmaddr %#x size %#x'
              % (info['file_off'], info['cputype'], info['cpusubtype'], info['filetype'],
                 vmaddr, vmsize))
        print('     uuid %s  brk #%d at +%#x' % (info['uuid'], info['imm'], info['delta']))
        if info['dylibs']:
            print('     links: %s' % ', '.join(d.split('/')[-1] for d in info['dylibs'][:6]))
        for line in disasm(image, info):
            print('     %s%s' % (line, '   <<< trap' if 'brk' in line else ''))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--image', required=True)
    ap.add_argument('--pc', default=None)
    ap.add_argument('--comment', default='1')
    ap.add_argument('--base', default=None)
    ap.add_argument('--path', default=None)
    ap.add_argument('--log', default=None)
    args = ap.parse_args()

    if args.log:
        text = pathlib.Path(args.log).read_bytes().decode('latin-1', 'replace')
        done = set()
        for pid, comm, pc, base, off, path in TEXT_PC.findall(text):
            key = (comm, pc)
            if key in done:
                continue
            done.add(key)
            print('\n## %s pid %s path %s' % (comm, pid, path.strip()))
            report(args.image, int(pc, 16), None, int(base, 16), path.strip())
        if not done:
            for pid, comm, pc, comment in BRK_PC.findall(text):
                value = int(pc, 16)
                if value >= 0x180000000:
                    continue
                key = (comm, pc)
                if key in done:
                    continue
                done.add(key)
                report(args.image, value,
                       int(comment, 16) if comment else None, None, None)
        return 0

    if not args.pc:
        raise SystemExit('give --pc or --log')
    report(args.image, int(args.pc, 16),
           int(args.comment, 16) if args.comment else None,
           int(args.base, 16) if args.base else None, args.path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
