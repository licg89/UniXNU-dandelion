#!/usr/bin/env python3
"""Decode a DDNLOG01 block written by the firmware's PayloadBootDxe.

Layout (little-endian, first sector of the block):

    0   "DDNLOG01"
    8   u32 version
    12  u32 text size in bytes
    16  u32 total sectors in the block
    20  u32 checksum: sum = sum * 31 + byte, over the text
    24  u64 LBA the block was written at
    512 text

The firmware moves this block by editing one constant, so never assume the
offset: pass --lba (dandelion's is 30512, inside the 'flashinfo' partition) and
the tool verifies the magic and the checksum before printing anything.

Usage:
    ddnlog.py IMAGE                       # scan the whole image for the magic
    ddnlog.py IMAGE --lba 30512           # only the block at that sector
    ddnlog.py IMAGE --lba 30512 --out fw.log
    ddnlog.py --self-test                 # build a block, decode it, compare
"""
import argparse
import struct
import sys

MAGIC = b"DDNLOG01"
SECTOR = 512
MAX_TEXT = 32 * 1024 * 1024


def find_blocks(data):
    """Yield (offset, header) for every valid block, scanning sector boundaries."""
    start = 0
    while True:
        index = data.find(MAGIC, start)
        if index < 0:
            return
        start = index + 1
        if index % SECTOR != 0 or len(data) - index < SECTOR:
            continue

        header = data[index:index + SECTOR]
        version, size, sectors, checksum = struct.unpack_from("<IIII", header, 8)
        lba = struct.unpack_from("<Q", header, 24)[0]

        if not (0 < size <= MAX_TEXT):
            continue
        if not (1 <= sectors <= 4096):
            continue
        if index + sectors * SECTOR > len(data):
            continue

        text = data[index + SECTOR:index + SECTOR + size]
        total = 0
        for byte in text:
            total = (total * 31 + byte) & 0xFFFFFFFF

        yield index, {
            "version": version,
            "size": size,
            "sectors": sectors,
            "checksum": checksum,
            "checksum_ok": total == checksum,
            "lba": lba,
            "text": text,
        }


def encode_block(text, lba):
    """The inverse of find_blocks, used by --self-test and by the round script."""
    if len(text) > (128 - 1) * SECTOR:
        raise ValueError("text does not fit in one block")
    text = text + b"\0" * ((SECTOR - 1 - len(text) % SECTOR) % SECTOR)
    sectors = 1 + len(text) // SECTOR
    total = 0
    for byte in text:
        total = (total * 31 + byte) & 0xFFFFFFFF
    header = struct.pack("<8sIIIIQ", MAGIC, 1, len(text), sectors, total, lba)
    return header + b"\0" * (SECTOR - len(header)) + text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image", nargs="?")
    ap.add_argument("--lba", type=int, help="only the block at this sector")
    ap.add_argument("--out", help="write the decoded text here instead of stdout")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        text = b"PayloadBoot: FAT OK\r\nPayloadBoot: found \\UniXNU.efi\r\n"
        image = b"\xff" * (30512 * SECTOR) + encode_block(text, 30512)
        blocks = list(find_blocks(image))
        assert len(blocks) == 1, blocks
        offset, block = blocks[0]
        assert block["checksum_ok"], block
        assert block["lba"] == 30512, block
        assert block["text"].rstrip(b"\0") == text, block["text"]
        print("self-test ok: offset %d, %d sectors, checksum ok" % (offset, block["sectors"]))
        return 0

    if not args.image:
        ap.error("an image is required unless --self-test")

    with open(args.image, "rb") as fh:
        data = fh.read()

    blocks = list(find_blocks(data))
    if not blocks:
        sys.exit("no DDNLOG01 block in %s (wrong partition, or the firmware never flushed)" % args.image)

    if args.lba is not None:
        wanted = args.lba * SECTOR
        blocks = [b for b in blocks if b[0] == wanted]
        if not blocks:
            sys.exit("no DDNLOG01 block at sector %d" % args.lba)

    if len(blocks) > 1:
        for where, info in blocks:
            print("  sector %-6d size %-6d lba %d" % (where // SECTOR, info["size"], info["lba"]),
                  file=sys.stderr)
        sys.exit("several blocks; pass --lba to choose one (the magic does not say which is newest)")

    _offset, block = blocks[0]
    text = block["text"].rstrip(b"\0")
    if args.out:
        with open(args.out, "wb") as fh:
            fh.write(text)
        print("wrote %d bytes to %s" % (len(text), args.out))
    else:
        sys.stdout.write(text.decode("utf-8", "replace"))
    if not block["checksum_ok"]:
        print("warning: checksum mismatch; the block may be truncated", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
