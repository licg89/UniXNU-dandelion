# MT6768 vs MT6765 硬件对比和 UEFI 移植指南

**日期**: 2026-10-21  
**目的**: 识别从 MT6768 移植 UEFI 到 MT6765 需要的所有修改

---

## 📊 核心硬件对比

### CPU 配置

| 特性 | MT6768 (Helio P65/G85) | MT6765 (Helio G25) | 需要修改 |
|------|------------------------|---------------------|----------|
| 核心数 | 8 (2+6) | 8 (4+4) | ✅ |
| 集群配置 | 2x Cortex-A75 + 6x Cortex-A55 | 4x Cortex-A53 + 4x Cortex-A53 | ✅ |
| 最大频率 | 2.0 GHz | 2.0 GHz | ❌ 相同 |
| 架构 | ARMv8.2-A | ARMv8-A | ✅ |

**需要的修改：**
```c
// MT6768
PcdCoreCount = 8
PcdClusterCount = 2

// MT6765
PcdCoreCount = 8
PcdClusterCount = 2  // 虽然都是 A53，但物理上是 2 个集群
```

### 中断控制器 (GIC)

| 特性 | MT6768 | MT6765 | 需要修改 |
|------|--------|--------|----------|
| GIC 版本 | GICv3 | GICv2 | ✅ **重要** |
| Distributor 基址 | 0x0C000000 | 0x10221000 | ✅ |
| Redistributors 基址 | 0x0C040000 | N/A (GICv2 没有) | ✅ |
| CPU Interface 基址 | N/A (GICv3 用 redistributor) | 0x10222000 | ✅ |

**需要的修改：**
```c
// MT6768 (GICv3)
PcdGicDistributorBase = 0x0C000000
PcdGicRedistributorsBase = 0x0C040000

// MT6765 (GICv2)
PcdGicDistributorBase = 0x10221000
PcdGicCpuInterfaceBase = 0x10222000  // 新增
// 删除 PcdGicRedistributorsBase
```

**关键差异：**
- GICv2 使用 CPU Interface，GICv3 使用 Redistributor
- 需要修改所有 GIC 相关代码
- 中断初始化流程不同

### 内存映射

| 区域 | MT6768 | MT6765 | 需要修改 |
|------|--------|--------|----------|
| RAM 基址 | 0x40000000 | 0x40000000 | ❌ 相同 |
| RAM 大小 | 4-8 GB | 2-4 GB | ✅ |
| IO 区域 | 0x00000000-0x1FFFFFFF | 0x10000000-0x15000000 | ✅ |

### 外设基地址

| 外设 | MT6768 | MT6765 | 需要修改 |
|------|--------|--------|----------|
| UART0 | 需查证 | 0x11002000 | ✅ |
| UART1 | 需查证 | 0x11003000 | ✅ |
| I2C0 | 需查证 | 0x11007000 | ✅ |
| I2C1 | 需查证 | 0x11008000 | ✅ |
| I2C2 | 需查证 | 0x11009000 | ✅ |
| I2C3 | 需查证 | 0x1100F000 | ✅ |
| eMMC (MSDC0) | 需查证 | 0x11230000 | ✅ |
| PMIC Wrapper | 需查证 | 0x1000D000 | ✅ |
| Display (DDP) | 需查证 | 0x14000000 | ✅ |
| DSI | 需查证 | 0x14014000 | ✅ |
| Timer | 需查证 | 0x10008000 | ✅ |
| Watchdog | 需查证 | 0x10007000 | ✅ |

---

## 🔧 需要修改的文件清单

### 1. SoC 级别 (Silicon/MediaTek/MT6765Pkg/)

#### MT6765Pkg.dsc.inc
```diff
- # GIC (GICv3)
- gArmTokenSpaceGuid.PcdGicDistributorBase|0x0C000000
- gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x0C040000

+ # GIC (GICv2)
+ gArmTokenSpaceGuid.PcdGicDistributorBase|0x10221000
+ gArmTokenSpaceGuid.PcdGicCpuInterfaceBase|0x10222000  # 新增
+ # 删除 PcdGicRedistributorsBase

- gArmPlatformTokenSpaceGuid.PcdCoreCount|8
- gArmPlatformTokenSpaceGuid.PcdClusterCount|2
+ # 保持不变（MT6765 也是 8 核 2 集群）

- *_*_AARCH64_CC_FLAGS = -march=armv8.2-a+crypto+rcpc
+ *_*_AARCH64_CC_FLAGS = -march=armv8-a  # MT6765 是 ARMv8-A
```

