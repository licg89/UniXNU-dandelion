# XNU Kernel Patches for MT6765

This directory contains patches and configuration for adapting XNU to run on the MediaTek MT6765 SoC.

## Contents

### Platform (`platform/`)
- `platform_mt6765.h` - Platform-specific definitions
  - SoC identification
  - CPU configuration (4x Cortex-A53)
  - Memory layout
  - Peripheral base addresses
  - Interrupt controller settings

### Memory (`memory/`)
- `memory_map_mt6765.h` - Memory map definitions
  - Kernel code/data regions
  - Page table layout
  - Boot stack
  - Device tree location
  - Initrd location
  - I/O memory regions

### Boot (`boot/`)
- `boot_mt6765.s` - ARM64 assembly entry point
  - Exception vectors
  - MMU setup
  - BSS initialization
  - Early console init
  - GIC initialization
  - Timer initialization
  - SMP bringup

### Device Tree (`dts/`)
- `mt6765-dandelion.dts` - Device tree source
  - CPU definitions (4 cores)
  - Memory layout
  - Interrupt controller (GIC-400)
  - UART console
  - PMIC (MT6357)
  - I2C + touch controller
  - Display subsystem
  - eMMC storage
  - System timer

## Memory Layout

```
0x40000000 - Boot args (4KB)
0x40001000 - Device tree (256KB)
0x40080000 - Kernel text (16MB)
0x41080000 - Kernel data (8MB)
0x41880000 - Kernel BSS (2MB)
0x42000000 - Initrd (256MB)
0x44000000 - Page tables (4MB)
0x44800000 - Boot stack (256KB)
0x52000000 - Kernel heap (512MB)
```

## Boot Flow

1. **LK (Little Kernel)** loads XNU kernel image
2. **XNU entry point** (`_start`) is called with:
   - x0 = DTB address
   - x1 = boot args address
3. **Early initialization**:
   - Disable interrupts
   - Set up exception vectors
   - Initialize MMU and caches
   - Clear BSS
   - Initialize UART (early console)
   - Initialize GIC (interrupts)
   - Initialize timer
   - Bring up secondary CPUs
4. **Jump to C entry** (`kernel_main`)
5. **XNU kernel** takes over
6. **Load iOS userland** from eMMC
7. **Boot complete**

## Integration Steps

### 1. Patch XNU Source

Apply platform-specific patches to XNU:

```bash
cd xnu-source
patch -p1 < ../patches/xnu-platform-mt6765.patch
```

### 2. Build Device Tree

```bash
dtc -I dts -O dtb -o mt6765-dandelion.dtb patches/dts/mt6765-dandelion.dts
```

### 3. Build Kernel

```bash
# Build XNU for MT6765
make ARCH=arm64 PLATFORM=mt6765 -j$(nproc)
```

### 4. Create Boot Image

```bash
# Package kernel + DTB + boot args
mkbootimg \
  --kernel arch/arm64/boot/Image.gz \
  --dtb mt6765-dandelion.dtb \
  --cmdline "debug=0x8 loglevel=15" \
  --base 0x40000000 \
  --output boot.img
```

## Key Challenges

### 1. LK Integration

MTK uses LK instead of UEFI/ABL. Options:
- Modify LK to load XNU (preferred)
- Chain-load from LK to custom bootloader
- Bypass LK entirely (risky)

### 2. MMU Configuration

XNU expects specific page table layout:
- 16KB pages (iOS standard)
- TTBR0/TTBR1 split
- Kernel space mapping
- Device memory mapping

### 3. Interrupt Handling

Need to ensure:
- GIC properly initialized before XNU
- Timer interrupts working
- IRQ routing correct
- SMP interrupt affinity

### 4. Device Tree Compatibility

XNU needs device tree with:
- Correct CPU definitions
- Memory layout
- Interrupt controller
- Essential peripherals
- PSCI for SMP

### 5. Power Management

Cortex-A53 needs:
- PSCI interface
- CPU idle states
- Thermal management
- Voltage regulation

## Testing

### QEMU Testing (Initial)

```bash
qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a53 \
  -smp 4 \
  -m 4G \
  -kernel Image \
  -dtb mt6765-dandelion.dtb \
  -nographic
```

### Hardware Testing

1. **UART debug**: Connect to UART0 (115200 8N1)
2. **Boot from LK**: Modify LK to load XNU
3. **Check console**: Should see XNU boot messages
4. **Test peripherals**: Timer, GIC, UART first
5. **Debug crashes**: Use UART output for debugging

## Status

- [x] Platform definitions
- [x] Memory map
- [x] Boot assembly skeleton
- [x] Device tree
- [ ] MMU initialization
- [ ] Early console implementation
- [ ] GIC integration
- [ ] Timer integration
- [ ] SMP bringup
- [ ] XNU source patches
- [ ] LK integration

## References

- [XNU source code](https://github.com/apple/darwin-xnu)
- [ARM Architecture Reference Manual](https://developer.arm.com/documentation/ddi0487/latest)
- [MTK Linux kernel](https://github.com/xiaomi-sdm-439/android_kernel_xiaomi_mt6765)
- [UniXNU project](https://github.com/UniXNU/UniXNU)

## Next Steps

1. Complete boot assembly implementation
2. Implement MMU page table setup
3. Add early UART driver
4. Integrate GIC and timer drivers
5. Patch XNU source for MT6765
6. Test on QEMU
7. Modify LK to load XNU
8. Test on real hardware
