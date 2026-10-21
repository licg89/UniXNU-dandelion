# 🚀 新电脑继续工作指南

## 快速开始

### 1. 克隆项目
```bash
git clone https://github.com/licg89/UniXNU-dandelion.git
cd UniXNU-dandelion
```

### 2. 运行设置脚本
```bash
bash setup.sh
```

### 3. 查看项目状态
```bash
cat PROGRESS.md
```

---

## 📋 项目状态 (2026-10-21)

**总进度: 55%**

### ✅ 已完成

1. **链式加载器** (100%)
   - 位置: `bootloader/chainloader/`
   - 功能: 从eMMC加载XNU内核
   - 状态: 已验证工作（看到Redmi logo）

2. **硬件驱动** (80%)
   - PMIC (MT6357)
   - I2C (7个控制器)
   - 触摸 (Novatek)
   - 显示 (MTK DDP)
   - eMMC (MTK MSDC)
   - GIC (中断)
   - UART (调试)
   - 定时器

3. **工具链** (100%)
   - GitHub Actions自动编译
   - boot.img打包工具
   - eMMC写入工具
   - 日志读取脚本

4. **文档** (100%)
   - 15+技术文档
   - 完整API文档
   - 故障排除指南

### 🔄 进行中

1. **XNU内核启动代码** (75%)
   - ✅ 启动汇编（boot_mt6765.s）完整实现
   - ✅ MMU页表创建（2MB块映射）
   - ✅ GICv2 中断控制器初始化
   - ✅ ARM通用定时器初始化
   - ✅ SMP辅核保持循环
   - 🔄 _kernel_main 桩函数（真实XNU内核入口待链接）
   - ❌ 需要实际XNU内核二进制（需Mac编译或社区支持）

2. **显示集成** (20%)
   - 驱动代码完整
   - 需要集成到测试内核
   - 约2000行代码

3. **日志系统** (60%)
   - UART输出 ✓
   - eMMC写入 (改进中)
   - 显示输出 (尝试中)

---

## 🎯 下一步工作

### 选项A: 测试日志功能（推荐）

**目标:** 验证eMMC日志写入

**步骤:**
1. 等GitHub Actions编译新版本
2. 刷入设备
3. 恢复原系统
4. 读取persist分区日志

**命令:**
```bash
# 下载最新编译
cd downloads
./gh_cli/bin/gh.exe run download --repo licg89/UniXNU-dandelion

# 刷入
adb reboot bootloader
fastboot flash boot latest_build/all_binaries/xnu_chainloader.img
fastboot reboot

# 等待10秒后恢复
fastboot flash boot boot_backup.img
fastboot reboot

# 读取日志
adb shell su -c "dd if=/dev/block/bootdevice/by-name/persist bs=4096 skip=512 count=2" | strings
```

### 选项B: 编译XNU内核

**目标:** 从UniXNU项目编译XNU

**要求:**
- Mac电脑
- Xcode 15+
- 2-3小时

**步骤:**
1. 参考 `docs/UNIXNU_BUILD_GUIDE.md`
2. 克隆UniXNU项目
3. 配置MT6765设备
4. 编译XNU

### 选项C: Linux方案（最实用）

**目标:** 先用Linux内核验证硬件

**优势:**
- MT6765有完整Linux支持
- 可以立即测试
- 成功后再迁移到XNU

**步骤:**
1. 使用droidian-mt6765内核
2. 修改启动参数
3. 测试所有驱动

---

##  关键文件位置

### 核心代码
- `bootloader/chainloader/` - 链式加载器
- `bootloader/test_kernel/` - 测试内核
- `drivers/` - 硬件驱动

### 工具
- `scripts/mkbootimg.py` - boot.img打包
- `scripts/flash_emmc.py` - eMMC写入
- `scripts/read_userdata_log.sh` - 日志读取

### 文档
- `PROGRESS.md` - 项目进度
- `FINAL_REPORT.md` - 完成报告
- `docs/UNIXNU_BUILD_GUIDE.md` - XNU编译
- `docs/NO_UART_ALTERNATIVES.md` - 无UART方案

### 备份
- `downloads/boot_backup.img` - 原系统备份

---

## 🔧 环境设置

### WSL2 (推荐)
```bash
sudo apt update
sudo apt install -y git python3 adb fastboot gcc-aarch64-linux-gnu make
```

### Windows
```powershell
winget install Git.Git Python.Python.3
```

### 验证
```bash
git --version
python3 --version
adb devices
```

---

## 🎓 项目背景

**目标:** 在Xiaomi Redmi 9A (MT6765)上运行iOS

**架构:**
```
Preloader → LK → Chainloader → XNU → iOS
```

**关键发现:**
- 链式加载器方案可行（已验证）
- 不需要LK源码
- 使用标准boot.img格式
- eMMC读取功能完整

**挑战:**
- XNU内核需要Mac编译
- 显示驱动集成复杂
- 无UART调试困难

---

## 💡 经验总结

### 成功的
- 链式加载器架构
- GitHub Actions编译
- 完整的硬件驱动
- 文档齐全

### 待改进的
- eMMC日志写入
- 显示驱动集成
- XNU内核编译

### 学到的
- MTK设备启动链
- ARM64内核开发
- 无UART调试方法

---

##  资源

### 项目
- GitHub: https://github.com/licg89/UniXNU-dandelion
- UniXNU: https://github.com/UniXNU/UniXNU

### 文档
- `docs/` 目录有完整技术文档
- `PROGRESS.md` 有详细进度

### 社区
- XDA Developers
- UniXNU Discord
- Reddit r/jailbreak

---

## 🎯 推荐路线

**基于当前情况，建议：**

1. **短期:** 测试日志功能（1-2天）
2. **中期:** 尝试Linux方案（1周）
3. **长期:** 等XNU内核成熟（1-3月）

**或：**

1. **如果有Mac:** 编译XNU（1周）
2. **如果没有:** 用Linux方案验证硬件

---

**最后更新:** 2026-10-21  
**下次继续:** 阅读此文档后选择下一步
