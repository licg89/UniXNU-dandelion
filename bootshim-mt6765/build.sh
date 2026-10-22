#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
src=$(cd "$(dirname "$0")" && pwd)
out=${SHIM_OUT:-$src/../out/bootshim}
prefix=${CROSS_COMPILE:-aarch64-linux-gnu-}
mkdir -p "$out"
flags=(-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align
       -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables
       -mcpu=cortex-a53 -Wall -Wextra -Werror)
"${prefix}gcc" "${flags[@]}" -c "$src/start.S" -o "$out/start.o"
"${prefix}gcc" "${flags[@]}" -c "$src/shim.c" -o "$out/shim.o"
"${prefix}gcc" "${flags[@]}" -c "$src/fdt.c" -o "$out/fdt.o"
"${prefix}gcc" "${flags[@]}" -c "$src/adt.c" -o "$out/adt.o"
"${prefix}gcc" "${flags[@]}" -c "$src/handoff.c" -o "$out/handoff.o"
"${prefix}gcc" "${flags[@]}" -c "$src/rng.c" -o "$out/rng.o"
if "${prefix}readelf" -r "$out/shim.o" "$out/fdt.o" "$out/adt.o" "$out/handoff.o" "$out/rng.o" | grep -q 'R_AARCH64_ABS'; then
    echo 'Absolute C relocation is incompatible with a relocated shim' >&2; exit 1
fi

# Try PE/COFF format first (for MTK LK), fallback to binary
if "${prefix}objcopy" --help 2>&1 | grep -q "pe-aarch64"; then
    echo "Building PE/COFF format for MTK LK..."
    "${prefix}gcc" "${flags[@]}" -static -T "$src/shim.ld" -Wl,--build-id=none \
        -Wl,-N "$out/start.o" "$out/shim.o" "$out/fdt.o" "$out/adt.o" "$out/handoff.o" "$out/rng.o" -o "$out/shim.elf"
    "${prefix}objcopy" -O pe-aarch64 "$out/shim.elf" "$out/Image" 2>/dev/null || {
        echo "PE/COFF failed, using binary format..."
        "${prefix}objcopy" -O binary "$out/shim.elf" "$out/Image"
    }
else
    "${prefix}gcc" "${flags[@]}" -static -T "$src/shim.ld" -Wl,--build-id=none \
        -Wl,-N "$out/start.o" "$out/shim.o" "$out/fdt.o" "$out/adt.o" "$out/handoff.o" "$out/rng.o" -o "$out/shim.elf"
    "${prefix}objcopy" -O binary "$out/shim.elf" "$out/Image"
fi

"${prefix}nm" "$out/shim.elf" > "$out/symbols.txt"
python3 - "$out" <<'PY'
from pathlib import Path
import hashlib, json, struct, sys
p=Path(sys.argv[1]); syms={line.split()[2]:int(line.split()[0],16) for line in (p/'symbols.txt').read_text().splitlines() if len(line.split())==3}
image=p/'Image'
with image.open('r+b') as f: f.truncate(syms['_end'])
b=image.read_bytes()
# Check for ARMd header or MZ header
if b[56:60]==b'ARM\x64' or b[0:2]==b'MZ':
    fmt = "PE/COFF" if b[0:2]==b'MZ' else "ARM64 Image"
    (p/'build.json').write_text(json.dumps({'kind':'diagnostic-shim','format':fmt,'cpu':'cortex-a53','soc':'MT6765','device':'Redmi 9A (dandelion)','bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'xnu_boot_verified':False,'hardware_tested':False},indent=2)+'\n')
    print(json.dumps({'image':str(image),'format':fmt,'bytes':len(b),'hardware_tested':False}))
else:
    print("ERROR: Invalid image format", file=sys.stderr)
    sys.exit(1)
PY
