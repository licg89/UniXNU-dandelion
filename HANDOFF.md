# dandelion 移植交接（给另一台机器的 opencode）

最后更新：**2026-11-02 18:20 (+08:00)**。这份文件是**唯一的事实来源**，按它就能接着干。

---

## 0. 一句话现状

已按 **UniXNU 官方流程**补齐固件的"必需集成"，并找到"纯灰屏 + 约 10 秒复位 + 固件日志一个字节不写"的根因（**固件卷地址三处不一致**，已修）。最新镜像由 CI 构出（run `41423677833`，commit `8a7021e`），**尚未在真机上验证**。

---

## 0.1 本轮结论（2026-11-02 下午）

### 已验证的事实（都有证据，不是推测）

| 事实 | 证据 |
|---|---|
| 面板与刷机链正常 | 旧固件 `dandelion-xnu/artifacts/boot-uefi-redband.img` 刷入后能显示文字、彩色横条和 UniXNU 菜单 |
| 新固件停在极早期 | 厂商 logo → **纯灰**（无绿条、无文字、无彩条）→ 约 10 秒复位 |
| 新固件从未运行 `PayloadBootDxe` | `flashinfo@30512` 的 `DDNLOG01` 块**逐字节不变**（sha256 前缀 `B6FA702A44ED6311`，内容仍是 2026-10-29 旧固件的日志）；该驱动在 APRIORI 里几乎最后 |
| 灰屏与 GOP 无关 | 第一次 CI 构建（run `41410434932`）时 `USE_CUSTOM_DISPLAY_DRIVER=1`（无 GOP）、且还没有任何官方模块，**当时就是灰屏** |
| 灰屏与官方四模块无关 | 同上 |
| 内核不在 flashinfo | `UniXNU/scripts/unixnu/payload.py` 首行：payload 是"**the FAT volume that becomes the ramdisk of the boot image**"；实测 `boot`=64 MiB、`flashinfo`=16 MiB |

### 根因（已修，待真机验证）

固件卷地址在**三处**必须一致，其中一处没跟着改：

| 位置 | 值 |
|---|---|
| `Resources/Configs/dandelion.toml` `[uefi_fd]` | `base 0x41080000`, `size 0x300000` |
| CI 编译参数 | `-D FD_BASE=0x41080000 -D FD_SIZE=0x300000 -D FD_BLOCKS=768` |
| `Library/MemoryMapLib/MemoryMapLib.c` 的 `"UEFI FD"` | **`0x40080000, 0x00200000`（旧值，没同步）** |

后果不是"找不到地址"，而是**真正的 FD 区间 `0x41080000..0x41380000` 没有被保留**，PEI/DXE 会把它当普通内存分配，**覆盖正在执行的固件卷**。旧项目 `CORRECTIONS.md` C8 记录过这个故障签名：

> 把 FD 基址改错后，"表现就是和「没执行」一模一样的 10 秒复位，很容易误判"。

修复（`8a7021e`）：

```c
{"UEFI FD", 0x41080000, 0x00300000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
```

旧固件为什么没事：它**没有 BootShim**，FD 直接链接在 `0x40080000/2 MiB`，与内存图完全一致（旧 `dandelion.toml` 注释里写明了"不需要 BootShim"）。新线加了 BootShim、把 FD 挪到 `0x41080000`、尺寸涨到 3 MiB，却漏改内存图。

### 本轮按官方文档补的固件集成（`72065b8`、`99aea32`、`0d920da`）

官方 `unixnu.com/porting/uefi-firmware` 的"Required integration"要求 DSC `[Components]` 和 FV 里都有这六个模块，我们原来**一个 UniXNU/DarwinBoot 的都没有**（只有旧项目的 `PayloadBootDxe`，它启动 FV 里预编译的旧 `UniXNU.efi`）：

| 模块 | 状态 |
|---|---|
| `UniXNUPkg/Application/UniXNU/UniXNU.inf` | 已加 |
| `UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf` | 已加 |
| `UniXNUPkg/Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.inf` | 已加 |
| `DarwinBootPkg/Driver/DarwinBoot.inf` | 已加（注意：`DarwinBootPkg/DarwinBoot.dsc` 是独立 platform DSC，**不能** `!include`） |
| `DiskIoDxe` / `PartitionDxe` / `Fat` / `EnglishDxe` | 原本就有 |
| `[LibraryClasses]` 的 `UniXNULib` / `HobLib` / `FdtLib` | 已加 |

