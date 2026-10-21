# LK Bootloader 源码获取指南

## 什么是LK?

**LK (Little Kernel)** 是联发科(MTK)设备使用的bootloader,类似于高通的ABL。

**你的设备**: Xiaomi Redmi 9A (dandelion) 使用 MT6765,启动流程:
```
Preloader → LK → XNU (我们的目标)
```

## LK源码在哪里?

### 1. 小米开源仓库 ✅

小米会发布部分源码:
- **地址**: https://github.com/MiCode/Xiaomi_Kernel_OpenSource
- **搜索**: "dandelion" 或 "mt6765"
- **包含**: 内核源码 + 部分bootloader

**步骤**:
```bash
# 克隆小米内核源码
git clone https://github.com/MiCode/Xiaomi_Kernel_OpenSource.git -b dandelion-q-oss

# 查看bootloader相关目录
cd Xiaomi_Kernel_OpenSource
ls -la bootloader/  # 或 lk/
```

### 2. 联发科开源项目 ✅

MTK官方开源:
- **地址**: https://github.com/MediaTekMTK/mtk-open-source
- **包含**: LK源码、驱动等

**步骤**:
```bash
# 克隆MTK开源项目
git clone https://github.com/MediaTekMTK/mtk-open-source.git

# 查找LK
find . -name "lk" -o -name "littlekernel"
```

### 3. Android开源项目 (AOSP) ⚠️

AOSP包含通用LK:
- **地址**: https://android.googlesource.com/platform/external/lk
- **包含**: 通用LK框架,需要MTK特定适配

### 4. 第三方项目 ✅

社区整理的MTK LK:
```bash
# 搜索GitHub
# 关键词: "mt6765 lk", "mtk bootloader", "dandelion lk"

# 示例(需要验证):
git clone https://github.com/your-repo/mt6765-lk.git
```

## 如何获取LK源码?

### 方法1: 从设备提取 (推荐) ⭐⭐⭐⭐⭐

```bash
# 1. 需要root权限
adb shell su

# 2. 找到LK分区
ls -l /dev/block/by-name/
# 或
cat /proc/partitions | grep lk

# 3. 备份LK
dd if=/dev/block/by-name/lk of=/sdcard/lk.img

# 4. 拉取到电脑
adb pull /sdcard/lk.img

# 5. 分析(需要逆向工程)
# 使用工具: binwalk, ghidra, IDA Pro
```

### 方法2: 下载编译好的LK ⭐⭐⭐⭐

```bash
# 从小米或MTK下载完整固件
# 提取LK分区
# 反编译分析
```

### 方法3: 使用现有开源LK ⭐⭐⭐

```bash
# 克隆通用LK
git clone https://github.com/littlekernel/lk.git

# 添加MTK特定代码
# 需要参考MTK文档和现有驱动
```

## LK源码结构

典型的MTK LK结构:
```
lk/
├── app/              # 应用程序
│   ├── aboot/       # Android boot
│   ├── fastboot/    # Fastboot模式
│   └── shell/       # 命令行
├── arch/            # 架构相关
│   └── arm/
│       └── arm64/
├── dev/             # 设备驱动
│   ├── display/     # 显示
│   ├── input/       # 输入
│   └── mmc/         # 存储
├── platform/        # 平台相关
│   └── mt6765/      # MT6765特定
├── kernel/          # 内核
├── lib/             # 库
└── Makefile
```

## 修改LK加载XNU

### 方案A: 直接修改LK (需要源码) ⭐⭐⭐⭐⭐

1. **找到内核加载代码**:
```c
// 通常在 app/aboot/aboot.c
void boot_linux(void *kernel, unsigned tags, ...)
{
    // 加载Linux/Android内核
}
```

2. **修改为加载XNU**:
```c
void boot_xnu(void *kernel, unsigned tags, ...)
{
    // 设置XNU启动参数
    // 跳转到XNU入口
}
```

3. **编译LK**:
```bash
make PLATFORM=mt6765
```

### 方案B: 链式加载 (不需要LK源码) ⭐⭐⭐⭐

```
LK(原版) → 自定义加载器 → XNU
```

1. **创建自定义加载器**:
```c
// mini_loader.c
void main(void)
{
    // 从eMMC加载XNU
    // 设置启动参数
    // 跳转到XNU
}
```

2. **打包为Android boot.img**:
```bash
mkbootimg --kernel mini_loader --output boot.img
```

3. **LK会加载这个boot.img**:
```
LK → mini_loader → XNU
```

### 方案C: 使用fastboot ⭐⭐⭐

```bash
# 直接启动XNU(不修改LK)
fastboot boot xnu_boot.img

# 或刷写到boot分区
fastboot flash boot xnu_boot.img
fastboot reboot
```

## 下一步行动

### 1. 搜索LK源码 (今天)
```bash
# 在GitHub搜索
# 关键词: "xiaomi dandelion lk" "mt6765 bootloader"

# 检查小米开源
git clone https://github.com/MiCode/Xiaomi_Kernel_OpenSource.git -b dandelion-q-oss

# 检查MTK开源
git clone https://github.com/MediaTekMTK/mtk-open-source.git
```

### 2. 分析现有boot.img (今天)
```bash
# 从设备提取boot.img
adb pull /dev/block/by-name/boot boot.img

# 分析boot.img
unpackbootimg -i boot.img

# 查看内核头部
hexdump -C boot.img | head -20
```

### 3. 尝试链式加载 (1-2天)
```bash
# 编写简单的加载器
# 测试能否从LK启动
```

### 4. 等待UART调试线 (购买后1-3天)
```bash
# 收到后连接UART
# 测试启动输出
# 调试启动问题
```

## 参考资源

### 小米开源
- https://github.com/MiCode/Xiaomi_Kernel_OpenSource
- 搜索 "dandelion" 或 "mt6765"

### 联发科开源
- https://github.com/MediaTekMTK/mtk-open-source
- 搜索 "MT6765" 或 "Helio G25"

### LK项目
- https://github.com/littlekernel/lk (官方LK)
- https://github.com/MediaTek-LK/lk (MTK LK,如果存在)

### 教程
- [XDA Developers](https://forum.xda-developers.com/)
- [4PDA](https://4pda.to/) (俄语,但有大量MTK资料)

## 总结

**LK源码获取难度**: ⭐⭐⭐ (中等)

**最佳方案**:
1. ✅ 搜索小米/MTK开源仓库
2. ✅ 使用链式加载(不需要完整LK源码)
3. ✅ 购买UART调试线用于测试

**时间估计**: 1-2周找到并集成LK

---

**现在可以做**:
- 继续完善QEMU测试
- 搜索LK源码
- 购买UART调试线(25元)

**等你决定后继续!**
