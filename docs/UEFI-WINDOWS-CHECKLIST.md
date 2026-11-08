# 在 Windows 上重建 UEFI（内存翻倍，1 GB → 2 GB）

这份清单是 HANDOFF §0.93/§0.94 的操作版。目标：产出带 `DRAM High Conv` 的 `SILICIUM_UEFI.fd`。

## 为什么需要它

- 设备有 **4 GB** 内存（Android `MemTotal = 3897064 kB`）
- 内核只拿到 **1 GB**（`vm_page_bootstrap: 65536 页`）
- 原因：UEFI 的内存映射表只描述到 3 GB，顶部 2 GB 对 DXE/DarwinBoot 不可见
- 后果：`owned_memory_size` 提到 1 GB 或 2 GB 都返回 `Out of Resources`

## 需要的改动（已经写好，只需应用）

在 `uefi-dandelion/Platforms/Xiaomi/dandelionPkg/Library/MemoryMapLib/MemoryMapLib.c`
的 `gMemoryDescriptor[]` 里，`ATF Ramdump` 那一行之后加一条：

```c
  {"DRAM High Conv",     0xC0000000, 0x80000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},
```

（该改动已提交在 uefi-dandelion 仓库的 `9e10dd5`）

## Windows 构建步骤

1. **准备**
   - Git for Windows、Python 3.11+、LLVM、VS Build Tools、GnuMake32
   - 把 `GnuMake32` 的 `bin` 目录加进 PATH
   - `python3 -m pip install -r pip-requirements.txt`

2. **克隆（含子模块）**
   ```cmd
   git clone --recursive https://github.com/Project-Silicium/Mu-Silicium.git
   ```
   我们的设备仓库是 `uefi-dandelion`（含 dandelionPkg）

3. **应用内存映射修复**
   按上面那一条改 `MemoryMapLib.c`（或 cherry-pick `9e10dd5`）

4. **拉依赖**
   ```cmd
   stuart_update -c Platforms\Xiaomi\dandelionPkg\DeviceBuild.py
   ```

5. **构建**
   ```cmd
   python Platforms\Xiaomi\dandelionPkg\DeviceBuild.py
   ```

6. **产物**
   ```
   Build\dandelionPkg\RELEASE_*\SILICIUM_UEFI.fd
   ```

## 拿回来之后（我在 WSL 里做）

1. 把 `SILICIUM_UEFI.fd` 放到 `D:\aios\` 下
2. 我把 `owned_memory_size` 改成 `0x80000000`（2 GB）
3. 重建 boot.img → 刷入
4. 验证内核报告 **约 131000 free pages**（而不是 65577）
5. 观察 jetsam 是否推迟、`procs` 是否突破 366

## 已在 WSL 里做好的（不用重做）

- ✅ `Silicium-ACPI` 已 clone 到 `uefi-dandelion/Silicium-ACPI`
- ✅ `nasm_ext_dep.yaml` 已改名（arm64 不需要 x86 汇编器）
- ✅ `UniXNUPkg` 已软链接进 `dandelionPkg`
- ✅ 内存映射修复已提交（`9e10dd5`）

## 当前卡点（WSL 侧）

`Common/Mu`、`Common/Mu_OEM_Sample`、`Mu_Basecore` 三个子模块未初始化 ✗
→ `CommonPlatform.PackagesPath` 缺失 → DSC 的 `UniXNUPkg/...` 相对路径解析失败

在 Windows 上用 `git clone --recursive` 一次就能带全 ✓ 所以那边更容易成功 ✓