另外：

- 删除了会与 `UniXNU.inf` **撞 GUID**（`9B665204-…`）的旧预编译 `UniXNU.efi` FFS；
- `USE_CUSTOM_DISPLAY_DRIVER` 回到 `1`（**不要 GOP**）。官方 `troubleshooting` 对 "No display" 的建议是 `console = "text"`，而且 UniXNU 的 `Ui.c` 在拿不到 GOP 时会**自动退回文本控制台**；我们的 `DandelionConsoleDxe` 只产 `SimpleTextOut`、不产 GOP；
- 参考实现：`Platforms/Benesse/a04br3Pkg`（DSC 里的 `UniXNULib`/`UniXNU.inf`/`UniXNUDxe.inf`，`Include/DXE.inc` 里的 `DarwinBoot.inf`）。

---

## 0.2 下一步（严格按官方顺序，不要跳步）

1. **刷 run `41423677833`（`8a7021e`）的 `boot.img`**，看是否出现文字/彩条/菜单。
   - 产物：GitHub artifact `images-17`；本地曾下载到 `D:\aios\out3\…\boot\boot.img`（27.7 MB 的 zip，boot.img 59.5 MB）。
   - 刷法见 §8。
2. 若**仍然纯灰**：下一个单变量是 FD 基址 —— 回到 `0x40080000`（需处理 BootShim 自覆盖：`dst == src - shim_len`，会覆盖自身；要么把 shim 拷贝改成从高地址倒序，要么让 `FD_BASE` 与 `kernel_addr` 保持不同但换一个确认可用的地址），或者走官方 `reference/qemu` 用 QEMU 验证**同一份 FD**，先分清"FD 本身"还是"本机内存布局"。
3. 菜单一旦出现：按官方 `porting/log-capture` 接 **`AdvancedLogDxe` + Tools → Advanced Logger + 二维码**（这是固件日志的正式通道），然后按 `guide/debugging#green-bar` 做内核交接（绿条 → entry probe 蓝/品红条）。
4. 本机没有 UART、也没有 USB device mode；**持久日志**用 `flashinfo@30512` + adb（官方 `log-capture` §4 认可的 reader 方案）。

### 关于 HANDOFF 原标题 5-B（缩内核到 16 MB）——前提不成立

- 内核**不在 flashinfo**，而在 boot 分区的 payload FAT 卷里；`boot` 分区是 **64 MiB**，`boot.img` 实测 59.5 MB，装得下；
- 把 profile 的 `roots` 从 **31 裁到 9**（`licg89/UniXNU` 的 `profiles/os/ipados-17.7.11-21H461/profile.toml`，commit `892eb33`）之后，`kernel.macho` 从 51,003,392 **变成 51,683,328（反而大了 680 KB）**——说明 `roots` 不是尺寸主因，`xnu-10063` 本体才是。**所以 B 不是当前瓶颈，可以暂缓。**
- 注意 `com.apple.kec.AppleMatch` 是笔误，正确的是 **`com.apple.kext.AppleMatch`**；另外保留了 `com.apple.kext.CoreTrust`（AppleImage4 的信任链需要）。

---

## 1. 仓库（全部在 GitHub，`licg89/` 名下）

| 仓库 | 作用 | 当前 HEAD（本轮结束时） |
|---|---|---|
| `UniXNU-dandelion` | 设备仓库（board.toml / dts / defconfig / CI / HANDOFF） | `609109f` |
| `UniXNU-mt6765-common` | SoC 仓库（soc.toml / dts / configs；**xnu 补丁序列目前为空**，GIC/定时器交给 ArmCpu 组件） | `97d9303` |
| `uefi-dandelion` | 固件仓库（Mu-Silicium 基座 + dandelionPkg/MT6765Pkg） | `8a7021e` |
| `UniXNU`（fork） | 框架 fork（profile 的 roots 已裁） | `892eb33` |
| `dandelion-xnu` | **旧项目，仅参考**（硬件实测、eMMC 日志通道、原厂 boot 备份） | `bef572f` |

---

## 2. 构建环境

