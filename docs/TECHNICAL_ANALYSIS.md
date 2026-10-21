# UniXNU-dandelion 技术分析报告

**项目**: 在 Xiaomi Redmi 9A (MT6765) 上运行 iOS  
**日期**: 2026-10-21  
**状态**: 基础研究完成，技术路线确定

---

## 1. 项目目标

在 Xiaomi Redmi 9A (代号 dandelion) 上运行 iOS 16，通过移植 Apple XNU 内核到 MediaTek MT6765 (Helio G25) 平台。

---

## 2. 硬件信息

### 2.1 设备规格

| 组件 | 规格 |
|------|------|
| 设备 | Xiaomi Redmi 9A (M2006C3LC) |
| SoC | MediaTek MT6765 (Helio G25) |
| CPU | 8x Cortex-A53 @ 2.0 GHz |
| GPU | PowerVR GE8320 |
| RAM | 2/3/4 GB LPDDR3 |
| 存储 | 32 GB eMMC |
| 显示 | 6.53" IPS LCD, 720x1600 @ 60Hz |
| 显示接口 | MIPI DSI |
| 显示驱动 | Novatek NT36525B |
| 触控 | Novatek 触控控制器 (I2C3, 0x5D) |
| PMIC | MediaTek MT6357 |
| 调制解调器 | 集成 LTE |

### 2.2 内存映射

```
物理地址空间:
0x40000000 - 0xDFFFFFFF  RAM (~4GB)
0x10000000 - 0x14FFFFFF  外设 I/O (80MB)
  0x10221000             GIC Distributor
  0x10222000             GIC CPU Interface
  0x11002000             UART0
  0x1100F000             I2C3
  0x11230000             MSDC0 (eMMC)
  0x14000000             DDP (显示)
  0x14010000             DSI
```

### 2.3 启动流程

```
Boot ROM (MTK 专有)
    ↓
Preloader (MTK 专有)
    ↓
LK (Little Kernel) - Android 引导加载程序
    ↓
加载 boot.img 到 0x40000000
    ↓
执行 boot.img 中的内核
```

**关键信息：**
- LK 加载 boot.img 到物理地址 `0x40000000`
- boot.img 中的内核被加载到 `0x40080000`（从原始 boot.img 分析得出）
- Tags/DTB 放置在 `0x47880000`

---

## 3. 尝试过的方法

### 3.1 直接启动方案

**思路：** 从 LK 直接加载 XNU 内核

**实现：**
1. 编写 chainloader（从 boot.img 加载）
2. chainloader 从 eMMC 读取 XNU 内核
3. 跳转到 XNU 入口点

**结果：** ❌ 失败

**原因分析：**
- 内核立即崩溃，无法看到任何输出
- 没有 UART，无法调试
- 可能原因：
  - 启动参数格式不兼容
  - XNU 期望的硬件环境不满足
  - 缺少必要的初始化代码
  - 设备树格式不正确

### 3.2 测试内核验证

**思路：** 使用简单的测试内核验证启动链

**实现：**
1. 编写最小化测试内核（UART 输出 + 显示 + eMMC 日志）
2. 打包为 boot.img 格式
3. 尝试刷入和启动

**结果：** ❌ 失败

**原因：**
- 测试内核也立即崩溃
- 即使是 gzip 压缩格式也失败
- 可能是 boot.img 格式或加载地址问题

---

## 4. UniXNU 方案分析

### 4.1 UniXNU 项目概述

**项目地址**: https://unixnu.com/  
**GitHub**: https://github.com/UniXNU/UniXNU

**目标：** 在非 Apple ARM64 硬件上运行 iOS/macOS

**当前支持：**
- ✅ Xiaomi Pad 6S Pro (sheng) - Snapdragon 8 Gen 2 (SM8550)
- ❌ 不支持 MTK 设备

### 4.2 UniXNU 启动链

```
Android Bootloader
    ↓ 加载 boot.img (gzip 压缩)
BootShim (小型汇编引导程序)
    ↓ 解压并加载 UEFI 固件
Mu-Silicium UEFI (完整 UEFI 环境)
    ↓ 启动 UniXNU 启动管理器
UniXNU Boot Manager
    ↓ 加载 DarwinBoot
DarwinBoot
    ↓ 加载 XNU 内核
XNU Kernel
    ↓
iOS Userland
```

### 4.3 关键技术要点

**1. 需要 UEFI 固件**
- UniXNU 依赖 UEFI 提供的服务
- 使用 Mu-Silicium (Project Mu 的 ARM64 移植)
- 需要为每个设备单独移植 UEFI

**2. XNU 内核要求**
- 需要特定的启动参数格式 (DARWIN_BOOT_ARGS)
- 需要 Apple Device Tree (ADT) 格式
- 需要特定的设备树节点：
  - `/cpus`
  - `/arm-io`
  - `/defaults`
  - `/options`
  - `/chosen` (包含 CHRP NVRAM 代理)

**3. 编译要求**
- 需要 macOS + Xcode 编译 XNU
- XNU 版本：xnu-10063.141.1 (iOS 16)
- 页大小：4KB (BCM2837 配置) 或 16KB (iOS 标准)

### 4.4 Mu-Silicium 分析

**项目地址**: https://github.com/Project-Silicium/Mu-Silicium