#### PlatformSecLib/PlatformRegisters.h
需要添加 MT6765 特定的寄存器定义：
```c
// MT6765 UART
#define UART0_BASE          0x11002000
#define UART1_BASE          0x11003000

// MT6765 Timer
#define TIMER_BASE          0x10008000

// MT6765 Watchdog
#define WDT_BASE            0x10007000

// MT6765 GIC (GICv2)
#define GICD_BASE           0x10221000
#define GICC_BASE           0x10222000

// MT6765 PMIC
#define PMIC_WRAP_BASE      0x1000D000

// MT6765 Display
#define DDP_BASE            0x14000000
#define DSI_BASE            0x14014000

// MT6765 eMMC
#define MSDC0_BASE          0x11230000

// MT6765 I2C
#define I2C0_BASE           0x11007000
#define I2C1_BASE           0x11008000
#define I2C2_BASE           0x11009000
#define I2C3_BASE           0x1100F000
```

#### PlatformSecLib/PlatformSec.c
需要修改：
- GIC 初始化代码（GICv2 vs GICv3）
- 中断处理流程
- 可能的定时器初始化

### 2. 设备级别 (Platforms/Xiaomi/dandelionPkg/)

#### dandelion.dsc
```diff
- # 显示分辨率（Redmi 9 是 1080x2340）
- gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1088
- gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2340

+ # 显示分辨率（Redmi 9A 是 720x1600）
+ gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|720
+ gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|1600

- gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Redmi 9"
+ gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Redmi 9A"
```

#### MemoryMapLib
需要调整内存映射以匹配 MT6765 的实际内存布局。

### 3. 驱动库

#### GpioImplLib
- GPIO 基地址可能不同
- 需要查证 MT6765 的 GPIO 控制器地址

#### ClockImplLib
- 时钟控制器地址可能不同
- PLL 配置可能不同

#### MsdcImplLib (eMMC)
- MSDC 基地址：0x11230000 (MT6765)
- 可能需要调整时序参数

#### PmicWrapperImplLib
- PMIC Wrapper 基址：0x1000D000 (MT6765)
- PMIC 型号：MT6357 (MT6765) vs MT6358/MT6360 (MT6768)

---

## ⚠️ 关键挑战

### 1. GICv2 vs GICv3

**这是最大的差异！**

MT6768 使用 GICv3：
- 使用 Redistributor per CPU
- 中断路由更复杂
- 支持虚拟化扩展

MT6765 使用 GICv2：
- 使用 CPU Interface
- 更简单的中断模型
- 代码结构完全不同

**影响：**
- 需要重写所有 GIC 相关代码
- 中断初始化流程不同
- 可能需要不同的 EDK2 PCD

### 2. 缺少硬件文档

MT6765 的许多外设地址需要从以下来源获取：
- Linux 内核源码（drivers/）
- Android 内核源码
- 设备树文件（.dts）
- 逆向工程

### 3. 显示驱动

MT6765 的显示子系统：
- DDP (Display Data Path): 0x14000000
- DSI (Display Serial Interface): 0x14014000
- 面板：NT36525B (720x1600)

需要：
- 显示控制器初始化代码
- MIPI DSI 配置
- 面板初始化序列
- Framebuffer 设置

---

## 📝 移植步骤

### Phase 1: 基础框架（1-2 周）

1. **创建 MT6765Pkg 目录结构**
   - 复制 MT6768Pkg
   - 修改所有文件名为 MT6765

2. **更新硬件地址**
   - GIC 地址（GICv2）
   - UART 地址
   - Timer 地址
   - 其他外设地址

3. **修改 CPU 配置**
   - ARMv8-A (不是 ARMv8.2-A)
   - 集群配置

### Phase 2: GICv2 适配（2-3 周）

1. **修改 GIC 初始化代码**
   - 使用 GICv2 API
   - 配置 Distributor
   - 配置 CPU Interface