- WSL Ubuntu（**不要在 `/mnt/d` 上构建**，9p 慢 5–10 倍）；工具链 `/opt/unixnu-linux/llvm/bin/clang`（clang 24，`scripts/linux/setup-toolchain.py`，1–2 小时）；`lzfse`；`dtc`。
- **本机（Windows）没有 WSL 发行版**，所以一律用 GitHub Actions 构建：
  `py -3.13 tools/ci/waitfetch.py` 是给 XNU 用的；本线用 `UniXNU-dandelion` 的 `package` workflow（`workflow_dispatch`，输入 `os=17`，macos-26，约 25 分钟）。
  触发/取产物的小工具在 `%TEMP%\opencode\pkg_run.py`、`getart.py`；`pkg_run.py dispatch` 触发，`pkg_run.py` 轮询并打印 artifact。

---

## 3. 已经跑通并验证的部分

| 步骤 | 证据 |
|---|---|
| 配置校验 | `== check done: 59 patches, configuration dandelion_defconfig` |
| 依赖 fetch | xnu `d8b80295118e`、libplatform、libdispatch、libpthread、corecrypto、IOStorageFamily |
| IPSW | 框架自动下载 iPadOS 17.7.11 (21H461) 并解包 |
| 内核编译/链接 | `mach.development.bcm2837`（ArmCpu + ArmDTPlatform） |
| prelink | `kernel.macho` **51,683,328** 字节（9 个 roots） |
| 固件 | `SILICIUM_UEFI.fd` **3 MiB**（FD_SIZE 0x300000，`FD_BLOCKS=768`） |
| payload | `payload.img` **56 MiB**（FAT 卷，含 `EFI/UniXNU/…` 与 `kernel.macho`） |
| bootimg | `boot.img` **59,504,640** 字节，`step verify` 通过 |
| CI 产物路径 | 已修：artifact 从 `out/example/…` 改为 `out/*/*/*/…`（`9294415`），否则每次打包在最后一步失败 |
| **真机** | 旧固件 ✅ 文字+彩条+菜单；新固件 ❌ 纯灰 → 复位（根因见 §0.1） |

---

## 4. 卡点（当前唯一）

**新固件在真机上停在 DXE 之前**：纯灰、无任何输出、日志块不写。已找到根因并修复（§0.1），**但还没验证**。

---

## 5. 工具（本轮新增，都在 `UniXNU-dandelion/tools/`）

| 工具 | 用途 |
|---|---|
| `tools/collect-log.ps1` | 进 Android 后一条命令读固件日志：`adb dd flashinfo@30512 → pull → 解码`；**必须传 `-Serial`**（本机还挂着模拟器，裸 `adb` 会报 multiple devices） |
| `tools/ddnlog.py` | 解码 `DDNLOG01` 块；`--self-test` 自检；用旧项目真实 dump 验证过，与旧解码器**逐字节一致**（sha256 `6C4DBD61DC0C72C7…`） |
| `tools/round.ps1` | 一轮流程：`flash` / `android` / `log` / `status` / `wait-fastboot`（注意：`flash` 阶段要用 `-Serial`） |

日志块布局：`flashinfo` 相对扇区 **30512** 起 **128 扇区**（64 KiB），magic `DDNLOG01`，由 `PayloadBootDxe` 写入；写前要求目标区是 0x00/0xFF **或** 是它自己上次写的 `DDNLOG01`/`DLXLOG01`（`SpanIsOurs()`），所以旧块不会挡住它。

---

## 6. 一路上踩过并已修掉的坑

