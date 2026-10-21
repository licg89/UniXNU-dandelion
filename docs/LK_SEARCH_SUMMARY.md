# LK Bootloader 源码搜索总结

**日期**: 2026-10-21  
**目标**: Xiaomi Redmi 9A (dandelion)  
**SoC**: MediaTek MT6765 (Helio G25)

---

## 📋 搜索结果

### 1. 小米开源仓库 ✅ 已尝试

**仓库**: https://github.com/MiCode/Xiaomi_Kernel_OpenSource

**尝试的分支**:
- `dandelion-q-oss` (主要)
- `dandelion-p-oss`
- `dandelion-oss`
- `mt6765`

**状态**: 脚本正在克隆中...

**预期内容**:
- Linux内核源码
- 部分bootloader代码
- 设备树
- 驱动程序

### 2. 联发科开源 ✅ 已尝试

**仓库**: https://github.com/MediaTekMTK/mtk-open-source

**状态**: 脚本正在克隆中...

**预期内容**:
- MTK通用驱动
- LK框架代码
- 平台相关文件

### 3. LineageOS设备仓库 ✅ 已尝试

**仓库**: https://github.com/LineageOS/android_device_xiaomi_dandelion

**状态**: 脚本正在克隆中...

**预期内容**:
- 设备配置
- 可能包含bootloader相关信息

### 4. GitHub手动搜索 📝

**推荐搜索关键词**:
```
mt6765 lk
dandelion bootloader
mediatek littlekernel
redmi 9a lk
mtk lk source
```

**搜索链接**:
- https://github.com/search?q=mt6765+lk
- https://github.com/search?q=dandelion+bootloader
- https://github.com/search?q=mediatek+littlekernel

### 5. 从设备提取 🔧

**方法**: 需要root权限

```bash
# 提取boot分区
adb shell su
dd if=/dev/block/by-name/boot of=/sdcard/boot.img
adb pull /sdcard/boot.img

# 分析
unpackbootimg -i boot.img
binwalk boot.img
hexdump -C boot.img | head -50
```

**优势**: 完整的LK代码  
**劣势**: 需要逆向工程

---

## 📊 LK源码来源对比

| 来源 | 可用性 | 完整性 | 难度 | 推荐度 |
|------|--------|--------|------|--------|
| 小米开源 | ⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐⭐⭐ |
| MTK开源 | ⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐ |
| 设备提取 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |
| 通用LK | ⭐⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐ |

---

## 🎯 修改LK的方案

### 方案A: 直接修改LK ⭐⭐⭐⭐⭐

**前提**: 有完整LK源码

**步骤**:
1. 找到内核加载代码 (`app/aboot/aboot.c`)
2. 修改 `boot_linux()` 函数
3. 改为加载XNU
4. 编译LK
5. 刷写测试

**优点**: 最干净,最稳定  
**缺点**: 需要完整源码,工作量大

### 方案B: 链式加载 ⭐⭐⭐⭐

**原理**:
```
LK(原版) → 自定义加载器 → XNU
```

**步骤**:
1. 编写小型加载器 (mini_loader.c)
2. 加载器从eMMC读取XNU
3. 打包为Android boot.img
4. LK加载这个boot.img
5. 加载器启动XNU

**优点**: 不需要修改LK  
**缺点**: 多一层,稍复杂

### 方案C: Fastboot启动 ⭐⭐⭐

**步骤**:
```bash
# 每次手动启动
fastboot boot xnu_boot.img
```

**优点**: 简单,不修改任何东西  
**缺点**: 每次需要手动操作

### 方案D: 修改boot分区 ⭐⭐

**步骤**:
1. 创建包含XNU的boot.img
2. 刷写到boot分区
3. 正常启动

**优点**: 自动化启动  
**缺点**: 需要root,可能变砖

---

## 🔍 LK代码结构

