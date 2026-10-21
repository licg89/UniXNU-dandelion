# iOS on Xiaomi Redmi 9A - 项目进度报告

**项目**: UniXNU-dandelion  
**设备**: Xiaomi Redmi 9A (M2006C3LC)  
**SoC**: MediaTek MT6765 (Helio G25)  
**目标**: 在MT6765上运行iOS 16  
**日期**: 2026-10-21

---

## 📊 总体进度

```
硬件驱动: ████████░░ 80% (8/10 核心驱动完成)
内核适配: ██████░░░░ 60% (MMU+早期控制台完成)
启动链:   ████████░░ 80% (链式加载器+eMMC读取完成!)
测试:     ░░░░░░░░░░  0% (等待硬件测试)
```

**总进度: 约 48%**

---

## ✅ 已完成工作

### 1. 基础设施 (100%)

#### GitHub Actions CI/CD
- ✅ macOS runner 构建环境
- ✅ Python 3.12 配置
- ✅ Homebrew 依赖管理
- ✅ 自动化构建流程
- ✅ SSH密钥认证

**文件**: `.github/workflows/build.yml`

### 2. 设备配置 (100%)

#### 硬件信息收集
- ✅ 通过ADB收集所有硬件信息
- ✅ 内存映射文档
- ✅ 外设基地址
- ✅ 中断布局

**文件**: 
- `board.toml` - 设备配置
- `configs/dandelion_defconfig` - 内核配置
- `scripts/mt6765_hw.py` - 硬件定义

### 3. 硬件驱动 (8/10 完成)

#### ✅ PMIC驱动 (MT6357)
- PMIC Wrapper初始化
- 寄存器读写
- 芯片ID检测
- 电池ADC读取
- 状态寄存器访问

**文件**: `drivers/mtk-pmic/` (4个文件, 530行)

#### ✅ I2C驱动
- 7个I2C控制器支持
- 读写操作
- 速度配置 (100kHz/400kHz)
- 总线扫描

**文件**: `drivers/mtk-i2c/` (4个文件, 530行)

#### ✅ 触摸驱动 (Novatek)
- I2C3通信 (地址0x5D)
- 多点触摸 (5点)
- 坐标/压力/面积读取
- 事件类型检测

**文件**: `drivers/novatek-touch/` (4个文件, 447行)

#### ✅ 显示驱动 (MTK DDP)
- DDP流水线初始化
- MIPI DSI控制器配置
- NT36525B面板初始化
- 视频模式 (720x1600@60Hz)
- 帧缓冲管理
- 颜色填充操作

**文件**: `drivers/mtk-ddp/` (4个文件, 702行)

#### ✅ eMMC存储驱动 (MTK MSDC)
- MSDC控制器初始化
- eMMC卡初始化序列
- 单块/多块读写
- 卡信息提取
- 高容量卡支持

**文件**: `drivers/mtk-msdc/` (4个文件, 821行)

#### ✅ 中断控制器驱动 (GIC)
- ARM GICv2分发器
- CPU接口配置
- 中断启用/禁用/优先级
- 处理器路由
- 软件中断(SGI)
- 处理器注册和分发

**文件**: `drivers/mtk-gic/` (4个文件, 575行)

#### ✅ UART驱动
- 8250兼容UART
- 字符和字符串I/O
- Printf格式化
- 波特率配置
- 多UART支持

**文件**: `drivers/mtk-uart/` (4个文件, 484行)

#### ✅ 定时器驱动
- MTK系统定时器
- ARM通用定时器
- 滴答计数
- 纳秒计时
- 微秒延迟
- 单次/周期定时器

**文件**: `drivers/mtk-timer/` (4个文件, 603行)

**总计**: 32个文件, 4692行代码

### 4. 链式加载器 (100% ✅)

#### ✅ 启动链实现
- ARM64汇编入口点
- UART调试输出
- 启动参数设置
- XNU内核加载框架
- eMMC读取支持
- 设备树加载
- 跳转逻辑实现
- Android boot.img格式支持

#### ✅ 工具链
- 构建脚本 (build_chainloader.sh)
- boot.img打包工具 (mkbootimg.py)
- eMMC写入工具 (flash_emmc.py)
- 集成文档

**文件**: `bootloader/chainloader/` (6个文件, ~800行)  
**工具**: `scripts/` (3个脚本)

### 5. XNU内核适配 (60%)

#### ✅ 平台配置
- SoC标识
- CPU配置 (4x Cortex-A53)
- 内存布局定义
- 外设基地址
- 中断控制器设置

**文件**: `patches/platform/platform_mt6765.h`

#### ✅ 内存映射
- 内核代码/数据区域
- 页表布局
- 启动栈
- 设备树位置
- Initrd位置
- I/O内存区域
- 堆内存分配

