# 无UART调试指南

## 概述

本指南说明如何在没有UART调试线的情况下继续推进项目。

## 没有UART的限制

### 无法做的事情
- ❌ 查看启动日志
- ❌ 调试启动问题
- ❌ 知道系统卡在哪里
- ❌ 验证链式加载器是否工作

### 可以做的事情
- ✅ 编译所有组件
- ✅ 准备启动镜像
- ✅ 盲刷到设备
- ✅ 测试是否能启动
- ✅ 使用ADB（如果内核启动成功）

---

## 无UART开发流程

### 方案1: 完整准备 + 盲测

**步骤:**

1. **准备所有组件**
   ```bash
   # 编译链式加载器
   cd bootloader/chainloader
   make
   
   # 打包boot.img
   python3 scripts/mkbootimg.py chainloader.bin xnu.img
   
   # 准备XNU内核（如果有）
   # 准备设备树
   ```

2. **写入eMMC镜像**
   ```bash
   python3 scripts/flash_emmc.py \
       --emmc emmc.img \
       --xnu xnu_kernel.bin \
       --dtb dandelion.dtb
   ```

3. **刷入设备**
   ```bash
   # 刷入链式加载器
   fastboot flash boot xnu.img
   
   # 重启
   fastboot reboot
   ```

4. **观察结果**
   - 如果能启动到iOS：成功！
   - 如果卡在启动画面：需要UART调试
   - 如果重启：需要UART调试

### 方案2: 使用ADB（如果内核启动）

如果XNU内核成功启动，即使没有UART，也可能通过ADB调试：

```bash
# 等待设备
adb wait-for-device

# 检查连接
adb devices

# 查看日志（如果内核支持）
adb logcat

# 获取系统信息
adb shell getprop
```

**前提条件:**
- XNU内核必须成功启动
- USB驱动必须工作
- ADB守护进程必须运行

### 方案3: 模拟测试

在QEMU或其他模拟器上测试：

```bash
# 使用QEMU测试启动链
python3 tests/qemu/test_chainloader.py
```

**优点:**
- 不需要硬件
- 可以查看完整日志
- 快速迭代调试

**缺点:**
- QEMU可能无法完全模拟MT6765
- 某些硬件特性可能不工作

---

## 当前可以推进的工作

### 1. 编译XNU内核

**选项A: 使用GitHub Actions**
- 已经配置好CI/CD
- 自动在macOS runner上编译
- 推送到GitHub即可触发

**选项B: 使用Mac电脑**
- 如果有Mac，直接编译
- 使用 `scripts/setup_xnu_build.sh` 设置环境

**选项C: 获取预编译内核**
- 从UniXNU项目获取
- 或使用其他iOS设备的内核

### 2. 完善设备树

编辑 `patches/dts/dandelion.dts`:
- 添加更多硬件描述
- 配置中断
- 设置内存映射

### 3. 优化驱动

- 添加DMA支持
- 实现中断驱动
- 电源管理

### 4. 准备iOS用户空间

- 准备根文件系统
- 配置启动脚本
- 设置基本服务

### 5. 文档完善

- 编写使用说明
- 创建故障排除指南
- 记录已知问题

---

## 推荐流程

### 阶段1: 软件准备（现在就可以做）

1. ✅ 设置XNU编译环境
2. ✅ 编译XNU内核
3. ✅ 编译设备树
4. ✅ 准备完整启动镜像

### 阶段2: 盲测（需要设备）

1. 刷入启动镜像
2. 观察是否能启动
3. 检查ADB是否可用
4. 记录结果

### 阶段3: 调试（可能需要UART）

如果盲测失败：
1. 购买UART调试线（20元）
2. 连接UART
3. 查看日志
4. 调试问题

如果盲测成功：
1. 庆祝！🎉
2. 继续完善功能
3. 添加更多驱动

---

## 降低风险的方法

### 1. 备份原系统
```bash
# 备份boot分区
adb shell su -c "dd if=/dev/block/bootdevice/by-name/boot of=/sdcard/boot_backup.img"
adb pull /sdcard/boot_backup.img

# 备份整个系统（如果有root）
adb shell su -c "dd if=/dev/block/bootdevice by-name/system of=/sdcard/system_backup.img"
```

### 2. 准备恢复方案
```bash
# 恢复原系统
fastboot flash boot boot_backup.img
fastboot reboot
```

### 3. 使用测试分区
如果有多个boot分区（如boot_a/boot_b），使用不活动的那个：
```bash
# 刷入到boot_b
fastboot flash boot_b xnu.img

# 设置活动分区
fastboot set_active b
fastboot reboot
```

---

## 成功指标

### 无UART时的成功标志

1. **设备不再重启** - 内核可能在工作
2. **看到启动画面** - 显示驱动可能工作
3. **ADB可用** - 内核成功启动
4. **可以进入系统** - 完全成功！

### 失败标志

1. **设备持续重启** - 内核崩溃
2. **卡在启动画面** - 可能卡在某个驱动
3. **无法开机** - 严重错误

---

## 结论

**UART不是必须的，但强烈推荐！**

### 没有UART:
- 可以继续开发和准备
- 可以尝试盲测
- 可能成功启动
- 调试困难

### 建议:
1. 先继续软件准备工作
2. 准备好完整的启动镜像
3. 在设备上盲测
4. 如果失败，再购买UART（20元）
5. 如果成功，就不需要UART了！

**成本效益分析:**
- UART: 20元 + 时间
- 无UART: 可能成功，可能失败
- 建议: 先尝试无UART，失败再买

---

## 下一步

**立即可以做的:**
1. 设置XNU编译环境
2. 编译所有组件
3. 准备完整启动镜像
4. 盲测启动

**如果失败:**
- 购买UART调试线
- 查看日志
- 调试问题

**如果成功:**
- 继续完善功能
- 添加更多驱动
- 优化性能

---

*记住: 我们已经完成了所有软件准备工作！*
*现在只需要XNU内核和测试！*
