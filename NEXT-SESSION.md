# 新会话交接 —— 从这里开始

## 项目

把 **iPadOS 17.7.11 (21H461)** 移植到 **Redmi 9A**（代号 dandelion，MT6762G，4 GB RAM）
用的是 UniXNU 官方流程。设备仓库：`D:\aios\_work\UniXNU-dandelion`
唯一的真相来源：**`HANDOFF.md`**（已写到 §0.101，共 96+ 节，先读 §0.90–§0.101）

## 现在的状态（截至 2026-11-09 09:45）

**系统已经能启动到 iOS 的服务图**：

| 里程碑 | 状态 |
| --- | --- |
| UEFI → 内核 → eMMC → APFS | ✅ |
| 根卷 + SystemOS 挂载 | ✅ |
| dyld 共享缓存 | ✅ |
| launchd 启动 + **28 个 boot task 全过** | ✅ |
| `Early boot complete. Continuing system boot.` | ✅ |
| **366 个进程**、APFS 事务、Mach-O 加载 | ✅ |
| 存储速度 | **25–33 MB/s**（原 226 KB/s，约 100 倍）|
| **显示驱动** | ⏳ **`IOMobileFramebufferLegacy::start()` 已执行，但被 provider 类型拒绝** |

## 立刻要做的第一件事

**显示卡在这里**（日志原文）：

```
virtual bool IOMobileFramebufferLegacy::start(IOService *)
virtual bool IOMobileFramebufferLegacy::start(IOService *): provider was not AppleARMIODevice.
```

`provider was not AppleARMIODevice` **不在本仓库、也不在 GopFramebuffer 组件里** ✓
它来自**原始 Apple 的 `IOMobileFramebufferLegacy::start()`**（通过 ABI 表复用）
即：接线、ABI、方法地址**全都对** ✓ 只是原版代码要求父节点是 `AppleARMIODevice`

`start` 是 **IOKit 自动调用**的（源码里没有显式调用）
→ 能控制的只有 **provider 是谁** ✓

**第一步**：读 `package/DandelionLegacyDisplay.cpp` 里创建 nub / `displayTreeParent` /
`attach` 的那段代码（`native_legacy_display_checkpoint()` 附近），把 nub 改成
`AppleARMIODevice` 类型（或其子类），或看 `drivers/GopFramebuffer/NativeLegacyDisplay.cpp`
里 `startDisplay()` 的位置。HANDOFF §0.101 列了三条路线。

## 第二件事（独立，也重要）

```
VM Swap Subsystem is ON
Failed to open swap file 30
vm_swap_create_file failed @ 276 / 296 secs
```

Data 卷已挂载 ✓ 但 `/private/var/vm` 可能不存在或不可写
→ 在 `scripts/build-data-seed.py` 的 `SEED_FILES` 里加该目录即可

## 第三件事（内存，收益最大）

设备 **4 GB**，内核只拿到 **1 GB**（`vm_page_bootstrap: 65536 页`）→ jetsam 提前杀进程

根因已找到并修复：`uefi-dandelion/Platforms/Xiaomi/dandelionPkg/Library/MemoryMapLib/MemoryMapLib.c`
的内存映射表**只描述到 3 GB**，顶部 2 GB 不可见 ✓
已提交 `9e10dd5`：`{"DRAM High Conv", 0xC0000000, 0x80000000, ...}`

**但 WSL 里编不了 UEFI**（EDK2 环境缺 `Common/Mu` 等子模块）
→ 需要在 **Windows** 上编译，操作清单在 **`docs/UEFI-WINDOWS-CHECKLIST.md`** ✓

## 关键路径

```
设备仓库     D:\aios\_work\UniXNU-dandelion
框架         /root/work/UniXNU（WSL Ubuntu）
UEFI 仓库    /root/work/uefi-dandelion
构建        bash /mnt/d/aios/_work/build_with_bootstrap.sh
            产物 D:\aios\out3\boot-data.img
刷机        fastboot flash boot D:\aios\out3\boot-data.img
读日志      powershell -File D:\aios\_work\collect_nowipe.ps1  （需手机在 fastboot）
```

## ⚠️ 三条硬规矩（都是血的教训）

1. **绝不能删 `tree/build`** ✗
   它含两个引导依赖：`storage-abi/IOBlockStorageDevice-reference.o`（存储符号派生 ✓）
   和 `xnu/ios-obj`（框架的跳过判定 ✓）。删了要花很久才能恢复（见 §0.97）。
   **想强逼内核重编译 → 改补丁内容**（框架按内容哈希判定），例如给补丁加一行注释。

2. **补丁锚点不能只匹配函数名** ✗
   会落在 `static void` 和函数名之间，声明变成函数内局部。锚点要包含返回类型那一行。

3. **`native_keyevents_allocate` 的真实签名**是 `IOService *(void)`
   （`drivers/AppleKeyStoreState/NativeKeyStoreEvents.cpp:162`），签名不符会一直 undefined。

## 所有代码已提交

- 设备仓库 `main` 最新提交 `1891cd9`
- UEFI 仓库 `9e10dd5` / `03aaa42`
- 工作区干净 ✓
