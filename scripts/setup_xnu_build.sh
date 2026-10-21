#!/bin/bash
#
# XNU Kernel Build Environment Setup
# This script prepares the environment for building XNU for MT6765
#

set -e

echo "=========================================="
echo "  XNU Build Environment Setup"
echo "=========================================="
echo ""

# Check if running on macOS
if [[ "$OSTYPE" != "darwin"* ]]; then
    echo "WARNING: XNU is typically built on macOS"
    echo "You are on: $OSTYPE"
    echo ""
    echo "Options:"
    echo "  1. Use macOS for building (recommended)"
    echo "  2. Use cross-compilation toolchain (advanced)"
    echo "  3. Use pre-built XNU kernel (if available)"
    echo ""
    read -p "Continue anyway? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Check for required tools
echo "[1/5] Checking required tools..."

REQUIRED_TOOLS="git make clang ld"
MISSING_TOOLS=""

for tool in $REQUIRED_TOOLS; do
    if command -v $tool &> /dev/null; then
        echo "  ✓ $tool found"
    else
        echo "  ✗ $tool NOT found"
        MISSING_TOOLS="$MISSING_TOOLS $tool"
    fi
done

if [ -n "$MISSING_TOOLS" ]; then
    echo ""
    echo "Missing tools:$MISSING_TOOLS"
    echo "Please install them before continuing"
    exit 1
fi

echo ""

# Setup XNU source
echo "[2/5] Setting up XNU source..."

XNU_DIR="xnu-source"
if [ ! -d "$XNU_DIR" ]; then
    echo "Cloning XNU source..."
    git clone --depth 1 https://github.com/apple/darwin-xnu.git "$XNU_DIR"
    echo "  ✓ XNU source cloned"
else
    echo "  ✓ XNU source already exists"
fi

echo ""

# Create build configuration
echo "[3/5] Creating build configuration..."

BUILD_CONFIG="configs/xnu_mt6765_config.mk"
mkdir -p configs

cat > "$BUILD_CONFIG" << 'EOF'
# XNU Build Configuration for MT6765
# Target: Xiaomi Redmi 9A (dandelion)

# Architecture
ARCH_CONFIGS = ARM64
CURRENT_SDK_CONFIG = iOS

# Platform
PLATFORM = mtk6765
MACHINE = dandelion

# Memory
VM_SIZE = 512*1024*1024
PAGE_SIZE = 16384

# CPU
CPU_COUNT = 8
CPU_TYPE = cortex-a53

# Features
ENABLE_SMP = 1
ENABLE_NEON = 1
ENABLE_VFP = 1

# Debug
DEBUG = 0
LOG_LEVEL = 3

# Paths
DSTROOT = $(SRCROOT)/build
SYMROOT = $(SRCROOT)/build/sym
OBJROOT = $(SRCROOT)/build/obj
EOF

echo "  ✓ Build config created: $BUILD_CONFIG"
echo ""

# Create device tree source
echo "[4/5] Creating device tree..."

DTS_DIR="patches/dts"
DTS_FILE="$DTS_DIR/dandelion.dts"

if [ ! -f "$DTS_FILE" ]; then
    mkdir -p "$DTS_DIR"
    cat > "$DTS_FILE" << 'EOF'
/dts-v1/;

/ {
    compatible = "xiaomi,dandelion", "mediatek,mt6765";
    model = "Xiaomi Redmi 9A";

    #address-cells = <2>;
    #size-cells = <2>;

    memory {
        device_type = "memory";
        reg = <0x0 0x40000000 0x0 0x20000000>; /* 512MB at 0x40000000 */
    };

    cpus {
        #address-cells = <1>;
        #size-cells = <0>;

        cpu0: cpu@0 {
            device_type = "cpu";
            compatible = "arm,cortex-a53";
            reg = <0x0>;
        };

        cpu1: cpu@1 {
            device_type = "cpu";
            compatible = "arm,cortex-a53";
            reg = <0x1>;
        };

        cpu2: cpu@2 {
            device_type = "cpu";
            compatible = "arm,cortex-a53";
            reg = <0x2>;
        };

        cpu3: cpu@3 {
            device_type = "cpu";
            compatible = "arm,cortex-a53";
            reg = <0x3>;
        };

        cpu4: cpu@4 {
            device_type = "cpu";
            compatible = "arm,cortex-a53";
            reg = <0x4>;
        };

        cpu5: cpu@5 {
            device_type = "cpu";
            compatible = "arm,cortex-a53";
            reg = <0x5>;
        };

        cpu6: cpu@6 {
            device_type = "cpu";
            compatible = "arm,cortex-a53";
            reg = <0x6>;
        };

        cpu7: cpu@7 {
            device_type = "cpu";
            compatible = "arm,cortex-a53";
            reg = <0x7>;
        };
    };

    uart0: serial@11002000 {
        compatible = "mediatek,mt6765-uart", "ns16550a";
        reg = <0x0 0x11002000 0x0 0x1000>;
        clock-frequency = <26000000>;
        interrupts = <0 91 0x4>;
    };

    interrupt-controller@10221000 {
        compatible = "arm,gic-400";
        #interrupt-cells = <3>;
        interrupt-controller;
        reg = <0x0 0x10221000 0x0 0x1000>,
              <0x0 0x10222000 0x0 0x2000>;
    };
};
EOF
    echo "  ✓ Device tree created: $DTS_FILE"
else
    echo "  ✓ Device tree already exists"
fi

echo ""

# Create build script
echo "[5/5] Creating build script..."

BUILD_SCRIPT="scripts/build_xnu.sh"
cat > "$BUILD_SCRIPT" << 'EOF'
#!/bin/bash
#
# Build XNU kernel for MT6765
#

set -e

echo "Building XNU for MT6765..."

# Load configuration
source configs/xnu_mt6765_config.mk

# Build XNU
cd xnu-source
make SDKROOT=macosx ARCH_CONFIGS="arm64" \
     KERNEL_CONFIGS="release" \
     SRCROOT="$(pwd)" \
     DSTROOT="../build" \
     all

echo "Build complete!"
echo "Output: build/"
EOF

chmod +x "$BUILD_SCRIPT"
echo "  ✓ Build script created: $BUILD_SCRIPT"

echo ""
echo "=========================================="
echo "  Setup Complete!"
echo "=========================================="
echo ""
echo "Next steps:"
echo "  1. Review build config: $BUILD_CONFIG"
echo "  2. Review device tree: $DTS_FILE"
echo "  3. Build XNU: $BUILD_SCRIPT"
echo ""
echo "Note: XNU requires macOS to build."
echo "If you're on Windows/Linux, you can:"
echo "  - Use a Mac for building"
echo "  - Use GitHub Actions (already configured)"
echo "  - Get a pre-built XNU kernel"
echo ""