| 现象 | 根因 | 修法 |
|---|---|---|
| `--os ios-16.5-…: expected one of …` | 框架只有 ipados profile | 用 `--os 17` |
| `feeds.toml: Invalid statement` | Windows 写文件带 **BOM** | Python 以 `encoding='utf-8'` 重写 |
| `is an EXAMPLE device repository` | `example = true` | 改 `false` |
| `scripts/package.sh: Permission denied` | Windows 丢可执行位 | `git update-index --chmod=+x` |
| `couldn't find remote ref 97d9303` | feeds 用了缩写 SHA | 用完整 40 位 |
| `is not a Mu-Silicium checkout` | 固件仓库不是 Mu 树 | 以 `mouseos/uefi-a04br3` 为基座 + 我们的包 |
| `PACKAGES_PATH doesn't exist: Common/Mu` | 子模块没有 gitlink | clone 三个子模块后 `git add` 记录 gitlink |
| `error 000E: MT6765Pkg/MT6765Pkg.dsc.inc not found` | DSC include 路径 | 改 `Silicon/MediaTek/MT6765Pkg/...`，并在 `[firmware] packages` 列出包目录 |
| `#if ENABLE_SECUREBOOT == 1` invalid token | DSC 缺 `DEFINE` | `!ifndef` + `DEFINE = 0`，必须在 `!include` 之前 |
| `Private set but not used [-Werror]` | clang 24 更严 | `*_CLANGPDB_*_CC_FLAGS = … -fshort-wchar -Wno-error -Wno-unused-but-set-variable`（用 `=`，不要 `==`） |
| `sizeof (L"A") == 8` static assert | 缺 `-fshort-wchar` | 同上 |
| `_mockfs_vfsops` undefined（间歇） | OS 片段把 `CONFIG_XNU_SOURCE_MOCKFS` 设为 y | defconfig 里写 `CONFIG_XNU_SOURCE_MOCKFS=n`（**值不能带行尾注释**） |
| `mockfs fixture archive missing` | 同上 | 保留 `build-mockfs-fixture.py` 在 `[kernel] archives` |
| `CPU affinity must fit a u32` | `/cpus` 下有无 `reg` 的 Linux 节点 | 给它们 `status = "disabled";`（不要删） |
| `timebase-frequency requires one cell` | DTS 没该属性 | `/cpus` 里加 `timebase-frequency = <0xc65d40>;` |
| `chip_id = "TODO"` | 模板占位 | `chip_id = "6765"` |
| `DTB image must not be empty` | `header_version = 2` 要求独立 `--dtb` | 固件配置 `header_version = 0`、`append_dtb = false` |
| `the payload files take 48 MiB; raise size_mib` | 容量必须严格大于文件 | `[payload] size_mib = 56` |
| `boot.img` 超 64 MiB 分区 | payload + FD + shim | `size_mib = 56` → 59.5 MB ✓ |
| CI 全部失败（`payments failed`） | 私有仓库 Actions 计费 | 设备仓库设为 **public** |
| **CI 打包成功但 artifact 为空** | `package.yml` 仍指向 `out/example/…` | 改成 `out/*/*/*/…`（`9294415`） |
| **prelink: Missing declared dependency com.apple.kec.AppleMatch** | HANDOFF 里写的 bundle id 是笔误 | 用 `com.apple.kext.AppleMatch`（`892eb33`） |
| **`DarwinBootPkg/Driver/DarwinBoot.dsc.inc` not found** | 当前 DarwinBoot 包没有这个文件，只有独立 `DarwinBoot.dsc` | 直接把 `DarwinBoot.inf` 加进 `[Components]`（`99aea32`） |
| **纯灰屏 + 10 秒复位 + 零输出** | FD 基址/大小与 `MemoryMapLib` 不一致 → FD 区间未保留被覆盖 | §0.1（`8a7021e`） |
| `adb` 报 multiple devices/emulators | 本机挂着 3 个模拟器 | 一律 `-Serial QS55XCPRCA9LDA5T` |
| PowerShell `>` 写出 UTF-16 破坏补丁/切片 | 重定向默认编码 | 用 Python 写二进制，或 `Out-File -Encoding utf8` |

---

## 7. 硬件实测值（本机复核过的）

| 项 | 值 |
|---|---|
| 设备 | Xiaomi Redmi 9A（M2006C3LC / dandelion），MT6762G，8×A53，Mali-G72 MP1 |
| 系统 | LineageOS 20 / Android 13，Magisk root（`adb shell su -c id` → uid=0） |
| 分区实测 | `boot` **64 MiB**、`flashinfo` **16 MiB**、`userdata` 53,044 MiB、`super` 4.5 GiB、`metadata` 32 MiB |
| 原厂/当前 Android boot | `D:\aios\artifacts\boot-android-lineage.img` = `D:\aios\dandelion-xnu\out\boot_backup.img`（sha256 `98F5ED08…`，67,108,864 字节） |
| 内核窗口 | DarwinBoot 上限 **256 MiB**（2G/1G/512M 都 `out of resources`） |
| 链接基址 | `0xFFFFFFF007004000` |
| GIC | GICv3，GICD `0x0c000000`(+0x40000)，GICR `0x0c100000`(+0x200000)；INTID 27/30 都能投递 |
| 定时器 | 13 MHz；上游路径 CNTV/PPI 27 → 100 Hz 正常；自写 CNTP 会产生 5.8 kHz 风暴 |
| 显示 | 720×1600，DSI0 `0x14014000`，stride 2944，fb `0x7ec50000` |
| 触摸 | Novatek NT36525B，SPI3 `0x11013000`，`novatek,irq-gpio=329` |
| 固件卷 | `base 0x41080000`，`size 0x300000`（3 MiB），`kernel_addr 0x40080000` |
| 日志块 | `flashinfo` + **30512 扇区**，128 扇区，`DDNLOG01` |

