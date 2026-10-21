# QEMU Test Configuration for XNU on MT6765

## Overview

This directory contains scripts and configuration for testing XNU boot code in QEMU before deploying to real hardware.

## Prerequisites

- QEMU 6.0 or later with ARM64 support
- Cross-compiler (aarch64-linux-gnu-gcc or clang)
- Device Tree Compiler (dtc)

## Files

### test_qemu.sh
Main test script that:
- Builds kernel and DTB if needed
- Launches QEMU with correct parameters
- Displays boot output

### Makefile
Build system for:
- Compiling boot assembly
- Generating DTB from DTS
- Creating boot image

## Usage

### Quick Test
```bash
./test_qemu.sh
```

### Manual Build
```bash
# Build boot code
make boot

# Build device tree
make dtb

# Run QEMU
make qemu
```

### Debug Mode
```bash
# Run with GDB
make debug

# In another terminal
gdb-multiarch
(gdb) target remote localhost:1234
(gdb) break _start
(gdb) continue
```

## QEMU Configuration

The test uses:
- Machine: virt (generic ARM64)
- CPU: cortex-a53 (matches MT6765)
- Memory: 4GB (matches device)
- CPUs: 4 (matches MT6765)
- Serial: mon:stdio (console output)

## Expected Output

```
UniXNU: Booting on MT6765...
  DTB address: 0x40001000
  Initializing MMU...
  OK
  Initializing GIC...
  OK
  Initializing Timer...
  OK
  Jumping to kernel...
```

## Troubleshooting

### No output
- Check serial configuration
- Verify early console is initialized
- Check UART base address

### Crashes early
- Enable QEMU debug: `-d in_asm,int`
- Use GDB to step through
- Check exception vectors

### Wrong CPU type
- Ensure `-cpu cortex-a53` is used
- MT6765 uses Cortex-A53, not generic

### Memory errors
- Verify memory map matches QEMU config
- Check page table setup
- Ensure 16KB page support

## Next Steps

1. ✅ Basic boot test
2. ⏳ MMU initialization test
3. ⏳ Early console test
4. ⏳ GIC initialization test
5. ⏳ Timer test
6. ⏳ SMP test (multi-core)
7. ⏳ XNU kernel load test

## Real Hardware

After QEMU testing succeeds:
1. Purchase UART debug cable (20-30 RMB)
2. Connect to UART0 on device
3. Flash boot image via fastboot
4. Monitor serial output

## References

- [QEMU ARM64 documentation](https://www.qemu.org/docs/master/system/target-arm.html)
- [QEMU virt machine](https://www.qemu.org/docs/master/system/arm/virt.html)
