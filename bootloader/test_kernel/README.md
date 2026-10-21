# Test Kernel for Chainloader Verification

## 概述

这是一个最小化的ARM64测试内核，用于验证链式加载器是否正常工作。

**用途**: 在没有XNU内核的情况下测试启动链

## 功能

- ✅ 初始化UART
- ✅ 输出调试信息
- ✅ 测试内存访问
- ✅ 循环运行
- ✅ 验证链式加载器工作

## 构建

```bash
cd bootloader/test_kernel
make
```

输出: `test_kernel.bin`

## 使用

### 1. 构建测试内核
```bash
cd bootloader/test_kernel
make
```

### 2. 写入eMMC
```bash
cd ../..
python3 scripts/flash_emmc.py \
    --emmc emmc.img \
    --xnu bootloader/test_kernel/test_kernel.bin
```

### 3. 打包链式加载器
```bash
cd bootloader/chainloader
make
cd ../..
python3 scripts/mkbootimg.py \
    bootloader/chainloader/chainloader.bin \
    xnu_chainloader.img
```

### 4. 刷入设备
```bash
fastboot flash boot xnu_chainloader.img
fastboot reboot
```

### 5. 查看输出

连接UART（115200 8N1），应该看到：

```
========================================
  XNU Chainloader for MT6765
  Loading iOS on Xiaomi Redmi 9A
========================================

[CHAIN] Loading XNU kernel...
[CHAIN] Kernel found at 0x40100000
[CHAIN] Setting up boot parameters...
[CHAIN] Jumping to XNU...

========================================
  Minimal Test Kernel for MT6765
  Chainloader Verification
========================================

[TEST] System Information:
  Architecture: ARM64
  Platform: MediaTek MT6765
  Device: Xiaomi Redmi 9A (dandelion)
  UART Base: 0x11002000

[TEST] UART Test:
  Printing characters: ABCDEFGHIJKLMNOPQRSTUVWXYZ
  Numbers: 0123456789
  [OK] UART working

[TEST] Memory Test:
  Write: 0x12345678
  Read: 0x12345678
  [OK] Memory working

========================================
  ALL TESTS PASSED!
========================================

[TEST] Chainloader is working correctly!
[TEST] You can now proceed with XNU kernel

[TEST] Kernel running... 0
[TEST] Kernel running... 1
...
```

## 内存布局

```
0x40000000 - Chainloader
0x40100000 - Test Kernel (这里加载)
0x47000000 - Device Tree
```

## 测试内容

1. **UART测试** - 验证串口输出
2. **内存测试** - 验证内存访问
3. **启动流程** - 验证链式加载器
4. **硬件初始化** - 验证基本硬件

## 下一步

如果测试成功：
1. ✅ 链式加载器工作正常
2. ✅ 启动流程正确
3. ✅ 可以开始使用XNU内核

替换测试内核为真正的XNU：
```bash
python3 scripts/flash_emmc.py \
    --emmc emmc.img \
    --xnu xnu_kernel.bin \
    --dtb dandelion.dtb
```

## 故障排除

### 没有UART输出
- 检查UART连接
- 确认波特率（115200）
- 检查TX/RX是否接反

### 卡住
- 检查设备是否重启
- 可能UART未初始化
- 检查内存地址

### 测试失败
- 检查eMMC写入
- 验证链式加载器
- 重新构建所有组件

## 参考

- 链式加载器: `../chainloader/`
- eMMC布局: `../../docs/CHAINLOADER_INTEGRATION.md`
- 无UART指南: `../../docs/NO_UART_GUIDE.md`
