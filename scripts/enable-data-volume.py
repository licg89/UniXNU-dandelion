#!/usr/bin/env python3
"""Prepare every local change that a Data seed needs, parameterised by its volume UUID.

Run this after the CI artifact's dandelion-data.dbr.json arrives:
    python3 scripts/enable-data-volume.py <volume_uuid> <path/to/dandelion-data.dbr>

It edits the defconfig, board.toml ADT, and records the seed path, but does NOT build: the
kernel/payload/bootimg steps are documented in HANDOFF 0.51 and run separately so a build
failure is attributable to one change at a time.
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
uuid = sys.argv[1].upper()
seed = pathlib.Path(sys.argv[2]).resolve()
if not re.fullmatch(r'[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}', uuid):
    raise SystemExit('volume UUID must be the canonical 8-4-4-4-12 form')
if not seed.is_file():
    raise SystemExit(f'seed not found: {seed}')

# 1. defconfig switches
cfg = ROOT / 'configs/dandelion_defconfig'
text = cfg.read_text(encoding='utf-8')
for key, value in (('CONFIG_XNU_OWNER_DATA_MOUNT', 'y'),
                   ('CONFIG_XNU_NATIVE_RAM_BLOCK', 'y'),
                   ('CONFIG_XNU_OWNER_DATA_VOLUME_UUID', f'"{uuid}"')):
    line = f'{key}={value}'
    if re.search(rf'^{re.escape(key)}=', text, re.M):
        text = re.sub(rf'^{re.escape(key)}=.*$', line, text, flags=re.M)
    else:
        text += f'\n# Data volume (from the CI-built APFS seed).\n{line}\n'
cfg.write_text(text, encoding='utf-8', newline='\n')
print('defconfig: OWNER_DATA_MOUNT=y, NATIVE_RAM_BLOCK=y, OWNER_DATA_VOLUME_UUID set')

# 2. [payload.adt] packed_data_ramdisk
board = ROOT / 'board.toml'
b = board.read_text(encoding='utf-8')
if 'packed_data_ramdisk' not in b:
    marker = 'root_match_volume_uuid = '
    line = next(l for l in b.splitlines() if l.startswith(marker))
    b = b.replace(line, line + '\npacked_data_ramdisk = true   # the CI seed is DBRDISK1 packed', 1)
    board.write_text(b, encoding='utf-8', newline='\n')
    print('board.toml: [payload.adt] packed_data_ramdisk = true')
else:
    print('board.toml: packed_data_ramdisk already present')

# 3. record the seed for the payload step
record = ROOT / 'build' / 'data-seed.json'
record.parent.mkdir(exist_ok=True)
report = json.loads(pathlib.Path(str(seed) + '.json').read_text()) if pathlib.Path(str(seed) + '.json').is_file() else {}
record.write_text(json.dumps({
    'seed': str(seed),
    'seed_sha256': report.get('output_sha256'),
    'volume_uuid': uuid,
    'size_mib': report.get('size_mib'),
    'packed': True,
}, indent=2) + '\n')
print('wrote', record)
