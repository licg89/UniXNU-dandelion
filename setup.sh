#!/bin/bash
#
# Quick setup script for new environment
#

echo "========================================"
echo "  UniXNU-dandelion Setup"
echo "========================================"
echo ""

# Check if in WSL
if grep -q Microsoft /proc/version 2>/dev/null; then
    echo "✓ Running in WSL"
else
    echo "⚠ Not in WSL, some features may not work"
fi

echo ""

# Install tools (WSL)
if command -v apt &> /dev/null; then
    echo "[1/4] Installing tools..."
    sudo apt update -qq
    sudo apt install -y -qq git python3 python3-pip adb fastboot gcc-aarch64-linux-gnu make
    echo "  ✓ Tools installed"
else
    echo "[1/4] Skipping apt install (not Debian/Ubuntu)"
fi

echo ""

# Clone repo
echo "[2/4] Cloning repository..."
if [ ! -d "UniXNU-dandelion" ]; then
    git clone https://github.com/licg89/UniXNU-dandelion.git
    echo "  ✓ Repository cloned"
else
    echo "  ✓ Repository already exists"
    cd UniXNU-dandelion
fi

echo ""

# Verify tools
echo "[3/4] Verifying tools..."
echo "  Git: $(git --version | head -1)"
echo "  Python: $(python3 --version 2>&1)"
echo "  ADB: $(adb version 2>&1 | head -1)"
echo "  Fastboot: $(fastboot --version 2>&1 | head -1)"

echo ""

# Show project info
echo "[4/4] Project Info:"
echo "  Files: $(find . -name "*.c" -o -name "*.h" -o -name "*.s" | wc -l) source files"
echo "  Drivers: $(ls -d drivers/*/ | wc -l) driver modules"
echo "  Docs: $(find docs -name "*.md" | wc -l) documents"

echo ""
echo "========================================"
echo "  Setup Complete!"
echo "========================================"
echo ""
echo "Next steps:"
echo "  cd UniXNU-dandelion"
echo "  cat PROGRESS.md"
echo "  cat docs/UNIXNU_BUILD_GUIDE.md"
echo ""
