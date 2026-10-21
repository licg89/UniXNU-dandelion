# UniXNU-dandelion

**iOS on Xiaomi Redmi 9A (dandelion) - MediaTek MT6765 (Helio G25)**

This is the device repository for UniXNU on the Xiaomi Redmi 9A, targeting iOS 16 on the MediaTek MT6765 SoC.

## 📱 Device Information

| Component | Specification |
|---|---|
| **Device** | Xiaomi Redmi 9A |
| **Model** | M2006C3LC |
| **Codename** | dandelion |
| **SoC** | MediaTek MT6765 (Helio G25) |
| **CPU** | 4× Cortex-A53 @ 2.0 GHz |
| **GPU** | PowerVR GE8320 |
| **RAM** | 2/3/4 GB LPDDR3 |
| **Storage** | 32/64 GB eMMC 5.1 |
| **Display** | 6.53" IPS LCD, 720×1600 |
| **Display IC** | Novatek NT36525B |
| **Touch** | Novatek touchscreen (I²C) |
| **PMIC** | MediaTek MT6357 |
| **Bootloader** | MediaTek LK (Little Kernel) |

## 🎯 Target

- **iOS Version**: iOS 16.7.16 (20H392)
- **Target Device**: iPhone X (iPhone10,3 / D22AP)
- **XNU Version**: xnu-8796 (iOS 16 generation)
- **Page Size**: 16KB (standard iOS configuration)

## 🏗️ Repository Structure

```
UniXNU-dandelion/
├── board.toml              # Device configuration
├── configs/
│   └── dandelion_defconfig # Kernel configuration
├── drivers/
│   ├── mtk-pmic/          # MT6357 PMIC driver ✅
│   ├── mtk-i2c/           # I2C controller driver ✅
│   ├── novatek-touch/     # Novatek touch driver ✅
│   ├── mtk-ddp/           # Display (DDP) driver ✅
│   ├── mtk-msdc/          # eMMC storage driver ✅
│   ├── mtk-gic/           # Interrupt controller ✅
│   ├── mtk-uart/          # Debug UART driver ✅
│   └── mtk-timer/         # System timer driver ✅
├── patches/               # MTK-specific XNU patches
├── scripts/
│   └── mt6765_hw.py      # Hardware definitions
└── README.md
```

## 🚧 Current Status

**Phase**: Driver bringup complete, XNU integration next

### ✅ Completed (8 drivers)
- [x] Device repository structure
- [x] Hardware information collection (via adb)
- [x] Memory map documentation
- [x] Peripheral base addresses
- [x] Interrupt layout
- [x] Kernel defconfig
- [x] **MT6357 PMIC driver** - Power management
- [x] **MTK I2C driver** - 7 I2C controllers, touch bus
- [x] **Novatek touch driver** - Multi-touch, 5 points
- [x] **MTK DDP display driver** - NT36525B MIPI DSI, 720x1600
- [x] **MTK MSDC eMMC driver** - Storage read/write
- [x] **MTK GIC driver** - Interrupt handling (GICv2)
- [x] **MTK UART driver** - Debug console, 115200 baud
- [x] **MTK Timer driver** - ARM generic timer, scheduling
- [x] **GitHub Actions CI/CD** - macOS build infrastructure

### 🚧 In Progress
- [ ] XNU kernel patches for MT6765
- [ ] LK bootloader integration
- [ ] Driver integration with XNU IOKit
- [ ] Boot image creation

### ❌ Not Started
- [ ] UEFI firmware (if applicable)
- [ ] iOS userland integration
- [ ] Filesystem support (HFS+/APFS)
- [ ] GPU driver (PowerVR GE8320)
- [ ] Audio driver
- [ ] WiFi/Bluetooth driver

## 🔧 Hardware Details

### Memory Map

```
0x40000000 - 0x47C7FFFF : System RAM (~200MB available for kernel)
0x40080000 - 0x4195FFFF : Kernel code
0x41960000 - 0x4224FFFF : Reserved
0x42250000 - 0x42A3CFFF : Kernel data
0x47C90000 - 0x47C9FFFF : Persistent RAM (ramoops)
```

### Key Peripherals

| Peripheral | Base Address | IRQ | Notes |
|---|---|---|---|
| PMIC (MT6357) | 0x1000D000 | 161 | PMIC wrapper interface |
| I²C3 (Touch) | 0x1100F000 | 226 | Novatek @ 0x5D |
| Display (DDP) | 0x14000000 | - | MIPI DSI |
| eMMC (MSDC0) | 0x11230000 | 18 | 8-bit mode |
| UART0 | 0x11002000 | 219 | Debug console |

### Boot Flow

```
MediaTek preloader
  ↓
LK (Little Kernel)
  ↓
[Custom: load XNU]
  ↓
XNU kernel
  ↓
iOS userland
```

**Challenge**: MTK uses LK instead of Qualcomm's ABL. We need to either:
1. Modify LK to load XNU directly, or
2. Bypass LK and load from preloader

## 📚 References

- [UniXNU Main Repository](https://github.com/UniXNU/UniXNU)
- [ios-sweet (Qualcomm reference)](https://github.com/dipa-studio/ios-sweet)
- [MediaTek Linux Kernel](https://github.com/xiaomi-sdm-439/android_kernel_xiaomi_mt6765)
- [UniXNU Documentation](https://unixnu.com)

## 🛠️ Development

### Prerequisites

- GitHub Actions (macOS runner)
- Python 3.11+
- ARM64 cross-compiler (clang/lld)
- MediaTek MT6765 hardware documentation

### Build (Future)

```bash
# Clone main UniXNU repository
git clone https://github.com/UniXNU/UniXNU.git
cd UniXNU

# Clone this device repository
git clone https://github.com/licg89/UniXNU-dandelion.git devices/dandelion

# Build
python3 unixnu -D devices/dandelion build all
```

### Debug (Future)

```bash
# Connect to device via UART
minicom -D /dev/ttyUSB0 -b 921600

# Boot test image
fastboot boot boot.img

# Pull kernel log
adb shell dmesg > kernel.log
```

## 📝 Notes

### Challenges

1. **No UEFI**: MTK devices don't use UEFI, so we can't use Mu-Silicium directly
2. **LK Integration**: Need to understand LK boot flow and modify it
3. **Driver Porting**: All drivers need to be written or ported from Linux
4. **Limited Documentation**: MTK hardware docs are not publicly available
5. **Older CPU**: Cortex-A53 lacks some features of newer cores

### Potential Solutions

1. **Custom bootloader**: Write a minimal bootloader that loads XNU from LK
2. **Linux driver analysis**: Reverse engineer MTK drivers from Linux kernel source
3. **QEMU testing**: Use QEMU for initial kernel bringup before testing on hardware
4. **UART debugging**: Essential for kernel development

## 🤝 Contributing

This is an experimental bringup project. Contributions welcome!

Areas that need help:
- MTK driver development
- XNU kernel porting
- LK bootloader modification
- Hardware testing
- Documentation

## 📄 License

Same as UniXNU: BSD-2-Clause-Patent

## ⚠️ Disclaimer

This is an experimental project. Running iOS on non-Apple hardware may violate Apple's EULA. Use at your own risk. This project is for educational purposes only.

---

**Status**: 🚧 Early bringup phase
**Last Updated**: 2026-10-21
