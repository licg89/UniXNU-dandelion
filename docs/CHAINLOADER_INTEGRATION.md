# Chainloader Integration Guide

## 概述

本文档说明如何将XNU内核集成到链式加载器启动流程中。

## 启动流程

```
[Preloader (MTK)]
    ↓
[LK Bootloader (Android)]
    ↓  加载 boot.img
[Chainloader]
    ↓  初始化UART
    ↓  从eMMC读取XNU内核
    ↓  从eMMC读取DTB
    ↓  设置启动参数
    ↓  跳转到XNU
[XNU Kernel]
    ↓  启动iOS
```

## 准备步骤

### 1. 编译链式加载器

```bash
cd bootloader/chainloader
make
```

输出: `chainloader.bin`

### 2. 打包boot.img

方法A - 使用Makefile脚本:
```bash
cd ../..
bash scripts/build_chainloader.sh
```

方法B - 使用Python脚本:
```bash
python3 scripts/mkbootimg.py \
    bootloader/chainloader/chainloader.bin \
    xnu_chainloader.img
```

方法C - 使用Android mkbootimg:
```bash
mkbootimg \
    --kernel bootloader/chainloader/chainloader.bin \
    --base 0x40000000 \
    --cmdline "xnu_chainloader" \
    --output xnu_chainloader.img
```

### 3. 准备XNU内核

XNU内核需要编译为ARM64二进制文件:
- 入口点: 0x40100000
- 格式: 原始二进制 (raw binary)
- 页大小: 16KB

### 4. 准备设备树

编译设备树源文件:
```bash
dtc -I dts -O dtb -o dandelion.dtb patches/dts/mt6765-dandelion.dts
```

### 5. 写入eMMC

方法A - 使用eMMC dump文件 (测试):
```bash
# 创建空eMMC镜像 (32GB)
dd if=/dev/zero of=emmc.img bs=1M count=32768

# 写入XNU内核
python3 scripts/flash_emmc.py \
    --emmc emmc.img \
    --xnu xnu_kernel.bin \
    --dtb dandelion.dtb

# 验证
python3 scripts/flash_emmc.py --emmc emmc.img --verify
```

方法B - 写入真实设备:
```bash
# 1. 刷入链式加载器到boot分区
fastboot flash boot xnu_chainloader.img

# 2. 将XNU内核写入eMMC特定分区
# (需要使用SP Flash Tool或自定义fastboot命令)
```

## eMMC分区布局

```
eMMC布局 (32GB):
┌─────────────────────┐ 0x00000000
│  Preloader          │
├─────────────────────┤ 0x00100000
│  LK Bootloader      │
├─────────────────────┤ 0x00400000
│  Boot Partition     │  ← chainloader (boot.img)
├─────────────────────┤ 0x04000000
│  XNU Kernel         │  ← XNU内核 (64MB max)
├─────────────────────┤ 0x08000000
│  Device Tree (DTB)  │  ← 设备树 (1MB max)
├─────────────────────┤ 0x08100000
│  ...                │
├─────────────────────┤ 0x80000000
│  System Partition   │
├─────────────────────┤
│  Data Partition     │
└─────────────────────┘ 0xFFFFFFFF
```

## 内存布局

```
启动时内存:
0x40000000 - Chainloader加载地址 (LK加载boot.img)
0x40001000 - 启动参数
0x40100000 - XNU内核加载地址
0x47000000 - 设备树(DTB)
0x48000000 - Initrd (可选)
```

## 调试

### UART输出

连接USB转TTL到设备调试串口:
- 波特率: 115200
- 数据位: 8
- 停止位: 1
- 校验: 无

预期输出:
```
========================================
  XNU Chainloader for MT6765
  Loading iOS on Xiaomi Redmi 9A
========================================

[CHAIN] Loading XNU kernel...
[CHAIN] Initializing eMMC controller...
[CHAIN] eMMC ready
[CHAIN] Reading XNU from eMMC...
  Offset: 0x04000000
  Destination: 0x40100000
[CHAIN] eMMC read: block=0x8000 count=0x8000
[CHAIN] Kernel read complete
[CHAIN] Reading DTB from eMMC...
[CHAIN] DTB read complete
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

### 常见问题

1. **eMMC初始化失败**
   - 检查MSDC控制器地址是否正确
   - 确认eMMC分区布局
   - 尝试增加延时

2. **内核找不到**
   - 验证eMMC写入是否正确
   - 检查偏移地址是否匹配
   - 使用flash_emmc.py --verify验证

3. **启动后无输出**
   - 检查UART连接
   - 确认UART基址 (0x11002000)
   - 检查波特率设置

4. **XNU启动后崩溃**
   - 检查设备树是否正确
   - 验证XNU编译配置
   - 确认内存布局

## 下一步

1. 在QEMU上测试链式加载器
2. 在真实设备上测试
3. 调试XNU启动过程
4. 完善设备树
5. 集成iOS用户空间

## 参考

- [Chainloader README](../bootloader/chainloader/README.md)
- [eMMC分区布局](./emmc_layout.md)
- [XNU启动指南](./xnu_boot.md)