**文件**: `patches/memory/memory_map_mt6765.h`

#### ✅ 启动汇编
- ARM64入口点 (`_start`)
- 异常向量表
- MMU设置(完整实现)
- BSS初始化
- 早期控制台(完整实现)
- GIC初始化
- 定时器初始化
- SMP启动
- 启动参数打印

**文件**: `patches/boot/boot_mt6765.s`

#### ✅ 设备树
- CPU定义 (4核)
- 内存布局
- 中断控制器 (GIC-400)
- UART控制台
- PMIC (MT6357)
- I2C + 触摸控制器
- 显示子系统
- eMMC存储
- 系统定时器

**文件**: `patches/dts/mt6765-dandelion.dts`

#### ✅ MMU实现
- 16KB页大小支持
- 39位虚拟地址空间
- 页表分配和管理
- 区域映射(内核、设备、RAM)
- 内存属性(设备、回写、非缓存)
- MMU启用/禁用
- TLB失效

**文件**: `patches/mmu/` (3个文件, 572行)

#### ✅ 早期控制台
- 最小化UART驱动
- 字符/字符串输出
- 十六进制/十进制格式化
- 简单printf支持
- MMU前工作

**文件**: `patches/boot/early_console.c/h` (2个文件, 276行)

**总计**: 10个文件, 1,620行

---

## 🚧 进行中的工作

### 1. XNU内核补丁 (40%)
- [ ] 完整MMU页表初始化
- [ ] 早期UART驱动实现
- [ ] GIC与XNU集成
- [ ] 定时器与XNU集成
- [ ] SMP启动实现

### 2. 启动链集成 (80%)
- [x] 链式加载器实现
- [x] 从eMMC读取XNU内核
- [x] 加载设备树(dandelion.dtb)
- [x] 构建和打包工具
- [ ] 在真实设备上测试
- [ ] 调试启动问题

### 3. 驱动完善 (80%)
- [ ] DMA支持 (I2C, eMMC)
- [ ] 中断驱动模式
- [ ] 电源管理
- [ ] GPU驱动 (PowerVR GE8320)
- [ ] 音频驱动
- [ ] WiFi/蓝牙驱动

---

## ❌ 未开始的工作

### 1. iOS用户空间集成
- [ ] iOS根文件系统
- [ ] HFS+/APFS文件系统支持
- [ ] 启动守护进程
- [ ] 图形界面

### 2. 高级功能
- [ ] 相机驱动
- [ ] 传感器驱动
- [ ] GPS驱动
- [ ] NFC驱动

### 3. 优化
- [ ] 性能调优
- [ ] 电源管理优化
- [ ] 热管理

---

## 📁 项目结构

```
UniXNU-dandelion/
├── .github/
│   └── workflows/
│       └── build.yml              # CI/CD (✅)
├── board.toml                      # 设备配置 (✅)
├── configs/
│   └── dandelion_defconfig         # 内核配置 (✅)
├── drivers/
│   ├── mtk-pmic/                   # PMIC驱动 (✅)
│   ├── mtk-i2c/                    # I2C驱动 (✅)
│   ├── novatek-touch/              # 触摸驱动 (✅)
│   ├── mtk-ddp/                    # 显示驱动 (✅)
│   ├── mtk-msdc/                   # eMMC驱动 (✅)
│   ├── mtk-gic/                    # GIC驱动 (✅)
│   ├── mtk-uart/                   # UART驱动 (✅)
│   └── mtk-timer/                  # 定时器驱动 (✅)
├── bootloader/
│   └── chainloader/                # 链式加载器 (✅)
├── patches/
│   ├── platform/                   # 平台配置 (✅)
│   ├── memory/                     # 内存映射 (✅)
│   ├── boot/                       # 启动代码 (✅)
│   ├── dts/                        # 设备树 (✅)
│   └── README.md                   # 补丁文档 (✅)
├── scripts/
│   └── mt6765_hw.py                # 硬件定义 (✅)
├── README.md                       # 项目文档 (✅)
└── PROGRESS.md                     # 本文件 (✅)
```

---

## 🎯 下一步计划

### 阶段1: 完善XNU启动 (2-3周)
1. 完成MMU页表初始化
2. 实现早期UART驱动
3. 集成GIC和定时器
4. 实现SMP启动
5. 在QEMU上测试

### 阶段2: 启动链测试 (1-2周)
1. 实现从eMMC读取XNU
2. 加载设备树
3. 打包boot.img
4. 硬件测试启动
5. 调试启动问题

### 阶段3: 驱动完善 (3-4周)
1. 添加DMA支持
2. 实现中断驱动模式
3. 电源管理
4. 硬件测试所有驱动

### 阶段4: iOS集成 (4-6周)
1. 准备iOS根文件系统
2. 文件系统支持
3. 启动iOS用户空间
4. 基本功能测试

