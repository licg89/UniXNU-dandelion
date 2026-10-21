# 🚀 快速开始指南

## 你现在可以做什么

即使没有XNU内核和Mac，你也能：

1. ✅ **构建测试内核** - 验证链式加载器
2. ✅ **测试启动流程** - 确认一切工作
3. ✅ **查看UART输出** - 验证硬件
4. ✅ **为XNU做准备** - 所有基础设施就绪

---

## 📋 完整步骤（今天就能做）

### 步骤1: 构建测试内核

```bash
cd D:/unixnu/UniXNU-dandelion/bootloader/test_kernel
make
```

**输出**: `test_kernel.bin` (约5-10KB)

### 步骤2: 构建链式加载器

```bash
cd ../chainloader
make
```

**输出**: `chainloader.bin` (约10-20KB)

### 步骤3: 创建eMMC测试镜像

```bash
cd D:/unixnu/UniXNU-dandelion

# 创建32GB eMMC镜像
dd if=/dev/zero of=emmc_test.img bs=1M count=32768

# 写入测试内核到eMMC
python scripts/flash_emmc.py \
    --emmc emmc_test.img \
    --xnu bootloader/test_kernel/test_kernel.bin
```

### 步骤4: 打包boot.img

```bash
python scripts/mkbootimg.py \
    bootloader/chainloader/chainloader.bin \
    xnu_chainloader.img
```

### 步骤5: 备份原系统（重要！）

```bash
# 连接手机（开启USB调试和root）
adb devices

# 备份boot分区
adb shell su -c "dd if=/dev/block/bootdevice/by-name/boot of=/sdcard/boot_backup.img"
adb pull /sdcard/boot_backup.img
```

### 步骤6: 刷入设备

```bash
# 进入fastboot模式
adb reboot bootloader

# 刷入链式加载器
fastboot flash boot xnu_chainloader.img

# 重启
fastboot reboot
```

### 步骤7: 观察结果

**连接UART** (115200 8N1):
- 如果看到测试输出 → 成功！
- 如果看到链式加载器日志 → 启动链工作
- 如果设备重启 → 需要调试

---

## 🎯 预期结果

### 成功的情况

**UART输出:**
```
========================================
  XNU Chainloader for MT6765
  Loading iOS on Xiaomi Redmi 9A
========================================

[CHAIN] Loading XNU kernel...
[CHAIN] Kernel found at 0x40100000
[CHAIN] Setting up boot parameters...
[CHAIN] Jumping to XNU at 0x40100000

========================================
  Minimal Test Kernel for MT6765
  Chainloader Verification
========================================

[TEST] System Information:
  Architecture: ARM64
  Platform: MediaTek MT6765
  ...

========================================
  ALL TESTS PASSED!
========================================

[TEST] Chainloader is working correctly!
```

**这意味着:**
- ✅ 链式加载器工作
- ✅ 启动流程正确
- ✅ UART输出正常
- ✅ 可以开始使用XNU

### 如果设备重启

- 检查链式加载器是否刷入正确
- 检查eMMC镜像是否写入
- 尝试恢复原系统: `fastboot flash boot boot_backup.img`

---

## 🛡️ 安全措施

### 1. 始终备份
```bash
# 备份原boot分区
adb shell su -c "dd if=/dev/block/bootdevice/by-name/boot of=/sdcard/boot_backup.img"
adb pull /sdcard/boot_backup.img
```

### 2. 使用备用分区
```bash
# 如果有A/B分区
fastboot flash boot_b xnu_chainloader.img
fastboot set_active b
fastboot reboot
```

### 3. 准备恢复
```bash
# 恢复原系统
fastboot flash boot boot_backup.img
fastboot reboot
```

---

## 📊 下一步计划

### 如果测试成功

1. **庆祝！** 🎉 链式加载器工作
2. **获取XNU内核** - 从UniXNU项目或社区
3. **替换测试内核** - 使用真正的XNU
4. **开始调试iOS启动**

### 如果测试失败

1. **购买UART** (20元) - 查看日志
2. **调试问题** - 根据日志修复
3. **重新测试** - 直到成功

---

## 💡 没有UART怎么办？

### 可以盲测

1. 刷入启动镜像
2. 观察设备行为:
   - 如果不再重启 → 可能成功
   - 如果看到启动画面 → 很可能成功
   - 如果ADB可用 → 确认成功

### 检查ADB

```bash
# 等待设备
adb wait-for-device

# 检查连接
adb devices

# 查看日志（如果内核支持）
adb logcat
```

---

## 🎊 总结

### 你现在可以做的

✅ 构建测试内核  
✅ 测试启动流程  
✅ 验证链式加载器  
✅ 盲测或UART测试  

### 不需要

❌ XNU内核（用测试内核代替）  
❌ Mac电脑  
❌ 复杂编译环境  

### 需要的

✅ Windows/Linux电脑  
✅ USB数据线  
✅ 红米9A设备  
✅ UART调试线（可选，20元）  

---

## 🚀 立即开始

**执行以下命令:**

```bash
# 1. 进入项目目录
cd D:/unixnu/UniXNU-dandelion

# 2. 构建测试内核
cd bootloader/test_kernel
make

# 3. 构建链式加载器
cd ../chainloader
make

# 4. 返回项目根目录
cd ../..

# 5. 打包boot.img
python scripts/mkbootimg.py \
    bootloader/chainloader/chainloader.bin \
    xnu_chainloader.img

# 6. 完成！
```

**然后刷入设备测试！**

---

*准备好了吗？让我们测试链式加载器！* 🎯
