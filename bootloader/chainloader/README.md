# Chainloader for XNU on MT6765

## 概述

这是一个链式加载器，用于在MTK MT6765平台上启动XNU内核(iOS)。

## 工作原理

```
启动流程:
Preloader → LK (Android原版) → 链式加载器 → XNU
                                ↓
                           从boot分区加载XNU
                           设置启动参数
                           跳转到XNU入口
```

## 为什么需要链式加载器？

1. **不需要LK源码**: MTK的LK bootloader是闭源的，直接修改很困难
2. **更安全**: 不破坏原有的Android启动流程
3. **更灵活**: 可以独立开发和测试
4. **兼容性**: 使用标准Android boot.img格式，LK可以直接加载

## 文件说明

- `chainloader.h` - 头文件，定义内存地址和数据结构
- `chainloader.c` - 主C代码，实现加载和启动逻辑
- `chainloader_asm.s` - ARM64汇编入口点
- `link.ld` - 链接脚本，定义内存布局
- `Makefile` - 构建系统

## 内存布局

```
0x40000000 - 链式加载器加载地址 (LK加载boot.img的位置)
0x40001000 - 启动参数
0x40100000 - XNU内核加载地址
0x47000000 - 设备树(DTB)地址
```

## 构建

```bash
make
```

需要ARM64交叉编译工具链:
```bash
sudo apt-get install gcc-aarch64-linux-gnu
```

## 生成boot.img

构建完成后，需要打包成Android boot.img格式:

```bash
# 使用mkbootimg工具
mkbootimg \
  --kernel chainloader.bin \
  --base 0x40000000 \
  --cmdline "xnu_chainloader" \
  --output xnu_chainloader.img
```

## 刷入设备

```bash
# 使用fastboot刷入boot分区
fastboot flash boot xnu_chainloader.img
fastboot reboot
```

## 调试

链式加载器会通过UART输出调试信息:
- UART基址: 0x11002000
- 波特率: 115200
- 格式: 8N1

连接USB转TTL模块到设备的调试串口，可以看到:
```
========================================
  XNU Chainloader for MT6765
  Loading iOS on Xiaomi Redmi 9A
========================================

[CHAIN] Loading XNU kernel...
[CHAIN] Kernel found at 0x40100000
[CHAIN] Setting up boot parameters...
[CHAIN] Bootargs: debug=0x8 loglevel=15 serial=0x11002000
[CHAIN] Boot params:
  DTB: 0x47000000
  Kernel: 0x40100000
  CPUs: 8
[CHAIN] Jumping to XNU at 0x40100000
[CHAIN] DTB at 0x47000000
[CHAIN] Bootargs at 0x40001000
[CHAIN] ========================================
[CHAIN] Starting XNU...
[CHAIN] ========================================
```

## 下一步

1. 实现从eMMC读取XNU内核
2. 加载设备树(dandelion.dtb)
3. 配置XNU启动参数
4. 测试在真实设备上启动

## 硬件要求

- Xiaomi Redmi 9A (dandelion)
- MT6765 (Helio G25) SoC
- 3GB RAM
- 32GB eMMC存储
- USB转TTL调试模块(用于查看启动日志)

## 参考

- [UniXNU项目文档](../../docs/)
- [MT6765内核分析](../../docs/MT6765_KERNEL_ANALYSIS.md)
- [LK源码指南](../../docs/LK_SOURCE_GUIDE.md)