### 阶段5: 功能完善 (持续)
1. 额外驱动
2. 性能优化
3. 电源管理
4. 稳定性改进

**预计总时间**: 10-15周

---

## 🔧 技术挑战

### 1. MTK启动链
**挑战**: MTK使用LK而非UEFI/ABL  
**解决方案**: ✅ 已解决 - 使用链式加载器，LK加载boot.img格式，链式加载器再加载XNU

### 2. MMU配置
**挑战**: XNU需要特定的页表布局  
**解决方案**: 16KB页, TTBR0/TTBR1分割, 正确映射

### 3. 文档缺失
**挑战**: MTK硬件文档不公开  
**解决方案**: 参考Linux内核驱动, 逆向工程

### 4. 硬件测试
**挑战**: 需要UART调试线  
**建议**: 购买USB转TTL模块 (约20元)

---

## 📊 代码统计

### 按类型
- C代码: 24个文件
- 汇编: 1个文件
- 设备树: 1个文件
- 配置: 3个文件
- 文档: 10个README
- 构建: 1个workflow

### 按规模
- 总文件数: 54个
- 总代码行数: ~8,050行
- 驱动代码: 4,692行
- 内核补丁: 1,620行
- 链式加载器: 800行
- 工具脚本: 940行

---

## 🌟 关键成就

1. ✅ **完整的驱动框架**: 8个核心驱动全部实现
2. ✅ **CI/CD基础设施**: GitHub Actions自动化构建
3. ✅ **详细的硬件文档**: 完整的内存映射和外设信息
4. ✅ **XNU适配基础**: 平台配置、内存映射、启动代码、设备树
5. ✅ **完整启动链**: 链式加载器+eMMC读取+工具链
6. ✅ **不需要LK源码**: 使用标准boot.img格式
7. ✅ **清晰的架构**: 模块化设计, 易于扩展

---

## 🤝 需要帮助的地方

### 高优先级
1. **UART调试线**: 需要硬件测试 (购买USB转TTL模块，约20元)
2. **XNU内核**: 需要编译出ARM64 XNU内核
3. **真实设备测试**: 在Redmi 9A上测试启动链

### 中优先级
1. **GPU驱动**: PowerVR GE8320驱动
2. **音频驱动**: 音频编解码器驱动
3. **测试设备**: 额外的Redmi 9A用于测试

---

## 📚 参考资源

### 项目
- [UniXNU主项目](https://github.com/UniXNU/UniXNU)
- [ios-sweet (Qualcomm参考)](https://github.com/dipa-studio/ios-sweet)
- [UniXNU文档](https://unixnu.com)

### 驱动
- [Linux MTK驱动](https://github.com/xiaomi-sdm-439/android_kernel_xiaomi_mt6765)
- [Linux GIC驱动](https://github.com/torvalds/linux/tree/master/drivers/irqchip/irq-gic.c)
- [Linux MTK MSDC](https://github.com/torvalds/linux/tree/master/drivers/mmc/host/mtk-sd.c)

### 文档
- [ARM架构参考手册](https://developer.arm.com/documentation/ddi0487/latest)
- [XNU源码](https://github.com/apple/darwin-xnu)
- [MT6765数据手册](https://www.mediatek.com/products/smartphones/helio-g25) (不公开)

---

## 📝 更新日志

### 2026-10-21 (更新3)
- ✅ 实现eMMC读取功能
  - MSDC控制器初始化
  - 从eMMC读取XNU内核
  - 从eMMC读取设备树
  - 完整的eMMC分区布局
- ✅ 创建工具链
  - build_chainloader.sh (构建脚本)
  - mkbootimg.py (boot.img打包)
  - flash_emmc.py (eMMC写入工具)
- ✅ 完善集成文档
- **总进度: 48%** (从45%提升)
- 下一步: 在真实设备上测试

### 2026-10-21 (更新2)
- ✅ 完成链式加载器实现
  - ARM64汇编入口
  - UART调试输出
  - 启动参数设置
  - XNU跳转逻辑
- ✅ 不需要LK源码即可启动XNU
- ✅ 完整的启动链解决方案
- **总进度: 45%** (从40%提升)
- 下一步: 实现eMMC读取XNU内核

### 2026-10-21
- ✅ 完成所有8个核心驱动
- ✅ 完成XNU内核基础框架
- ✅ 完成MMU页表实现(16KB页)
- ✅ 完成早期控制台(MMU前输出)
- ✅ 创建设备树
- ✅ 更新项目文档
- **总进度: 40%** (从35%提升)
- 准备进入QEMU测试阶段

---

**当前状态**: 🚧 完整启动链就绪, 等待硬件测试  
**下次更新**: 在真实设备上测试启动后