---

## 8. 刷机与回退

```powershell
# 1) 刷本线的镜像（boot 分区）
adb -s QS55XCPRCA9LDA5T reboot bootloader
fastboot flash boot <out>/…/boot/boot.img
fastboot reboot

# 2) 看屏幕；若挂住，长按电源 ~10 s 关机，再 电源 + 音量下 进 fastboot

# 3) 回 Android（读日志用）
fastboot flash boot D:\aios\artifacts\boot-android-lineage.img
fastboot reboot
adb wait-for-device

# 4) 读固件日志
cd D:\aios\_work\UniXNU-dandelion
powershell -File tools\collect-log.ps1 -Serial QS55XCPRCA9LDA5T -Out out\fw.txt
```

---

## 9. 给 opencode 的第一条指令建议

> 读本文件。先看 §0.1 的根因和 §0.2 的下一步。第一件事：把 run `41423677833`（`8a7021e`）的 `boot.img` 刷进 `boot`，**拍照记录屏幕**：
> - 出现文字/彩条 → 根因确认，按官方顺序接 `AdvancedLogDxe` + 二维码日志，再做 green-bar / entry probe 的内核交接；
> - 仍然纯灰 → 只改一个变量：FD 基址（注意 BootShim 自覆盖），或用官方 QEMU 验证同一 FD。
>
> 每步都要贴证据（屏幕照片、`collect-log.ps1` 的输出、CI run id + 产物字节数），不要凭猜。

---

## 0.3 突破：内核交接成功（2026-11-03 00:30）

日志证据（`tools/collect-log.ps1` 读 flashinfo）：

```
DARWINBOOT: window requested 10000000 default 30000000
DARWINBOOT: ARM64 va=0xFFFFFFF007004000 pa=0x4D004000
DARWINBOOT: inputs validated; exiting boot services
DARWINBOOT: hand-off to the kernel
kernel   va 0xFFFFFFF007004000  pa 0x4D004000
args     pa 0x4DF08000  window 0x10000000 bytes
entry words D2ABF7C9 5280002A B900012A D2AFD8A9
```

### 之前十几轮"改了没效果"的根因（三个，逐一查实）

1. **设备从 `flashinfo` 的旧 ESP 加载 `UniXNU.efi` / `DarwinBoot.efi`**，不是从 boot.img 的
   payload。旧 ESP 是 10 月 29 日的文件，所以固件改动、`WINDOW_SIZE` 补丁全都不执行。
   → 修法：把新编的 `DarwinBoot.efi` 放进 ESP 源目录，用 `dandelion-xnu/tools/mk_unixnu_esp.py` 重打包。
2. **窗口值来自 ESP 上 `kernel.macho.adt`**（不是设备树、也不是 `[payload.adt] owned_memory_size`）。
   它在早先被手工改成 2 GiB，DarwinBoot 放不下（最大连续块 588 MiB）。
   → 修法：把该属性的 8 字节值改回 `0x10000000`。
3. **无法判断设备是否在跑新代码**。压缩 FV 里看不到字符串，日志时间戳也不可靠。
   → 修法：在 `DarwinBoot.c` 的 `AllocateWindow` 前加 `Print("window requested %lx default %lx")`，
   一次刷机就能确认（`default 30000000` 证明我们的二进制在跑）。

### 方法论（写下来避免重犯）

- 改任何东西之前，先确认**设备上运行的二进制是不是你改的那份**；用探针打印，不要用时间戳或字符串。
- 设备加载路径要查清：`PayloadBoot: found \UniXNU.efi` 是按 FAT → flashinfo → FV 顺序找的，
  看到这行就要知道它到底从哪个卷找到的。
