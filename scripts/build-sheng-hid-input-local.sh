#!/bin/bash
# Build ShengHidInput for this board with the official UniXNU builder, on a Linux host.
#
# Why a wrapper: `scripts/build-sheng-hid-input.py` wants macOS only through two commands, `xcrun` and
# `brew`.  scripts/shims answers both from the local LLVM, so the official script runs unmodified and
# still generates its own OriginalHidDeviceABI.h and binds the owned methods to the original
# IOHIDDevice vtable slots.  The kernel candidate it verifies against is this board's own prelink.
set -eu

TREE="$(cd "$(dirname "$0")/.." && pwd)"
BOARD="${UNIXNU_BOARD:-dandelion}"
OUT="$TREE/build/sheng-hid-input"

echo "== sheng-hid-input: preparing the reference candidate"
mkdir -p "$TREE/build/xnu-prelink" "$TREE/drivers"
CPREFIX="$TREE/build/xnu-prelink/native-owner-ufs-cpu-desktop-${BOARD}-candidate"
cp "$TREE/../kernel/kernel.macho" "$CPREFIX.macho"
cp "$TREE/../kernel/kernel.json" "$CPREFIX.json"

if [ ! -f "$TREE/drivers/ShengHidInput/NativeHidInput.cpp" ]; then
    echo "== sheng-hid-input: the component is not vendored at drivers/ShengHidInput" >&2
    exit 1
fi

export PATH="$TREE/scripts/shims:$PATH"

echo "== sheng-hid-input: running the official builder"
python3 "$TREE/scripts/build-sheng-hid-input.py"

echo "== sheng-hid-input: outputs"
ls -l "$OUT"