2. **修改中断处理**
   - 中断确认和结束流程
   - 优先级配置
   - CPU 目标配置

3. **测试中断**
   - 定时器中断
   - UART 中断
   - GPIO 中断

### Phase 3: 基础驱动（3-4 周）

1. **UART 驱动**
   - 早期控制台
   - 日志输出

2. **Timer 驱动**
   - 系统定时器
   - 延迟函数

3. **GPIO 驱动**
   - 基本输入输出
   - 按键检测

### Phase 4: 显示驱动（4-6 周）

1. **DDP 控制器初始化**
2. **MIPI DSI 配置**
3. **面板初始化**
4. **Framebuffer 设置**
5. **GOP 驱动实现**

### Phase 5: 存储驱动（2-3 周）

1. **eMMC 驱动**
   - MSDC 控制器
   - 命令和数据传输
   - 分区访问

2. **文件系统**
   - FAT 支持
   -  payload 卷

### Phase 6: 集成测试（2-4 周）

1. **UEFI 菜单**
2. **设备树生成**
3. **Payload 加载**
4. **DarwinBoot 集成**
5. **XNU 启动测试**

**总时间估计：14-22 周（3.5-5.5 个月）**

---

## 🎯 立即可以开始的工作

### 在你寻找 UART 的同时，我可以：

1. **收集 MT6765 硬件信息**
   - 从 Linux 内核源码提取地址
   - 从设备树提取配置
   - 整理完整的寄存器映射

2. **准备代码框架**
   - 创建 MT6765Pkg 完整结构
   - 修改所有地址定义
   - 准备 GICv2 适配代码

3. **研究显示驱动**
   - 分析 NT36525B 面板规格
   - 查找 MTK DDP/DSI 驱动代码
   - 准备初始化序列

4. **创建调试工具**
   - Framebuffer 日志系统
   - 存储日志系统
   - LED 调试模式（如果可能）

---

## 📚 参考资源

### MT6765 硬件信息

1. **Linux 内核源码**
   - https://github.com/torvalds/linux
   - arch/arm64/boot/dts/mediatek/mt6762.dtsi
   - drivers/clk/mediatek/clk-mt6765.c
   - drivers/mmc/host/mtk-sd.c

2. **Android 内核源码**
   - https://github.com/xiaomi-mt6765/android_kernel_xiaomi_mt6765
   - 包含完整的驱动代码

3. **设备树**
   - 从设备提取：`dd if=/dev/block/by-name/dtb of=dtb.img`
   - 反编译：`dtc -I dtb -O dts dtb.img > mt6765.dts`

### UEFI 移植参考

1. **MT6768 实现**
   - Mu-Silicium 仓库
   - Silicon/MediaTek/MT6768Pkg/

2. **其他 MTK 设备**
   - MT6789 (Helio G99)
   - 可以参考其实现

3. **EDK2 文档**
   - https://github.com/tianocore/edk2
   - MdePkg/Include/IndustryStandard/

---

## ✅ 检查清单

### 立即开始

- [ ] 从 Linux 内核提取 MT6765 硬件地址
- [ ] 创建 MT6765Pkg 目录结构
- [ ] 更新 MT6765Pkg.dsc.inc
- [ ] 创建 PlatformRegisters.h
- [ ] 准备 GICv2 适配代码

### 找到 UART 后

- [ ] 验证 UART 输出
- [ ] 测试早期控制台
- [ ] 调试 GIC 初始化
- [ ] 测试中断

### 显示输出后

- [ ] 实现 Framebuffer 驱动
- [ ] 显示 UEFI 菜单
- [ ] 测试 GOP 协议

---

## 💡 关键提示

1. **GICv2 是最大挑战** - 需要完全重写中断相关代码
2. **显示驱动是关键** - 没有显示就无法看到 UEFI 菜单
3. **分阶段测试** - 每个阶段验证后再进入下一阶段
4. **使用 UART** - 一旦找到，立即使用，这是调试的关键
5. **参考 Linux 驱动** - MTK 的 Linux 驱动是最好的参考

---

**文档版本**: 1.0  
**最后更新**: 2026-10-21  
**下一步**: 收集 MT6765 硬件地址，准备代码框架