- 日志里 `device tree NNNN bytes published` 是**固件卷里的 DTB 尺寸**，与 boot.img 里的 DTB 无关。

## 0.4 下一步：内核控制台（严格按 UniXNU 分层）

内核已被跳转执行，但**没有任何输出通道**。框架的 `profiles/arch/arm64` 里没有 console/display
补丁或配置项——按 UniXNU 的 `porting/drivers`，内核控制台由**板子自己**提供。

| 路径 | 需要什么 | 已知条件 |
|---|---|---|
| UART 控制台 | SoC 仓库提供 UART 驱动 | 固件日志已探到 **`uart base 0x11002000`**，13 MHz，115200 |
| framebuffer 控制台 | 显示驱动（DSI0 `0x14014000`，720×1600） | 面板参数已知，驱动未写 |

**推荐先做 UART**，它属于 SoC 资源，应放在 `UniXNU-mt6765-common`：

1. 在 `UniXNU-mt6765-common` 增加 UART 初始化与 `cnputc` 实现（可作为 `kits/Mt6765Platform` 或 SoC 补丁）
2. 在 `soc:patches/xnu/series` 里挂一个把 XNU 的 `cnputc`/`AppleARMPlatform` 串口路径接到该 UART 的补丁
3. 内核 `kernel_args` 已含 `serial=3`，配合该驱动即可输出

之后每一步内核阶段（`vm_page_bootstrap`、`bsd_init`、`bsd_ast`）都能直接读到，里程碑 2 才算完整验收。

## 0.5 本地构建（推荐，CI 约慢一倍）

```bash
cd ~/work/UniXNU-dandelion
export UNIXNU_UNIQUE_CHIP_ID=00002621
bash scripts/package.sh --os 17 --uefi ../uefi-dandelion
# 改过固件源码时先 rm -rf ~/work/uefi-dandelion/Build
# 产物：~/work/UniXNU/out/dandelion/ipados-17.7.11-21H461/dandelion/boot/boot.img
# 实测：全量约 5–7 分钟；CI 12–15.5 分钟
```

## 0.6 内核日志通道已打通，差最后一步（2026-11-03 01:5x）

`patches/xnu/device-emmc-log-console.patch`（来自旧项目的 `native-emmc-serial`）**成功应用到
xnu-10063 并编译通过**，内核 `printf` 现在会写进 flashinfo 的 LBA 30000 块。读取方式：

```powershell
adb shell "su -c 'dd if=/dev/block/by-name/flashinfo of=/data/local/tmp/klog.bin bs=512 skip=30000 count=512'"
adb pull /data/local/tmp/klog.bin out\klog.bin
```

读到的内容证明内核确实在跑：

```
arm_init: kernel_startup_bootstrap
Darwin Kernel Version 22.6.0 ... runner:obj/DEVELOPMENT_ARM64_DANDELION
vm_page_bootstrap: 60632 free pages, 4904 wired pages
ipc_kernel_map : 0xfffffff01c000000 - 0xfffffff01e000000 (32M)
> mach_ipc / machine_init / thread_machine_init_template
```

### 但那是**旧内核**

`Darwin Kernel Version 22.6.0`（Darwin 22 = xnu-8796）与 `MACHINE=dandelion` 是旧项目的特征；
新内核会打印 `MACHINE=BCM2837`。原因：ESP 只有 16 MiB，装不下 51 MB 新内核，而
`UniXNU.toml` 的 `path = "Payloads/kernel.macho"` 需要 UniXNU 在 **ramdisk 卷**上解析。

### 下一步（单点）

1. 确认 `UniXNURamDiskDxe.efi` 是否被加载并挂出卷（`UniXNU.toml` 已加 `[[driver]]`）
2. UniXNU 是否跨卷搜索 `Payloads/kernel.macho`；`[payload] volume = "DANDELION"` 就是给这个用的，
   payload FAT 的卷标已验证是 `DANDELION`
3. 若 UniXNU 只认加载它的那个卷，则需把内核放到该卷可达位置（受 16 MiB 限制，需要扩容方案）

**不要在设备树或 board.toml 上再试窗口值**——窗口问题已定论（256 MiB，由 ESP 上 ADT 决定）。