**支持的设备：**
- ✅ Exynos 设备 (Samsung)
- ✅ Snapdragon 设备 (Qualcomm)
- ✅ MediaTek 设备 (部分)
  - MT6768 (Helio P65/G85)
  - MT6789 (Helio G99)
- ❌ MT6765 (Helio G25) - 不支持

**关键发现：**
- MT6768 和 MT6765 都是 MTK，但寄存器地址不同
- MT6768 GIC: 0x0C000000
- MT6765 GIC: 0x10221000
- 需要为 MT6765 单独移植

---

## 5. 技术路线分析

### 5.1 方案对比

| 方案 | 优点 | 缺点 | 可行性 |
|------|------|------|--------|
| 直接启动 | 简单直接 | 缺少 UEFI 层，兼容性差 | ❌ 已证明失败 |
| UEFI 移植 | 正确的方法，UniXNU 验证可行 | 工作量大，需要 vendor blobs，需要 UART 调试 | ⚠️ 理论可行，实际困难 |
| 等待社区 | 最省力 | 时间不确定 | ⚠️ 被动 |
| 转向 Linux | 更成熟，更多支持 | 不是 iOS | ✅ 可行 |

### 5.2 UEFI 移植详细分析

**工作量估算：**
- 基础框架：2-3 周
- 驱动移植：1-2 月
- 调试优化：2-3 月
- **总计：4-6 月**

**关键挑战：**
1. **没有 UART** - 无法调试早期启动问题
2. **缺少 vendor blobs** - MTK 闭源二进制文件
3. **硬件差异** - MT6765 与现有支持的 MTK SoC 不同
4. **测试困难** - 每次测试都需要刷入，有变砖风险

**所需资源：**
- UART 调试线（必需）
- MTK 寄存器文档（不公开）
- Vendor blobs（难以获取）
- Mac 电脑（编译 XNU）
- 大量时间

---

## 6. 结论与建议

### 6.1 技术结论

1. **直接启动方案不可行** - 已证明失败，且无法调试
2. **UEFI 移植理论可行但实际困难** - 需要 UART、vendor blobs、大量时间
3. **当前条件下完成 iOS 启动的可能性极低**

### 6.2 建议路线

**短期（1-2 周）：**
1. 完善项目文档
2. 加入 Project Silicium Discord 和 UniXNU QQ 群
3. 询问社区是否有人做过 MTK 移植
4. 寻找 UART 测试点（虽然你说没有，但可以再找找）

**中期（1-3 月）：**
1. 如果找到 UART：开始 UEFI 移植
2. 如果没有 UART：
   - 继续完善文档
   - 等待社区支持
   - 考虑转向 Linux（更现实的目标）

**长期：**
1. 等待 Mu-Silicium 或 UniXNU 社区支持 MT6765
2. 或者等待有人分享 MTK vendor blobs
3. 或者放弃 iOS 目标，转向 Android/Linux 定制

### 6.3 现实评估

**成功概率：**
- 有 UART + vendor blobs：30%
- 有 UART，无 vendor blobs：10%
- 无 UART：5%

**时间投入：**
- 乐观估计：3-4 月
- 现实估计：6-12 月
- 悲观估计：永远无法完成

---

## 7. 已创建的资源

### 7.1 代码

- ✅ 8 个硬件驱动（PMIC、I2C、Touch、Display、eMMC、GIC、UART、Timer）
- ✅ Chainloader（从 LK 加载 XNU）
- ✅ 测试内核（验证启动链）
- ✅ MMU 初始化代码
- ✅ 启动汇编代码
- ✅ 设备树

### 7.2 文档

- ✅ 硬件信息收集
- ✅ 内存映射分析
- ✅ UniXNU 技术分析
- ✅ Mu-Silicium 结构分析
- ✅ UEFI 移植基础框架

### 7.3 工具

- ✅ WSL 编译环境
- ✅ 交叉编译工具链
- ✅ boot.img 打包工具
- ✅ QEMU 测试环境

---

## 8. 下一步行动

### 立即可做：

1. **加入社区**
   - Project Silicium Discord: https://discord.gg/Dx2QgMx7Sv
   - UniXNU Telegram: https://t.me/unixnu
   - UniXNU QQ 群: 1126116759

2. **寻找 UART**
   - 查看主板照片
   - 搜索 "Redmi 9A UART" 或 "MT6765 UART test point"
   - 查看 XDA/4PDA 论坛

3. **寻找 vendor blobs**
   - 搜索 MTK 官方文档
   - 查看其他 MTK 设备的开源项目
   - 联系 MTK 开发者关系

### 如果找到 UART：

1. 连接 UART，验证串口输出
2. 测试现有代码，看崩溃原因
3. 开始 UEFI 移植

### 如果找不到 UART：

1. 继续完善文档
2. 等待社区支持
3. 考虑转向 Linux 目标

---

## 9. 总结

这个项目展示了在一个具体的硬件平台上移植 iOS 的技术挑战。虽然我们收集了大量的硬件信息，创建了完整的驱动框架，并深入分析了 UniXNU 的技术方案，但受限于：

1. **缺少 UART 调试**
2. **缺少 vendor blobs**
3. **MTK 平台支持不足**

在当前条件下，完成 iOS 启动的可能性很低。

**建议：** 将此项目作为长期研究项目，继续文档化和社区参与，同时保持开放心态，考虑替代目标（如 Linux）。

---

**报告完成日期**: 2026-10-21  
**报告作者**: UniXNU-dandelion 项目团队