### 典型目录结构
```
lk/
├── app/
│   ├── aboot/          # Android boot (关键!)
│   │   ├── aboot.c     # 内核加载代码
│   │   └── fastboot.c  # Fastboot模式
│   ├── shell/          # 命令行
│   └── tests/          # 测试
├── arch/
│   └── arm/
│       ├── arm64/      # ARM64架构
│       └── include/
├── dev/
│   ├── display/        # 显示驱动
│   ├── input/          # 输入驱动
│   ├── mmc/            # eMMC驱动
│   └── usb/            # USB驱动
├── platform/
│   └── mt6765/         # MT6765平台 (关键!)
│       ├── platform.c
│       └── acpi.c
├── kernel/
│   └── main.c          # 内核主循环
├── lib/
│   ├── libc/
│   └── libfont/
└── Makefile
```

### 关键文件
1. **app/aboot/aboot.c** - 内核加载代码
2. **platform/mt6765/** - 平台特定代码
3. **arch/arm/arm64/** - ARM64启动代码

### 关键函数
```c
// 内核加载 (需要修改)
void boot_linux(void *kernel, unsigned tags, ...)
{
    // 当前: 加载Android/Linux内核
    // 目标: 改为加载XNU
}

// 平台初始化
void platform_init(void)
{
    // MT6765特定初始化
}

// 启动命令处理
int cmd_boot(const char *arg, ...)
{
    // fastboot boot 命令处理
}
```

---

## 📝 下一步行动

### 立即 (今天)
1. ✅ 运行搜索脚本
2. ⏳ 检查克隆结果
3. ⏳ 分析找到的代码

### 短期 (1-3天)
1. 搜索GitHub (手动)
2. 从设备提取boot.img (如有root)
3. 分析LK代码结构
4. 确定修改方案

### 中期 (1-2周)
1. 实现LK修改 (或链式加载)
2. 编译测试
3. 硬件测试 (需要UART)

---

## 🛠️ 工具准备

### 必需工具
```bash
# ARM交叉编译
sudo apt-get install gcc-aarch64-linux-gnu

# 设备树编译
sudo apt-get install device-tree-compiler

# 分析工具
sudo apt-get install binwalk
pip install unpackbootimg
```

### 可选工具
```bash
# 反编译
sudo apt-get install ghidra  # 或 IDA Pro

# 调试
sudo apt-get install gdb-multiarch
```

---

## 📚 参考资源

### 官方资源
- [LK Official](https://github.com/littlekernel/lk)
- [MTK Open Source](https://github.com/MediaTekMTK/mtk-open-source)
- [Xiaomi Open Source](https://github.com/MiCode/Xiaomi_Kernel_OpenSource)

### 社区资源
- [XDA Developers](https://forum.xda-developers.com/)
- [4PDA](https://4pda.to/) (俄语,MTK资源丰富)
- [MediaTek Developers](https://community.mediatek.com/)

### 教程
- [MTK Bootloader Unlock](https://forum.xda-developers.com/t/guide-all-mediatek-devices-unlock-bootloader.4306841/)
- [LK Development Guide](https://github.com/littlekernel/lk/wiki)

---

## 💡 建议

### 最可能的成功路径
1. **找到LK源码** (小米/MTK开源)
2. **使用链式加载** (不需要完整修改)
3. **等待UART** (硬件调试必备)
4. **QEMU测试** (先验证逻辑)

### 风险降低策略
1. 先用QEMU测试启动代码
2. 链式加载比直接修改LK更安全
3. 备份原始boot.img
4. 有UART后再硬件测试

### 时间估计
- 搜索LK源码: 1-2天
- 分析代码: 2-3天
- 修改LK: 3-5天
- 测试调试: 1-2周
- **总计**: 2-3周

---

## 📞 需要帮助?

### 如果找不到LK源码
1. 尝试从设备提取
2. 使用通用LK + MTK适配
3. 寻找社区帮助 (XDA/4PDA)
4. 考虑其他方案 (如UEFI移植)

### 如果LK修改失败
1. 使用链式加载方案
2. 使用fastboot手动启动
3. 寻求社区帮助
4. 重新评估项目可行性

---

**当前状态**: 🔍 搜索中  
**下次更新**: 搜索结果出来后
