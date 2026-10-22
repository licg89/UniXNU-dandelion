#!/usr/bin/env bash
set -euo pipefail
src=$(cd "$(dirname "$0")" && pwd)
out=${SHIM_OUT:-$src/../out/bootshim}
prefix=${CROSS_COMPILE:-aarch64-linux-gnu-}
mkdir -p "$out"
flags=(-mcpu=cortex-a53)
"${prefix}gcc" "${flags[@]}" -c "$src/empty.S" -o "$out/empty.o"
"${prefix}ld" -T text=0x40080000 --build-id=none -o "$out/empty.elf" "$out/empty.o"
"${prefix}objcopy" -O binary "$out/empty.elf" "$out/Image"
"${prefix}nm" "$out/empty.elf" > "$out/symbols.txt"
echo '{"kind":"empty-test","cpu":"cortex-a53","soc":"MT6765","device":"Redmi 9A (dandelion)","hardware_tested":false}' > "$out/build.json"
echo "Build complete: $out/Image"
ls -lh "$out/Image"
