# UniXNU 编译指南

## 概述

UniXNU项目 (https://github.com/UniXNU/UniXNU) 提供了在ARM64设备上运行iOS的框架。

## 项目结构

```
UniXNU/
├── feeds.toml          # 外部源配置 (XNU, libplatform等)
├── kits/               # 工具链和构建工具
├── patches/            # 内核补丁
├── drivers/            # 硬件驱动
├── scripts/            # 构建脚本
└── docs/               # 文档
```

## XNU内核来源

从`feeds.toml`可以看到：

```toml
[[source]]
name = "xnu"
kind = "refer"
git = "https://github.com/apple-oss-distributions/xnu.git"
ref = "xnu-10063.141.1"
commit = "d8b80295118ef25ac3a784134bcf95cd8e88109f"
darwin = "23.6.0"
version = "10063.141.1"
```

**XNU版本**: iOS 17.x (xnu-10063.141.1)  
**来源**: Apple官方开源  
**Darwin版本**: 23.6.0

## 编译要求

### 硬件
- Mac电脑 (M1/M2/M3推荐)
- 至少16GB RAM
- 至少100GB磁盘空间

### 软件
- macOS 13.0+ (Ventura或更新)
- Xcode 15.0+
- Command Line Tools
- Homebrew

### 依赖
```bash
# 安装Homebrew
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 安装依赖
brew install cmake ninja python3 dtc
```

## 编译步骤

### 1. 克隆UniXNU

```bash
git clone https://github.com/UniXNU/UniXNU.git
cd UniXNU
```

### 2. 初始化子模块

```bash
git submodule update --init --recursive
```

### 3. 下载XNU源码

```bash
# 使用uni命令下载
./unixnu feed sync
```

这会从Apple开源下载XNU源码到指定位置。

### 4. 配置设备

创建`devices/dandelion.toml`:

```toml
[device]
name = "dandelion"
codename = "redmi9a"
soc = "mt6765"
arch = "arm64"

[boot]
base = 0x40000000
kernel_offset = 0x00100000
dtb_offset = 0x07000000

[hardware]
cpu_cores = 8
memory_size = "3GB"
display = "720x1600"
touch = "novatek"
```

### 5. 应用补丁

```bash
./unixnu patch apply --device dandelion
```

### 6. 编译XNU

```bash
./unixnu build kernel --device dandelion
```

### 7. 编译结果

输出文件：
- `build/kernel/xnu-dandelion.bin` - XNU内核
- `build/kernel/xnu-dandelion.dtb` - 设备树
- `build/kernel/xnu-dandelion.img` - boot镜像

## 简化编译（推荐）

如果完整编译太复杂，可以使用GitHub Actions：

### 方法1: Fork项目

1. Fork UniXNU到你的GitHub
2. 添加`devices/dandelion.toml`
3. 推送代码
4. GitHub Actions自动编译
5. 下载编译产物

### 方法2: 使用现有配置

检查UniXNU是否已有MT6765支持：

```bash
ls devices/
cat devices/*.toml | grep -i mt6765
```

## 当前状态

**UniXNU项目状态:**
- ✅ 框架完整
- ✅ 支持Qualcomm设备
- ✅ XNU源码配置完整
- ️ MTK支持可能不完整
- ️ MT6765需要验证

**我们的优势:**
- ✅ 完整的MT6765驱动
- ✅ 链式加载器已验证
- ✅ 设备树已准备

## 下一步

### 短期
1. 检查UniXNU是否有MTK支持
2. 如果没有，创建MT6765设备配置
3. 尝试在Mac上编译

### 中期
1. 适配我们的驱动到UniXNU
2. 测试编译结果
3. 刷入设备测试

### 长期
1. 完善驱动
2. 优化性能
3. 添加更多功能

## 替代方案

如果编译XNU太困难：

### 方案A: 使用Linux内核
- 使用droidian-mt6765内核
- 验证硬件和驱动
- 成功后再迁移到XNU

### 方案B: 等待社区
- 等UniXNU官方支持MT6765
- 或使用其他社区的预编译内核

### 方案C: 混合方案
- 使用Linux内核启动
- 加载XNU作为虚拟机
- 逐步迁移

## 参考资源

- UniXNU: https://github.com/UniXNU/UniXNU
- XNU源码: https://github.com/apple-oss-distributions/xnu
- Apple开源: https://opensource.apple.com/
- Halium: https://halium.org/

---

**最后更新**: 2026-10-21  
**状态**: 研究中
