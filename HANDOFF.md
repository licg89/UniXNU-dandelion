# dandelion 移植交接（给另一台机器的 opencode）

最后更新：**2026-11-03 11:40 (+08:00)**。这份文件是**唯一的事实来源**，按它就能接着干。
**先读 §0.9**（当前唯一阻塞点的根因与判读表）；§0.8 里"DEBUG 不进日志块"和"下一步加 initrd 探针"两条已作废。

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
## 0.7 为什么必须让 payload 自包含（源码证据）

`package/UniXNUPkg/Library/UniXNULib/Volume.c:408`：

```c
UniXNUResolveImagePath (IN EFI_HANDLE Volume, IN CONST CHAR16 *Path)
{
  if (StrnCmp (Path, L"\\fv:", 4) == 0) return ResolveFvFile (Path + 4);  // 只有 fv:<GUID> 能进固件卷
  if (Volume == NULL) return NULL;
  return FileDevicePath (Volume, Path);   // 其余路径只在"加载 UniXNU 的那个卷"上解析
}
```

推论链：

1. UniXNU 是从 **flashinfo 的 ESP** 被 PayloadBootDxe 加载的 → 它的 `Volume` 就是 ESP
2. ESP 只有 **16 MiB**，新内核 **51 MB** → `Payloads/kernel.macho` 永远解析不到
3. `fv:<GUID>` 只能进固件卷（`SILICIUM_UEFI.fd` 3 MiB）→ 也放不下

**因此 payload 必须自包含**：`UniXNU.efi`、`UniXNUDxe.efi`、`UniXNURamDiskDxe.efi` 要和
`Payloads/kernel.macho`（已在 payload 里）放进**同一个 payload FAT 的 `\EFI\UniXNU\`**。
这样 PayloadBootDxe 的查找顺序 `FAT → flashinfo → FV` 会在第一步命中，UniXNU 的 `Volume`
就是 payload 卷，内核就在它旁边。

### 实现入口

- payload FAT 由框架的 `payload` 步骤生成；当前内容只有 `EFI/UNIXNU/unixnu.toml` 和 `EFI/UNIXNU/Payloads/`
- 需要确认 `payload.py` 是否支持带入额外文件（例如从设备 `package/` 或某个配置列表）
- 三个 efi 都能从 `uefi-dandelion/Build/dandelionPkg/DEBUG_CLANGPDB/AARCH64/` 直接取得

**这一步做完，跑的就是 51 MB 的新内核（`MACHINE=BCM2837`），而内核日志通道已验证可用。**
## 0.8 APRIORI 顺序修复无效，下一步加 initrd 探针（2026-11-03 上午）

试过并**已排除**：

| 假设 | 结果 |
|---|---|
| initrd 地址填错 | 排除。`boot.img` 头部 `ramdisk addr 0x51b00000 / size 58,720,256` → `0x55300000`，与设备树里 `linux,initrd-start/-end` 完全一致 |
| DXE 派发顺序（ramdisk 驱动早于设备树发布） | 排除。已把 `UniXNURamDiskDxe` 插到 `APRIORI.inc` 里 `PayloadBootDxe` 之后（`76ffc3f`），结果不变 |
| payload 缺 `UniXNU.efi` | 排除。payload FAT 里三个 EFI 都在 |
| flashinfo 旧 ESP 干扰 | 排除。已 `fastboot erase flashinfo` |

最新一次刷机（release `dandelion-apriori-ramdisk`，`boot-apriori.img`）后：

```
PayloadBoot: looking for a payload (FAT, then flashinfo, then FV)
PayloadBoot: no SimpleFileSystem handle
PayloadBoot: using the payload embedded in the FV
PAYLOAD OK - the load/start path works
```

`klog.bin`（flashinfo LBA 30000）**完全为空** → 内核从未执行。

### 下一步：让 ramdisk 驱动"可见"

`UniXNURamDiskDxe.c` 的所有提示都是 `DEBUG((DEBUG_ERROR, …))`，不进 PayloadBootDxe 写的
flashinfo 块，所以现在无法区分三种情况：

1. 它根本没被 DXE dispatcher 派发
2. 它跑了但 `gFdtTableGuid` 尚未安装（读不到 `/chosen/linux,initrd-*`）
3. 它读到 initrd 但拒绝该范围

**做法**（两种，任选其一，都只改我们能控的代码）：

```c
// A. 改 package/UniXNUPkg/Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.c
//    把每处 DEBUG((DEBUG_ERROR, ...)) 后面再加一句写 flashinfo 的调用，
//    或直接改成 Print(...)（Print 走 ConOut，PayloadBootDxe 能捕获）
//    —— 注意这需要重建 UniXNU.efi（framework 侧构建）并放回 payload

// B. 改 Platforms/Xiaomi/dandelionPkg/Drivers/PayloadBootDxe/PayloadBootDxe.c
//    在"looking for a payload"之前打印它能看到的 initrd 属性与 SimpleFileSystem 计数
//    —— 只改我们的固件包，重建最快
```

B 更快，但只能证明"树和属性没问题"；A 才能证明驱动自己在干什么。
建议先 B 再 A。

### 关键提醒

`PayloadBootDxe` 目前能打印 `no SimpleFileSystem handle`，说明它已经枚举过
`EFI_SIMPLE_FILE_SYSTEM_PROTOCOL` 且计数为 0 —— 也就是说 **payload FAT 完全没有以文件系统形式存在**。
若 B 探针显示 initrd 属性正常，则问题必定在 ramdisk 驱动本身（未派发或拒绝范围）。

---

## 0.9 真正的阻塞点：派发顺序，不是驱动逻辑（2026-11-03 11:40）

### 一句话

`PayloadBoot: no SimpleFileSystem handle` 的根因是 **`UniXNURamDiskDxe` 排在 `PayloadBootDxe` 之后**。
`PayloadBootDxe` 的入口点用 `gBS->StartImage()` 起 UniXNU，**不返回**（菜单在其中），所以排在它后面的任何驱动，
在"找 payload"那一刻都还没有执行过；payload FAT 当然不存在。`76ffc3f` 把它"插到 PayloadBootDxe 之后"，
方向正好相反，所以那一轮无效——这与"改了什么、日志一点没变"完全一致。

证据（上一版 FV，两重独立，都不是时间戳）：

| 证据 | 值 |
|---|---|
| `FVMAIN.Fv.txt` 文件偏移 | `3C7A9E10 StorageBeaconDxe @0x0E7E98` → `8A62C1D4 PayloadBootDxe @0x0EBEF8` → `75CA63AD UniXNURamDiskDxe @0x1232E0` |
| 从 FV 里直接读回 APRIORI 数组 | 同样顺序（0x2c4 / 0x2d4 / 0x2e4） |
| 日志 | `BLOCK OK n=44`（44 个 eMMC 分区，没有 ramdisk）+ `FAT NONE` + `no SimpleFileSystem handle` |

### 三处更正（别再走这些路）

1. **`tools/add_initrd_probe.py` 不能直接用**：它把片段插进 `PayloadBootEntryPoint()`，片段里用了 `Tree` /
   `TreeSize` 两个标识符——该函数乃至整个 `PayloadBootDxe.c` 都没有这两个名字（"tree" 只出现在注释里），
   插进去必然编译失败。
2. **它探的也是错的树**：`UniXNURamDiskDxe` 读的是 SEC 存进 `gSiliciumEntryArgHobGuid` HOB 的 **x0**
   （LK 交过来的那棵设备树），不是 `PayloadBootDxe` 从 FV 发布的那棵。所以
   "探针打印 0x51b00000/0x55300000 就说明树正常、故障在驱动里"这个推断不成立。
3. **不需要再为了"看不见"去改驱动**：`dandelion.dsc` 把 DXE_DRIVER / APPLICATION 的 `DebugLib` 指到
   `UefiDebugLibConOut`，`DEBUG()` → ConOut → `DandelionConsoleDxe` 的环形缓冲 → 由 `PayloadBootDxe` 写进
   flashinfo。日志里 `PayloadBoot: build Nov 2 2026 09:48:18` 本身就是 `DEBUG(DEBUG_ERROR)` 进了块的证据。
   **只要驱动先于 `PayloadBootDxe` 执行，它的每一条 `UNIXNURAMDISK: …` 都会出现在 fw.bin 里。**
   §0.8 里"DEBUG() 不进日志块"的结论作废。

### 本轮改动：只动 `APRIORI.inc` 的派发顺序

`uefi-dandelion`（Windows 克隆 `D:\aios\_work\uefi-dandelion`；WSL 构建树 `~/work/uefi-dandelion` 内容逐字节一致）：

```
  INF UniXNUPkg/Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.inf   ← 从文件末尾移到这里
  INF dandelionPkg/Drivers/StorageBeaconDxe/StorageBeaconDxe.inf
  INF DarwinBootPkg/Driver/DarwinBoot.inf                      ← 新增
  INF dandelionPkg/Drivers/PayloadBootDxe/PayloadBootDxe.inf
```

两处改动，各有一条独立的日志证据：

| 改动 | 依据 | 日志证据 |
|---|---|---|
| ramdisk 驱动移到 `StorageBeaconDxe` 之前 | StorageBeacon 的三遍 `ConnectController(recursive)` 会把 BlockIo 绑成 DiskIo→Partition→Fat（eMMC 那 44 个就是这样来的），所以块设备必须先存在 | `BLOCK OK n=45`、`FAT OK`、`UNIXNURAMDISK: initrd block device 0x51b00000 size 0x3800000` |
| `DarwinBoot.inf` 加进 APRIORI，放在 `PayloadBootDxe` 之前 | UniXNU 的 xnu 入口靠 `gBS->LocateProtocol(gUniXNULoaderProtocolGuid)` 找 loader（`UniXNULib/Boot.c:58`），DarwinBoot 是唯一实现者、却只在 DXE.inc 里 → 在 StartImage 不返回的这条 boot policy 下永远来不及派发，会停在 `xnu: no loader protocol installed` | `DARWINBOOT: loader v2 installed ELx` |

第二处是为了"这一轮就能跑到内核"；否则下一轮会卡在菜单里，白烧一次刷机。

### 为什么 payload 不用改

- `UniXNUConfigLoad` 是"先试加载它的那个卷，再扫所有 SFS 卷"（`Config.c` 的 `TryVolume` 循环），
  所以即使 `UniXNU.efi` 是从 FV 起的，它也会在 payload FAT 上读到 `\EFI\UniXNU\unixnu.toml`，
  并把 `Config->Volume` 设成那块 FAT；随后 `[[entry]] path = "Payloads/kernel.macho"` 就落在这块 FAT 上
  → 跑的是 51 MB 新内核。
- 因此 §0.7 的"payload 必须自带三个 EFI"**不是必需的**（`payload.py` 里那段拷贝只在固件已构建时才生效；
  全新构建时 payload 先于固件，本来也拷不到——这就是当前 payload 里只有 toml + Payloads 的原因，
  而 `payload.py` 首行注释也写着"the loader (DarwinBoot) is part of the firmware volume"）。

### 本轮产物

| 文件 | 大小 | sha256 |
|---|---|---|
| `D:\aios\out3\boot-payload-first.img` | 59,492,352 | `a3841ad3a9ea97c1b343142d7dc076a4166d3f7c6b3ff64f53a66bf24e104a13` |
| `D:\aios\out3\SILICIUM_UEFI-payload-first.fd` | 3,145,728 | `1973456450474f37cd0c7a9ec5e75a34aeec997b21909c9843e675ccf29e7efa` |
| payload.img（boot.img 的 ramdisk，落在 0x51b00000） | 58,720,256 | `bc24eeadad5f613291174ec78ee209b651774a025409fec9a7dfbe23279991f7` |

`boot.img` 头部复核：`kernel 0x40080000 / 769,087`、`ramdisk 0x51b00000 / 58,720,256`（末地址 0x55300000）、
`header_version 0`、页面 2048。

从**新 FV** 里读回的 APRIORI 顺序（这是判据，不是时间戳）：

```
0x174 DiskIoDxe   0x184 PartitionDxe   0x1b4 Fat   0x1c4 EnglishDxe
0x2c4 UniXNURamDiskDxe   0x2d4 StorageBeaconDxe   0x2e4 DarwinBoot   0x2f4 PayloadBootDxe
```

### 刷机 / 读日志（§8 的两个 Android 镜像路径已过期，用这份）

```powershell
adb -s QS55XCPRCA9LDA5T reboot bootloader
fastboot erase flashinfo          # 必须：否则旧 ESP 里的 \UniXNU.efi 会先被 TryFileSystem 命中
fastboot flash boot D:\aios\out3\boot-payload-first.img
fastboot reboot
# 看屏幕 30 秒；挂住就长按电源 10 s 关机，再 电源 + 音量下 进 fastboot
fastboot flash boot D:\aios\artifacts\boot_backup.img    # 67,108,864 B, sha256 98F5ED08… = LineageOS 的 boot
fastboot reboot
adb wait-for-device
cd D:\aios\_work\UniXNU-dandelion
powershell -File tools\collect-log.ps1 -Serial QS55XCPRCA9LDA5T -Out out\fw.txt
adb shell "su -c 'dd if=/dev/block/by-name/flashinfo of=/data/local/tmp/klog.bin bs=512 skip=30000 count=512'"
adb pull /data/local/tmp/klog.bin out\klog.bin
```

### 判读表

| fw.txt 里出现 | 含义 | 下一步 |
|---|---|---|
| `BLOCK OK n=45` + `FAT OK` + `DARWINBOOT: loader v2 installed` | 三件事都成立 | 看 `klog.bin`：非空且带新内核签名（`MACHINE=BCM2837`）= 里程碑达成 |
| 仍是 `BLOCK OK n=44` | ramdisk 驱动没发布块设备 | 紧跟的 `UNIXNURAMDISK: no entry argument HOB` / `x0 … is not a device tree` / `no initrd in /chosen` / `… is not a whole number of sectors` 会直接指出是哪一种 |
| `n=45` 但 `FAT NONE` | 块设备在，RAM 里那份 payload 不是可读 FAT | 在 `MemoryMapLib.c` 给 `0x51B00000 / 0x03800000` 加保留区（官方 `uefi-firmware` 要求 initrd 区间在使用期间保持保留；它现在落在 `DRAM Low Conv` 里，DXE 可以分配它） |
| 有 `DARWINBOOT: loader v2 installed` 但 `xnu: no loader protocol installed` | 顺序改动没生效 | 用上面的 APRIORI 读回脚本确认，不要看时间戳 |

---

## 0.10 真机两轮的结果：FAT 不在树里说的那个地址（2026-11-03 14:10）

### 已经做掉的架构改动（`PayloadBootDxe` 现在自己发布 payload 卷）

APRIORI 里**不再有** `UniXNURamDiskDxe` 和 `DarwinBoot.inf`：前者被搬进 `PayloadBootDxe.c` 的
`PublishPayloadVolume()`（从本固件发布的那棵设备树读 `/chosen/linux,initrd-*`，装只读 Block I/O，
自己调 `ConnectController` 绑 DiskIoDxe+Fat），后者由 `StartImageFromFirmwareVolume()` 在
`StartImage()` 之前从 FV 里起。理由：这条 boot policy 的入口点用 `StartImage` 起下一阶段且**不返回**，
排在它后面的驱动永远派发不到（两轮 `no SimpleFileSystem handle` 的真因），排在前面的驱动又跑在任何
日志块之前（那一轮 flashinfo 回来是**全零**）。

新的固化日志链（每步之间都 flush，出故障也能定位到上一步）：

```
PayloadBoot: firmware log written to flash
PayloadBoot: tree says initrd start/end/bytes    ← 本固件 DTB 的 /chosen（静态检查：0x51B00000..0x55300000，正确）
PayloadBoot: bytes at the tree's range + 7 个偏移 dump
PayloadBoot: previous stage said x0 is 0x…       ← SEC 的 gSiliciumEntryArgHobGuid（新）
PayloadBoot: x0 tree total size / x0 tree initrd start,end
PayloadBoot: scanned up to 0x…                   ← 分 4 块扫 0x40000000..0x5D000000，每块后 flush
PayloadBoot: file systems before/after
```

### 两轮真机结果

| 轮次 | 镜像 | 屏幕 | flashinfo |
|---|---|---|---|
| A | `boot-inline-volume.img`（`58d…`→`a38…`） | 黄条卡住 | 2017 B：`initrd start 0x51B00000 / end 0x55300000 / bytes 0x3800000`，紧接着 **`first 8 bytes at start = 0x00003370706F0100`** → 不是 FAT 引导扇区；`file systems before 0 / after 0` → 挂不上；随后正常回退 FV |
| B | `boot-scan-volume.img` | 黄条卡住 | **722 B**，只到 `firmware log published` → 死在没有边界的扫描里（`0x40000000..0x70000000`），读到未映射内存，看门狗复位后 LK 落回 fastboot |

**结论（本轮最重要的一条）**：`/chosen` 说的 `0x51B00000` 上**不是** payload。那 8 个字节
`00 01 6F 70 70 33 00 00` 里含 ASCII `opp3`，正是设备树字符串表里的 `opp3`（本固件 DTB 的 `opp3` 在
偏移 `0x33C`）——也就是说那片内存里躺着**设备树**，不是 FAT。要么 LK 把自己的 DTB 写在了那里并盖掉了
payload 开头，要么 payload 根本没被放到那里。

### 待验证的一轮（镜像已构好，等真机）

`D:\aios\out3\boot-memdump.img`（59,494,400 B，sha256 `a91213f02e810411eaf0954fd6bbb58d4644599f99bd7601aac5f85bab174fb6`）

它会在日志里回答三件事：

1. `previous stage said x0 is 0x…` → LK 交过来的设备树在哪；
2. `x0 tree initrd start/end` → **LK 自己**认为 payload 放在哪（这比我们 DTB 里写的那份可信）；
3. `+0x00 / +0x08 / … / +0x800000` 那一串 dump + `scanned up to 0x…` → 0x51B00000 上到底是什么、
   覆盖有多深、以及 0x40000000..0x5D000000 里还有没有 FAT（分块 flush，死在哪块一目了然）。

判读：

| 日志 | 含义 | 下一步 |
|---|---|---|
| `x0 tree initrd start` 也是 0x51B00000 | LK 认为它放好了 → 是 LK 自己的 DTB 盖掉了开头 | 换 ramdisk 地址（boot.img 头 0x14）到不与 LK 的 tags 冲突处，并把 DTB 的 `/chosen` 同步改掉 |
| `x0 tree initrd start` 是别的地址且那里有 FAT | LK 把 payload 放别处 | 直接把 DTB 的 `/chosen` 改成那个地址（或让固件优先用 x0 树，代码已按此实现） |
| 扫描找到 `found a FAT boot sector at 0x…` | payload 在，只是位置不是树说的 | 同上；并查为什么 LK 无视头部 0x14 |
| 扫完 4 块都没有 | payload 不在 0x40000000..0x5D000000 | 换载体（见下） |

### 如果 payload 真的没进 RAM

`boot` 分区 64 MiB 已被 boot.img（59.5 MB）占满，`flashinfo` 只有 16 MiB，而内核 `kernel.macho`
就有 51 MB。可选路径，按代价排序：

1. 改 boot.img 头的 `ramdisk_addr`（0x14）到别的高地址，重试——最便宜，但要先知道 LK 把 DTB/tags 放哪；
2. 从 **boot 分区**用 BlockIo 读 payload（固件已有按 GPT 找分区的代码），但需要把 payload 放到某个 ≥64 MiB
   的未用分区（`userdata` 前 64 MiB 可行但会毁掉 Android 数据）；
3. 缩 payload：内核 51 MB 是本体，除非裁 root / 换 OS 版本，否则下不来。

---

## 0.11 根因定论（2026-11-03 15:0x）：payload 在 RAM 里，只是高了 0x1F000；而固件看不见没声明的内存

这一节是**分析结论**，不是又一次尝试。两个独立的事实叠在一起，把前几轮全部解释了。

### 事实 A：固件只映射 `MemoryMapLib` 里列出的区间，别处一律 data abort

源码证据（`Silicon/Silicium/SiliciumPkg/Library/MemoryInitPeiLib/MemoryInitPei.c:104-150`）：

```c
GetMemoryMap (&MemoryDescriptor, &MemoryDescriptorCount);       // ← gMemoryDescriptor[]
for (i = 0; i < MemoryDescriptorCount; i++) {
    AddHob (MemoryDescriptor[i]);
    MemoryTable[Index].PhysicalBase = MemoryDescriptor[i].Address;   // ← 只有这些
    ...
}
ArmConfigureMmu (MemoryTable, ...);                              // ← 页面表只覆盖这些
```

`Platforms/Xiaomi/dandelionPkg/Library/MemoryMapLib/MemoryMapLib.c` 里**没有** 0x40000000..0x41080000
和 0x41380000..0x48000000。所以：

| 地址 | 状态 | 后果 |
|---|---|---|
| `0x51B00000`（树说的 initrd） | 在 "DRAM Low Conv" 内 → 已映射 | 能读，能写日志 |
| `0x47880000`（x0 = LK 的 DTB） | **没有声明 → 未映射** | 一读就 abort；这就是那两轮日志停在 x0 那一行、下一轮停在扫描第一块的原因 |
| `0x40000000`（上一版扫描起点） | **没有声明 → 未映射** | 扫描第一条就 abort → 日志块只剩 722 B、随后看门狗复位落回 fastboot |

推论：**任何越过 `gMemoryDescriptor` 的读取都会带走整次启动**，而 LK 交过来的设备树正好在没人声明的
0x47880000。前两轮"驱动没被派发 / 驱动拒绝范围"的猜测都不成立，真相是我们**读不了**那段内存。

### 事实 B：payload 就在 0x51B1F000，比设备树说的地址高 0x1F000（124 KiB）

上一轮日志在 0x51B00000 起的 8 个偏移各打了 8 字节。拿这些字节去 payload 镜像里做**唯一指纹**匹配：

| 日志里的地址 | 8 字节内容 | 在 payload 镜像中的偏移 | 反推 payload 基址 |
|---|---|---|---|
| `0x51B40000` | `01 74 00 00 02 74 00 00`（FAT 表项 0x7401→0x7402） | `0x21000` | **0x51B1F000** |
| `0x51C00000` | `CF FA ED FE 0C 00 00 01`（Mach-O 64 + ARM64） | `0xE1000` | **0x51B1F000** |

两个指纹在镜像里都**各自只出现一次**（0x21000 处是 FAT#1 中间的一段簇链；0xE1000 处是 kernel.macho 的头），
两次反推出的基址完全一致 ⇒ 不是巧合。

于是 0x51B00000..0x51B1F000 里那堆 `opp6` / `dbg_` / FDT token 就说得通了：那是 **LK 写在
"声明的 ramdisk 地址"上的启动参数设备树**，真正的 payload 被它放在后面 0x1F000 处。
旧项目 `dandelion-xnu/docs/HARDWARE_ANALYSIS.md:136` 早就写过这一条：

> - LK **覆盖** ramdisk 区域为启动参数结构
> - 无法在 ramdisk 中放置 payload

它说的是前半句（区域开头被参数树占了），而后半句"无法放置"是**过度概括**：payload 其实完整地躺在
0x1F000 之后，只是没人去那儿看。

### 因此

- `no SimpleFileSystem handle` / `FAT NONE` / `did not mount as FAT` 全部是"地址错了 0x1F000"的结果，
  与 `UniXNURamDiskDxe`、APRIORI 顺序、`x0` 内容都无关（DP 顺序那一轮之所以"无效"，是因为驱动在
  PayloadBoot 之后根本没派发；而把它移到前面那一轮，是驱动读了未映射的 x0 直接 abort）；
- payload 并没有被固件分配覆盖（+0x21000、+0x100000 两处都与镜像逐字节一致），之前"DXE 分配砸了
  payload"的担心可以先放下；
- 0x1F000 很可能等于 LK 那棵参数树的大小（向上取整），所以**每刷一次 boot.img 都该实测一次**，
  不能当成常量写死。

### 正确的修法（三处，都是小改）

1. `PayloadBootDxe::FindFatVolume()` 的扫描区间改成**只扫已映射区**：`0x4CE60000 .. 0x5D000000`
   （"DRAM Low Conv" + 我们新加的 "Boot Ramdisk" 保留区；0x5D000000 以上未验证）。起点绝不能再用
   0x40000000——那是未映射的，等于自杀。
2. 卷地址以**扫描结果**为准（本轮应命中 0x51B1F000），设备树里的 `/chosen` 只当线索；日志里同时打印
   两者，下次就知道 0x1F000 有没有变。
3. `MemoryMapLib.c` 的 "Boot Ramdisk" 保留区向右多留 1 MiB（`0x51B00000 + 0x3900000`），
   这样 payload 整体偏移 ≤1 MiB 时也不会被 DXE 分配砸到。**不要**把 0x40000000..0x48000000 声明成
   Conv（会被 DXE 分配）；如果只是想读 x0 那棵树，声明成 `Reserv` 即可。

### 还能顺带修掉的一个盲区

`MemoryMapLib` 里缺 0x40000000..0x48000000 这一整段（内核镜像在 0x40080000、LK 的 DTB 在
0x47880000）。把它声明成 `Reserv`（不参与分配）后，固件才第一次"看得见"加载器留下的东西——包括
x0 那棵树的 `/chosen/linux,initrd-start/end`，那才是 LK 认可的权威值。
---

## 0.12 通了：payload 卷挂上 → UniXNU 菜单 → 内核从 payload FAT 加载 → DarwinBoot 交接（2026-11-03 19:0x）

### 真机日志（`flashinfo@30512`，`tools/ddnlog.py` 解出 5021 B）

```
PayloadBoot: tree says initrd start   0x51B00000        <- 构建设置（错的）
PayloadBoot: bytes at the tree's range 0x0000CAC60A009C00  <- LK 的参数树，每轮都不一样
PayloadBoot: previous stage said x0 is  0x47880000
PayloadBoot: x0 tree total size        283783            <- 终于能读了（MemoryMapLib 补了映射）
PayloadBoot: x0 tree initrd start     0x51B00000         <- LK 自己也说 0x51B00000（错 0x1F000）
PayloadBoot: the tree's range does not hold a FAT boot sector
PayloadBoot: found a FAT boot sector at 0x51B1F000       <- 扫描命中，= 0x51B00000 + 0x1F000
PayloadBoot: using range 0x51B1F000 .. 0x5531F000
PayloadBoot: file systems before 0 / after 1
PayloadBoot: payload volume mounted
DARWINBOOT: loader v2 installed EL1
PayloadBoot: starting the UniXNU menu from the FV
UniXNU  (UniXNU 0.1.0)  config: \EFI\UniXNU\unixnu.toml   <- 配置是从 payload FAT 上读到的
> 1. UniXNU [xnu]  2. Restart  3. Power off
Booting UniXNU in 3 s / 2 s / 1 s
DARWINBOOT: window requested 10000000 default 30000000
DARWINBOOT: ARM64 va=0xFFFFFFF00427C000 pa=0x5547C000 args=0x594A0000 entry=0x58614488
DARWINBOOT: command line: -v serial=3
DARWINBOOT: inputs validated; exiting boot services
DARWINBOOT: hand-off to the kernel
```

`0xFFFFFFF00427C000` 是新内核的链接基址（旧内核是 `0xFFFFFFF007004000`），`-v` 来自 payload 的
`unixnu.toml`，`config:` 那行证明 UniXNU 是在 payload FAT 上找到配置的 —— **51 MB 的新内核确实被读出来
并交接了**。

### 结论：0.11 的推断被真机逐项证实

- `MemoryMapLib` 不声明就不映射：补上 `0x40000000..0x48000000`（Reserv）后，`x0` 那棵 283,783 字节的树
  第一次读出来，它的 `/chosen` 与我们的构建设置**完全一致（都是 0x51B00000）** —— 也就是说 LK 自己也
  不知道它把数据放到了 0x1F000 之后；
- payload 确实在 **0x51B1F000**，与用两个唯一指纹反推的结论一致；固件扫描一次命中；
- `UniXNURamDiskDxe` 已从 `DXE.inc` 移除（留在卷里只会在 0x51B00000 上多发布一个错的块设备）。

### 新的唯一阻塞：内核没有任何输出，原因已定位

`klog@LBA 30000` 全 0。原因不在内核本身：`patches/xnu/device-emmc-log-console.patch` 的早期控制台要求
设备树里有

```
emmc-log { log-lba = <30000>; log-sectors = <512>; }
```

（补丁内部：`SecureDTFindEntry("name", "emmc-log", ...)` 然后取 `log-lba` / `log-sectors`），而
`Platforms/Xiaomi/dandelionPkg/FdtBlob/dandelion.dtb`（127,203 B，二进制构建输入）里没有这个节点：

```
$ dtc -I dtb -O dts <fv.dtb> | grep -n 'emmc-log|log-lba|arm-io'   -> 无输出
```

所以内核即使跑起来了也一个字都写不出来。**下一步（一处改动）**：把 `dandelion.dtb` 用 dtc 反编译，加一个
顶层 `emmc-log` 节点（`log-lba = <0x7530>` = 30000、`log-sectors = <0x200>` = 512），重编译回 FdtBlob，
重新构建。之后 `klog.bin` 有字 = 内核在跑；仍全 0 = 才轮到查内核早期启动。

---

## 0.13 今天的进度（2026-11-03 晚）：整条链跑到内核，内核入口已验证，卡在 `serial_init`

### 现在真机上已经成立的事实（都有日志/照片）

| # | 事实 | 证据 |
|---|---|---|
| 1 | payload 卷挂载成功 | `PayloadBoot: found a FAT boot sector at 0x51B1F000` / `payload volume mounted` / `file systems after 1` |
| 2 | UniXNU 菜单出现，配置从 payload FAT 读 | `UniXNU 0.1.0  config: \EFI\UniXNU\unixnu.toml`；照片（logo + 三项菜单） |
| 3 | 51 MB 新内核从 payload FAT 读出并交接 | `DARWINBOOT: ARM64 va=0xFFFFFFF00427C000 pa=0x5547C000`（新内核链接基址） |
| 4 | 内核拿到真正的 framebuffer | `DARWINBOOT: video base=0x7ec50000 display=1 row=2944 width=720 height=1600 depth=32` |
| 5 | **内核入口代码在跑** | 照片：绿条（DarwinBoot）+ 蓝条（阶段 1）+ 品红条（阶段 2）= 官方色条图例的"入口已执行 / 到达开 MMU 前" |
| 6 | **MMU 已成功打开** | 阶段 3 复位探针：`entry_probe_reset_delay=6` 时设备自己反复重启（>6 s 后 PSCI 复位） |
| 7 | 卡点在 `serial_init` | 阶段 4 复位探针放到 `_arm_init` 里 `serial_init` **之后**时，设备**不再自行重启** → 执行没从 `serial_init` 返回 |

### 关键源码事实（用于下一步定位）

`_arm_init`（`0xfffffff0079da8f0`）里 `arm_vm_init`（MMU）之后只剩三次调用：

```
bl _arm_vm_init            <- MMU 打开（阶段 3 复位已证明越过）
bl _patch_low_glo
bl _PE_consistent_debug_inherit   <- 源码里查 /chosen/consistent-debug-root，我们没有 → 返回 -1，无害
bl _serial_init            <- 卡在这里
bl _kernel_startup_initialize_upto / _kprintf ...
```

`pexpert/arm/pe_serial.c:serial_init()` 的结构：

```c
soc_base = pe_arm_get_soc_base_phys();
if (soc_base == 0) { uart_initted = true; return 0; }   // 干净的返回
... 我们的 emmc-log 代码块（SecureDTFindEntry → reg → ml_io_map → register_serial_functions）
#ifdef PI3_UART / APPLE_UART / VMAPPLE_UART / DOCKCHANNEL_UART ... 都没匹配
uart_initted = true;
return gPESF != NULL;                                   // 也是干净的返回
```

尾部没有 panic，所以不是"找不到串口就 panic"，而是**在函数内部某处没了**。

### 下一步（一次一问，A/B 单变量）

**实验 A（推荐先做，最省事）**：把 `CONFIG_XNU_EMMC_SERIAL` 设回 `n`，复位探针仍放在 `serial_init` 之后。

- 若设备**自己重启** → `serial_init` 正常返回 ⇒ 凶手就是我们那段 emmc-log 代码（`SecureDTFindEntry` / `ml_io_map` / `register_serial_functions`）；
- 若仍**不动** → 与我们的补丁无关，问题在 `pe_arm_get_soc_base_phys()` 或 XNU 自身的 `serial_init` 路径（那时要看 ADT 的 `/arm-io` 是否合它口味）。

这条 A/B 只改一个变量，而且两种结果都直接指向下一处改法。

### 今天动到的文件（都提交了）

- `uefi-dandelion`（`878e6b9`、`a7ca2b5`）：
  - `MemoryMapLib.c`：补 `0x40000000..0x48000000` 映射（Reserv，只为能读 LK 的树）、`Boot Ramdisk` 保留区右扩 1 MiB；
  - `PayloadBootDxe.c`：自己发布 payload 卷（扫描已映射区）、起 DarwinBoot、**发布 GOP**（面板真实 stride 2944）；
  - `DXE.inc`：移出 `UniXNURamDiskDxe`；`SimpleFb.c`：stride 修正（当前未启用，留作备用）。
- `UniXNU-dandelion`：
  - `configs/dandelion_defconfig`：`CONFIG_XNU_EMMC_SERIAL=y`（**这个之前没开，内核里根本没有控制台代码** —— 这一点是用 `strings` 验的）；
  - `dts/dandelion.dts`：加 `emmc-log` 节点（`reg`/`log-lba=0x747b50f`/`log-sectors=0x200`），ADT 里生成 `/arm-io/emmc-log`；
  - `board.toml`：`[payload] panic_screen=false`、`entry_probe_reset_delay=6`、`entry_probe_reset_after="_serial_init"`、`entry_probe_reset_in="_arm_init"`。
- 框架 fork（`~/work/UniXNU`，未提交到远端）：
  - `scripts/unixnu/devicetree.py`：ADT 导出白名单加 `log-lba`/`log-sectors`/`clock-div`；
  - `scripts/build-xnu-4k-baseline.sh`：把 `XNU_EMMC_SERIAL` 变成 `-DXNU_EMMC_SERIAL=`（之前只有环境变量，没有编译宏）。
- 注：`panic_screen` 和 `entry_probe_reset_delay` **不能同时用**——阶段 3 的代码会把 panic 渲染器挤出 padding，脚本直接拒绝（"the padding ends at 0xfffffff007414ff0"）。

---

## 0.14 阶段 5 定位结果：内核在 `arm_vm_init` 内部停住，且**不是 panic**（2026-11-03 21:5x）

用官方入口探针（`patch-xnu-entry-probe.py`）按"一次一问"跑了四轮，全部在真机上：

| # | 探针位置 | 结果 | 结论 |
|---|---|---|---|
| 1 | 阶段 3（MMU 刚打开处） | 设备 **6 s 后自己重启** | MMU 打开成功 |
| 2 | `_arm_init` 里 `serial_init` 之后（脚本选中的调用点 `0x…9db084`） | 不重启 | **不可靠**：`_arm_init` 里有两个 `serial_init` 调用点，那个点在执行路径上可能根本走不到（`_arm_init` 实际从 `0xfffffff0079d9a74` 开始，之前 nm 报的 `0x…a8f0` 是另一段） |
| 3 | `_PE_consistent_debug_inherit` 之后 = `bl _serial_init` **之前**（`0xfffffff0079da204`） | 不重启 | 内核在 **`arm_vm_init` 返回之前**就停了 |
| 4 | 只装 panic 屏幕（带 framebuffer） | 屏幕上只有三条色条，**没有 panic 文字** | 不是 `panic()`，是异常/死循环（很可能是 VBAR 装好之前的数据异常，所以连 panic 都进不去） |

第 3 轮是关键纠正：**死点不在 `serial_init`，而在 `arm_vm_init` 里面**。
证据链：阶段 3 的复位点在 `arm_vm_init` 内部 MMU 打开之后（它停在那里等 6 s 再复位，所以"重启"只证明 MMU 开了，不证明函数返回）；
而 `arm_vm_init` 返回后紧接着的指令 `0xfffffff0079da204` 的探针不触发 ⇒ 函数体里某处没回来。

`arm_init` 的历史窗口（反汇编，`0xfffffff0079d9a74` 起）：

```
… PE_parse_boot_argn ×2
bl _arm_vm_init            0x…a1ec   ← MMU 在此函数内部打开
bl _patch_low_glo          0x…a200   （只是一条 lowGlo.lgStext 赋值，安全）
bl _PE_consistent_debug_inherit 0x…a204   ← 探针在这里，不触发
bl _serial_init            0x…a208
bl _kernel_startup_initialize_upto / _kprintf …
```

补充事实：`CONFIG_XNU_EMMC_SERIAL` 的 A/B（编译掉我们的控制台）结果**完全一样**（停住），所以与我们那段 emmc-log 代码无关。

### 下一步：走官方 QEMU 入口 fixture（不再靠刷机试）

官方 `reference/qemu` 就是为"内核入口停住"准备的：`scripts/qemu-entry/make-dt.py` 生成 QEMU 用的 ADT，
`package/Scripts/qemu-run.py` 跑 ESP，`scripts/qemu-entry/gdb-stop.py` 在符号处停住并抓寄存器/内存。
本机 `qemu-system-aarch64`、`gdb-multiarch`、`qemu-run.py`、`gdb-stop.py` **都在**。

用法（在 `~/work/UniXNU`）：

```sh
python3 scripts/qemu-entry/make-dt.py --armvirt --armcpu --smp 2 \
  --owned-memory-size 0x10000000 --output /tmp/qemu.adt
python3 package/Scripts/qemu-run.py out/esp --log out/qemu.log --timeout 60 \
  --expect 'DARWINBOOT: hand-off to the kernel'
python3 scripts/qemu-entry/gdb-stop.py --esp out/esp --kernel <kernel.macho> --system-registers
```

目标是把 `arm_vm_init` 里停住的 PC 抓出来（`--system-registers` 会给 TCR/TTBR/SCTLR），
再对着 `osfmk/arm64/arm_init.c` 的 `arm_vm_init` 逐段看是哪一步（`pmap_bootstrap`、physmap、io-ranges…）。
一处可疑点先记着：ADT 的 `dram-base = 0x40000000`、`dram-size` 直接来自 stock DTS 的 `memory` 节点，
而设备实际只有 2 GB 级别；如果 `dram-size` 报得过大，`pmap_bootstrap` 的 physmap 会越界。

---

## 0.15 阶段 5 连破三关：PAN 指令 → GIC 节点 → 现在是 AMFI 的 PMGR（2026-11-03 23:1x）

### 关键：QEMU 入口 fixture 成了不用刷机的调试器

`qemu-system-aarch64` + `package/Scripts/qemu-run.py` + 本机内核/ADT 能**逐字节复现真机**
（同样的 `DARWINBOOT: hand-off`、同样的 `entry words`）。再给 QEMU 加 `-d int`，它会打印每一次异常：

```
Taking exception 1 [Undefined Instruction] on CPU 0
...with ESR 0x0/0x2000000
...with ELR 0xfffffff0079da1dc
```

`0xfffffff0079da1dc` 就是 `msr PAN, #1` —— **ARMv8.1 才有的指令**。

### 根因一：内核按更新的 CPU 编译，跑在 ARMv8.0 的 A53 上

`osfmk/arm/arm_init.c:518`：

```c
#if __ARM_PAN_AVAILABLE__
	__builtin_arm_wsr("pan", 1);      /* → msr PAN, #1 */
#endif
	arm_vm_init(xmaxmem, args);
```

MT6762G 是 8×Cortex-A53，ARMv8.0-A，没有 PAN。执行到这条就是 UNDEF → 走异常 →
`handle_debugger_trap` → `wfe` 死循环（没有控制台、没 panic 屏幕，所以之前"什么都没发生"）。
这也解释了此前所有探针为什么在 `arm_vm_init` 之后就再也不触发。

修法（框架 `scripts/build-xnu-4k-baseline.sh` + 设备 defconfig）：

- `-mcpu=cortex-a53` 加入 `EXTRA_CFLAGS`；
- `CONFIG_XNU_NATIVE_PAN=n`（否则 copyio/pcb 三个对象又被单独加回 `-march=armv8.1-a`）；
- 验证：`grep` 内核里 `d500419f`（`msr PAN,#1` 的编码）**计数 0**。

**效果**：内核第一次真正跑起来，panic 屏幕打出真实文本。

### 根因二：ADT 里没有 GIC 节点

第一条 panic：

```
"CPU configuration in device tree is invalid" @AppleARMCPU.cpp:111
```

对照 QEMU 已知可用的 ADT（`scripts/qemu-entry/make-dt.py --armvirt --armcpu` 生成）发现：
它的 `/arm-io` 下有 `intc`（`compatible="arm,gic-v3"`、`reg` = 4 个 cell），我们**完全没有**。
而 `ArmCpu::mapGic()` 就是 `IORegistryEntry::fromPath("/arm-io/intc")`，且要求
`reg` 长度正好 32 字节（两对 address/size）。

修法：`dts/dandelion.dts` 里加一个**只用于导出**的节点（原 stock GIC 节点带三对 reg，
而且没有 `unixnu,adt-name`，所以从来没进 ADT）：

```dts
intc@c000000 {
	compatible = "arm,gic-v3";
	reg = <0x00 0x0c000000 0x00 0x00040000 0x00 0x0c100000 0x00 0x00200000>;
	redistributor-stride = <0x20000>;
	unixnu,adt-name = "intc";
};
```

**效果**：CPU 相关的 panic 消失，内核继续往下走（下一张照片就是证据）。

### 当前阻塞：AMFI 找不到 PMGR

```
"AMFI: No PMGR?\n" @ConfigurationSettings.cpp:235
```

这是"panic 给出 provider"那一类的标准情况（官方 troubleshooting：ADT 节点或内核开关）。
框架里有个 PMGR 垫片开关 `XNU_SHENG_PMGR`（`build-xnu-4k-baseline.sh:348`），但它需要一个归档：

```
build-xnu-4k-baseline.sh: line 786: test -f "$PMGR_ARCHIVE" failed
```

也就是说要先在 `board.toml` 的 `[kernel] archives` 里加上对应组件（板子当前的 `include` 里
只有 `ArmDTPlatform, ArmCpu, SoftwareAES, XnuPrelink, DarwinBoot`）。所以 defconfig 里
`CONFIG_XNU_SHENG_PMGR` 先保持注释掉，否则整棵树编不过。

### 下一步

1. 找到 PMGR 垫片组件（组件目录里搜 "pmgr"/"sheng"），把它的 archive 加进 `[kernel] archives`；
2. 或者走另一条：给 ADT 加一个 `pmgr` 节点（如果 AMFI 只是要一个能匹配的设备）；
3. 每改一次，**先 QEMU 验**（`qemu-run.py` + 内核/ADT），只有 QEMU 里过了再刷机。

---

## 0.16 AMFI 的 "No PMGR?" 是框架里已知的一步，两条路都查清了（2026-11-04 00:1x）

### 官方文档怎么说

`UniXNU/docs/devices/sirius-*.md`、`gta4l-gta4lunix.md` 都记着同一个阻塞：

> AMFI finds the PMGR through `IOService::serviceMatching("ApplePMGRNub")` +
> `waitForMatchingService`, then reads the provider at `nub+0x88` and calls its
> readAon/writeAon slots (OR bit 2).  The original classes are ApplePMGR
> (storage 0x4b8a8) and ApplePMGRNub (0x740);
> `UniXNU/scripts/audit-amfi-pmgr-contract.py` pins the exact instructions.
> `amfi_get_out_of_my_way=1` does not skip the check.  Two routes remain:
> (1) instantiate the original classes the way UniXNU-sheng does
> (`OSKextLoadKextWithIdentifier` + a generated `OriginalPMGRABI.h` + a published
> nub), or (2) patch the AMFI block in the prelinked kernel.

所以这不是我们特有的坑，是"非 Apple SoC 移植"的标准一步。

### 路线 1 的现成素材（已下载到 `~/work/pmgr-ref/`）

| 文件 | 来源 | 作用 |
|---|---|---|
| `ShengPlatformPower.cpp`（8.2 KB） | `UniXNU/UniXNU-sheng/main/package/` | 继承 ApplePMGR 的垫片，把 340 项虚表按 ABI 填好，只实现 readAon/writeAon，其余 `unavailable()` |
| `build-platform-power.py`（9.0 KB） | `xVanTuring/UniXNU-tb128fu/main/scripts/` | 生成 `OriginalPMGRABI.h` + `GuardedPMGRSlots.inc`，用参考内核的编译记录编出 `libShengPlatformPower.a` |
| `sheng-platform-power.patch`（352 B） | `UniXNU/UniXNU/main/patches/xnu/` | 在 `bsd_init.c` 的 `bsd_bufferinit()` 之后调 `sheng_pmgr_run()` |

要用它还需要：
1. 把 `ShengPlatformPower.cpp` 放进设备仓库 `kits/<kit>/`；
2. `[kernel] archives` 追加一个 `build-platform-power.py`（**要先把 xcrun 换成 Linux 的 kernel clang**——
   `scripts/build-arm-driver.py` 里的 `kernel_target.py` 就是现成范式）；
3. `patches/xnu/series` 加 `sheng-platform-power.patch`；
4. defconfig 打开 `CONFIG_XNU_SHENG_PMGR=y`（现在注释着，因为归档没有会 `test -f` 失败）。

脚本里还有几个 **tb128fu 专属硬编码地址**（`ApplePMGR` ctor/dtor/gMetaClass、AON reader 表
`0xfffffff005ce26d8`），换板子要重新从我们自己的内核里取。它的 `ORIGINAL_KERNELCACHE_SHA256`
是 `f18b4df8…`（iPad 17.7.11 的原始 kernelcache）——我们用的正是同一个 IPSW 家族，第一步可以先原样试。

### 路线 2（补丁 AMFI）今晚探到的边界

`AMFI: No PMGR?` 字符串在 `0xfffffff0042dde34`（`kernel.macho` 文件偏移 `0x1203e34`），
但**在 `__TEXT`/`__TEXT_EXEC` 里扫不到任何 `adrp/add` 引用它** —— 说明预链接内核里
AppleMobileFileIntegrity 用的是 `__DATA_CONST` 里的预绑定指针（chained fixups），
不是现算地址。要做这条路得先解 chained-fixup 的 rebase 编码，比路线 1 更细。
`scripts/audit-amfi-pmgr-contract.py` 提供了校验方法（按模块 `__TEXT_EXEC` 的 sha256 + 指令契约），
可以拿它当"补丁没打歪"的判据。

### 结论

下一步按路线 1 走；这也是官方文档给参考设备用的同一条路。先用 QEMU 验（`qemu-run.py` +
本机 kernel/ADT，`-d int` 看异常），通过了再刷机。

---

## 0.17 内核跨过 AMFI，停在“挂根设备”（2026-11-04 00:1x）

### 结果

panic 屏幕：

```
Failed to mount root device @IOKitBSDInit.cpp:984
frames fffffff0079c838c fffffff007710c58 fffffff0072cc504 fffffff00742c4a4
pages=0x000000000000bbf4  free=0x000000000000b259
```

`AMFI: No PMGR?` 已经消失。内核现在**走完了**：入口 → MMU → `arm_vm_init` → CPU/GIC 注册 →
`serial_init` → AMFI/ConfigurationSettings → `bsd_init` → `IOKitBSDInit`，最后因为**没有根设备**而停。
这正是官方 bring-up 的**阶段 6（存储与根卷）**入口。

### 这一关是怎么过来的（PMGR 桥接，路线 1）

1. **素材**（已下载到 `~/work/pmgr-ref/`）：`ShengPlatformPower.cpp`（UniXNU-sheng）、
   `build-platform-power.py`（xVanTuring/UniXNU-tb128fu）、`sheng-platform-power.patch`（UniXNU）；
2. `ShengPlatformPower.cpp` 放进设备仓库 `package/`（构建树里就是 `kits/dandelion/`）；
   `patches/xnu/series` 增加 `sheng-platform-power.patch`（在 `bsd_bufferinit()` 之后调 `sheng_pmgr_run()`）；
   `board.toml` 的 `[kernel] archives` 追加 `build-platform-power.py`；defconfig 打开 `CONFIG_XNU_SHENG_PMGR=y`；
3. **桥接只在有启动参数时才发布 nub**：`ShengPlatformPower.cpp:70` 先 `PE_parse_boot_argn("sheng-pmgr", ...)`，
   为 0 直接返回；所以 `[payload] kernel_args` 必须加 **`sheng-pmgr=2`**（2 = 发布原始 nub）。这一条是关键，
   少了它就是 `AMFI: No PMGR?`。

### 把 tb128fu 的构建脚本移植到这台 Linux 主机时踩的四个坑（都在框架 `scripts/build-platform-power.py`）

| 现象 | 修法 |
|---|---|
| `The original kernelcache is not in the IPSW cache` | 目录布局不同：`ROOT.parents[1]/ipsw` → 向上找第一个 `ipsw/` 目录 |
| 十个 `-Werror,-Wmissing-designated-field-initializers`（Apple 头文件） | 加 `-Wno-missing-designated-field-initializers`（内核构建本来就有） |
| 编译期错误被 `check_output` 吞掉 | `run()` 失败时把子进程输出打到 stderr |
| 链接：`ignoring file libShengPlatformPower.a ... unknown-unsupported file format ( 0x21 0x3C ... )`，`_sheng_pmgr_run` undefined | 本机 `xcrun ar` 是 GNU ar，写出 **SysV** 归档（`!<arch>\n/`），Mach-O 链接器读不了 → 改用 **`llvm-ar --format=darwin rcs`**（成员表为 `__.SYMDEF`） |

另外两个为本地工作流加的补丁：参考内核不存在但归档已存在时**保留归档**（框架的 kernel 步骤会先删
`kernel/kernel.macho`，否则永远只有首轮的 no-op stub）；契约审计在缺少 prelink plan 时**降级为提示**
而不是中断（否则失败过的树再也重建不出归档）。

### 下一步：阶段 6（存储与根卷）

按官方 `porting/ufs` + `guide/userdata`：内核需要一个提供 userdata 的块设备、`OS`/`SystemOS` 分区方案、
以及 `root_match_volume_uuid` 对应的根卷镜像。当前 `[payload]` 里没有 data ramdisk、也没有存储驱动，
所以"Failed to mount root device"是**预期**的下一关。

在这之前值得先确认的一件事：内核**自己的控制台**现在应该通了（`CONFIG_XNU_EMMC_SERIAL=y` +
ADT 的 `emmc-log` 节点）。如果 `klog@LBA 30000` 有内容，后面所有阶段都能直接读文本，不用再拍照。

---

## 0.18 内核控制台通了：整段内核启动日志可读（2026-11-04 00:4x）

`flashinfo@LBA 30000`（512 扇区）现在有 **7699 字节**文本，完整记录从 pmap 到挂根设备的过程。
（这就是 `patches/xnu/device-emmc-log-console.patch` + ADT 的 `emmc-log` 节点 + `sgn:CONFIG_XNU_EMMC_SERIAL=y`
三件事都到位之后的第一次可用输出。）

关键片段：

```
Darwin Kernel Version 23.6.0: Tue Nov  3 23:39:27 CST 2026; root:ios-obj/DEVELOPMENT_ARM64_BCM2837
pmap_startup() init/release time: 5711 microsec
vm_page_bootstrap: 48116 free pages, 17420 wired pages
…（zone/map 表全部打出来了）
AMFI: queried model name from device tree: iPad7,1
Security policy loaded: Apple Mobile File Integrity (AMFI)
AMFI: PMGRAON latched to: ffffffff          ← 我们的 PMGR 桥接库生效了
AMFI: Enabling developer mode since protected data is not available
apfs_module_start:3232: load: com.apple.filesystems.apfs, v2236.140.6
Still waiting for root device
```

两条要记住的：

- `debug_log_init: Error!! gPanicBase is still not initialized`（出现两次）——所以 panic 只走屏幕，
  不进 panic log；以后一切以 eMMC 控制台日志为准。
- 结尾 `Still waiting for root device` + IOKit 在等 `IOMedia`（Content = Apple_HFS）——阶段 6 的标准状态。

完整日志已存档：`out/klog10.txt`（262 KB 块，7699 B 有效文本）。

### 阶段 6 的现实：这块板没有可用的存储组件

组件目录（`components.toml`）里与块设备有关的只有：

| 组件 | 用途 | 我们能不能用 |
|---|---|---|
| `ShengUfs` | UFS 存储（Qualcomm） | 不能，dandelion 是 eMMC（MediaTek MSDC0） |
| `VirtioMmioBlock` | virtio-MMIO 块设备 | 不能，没有 virtio |
| `RamBlock` | 负载里的 RAM Data 卷 + ADT 占位符 | 能，但只给**可写 Data**，不给根 |

所以"挂根"这一步需要**新写一个 eMMC/SDHC 主机组件**（对标 `ShengUfs` 的规模：块设备、DMA/IOMMU、
分区方案、`VerifiedRoot` 绑定），再按 `guide/userdata` 造 userdata 镜像并按 `porting/ufs` 的清单验证。
这是下一个大阶段，不是一两轮能完的。

### 建议的下一步顺序

1. 先用 `--data-ramdisk` + `RamBlock` 拿到一个**可写 Data 卷**（validate：`/chosen/memory-map` 的
   `RAMDisk` 占位 + `rd=md0`），确认 RamBlock 与内核的 ABI 通；
2. 再写 eMMC 主机驱动（先只读、轮询、单块），目标是让 `IOMedia` 出现 userdata；
3. 然后按 `guide/userdata` 造根卷镜像 + `root_match_volume_uuid`，走到 `launchd`。

---

## 0.19 阶段 6 第一步：RamBlock 已进内核，但内核还是只看到“等根设备”（2026-11-04 01:1x）

### 做了什么

| 改动 | 位置 |
|---|---|
| 组件 `RamBlock` 加入 `include` | `board.toml` `[components]` |
| `build-arm-driver.py RamBlock` 加入 `[kernel] archives` | `board.toml` |
| Data seed（2 MiB 原始镜像） | `~/work/dandelion-data-seed.dbr`，经框架 `unixnu.local.toml` 的 `[build] data_ramdisk` 注入 |
| 内核重建 | `kernel.macho` 51,384,320 B，`payload.json` 里 `data_ramdisk_sha256` 非空 |

（先试了 256 MiB 种子：**构建直接拒绝** —— `the payload files take 305 MiB; raise size_mib (now 56)`。
payload 就是 boot.img 的 ramdisk，而 boot 分区只有 64 MiB，所以这块板上"塞一个真 Data 卷"不可行；
真 Data 必须来自持久存储，不是 payload。）

### 结果

内核日志（`flashinfo@LBA 30000`，7790 字节）结尾仍然是：

```
apfs_module_start:3232: load: com.apple.filesystems.apfs, v2236.140.6
Waiting on <dict ID="0"><key>IOProviderClass</key><string ID="1">IOMedia</string><key>Content</key><string ID="2">Apple_HFS</string></dict>
Still waiting for root device
```

而且**日志里没有任何 `NativeRamBlock` / `IOMedia` 行**——说明 RamBlock 要么没匹配上，
要么它的输出级别被过滤了。下一次要查的是它的匹配条件（ADT 里的 `/chosen/memory-map` 占位符名
`RAMDisk` vs `PackedRAMDisk`，以及它需要的启动参数），而不是继续加组件。

### 结论：阶段 6 的真正门槛是存储驱动，不是 payload

- 内核已经能跑完 MMU/CPU/GIC/AMFI/bsd_init，缺的是**一个能发布 userdata 的块设备**；
- 组件目录里只有 `ShengUfs`（Qualcomm UFS）、`VirtioMmioBlock`、`RamBlock`，**没有 MediaTek eMMC**；
- 所以下一步是写一个 eMMC/SDHC 主机组件（先只读、轮询、单块），对标 `ShengUfs` 的规模；
  固件侧我们已经有 `DandelionEmmcDxe`/`StorageBeaconDxe` 的实现可以对照（同样的 MSDC0 寄存器序列）。

### 今天的完整链路（供明天接着干）

```
LK → BootShim → SILICIUM_UEFI.fd（我们构建）
  → PayloadBootDxe：发布 DT、发布 flashinfo 日志、发布 payload 卷（0x51B1F000）、发布 GOP、起 DarwinBoot
  → UniXNU 菜单（FV 里的 app）→ 读 payload FAT 上的 unixnu.toml 与 kernel.macho（51 MB）
  → DarwinBoot：256 MiB 窗口、video 0x7ec50000/2944、ExitBootServices、交接
  → 内核：PAN 已按 A53 编译、GIC 在 ADT 里、AMFI 的 PMGR 由 ShengPlatformPower 垫片发布（sheng-pmgr=2）
  → bsd_init → IOKitBSDInit → “Failed to mount root device”（缺存储驱动）
内核控制台：flashinfo@LBA 30000（512 扇区），文本可读，见 out/klog11.txt
```

---

## 0.20 阶段 6 的接线已经完整，卡在"没有可挂的根卷"（2026-11-04 01:3x）

### 链路每一环都核对过（都在产物里）

| 环节 | 状态 | 证据 |
|---|---|---|
| 组件 `RamBlock` 链进内核 | ✓ | `[kernel] archives` 有 `build-arm-driver.py RamBlock`；构建日志 `== kernel: archive-14` |
| Data seed 进 payload | ✓ | `Payloads/scratch-data.img` 2,097,152 B；`payload.json` 的 `data_ramdisk_sha256` |
| UniXNU 把 initrd 交给 loader | ✓ | payload 的 `unixnu.toml` 有 `initrd = "Payloads/scratch-data.img"` |
| ADT 有 RAMDisk 占位符 | ✓ | `dump_adt.py`：`memory-map: RAMDisk=[0x0 0x0]`，另有 `ephemeral-storage=0x1` |
| DarwinBoot 交接 | ✓ | `DARWINBOOT: hand-off to the kernel`，`args pa=0x594FC000` |

### 但内核要的根卷不是 Data

内核日志最后一行是

```
Waiting on <dict ... IOProviderClass=IOMedia ... Content=Apple_HFS>
Still waiting for root device
```

`NativeRamBlock::start()` 只在 `/chosen/memory-map/RAMDisk` 被 DarwinBoot 填了 16 字节范围时才发布块设备
（且要求 4 KiB 对齐、≤2 GiB、落在内核物理窗口内），它给的是**可写 Data**，不是根；
而根卷必须是 `Content = Apple_HFS` 的卷（`IOKitBSDInit.cpp:984` 的等待条件）。

### 于是阶段 6 只剩两条真实路径（都需要本机没有的东西）

1. **存储驱动 + userdata 根卷**：写 MediaTek eMMC/SDHC 主机组件（组件目录里没有，`ShengUfs` 是 UFS），
   再按 `guide/userdata` 造 userdata 镜像 —— 造镜像那一步要 macOS（`hdiutil`/AEA 解密）。
2. **RAM 根 fixture**：把根卷镜像直接放进 payload。本机没有 `mkfs.hfsplus`/`hdiutil`，
   造不出 `Apple_HFS` 卷；这台 Linux 主机上只能借一台 macOS，或者先装 `hfsprogs` 试一个空 HFS+ 卷
   （空卷只能证明"根设备能挂上"，跑不到用户态）。

### 明天开工的第一件事

先确认本机能不能造 HFS+/APFS 卷（`apt install hfsprogs` 或一台 macOS）；能造就把空 HFS+ 当 ramdisk 试挂根，
把"根设备挂载"这个里程碑拿到手；不能造就直接起 eMMC 主机组件（固件侧 `DandelionEmmcDxe` 的寄存器序列可对照）。

---

## 0.21 根卷路径打通：内核已经在 `md0` 上尝试挂根（2026-11-04 02:0x）

### 关键一步：`rd=md0`

参考其它移植（`docs/devices/j716f.md`、`rmx1971.md`）都用 `rd=` 指定根设备：
j716f 是 `rd=md0`（APFS RAMDisk），rmx1971 是 `rd=ufsroot0`。我们之前只把 seed 放进 payload、
ADT 也有 `RAMDisk` 占位符，但**没有 `rd=md0`，内核就一直在等 UUID 匹配的卷**。

加上 `rd=md0` 之后，内核日志（`out/klog13.txt`，45,613 字节）第一次出现根设备：

```
BSD root: md0, major 3, minor 0
apfs_vfsop_mountroot:2965: apfs: mountroot called!
container_rootmount:2531: boot from ramdisk /dev/md0
dev_init:303: md0 device accelerated crypto: 0 (compiled @ Jul 11 2025 17:34:16)
dev_init:306: md0 device_handle block size 512 block count 8192 features 0 internal
apfs_vfsop_mount:2479: unable to root from devvp 0xfffffff44fc34790 (root_device): 79
apfs_mountroot failed: 79
cannot mount root, errno = 19
```

`block count 8192` × 512 = **4 MiB**，正好是我们塞进 payload 的 seed 大小 —— 说明
**DarwinBoot 正确填了 `/chosen/memory-map/RAMDisk`**，md 驱动拿到了正确范围，
`container_rootmount` 认出来"从 ramdisk 启动"。整条链路（payload → loader → ADT 占位符 → md0）全部成立。

### 只差文件系统格式

失败原因是 **79**（`ENOTSUP` 一类）：我们造的是 **HFS+** 卷，而 iOS 17 的内核只走 `apfs_vfsop_mountroot`。
所以下一步只需要一个**APFS 卷**，别的都不用改。

### APFS 卷怎么来（本机现状）

- `apt install apfsprogs` 只提供 `apfsck`（检查器），**没有 `mkfs.apfs`**；
- `libfsapfs-utils` 是只读访问；
- 参考记录的 `ramdisk_apfs_meta_10mb.dmg` 是在 macOS 上用 `hdiutil`/`diskutil` 做的。

所以这一步要么用一台 macOS 造一个最小 APFS 卷（比如 16 MiB，含 `SHNGROOT` 布局或至少一个空卷），
要么在 Linux 上自己拼一个最小 APFS 容器（工作量大，不建议）。

### 顺带修掉的构建脚本坑

`build_intc.sh` 末尾那段 `mcopy` 会把 payload 里的 kernel 复制到 QEMU ESP，遇到已存在的目标会**交互式询问**
`overwrite (y/n)?`，把整个构建脚本挂住（今晚卡了两次、每次十几分钟）。已把那段从构建脚本里移除，
QEMU 的 restaging 只在 `restage_qemu.sh` 里做。

---

## 0.22 eMMC 驱动已写但链不上；mockfs 开了但 RAM 盘仍抢先（2026-11-04 08:5x）

### eMMC 主机驱动：代码完成，链接受阻

`package/DandelionEmmc.cpp`（约 460 行）+ `scripts/build-dandelion-emmc.py` + `patches/xnu/dandelion-emmc.patch`
+ dts 的 `/arm-io/emmc` 节点都写好了，**归档编译通过**（`libDandelionEmmc.a` 258,624 B）。要点：

- 卡在 LK 阶段就初始化好了（固件驱动注释：`CMD0, no CMD1, no re-tuning`），所以内核驱动只做：
  `IOMemoryDescriptor::withPhysicalAddress` 映射 MSDC0 → 设分频/PIO → CMD8 读 EXT_CSD（SEC_COUNT 在 212 字节）
  → CMD17/CMD18 读、CMD24/CMD25 写 + CMD12 收尾，全部轮询、无中断；
- 寄存器/命令/FIFO 序列照抄 `DandelionEmmcDxe.c`（同一颗芯片、同一条路径）。

**链接失败**（这是真正的阻塞点，不是代码问题）：

```
Undefined symbols for architecture arm64:
  "__ZTV20IOBlockStorageDevice", referenced from: ...
  "__ZN20IOBlockStorageDeviceC2EPK11OSMetaClass", ...
  "__ZN9IOStorage8completeEP19IOStorageCompletioniy", ...
```

`IOStorageFamily` 是**预链接 kext**：内核链接阶段它的符号还不存在，而我们的驱动子类化了
`IOBlockStorageDevice`，于是整张虚表都要解析。两条路（都记在 `board.toml` 的注释里）：

1. 给内核链接加一个 **link-stub 归档**（defconfig 里 `EXTRA_LD_ARCHIVES` 那一行的注释就是为这个准备的）；
2. 或者把驱动做成**组件**（`components.toml` 里 `ShengUfs` 那样的 `kind="driver"`），由框架的组件机制接管链接。

另外查到：参考板 `stanveyjiang/UniXNU-camellia`（MT6833）**也没有存储驱动**，它走的就是 mockfs。

### mockfs：打开了，但 RAM 盘仍然抢先

`CONFIG_XNU_SOURCE_MOCKFS=y` 后内核能编过（`build exit=0`）。但内核日志仍然是：

```
container_rootmount:2531: boot from ramdisk /dev/md0
dev_init:306: md0 device_handle block size 512 block count 8192
apfs_mountroot failed: 79
```

而这一版 payload 里**已经没有 seed 了**（`payload.json` 的 `data_ramdisk_sha256` 为 None、
`Payloads/` 下没有 `scratch-data.img`、ADT 的 `memory-map` 为空），`rd=md0` 也从 `kernel_args` 里拿掉了，
固件日志里也没有任何 ramdisk 交接行。**所以"md0 是谁创建的"是下一次开工的第一个问题。**

可能的解释（按可能性排）：

1. 内核控制台块（LBA 30000）**跨启动累积**，我们读到的尾部是上一版（有 seed）那次启动的内容；
   验证方法：刷机前把 30000..30511 全清零，再读一次；
2. `container_rootmount` 在内核里另有一条 RAM 盘来源（例如 `boot_args` 里的字段被 DarwinBoot 填了）；
3. 刷写没有真正落到 `boot` 分区（`fastboot getvar current-slot` / 读回比对可排除）。

### 今天（这一夜）的完整进度

```
阶段 4-5  内核入口 → MMU → arm_vm_init → CPU/GIC → AMFI（PAN 指令 + GIC 节点 + PMGR 垫片）   ✅
阶段 6    根卷：RAM 盘路径全程打通（md0、8192 块、container_rootmount），只差 APFS 文件系统   ⏳
存储驱动  代码完成、归档可编，链接需要 link-stub 或组件化                                   ⏳
控制台    flashinfo@LBA 30000 文本日志可用（klog10..16）                                    ✅
APFS      本机造不出来（apfsprogs 只有 apfsck），已加 CI workflow `.github/workflows/apfs-fixture.yml` ⏳
```

---

## 0.23 用户态 exec：从 "SIGKILL" 一路查到具体检查（2026-11-04 13:0x-14:1x）

### 屏幕那句话的确切含义

`unexpected SIGKILL of init with reason -- namespce 9 code 0x1`：

- namespace 9 = **`OS_REASON_EXEC`**（`bsd/sys/reason.h:96`），不是 codesigning；
- code 1 = **`EXEC_EXIT_REASON_BAD_MACHO`**（同文件 `reason.h:248`）。

### 走过的三条死路（都有实测依据，不是猜的）

| 尝试 | 结果 |
|---|---|
| `amfi_get_out_of_my_way=1` | 无效：问题不在 AMFI，在 Mach-O 装载器 |
| `cs_enforcement_disable=1` | **panic**：`can't has cs_enforcement_disable @AppleMobileFileIntegrity.cpp:5261` |
| 手工写 `trust_cache_module1`（version 1 / 22 字节条目） | **`DARWINBOOT: boot refused: Compromised Data`**：DarwinBoot 校验的是另一种 bundle 格式（`count + offset[] + module(version **2**, 24 字节条目)`） |

修好 bundle 后内核日志出现 `loaded external trust cache module: 0` —— **trust cache 这条路通了**。

### 决定性证据：诊断打印

给内核加了一条临时补丁（`patches/xnu/dandelion-exec-diag.patch`），在 `kern_exec.c` 的
`lret = load_machfile(...)` 失败分支里打印返回码，eMMC 日志立刻给出：

```
DANDELION-EXEC: load_machfile lret=4 errno=85 cputype=16777228 flags=0x50c
DANDELION-EXEC: header filetype=2 ncmds=13 sizeofcmds=704
```

- `lret=4` = **`LOAD_FAILURE`**（`bsd/kern/mach_loader.h:135`，不是 `LOAD_BADMACHO=2`）；
- `errno=85` = **`EBADEXEC`**。

### 根因：静态可执行不能只有 LC_MAIN

`ld64.lld` 的 `-static` **未实现**（只给 warning），所以 fixture 实际是动态镜像（`LC_LOAD_DYLINKER`
+ chained fixups）。剥掉这些命令后它只剩 `LC_MAIN`，而 XNU 的 `result->using_lcmain` 是 **dyld 路径**
专用的；静态 `MH_EXECUTE` 应该带 **`LC_UNIXTHREAD`**（`ARM_THREAD_STATE64`，pc = 入口）——
这正是 `ld -static` 在 macOS 上会生成的形态。

已实现 `scripts/make_unixthread_exec.py`：去掉 `LC_MAIN`、追加 `LC_UNIXTHREAD`（34 个 uint64，
count 按 32 位字 = 68）、清 `MH_PIE`；再用 `scripts/resign_pages.py` 重算 13 个页哈希并生成新的
trust cache bundle（新 CDHash `cbfc2438…`）。

### 本机（Linux）造 fixture 的完整工具链（都在 `scripts/`）

| 脚本 | 作用 |
|---|---|
| `strip_dynamic_load.py` | 去掉 `LC_LOAD_DYLINKER` / `LC_DYLD_CHAINED_FIXUPS` / `LC_DYLD_EXPORTS_TRIE`，清 `MH_DYLDLINK` |
| `make_unixthread_exec.py` | `LC_MAIN` → `LC_UNIXTHREAD`（静态入口），清 `MH_PIE` |
| `resign_pages.py` | 重算 CodeDirectory 页哈希（大端），输出新 CDHash + loader 格式 trustcache bundle |
| `cdhash_trustcache.py` | 从已签名 Mach-O 解析 CDHash → bundle（含 DarwinBoot 的 `ValidTrustCaches` 规则） |
| `make_mockfs_image.py` | 组装 `N4KROOT` 容器（launchd@0x1000、probe@4K 对齐） |

### 待验证

镜像 `boot-intc.img`（sha256 `cb0d1e67…`）已构建：mockfs 容器 + `LC_UNIXTHREAD` fixture +
匹配的 trust cache。刷入后看两件事：
1. 若出现 `DANDELION-EXEC` 行 → 仍是 `LOAD_FAILURE`，按 lret 继续查；
2. 若没有该行、也没 panic → **PID 1 在 EL0 跑起来了**。

### 备用路线（若 Linux 产物始终不等价）

框架记录里跑通的 fixture 是 `build-native-user-fixture.py` 在 **macOS** 上用
`ld -static` + `codesign` 生成的。仓库已备好 `.github/workflows/el0-fixture.yml`：
在 fork 里跑一次即可拿到官方 `launchd.macho` + `trustcache.bundle`，替换 payload 里的两个输入即可。

---

## 0.24 里程碑：PID 1 在 EL0 跑起来了（2026-11-04 14:2x）

### 证据链（全部是同一次启动）

| 证据 | 内容 |
|---|---|
| 内核构建时间 | `Darwin Kernel Version 23.6.0: Wed Nov 4 13:36:14 CST 2026`（带诊断补丁的新内核） |
| 根文件系统 | `Added memory device md0/rmd0 ... for 0x10000` → `BSD root: md0`（mockfs 容器，64 KiB） |
| 信任缓存 | `attempting to load 1 external trust cache modules` → `loaded external trust cache module: 0` |
| 装载结果 | `load_init_program: attempting to load /sbin/launchd` 之后**没有** `DANDELION-EXEC` 行（= `load_machfile` 成功），也没有 `failed loading ... errno` |
| 内核状态 | 之后**没有 panic**；屏幕停在菜单/色条，机器保持运行 |

`bsd_init` 在 init 退出时会 panic（"unexpected SIGKILL of init ..."），所以"不 panic"= **init 仍在运行**，
也就是我们那个静态 EL0 fixture 正在用户态执行。

### 让它成立的两个关键（之前都不是）

1. **静态入口形式**：`LC_UNIXTHREAD`（`ARM_THREAD_STATE64`，pc = `__TEXT.vmaddr + entryoff`，`count` 按
   32 位字 = 68），而不是 `LC_MAIN`——后者是 dyld 路径专用；
2. **页哈希必须重算**：trust cache 只表示"这个 CDHash 允许运行"，内核仍逐页校验 CodeDirectory；
   改过 load command 之后必须用 `scripts/resign_pages.py` 重算 13 个页槽并生成新的 bundle。

配套的两个 loader 格式细节（之前都踩了）：
- trust cache bundle = `count + offset[] + module(version **2**, 24 字节条目)`（内核内部结构是 version 1/22 字节，不是这个）；
- `DARWINBOOT` 的 `ValidTrustCaches` 对这个格式逐条校验，格式错就是 `boot refused: Compromised Data`。

### 现在的完整启动链（本项目第一次全链路走通到用户态）

```
LK → BootShim → SILICIUM_UEFI.fd
  → PayloadBootDxe：DT / eMMC 日志 / payload 卷(0x51B1F000) / GOP / DarwinBoot
  → UniXNU 菜单 → payload FAT 上的 kernel.macho(51 MB) + scratch-data.img(N4KROOT) + trustcache
  → DarwinBoot：窗口/视频/参数校验、填 /chosen/memory-map/{RAMDisk,TrustCache}、ExitBootServices
  → 内核：PAN 按 A53 编译、ADT 有 GIC、AMFI 由 ShengPlatformPower 垫片满足（sheng-pmgr=2）
  → bsd_init → md0 → mockfs 根 → exec /sbin/launchd → **PID 1 在 EL0 运行** ✅
内核控制台：flashinfo@LBA 30000（文本），日志存档 out/klog*.txt
```

### 下一步（按框架的阶段顺序）

1. **真实根卷**：CI 造 APFS 卷（`.github/workflows/apfs-fixture.yml` 已备好）→ 替换 mockfs，
   让 `apfs_vfsop_mountroot` 挂上真实 APFS 根，PID 1 换成真实 launchd（`.github/workflows/el0-fixture.yml`
   产出的官方 fixture 可作为对照，确认我们的 Linux 产物与官方一致）；
2. **持久存储**：eMMC 主机驱动（`package/DandelionEmmc.cpp` 已写好、可编译），需要 link-stub 或组件化才能
   链进内核（见 §0.22）；
3. **Data 卷**：`--data-ramdisk` + RamBlock（§0.21 已验证 md0 路径），APFS 版 Data seed 同样来自 CI。

---

## 0.25 APFS 卷可以在本机造了（不用 macOS / 不用 CI）（2026-11-04 14:2x-14:5x）

之前判断"APFS 必须 macOS"是**错的**：`apfsprogs` 的 Ubuntu 包里只有 `apfsck`，但上游仓库自带 **`mkapfs`**，
本机编译即可：

```sh
git clone --depth 1 https://github.com/linux-apfs/apfsprogs.git /tmp/apfsprogs
make -C /tmp/apfsprogs/mkapfs          # 产出 mkapfs/mkapfs
truncate -s 4M image.apfs
/tmp/apfsprogs/mkapfs/mkapfs -L DANDELION image.apfs 1024    # 4 KiB 块，1024 块
```

实测可用尺寸：2 / 3 / 4 / 6 / 8 MiB 都能生成（`NXSB` 魔数落在 0x20）。16 MiB 塞不进 payload
（`the payload files take 65 MiB; raise size_mib (now 56)`），**4 MiB 是安全选择**。

脚本：`_work/stage_apfs_seed.sh <MiB>`。

### 这一版 payload 已核对（手机一到就能测）

| 项 | 值 |
|---|---|
| `Payloads/scratch-data.img` | 4,194,304 B，偏移 0x20 = `NXSB` ✓ |
| `unixnu.toml` | `initrd = "Payloads/scratch-data.img"`，`args = "-v serial=3 sheng-pmgr=2 rd=md0 amfi_get_out_of_my_way=1"` |
| ADT `/chosen/memory-map` | `RAMDisk=[0x0 0x0], TrustCache=[0x0 0x0]`（等 DarwinBoot 填） |
| boot.img | 59,494,400 B，sha256 `00188063…` |
| 内核 | 带 `dandelion-exec-diag.patch`（失败时会打印 `DANDELION-EXEC`），构建时间 13:36 |

### 判据（`_work/test_ios_boot.sh` 一键跑完并打印）

- 出现 `apfs_vfsop_mountroot: apfs: mountroot called!` 且**没有** `apfs_mountroot failed: 79`
  → **APFS 根卷挂载成功**（这一版镜像里没有任何文件，所以随后会是 `failed loading /sbin/launchd: errno 2`，
  那正是"挂上了但卷是空的"的正确表现）；
- 仍是 `apfs_mountroot failed: 79` → mkapfs 造的容器被内核拒绝，需要换布局（记录 size/块大小/特性）；
- 出现 `DANDELION-EXEC` → exec 侧又有新问题，按 lret 继续。

### 现状小结（这一夜的完整成果）

```
阶段 4-5  内核入口/MMU/CPU/GIC/AMFI                    ✅
阶段 6    根卷：md0 + mockfs + trust cache             ✅（PID 1 在 EL0 运行）
         真实 APFS 根卷                                ⏳（镜像已就绪，等手机）
         持久存储 eMMC                                  ⏳（驱动代码已写，需 link-stub/组件化）
```

---

## 0.26 APFS 那次的真相：mockfs 把它盖住了；eMMC 有现成社区驱动（2026-11-04 14:5x-15:0x）

### APFS 测试结果解读

日志（`out/klog12`/`/tmp/k12.txt`）：

```
Added memory device md0/rmd0 ... for 0x400000      ← 4 MiB，就是我们的 APFS 容器
BSD root: md0, major 3, minor 0
load_init_program: attempting to load /sbin/launchd
load_init_program: failed loading /sbin/launchd: errno 8     ← ENOEXEC
```

**没有任何 `apfs_vfsop_mountroot` 行**，而 errno 8 说明根卷是 **mockfs**：它把整块设备当成
`/sbin/launchd` 交给 exec（APFS 容器开头不是 Mach-O → ENOEXEC）。

也就是说：`vfs_mountroot` 里 APFS 没挂上（连日志都没走到），然后 **最后一个 mountroot——mockfs——总是成功**，
把失败盖掉了。这是 `CONFIG_XNU_SOURCE_MOCKFS=y` 的副作用。

**处理**：已把 `CONFIG_XNU_SOURCE_MOCKFS` 关掉并重建（`boot.img` sha256 `258888b9…`），
下一次启动日志会直接显示 `apfs_mountroot` 的真实结果，而不是被 mockfs 兜底。

### eMMC：社区已经有 MediaTek MSDC 驱动

用户点出的文档：

| 组件 | 仓库 | 范围 | 状态 |
|---|---|---|---|
| `NativeMsdc` | [mouseos/NativeMsdc](https://github.com/mouseos/NativeMsdc) | MT8167 MSDC eMMC 块驱动，发布根镜像窗口 + 持久 Data | “可针对 xnu-10063.141.1 头文件编译”；寄存器序列在早期 a04br3 移植上用过 |
| `NativeEmmc` | [Dracen-W/NativeEmmc](https://github.com/Dracen-W/NativeEmmc) | Redmi Note 9（merlinx，同为 MTK）eMMC 驱动 | 仓库存在，未记录真机结果 |

已克隆 NativeMsdc 到 `~/work/NativeMsdc`（1500 行 + 599 行头）。其 README 说明了我们缺的那一环：

> “They subclass the original IOBlockStorageDevice of the prelinked IOStorageFamily,
> so the kernel link needs the **storage bindings**.”

**storage bindings 在框架里的实现**是 `scripts/build-native-virtio-bindings.py`：它把驱动对象里的
vtable 重定位（`__ZTV<Component>`）解析到**参考预链接内核**里原始 IOStorageFamily 的真实符号地址，
生成 bindings 对象再参与链接。它当前支持的组件是 `VirtioMmioBlock / RamBlock / NativeVirtioBlock /
NativeRamBlock`——**加一个 `NativeMsdc` 是延长这个脚本的 component 映射表**，正是 §0.22 里“link-stub 或组件化”
那条路的官方做法。

另外核实：**上游 `UniXNU/UniXNU` main 分支没有 `NATIVE_MSDC` 支持**（`build-xnu-4k-baseline.sh` 里 0 处），
所以这套要自己接：defconfig 开关、archive 配方、personality patch（匹配 `mediatek,*-mmc` 节点）、bindings 步骤。

### eMMC 接入的具体工作清单（下一步）

1. `NativeMsdc.cpp` 适配 MT6762G：MSDC0 基址 0x11230000、CMD/DMA 序列参考固件 `DandelionEmmcDxe.c`；
   MT8167 与 MT6762G 的 MSDC IP 同源，先按 README 的 PIO 轮询路径跑通；
2. 扩展 `build-native-virtio-bindings.py` 的 component 表加入 `NativeMsdc`（生成 storage bindings）；
3. defconfig 加 `CONFIG_XNU_NATIVE_MSDC=y`；`[kernel] archives` 加配方；`patches/xnu` 加 personality patch；
4. 启动参数 `native-msdc-root=1` 先只读发布窗口，再用 `native-msdc-persist=1` 验证持久区；
5. 真机验证：读出的块与 `adb shell dd` 的同 LBA 内容逐字节比对（README 也这么要求）。

---

## 0.27 eMMC：社区 NativeMsdc 已移植进内核并链接成功（2026-11-04 15:0x-15:4x）

### 做了什么

1. **移植**：把 [mouseos/NativeMsdc](https://github.com/mouseos/NativeMsdc)（BSD-2-Clause-Patent，
   1500 行 + 599 行头）放进 `package/NativeMsdc.{cpp,h}`，并在文件头写明来源与许可；
   对 dandelion 的唯一改动是 personality 的 `compatible`：`mediatek,mt8167-mmc` → **`mediatek,mt6765-msdc`**
   （与 `dts/dandelion.dts` 的节点一致，3 处）；
2. **构建**：`scripts/build-native-msdc.py` —— 用内核自己的 `IOPlatformExpert.cpo` 编译记录 + `build/storage-abi/include`
   编译，产出 `build/native-msdc/libNativeMsdc.a`。**首次编译即通过**；
3. **链接**：`board.toml [kernel] archives` 加 `build-native-msdc.py`；
   defconfig 的 `CONFIG_XNU_EXTRA_LD_ARCHIVES` 指向该归档；
4. **实例化**：驱动原本靠 kext 的 Info.plist personality 匹配，内核内链接版本不会自己起来——
   在 `NativeMsdc.cpp` 末尾加 `dandelion_msdc_install()`：用 `IOService::addMatchingNotification`
   监听 `IOPlatformDevice`，匹配到 `/arm-io/emmc` 就 `new NativeMsdc` → `probe` → `attach` → `start`；
5. **bsd_init 钩子**：`patches/xnu/dandelion-msdc.patch`（在 sheng 桥之后调用 `dandelion_msdc_install()`），
   `git apply --check` 干净；
6. **启动参数**：`native-msdc-persist=1`（驱动默认不碰控制器；这个参数让它读 GPT 并验证持久区）。

### 为什么先按 `NATIVE_MSDC_BLOCK_DEVICES=0` 构建

驱动 README：窗口块设备（`NativeMsdcBlock : IOBlockStorageDevice`）“subclass 了预链接 IOStorageFamily 的类，
所以内核链接需要 **storage bindings**”。`BLOCK_DEVICES=0` 的版本不含块设备，**自己就能链接**，
但仍会做控制器 bring-up + 读 GPT —— 先用它验证硬件访问是真实的。

窗口块设备那一步（要 root 卷才需要）需要：
- 把 `build-native-virtio-bindings.py` 的组件表扩展到 `NativeMsdc`（它把 `__ZTV<Component>` 解析到参考预链接内核的
  IOStorageFamily 真实地址），
- 然后 `--block-devices 1` 重编，并把 `native-msdc-root=1` 加进启动参数。

### 构建结果（已验证）

```
kernel.macho sha256 c67a834b…            （含 52 个 NativeMsdc 符号）
payload args: -v serial=3 sheng-pmgr=2 rd=md0 amfi_get_out_of_my_way=1 native-msdc-persist=1
boot.img sha256 139cedcb…                （59,494,400 B，15:42）
```

### 下一次启动要看什么（`NativeMsdc:` 前缀的 IOLog，会进 eMMC 控制台）

| 日志 | 含义 |
|---|---|
| `NativeMsdc:` + step/manifest 相关行 | 控制器起来了、GPT 读到了 |
| 完全没有 `NativeMsdc:` 行 | 匹配没触发（`compatible` 不匹配或 nub 未发布），需要查 `/arm-io/emmc` 是否成为 IOPlatformDevice |
| `hostBringUp` 失败原因 | 时钟/CMD 序列与 MT6762G 的差异，按 `why` 调整 |

### 备注（构建时间）

`scripts/package.sh` 每次都会连 UEFI 固件一起构建，固件没变时那一步纯属浪费（本次 32 分钟里约 20 分钟是固件）。
以后只改内核/payload 时，用 `unixnu build kernel` + `unixnu build payload` + `unixnu bootimg` 三步可以省掉。

---

## 0.28 两个里程碑同时落地：APFS 根卷挂载成功 + eMMC 驱动工作（2026-11-04 15:5x）

### 1. APFS 根卷挂载成功

```
nx_mount: md0 checkpoint search: largest xid 1, best xid 1 @ 1
nx_mount: md0 stable checkpoint indices: desc 0 data 0
apfs_vfsop_mount: md0s1 fs iokit node was not found
handle_mount:817: md0s1 vol-uuid: 29097E4D-113B-4F46-84CB-8CBCCA84135F
                  block size: 4096 block count: 1024 (unencrypted; flags: 0x1; features: 1.0.2)
handle_mount:830: md0s1 setting dev block size to 4096 from 512
apfs_log_op_with_proc: md0s1 mount-complete volume DANDELION, requested by: kernel_task
```

之前那次 `apfs_mountroot failed: 79` 是**镜像不是 APFS**（HFS+），而 mockfs 又把失败盖住；
换成本机 `mkapfs` 造的真 APFS 容器后，**APFS 直接挂载完成**（卷名 DANDELION，1024 个 4 KiB 块）。

随后 `load_init_program` → `/sbin/launchd` → `errno 8`：**卷是空的**，没有 launchd。这是预期。

### 2. eMMC 驱动第一次在 XNU 里工作

```
NativeMsdc: controller 0x11230000 inherited CFG=0x02210499 STS=0x00000000
NativeMsdc: EXT_CSD rev 8, 122142720 sectors, cache 0, partition access 0
NativeMsdc: userdata sectors 13385728..122021854
NativeMsdc: no SHNGROOT manifest in userdata
```

逐条含义：

| 日志 | 证据价值 |
|---|---|
| `controller 0x11230000 inherited CFG=0x02210499` | 与固件日志里的 `cfg 0x02210499` **完全一致**——驱动继承的是固件留下的同一控制器状态，没有重置 |
| `EXT_CSD rev 8, 122142720 sectors` | 122,142,720 × 512 = **62.4 GB**，手机 eMMC 的真实容量 |
| `userdata sectors 13385728..122021854` | **读到了 GPT** 并找到 userdata 分区（Android 的分区表） |
| `no SHNGROOT manifest` | 预期：userdata 里是 Android 数据，不是 iOS 的 SHNGROOT 布局 |

到这一步，**XNU 已经能在这台手机的 eMMC 上读数据**：控制器、EXT_CSD、GPT、分区边界全部正确。

### 现在的完整状态

```
内核 → mockfs/APFS 根                    ✅（APFS 挂载已验证）
     → eMMC 读（控制器/EXT_CSD/GPT）      ✅
     → 块设备窗口（native-msdc-root=1）    ⏳ 需要 NATIVE_MSDC_BLOCK_DEVICES=1 + storage bindings
     → 真实 root（userdata 里的 OS/SystemOS） ⏳ 需要造 userdata 镜像
```

### 下一步的两条依赖

1. **块设备窗口**：`build-native-virtio-bindings.py` 的组件表加 `NativeMsdc`，
   `--block-devices 1` 重编，启动参数加 `native-msdc-root=1`；
2. **userdata 镜像**：按 `guide/userdata` 用 IPSW 的 OS/SystemOS 造（含 SHNGROOT manifest + `root_match_volume_uuid`）。
   造镜像那一步要 macOS 的 AEA 解密/hdiutil —— 这是唯一还需要 CI/macOS 的环节。

### 本次构建踩的时间坑

`scripts/package.sh` 每次都连 UEFI 固件一起构建；本次 32 分钟里约 20 分钟是没变的固件。
只改内核/payload 时应该用：
```sh
python3 ../UniXNU/unixnu build kernel  -D .
python3 ../UniXNU/unixnu build payload -D .
python3 ../UniXNU/unixnu bootimg       -D .
```

---

## 0.29 userdata 镜像怎么做 + 块设备 bindings 的进展（2026-11-04 15:5x-16:1x）

### 镜像是框架自带的一条命令

`board.toml` 需要 `[userdata]` 段（起止 LBA 来自**驱动自己读出的 GPT**，即 §0.28 的
`NativeMsdc: userdata sectors 13385728..122021854`）：

```toml
[userdata]
first_lba = 13385728
last_lba  = 122021854
lba_bytes = 512          # eMMC 用 512 字节扇区；UFS 板是 4096
```

然后：

```sh
cd ~/work/UniXNU
python3 unixnu userdata -D ../UniXNU-dandelion --dry-run   # 先看窗口与容量
python3 unixnu userdata -D ../UniXNU-dandelion             # 真做
```

dry-run 输出（已实测）：

```
== userdata: OS would be extracted from 044-76040-038.dmg (4918 MiB)
== userdata: SystemOS would be extracted from 044-76034-038.dmg (3150 MiB)
```

真跑会从 Apple CDN 下载这两个 DMG（约 8 GiB）并写出
`out/<device>/<os_profile>/<config>/userdata/userdata-root.img` + 报告（报告里有 fastboot 命令）。
**正在跑**（16:1x 时已下载 2.8 GB）。

注意：
- 文档写明 `--skip-setup` 与 `seed-data-grow` 需要 macOS `hdiutil`；主路径在 Linux 上用 `scripts/unixnu/udif.py` 读 DMG；
- AEA 加密的镜像由框架处理（`userdata.py` 头注释），若这步在 Linux 上受限，就用 CI（`.github/workflows/` 已有 runner 配置）。

### 块设备（native-msdc-root=1）的 bindings：过了三道 macOS 关卡，卡在参考候选

框架的 storage bindings 链路是 `audit-storage-interface.py` → `build-native-virtio-bindings.py`，
两者原本都假定 macOS。本机已解决：

| 关卡 | 解决方式 |
|---|---|
| macOS SDK 的 `TargetConditionals.h` | 造一个最小 SDK 树 + `xcrun` 垫片（`~/work/fake-macos-sdk`、`~/work/fake-bin/xcrun`） |
| clang 24 比记录工具链更严格（11 个 `-Werror`） | 给 audit 的编译命令加 `-Wno-missing-designated-field-initializers` 等 |
| `xcrun dwarfdump` | 垫片转成 `llvm-dwarfdump --name X`，已验证输出满足脚本的正则 |

**当前阻塞**：audit 需要一个固定参考候选
`build/xnu-prelink/native-apfs-load-positive.macho`（框架为 APFS/root 配置钉住的预链接产物），我们没有。
下一步两条路：

1. 自己写等价的审计：用 `kernel/kernel.json`（预链接计划，16.9 MB，已在）+ 原始 kernelcache 的
   IOStorageFamily kext 符号表，算出每个方法在**我们内核**里的地址，直接生成 bindings（`.set` 汇编）；
2. 或者让 CI 跑框架的原版流程产出那个候选。

在这之前，`NATIVE_MSDC_BLOCK_DEVICES=0` 的驱动已经能读 eMMC（§0.28），窗口发布是它之上的最后一步。

---

## 0.30 storage bindings：能解的都解了，差 4 个符号（2026-11-04 16:0x-16:3x）

### 做成的事

`scripts/bind-by-vtable.py`：**按 vtable 槽位配对**生成 IOStorageFamily 的绑定。

- 驱动的对象里有自己的 vtable（`__ZTV15NativeMsdcBlock`，190 槽），每个槽的重定位指向
  「自己的 override」或「要继承的基类方法名」；
- 我们**自己的内核**里，IOStorageFamily 的 `__DATA_CONST`（计划里 `va=0xfffffff005cb3200`）有一段
  vtable：`0xfffffff005cb37a0`，33/190 个槽指向 kext 代码；
- 用「驱动未定义的那些存储槽必须落在 kext 代码段」作为签名找到它，然后按槽对齐 → **168 个符号绑定成功**，
  产出 `libStorageBindings.a`；storage-specific 的槽位都验证过落在 kext 的 `__TEXT_EXEC` 里。

### 还差的 4 个符号

`OSDefineMetaClassAndStructors` 生成的代码还引用：

```
__ZTV20IOBlockStorageDevice                      (基类 vtable —— 已找到：0xfffffff005cb37a0)
__ZN20IOBlockStorageDeviceC2EPK11OSMetaClass      (基类构造)
__ZN20IOBlockStorageDeviceD2Ev                    (基类析构)
__ZN20IOBlockStorageDevice10gMetaClassE           (静态元类对象)
```

它们**不在任何可用的符号表里**：

| 来源 | 情况 |
|---|---|
| 我们预链接的 `kernel.macho` | 50,953 个符号，但 kext 符号在预链接时被剥掉（`IOStorage*` 计数 0） |
| IPSW 的 `kernelcache.decompressed` | 只有 6,056 个内核导出符号，没有 kext 的 C++ 符号 |
| 预链接计划的 `bindings`（69,294 条） | 只给 fixup 记录；IOStorageFamily 的 `site` 位移全是 `-0x1000000`（就是 slide），不含 vtable/ctor/dtor 地址 |

框架的 `audit-storage-interface.py` 是从**钉住的参考候选** `native-apfs-load-positive.macho` 取这三个值
（`original_protected_constructor` / `original_base_destructor` / `original_metaclass`），我们没有该产物。

### 因此

已把树恢复成**可工作状态**（`NATIVE_MSDC_BLOCK_DEVICES=0` + `native-msdc-persist=1`，§0.28 验证过的那套），
`bind-by-vtable.py` 与 `build-native-msdc.py --block-devices 1` 都保留在仓库里，等下面任一条路打通即可接入：

1. **让 CI 跑框架原版流程**产出那个参考候选（`.github/workflows/` 已有 runner 配置）；
2. **反汇编定位**这三个地址（基类构造/析构在 kext 的 `__TEXT_EXEC` 里，`gMetaClass` 在 `__DATA` 里，
   可由 vtable 的 typeinfo 指针与相邻对象推断）。

### userdata 镜像（同时进行，已过下载关）

```
== userdata: image 9382658048 bytes (8.74 GiB) in a partition of 55621697024 bytes
== userdata: OS from 044-76040-038.dmg
== userdata: SystemOS from 044-76034-038.dmg
```

**镜像 8.74 GiB**（不是整个 55.6 GB 分区），正在抽取 OS/SystemOS 两个窗口。
产物：`out/<device>/<os_profile>/<config>/userdata/userdata-root.img` + 报告（含 fastboot 命令）。

### 环境事故记录

16:2x 时 WSL 整体崩过一次（`Wsl/Service/E_UNEXPECTED`），`wsl --shutdown` 后恢复；
那次中断了 userdata 下载（DMG 已缓存，重启后直接从抽取继续）。构建/下载同时跑时注意内存（WSL 9 GB）。

---

## 0.31 环境事故与恢复 + 当前推进路线（2026-11-04 16:4x-18:0x）

### C: 盘清理（与本项目直接相关：构建输出都在 WSL 里，而 WSL 的 vhdx 在 C:）

C: 起始只剩 **0.39 GB**，清理后 **~24.7 GB**。逐项：

| 项目 | 释放 | 说明 |
|---|---|---|
| Windows Temp + Windows Update 缓存 + 回收站 | ~6.1 GB | 安全 |
| pip 缓存 | ~2.2 GB | 可重建 |
| npm 缓存 | ~1.9 GB | 可重建 |
| `JianyingPro\User Data\Cache` | 5.91 GB | 只删缓存；**Projects 1.01 GB 与 Presets 1.71 GB 保留** |
| `.lingma` | 5.57 GB | 用户同意删除 |
| `.qoder-cn` | 4.99 GB | 用户同意删除 |

未动（等用户决定）：Docker `docker_data.vhdx` 8.11 GB、WSL 里的 IPSW 镜像 8 GB、桌面 12.5 GB。
「把构建目录搬到 D:」试过一半后按用户要求回退，`/root/work/UniXNU/out` 保持原样（已确认完整：
`dandelion` 939 MB、`feeds` 730 MB、`ipsw` 8.0 GB；中断时删掉的只是那个 7 GB 的未完成 userdata 镜像）。

### WSL 两次故障与恢复

1. 16:2x `Wsl/Service/E_UNEXPECTED` —— `wsl --shutdown` 后恢复（当时下载中断，DMG 已缓存）；
2. 17:3x `Wsl/Service/CreateInstance/CreateVm/HCS_E_CONNECTION_TIMEOUT` ——
   `wsl --shutdown`、检查 `vmcompute/hns/HvHost`（都在跑，但无权限重启）、内存充足（18.9 GB 中空闲 8.2 GB）
   都无效；**`wsl --update` 是有效修复**（更新到 WSL 2.6.3.0 / 内核 6.18.54.1），之后正常。
   伴随一条无害告警：`Failed to start the systemd user session for 'root'`。

### 当前推进路线（用户要求回到移植）

1. **重建 userdata 镜像**（正在跑）：`unixnu userdata -D ../UniXNU-dandelion`，
   产物 8,382,658,048 B（8.74 GiB）/ `out/.../userdata/userdata-root.img`；
2. **刷入后验证驱动能读到 SHNGROOT**：`native-msdc-persist=1` 的驱动会读 GPT 并解析 userdata 头
   4096 字节的 SHNGROOT manifest；现在会从 `no SHNGROOT manifest in userdata` 变成
   打印出窗口（OS/SystemOS 的偏移与大小）——这是块设备之前能拿到的最后一个纯读取里程碑；
3. **块设备窗口**（最后一步）：168 个 vtable 方法已能绑定（`scripts/bind-by-vtable.py`），
   还差基类构造/析构/`gMetaClass` 三个地址，它们在预链接后被剥掉、IPSW kernelcache 里也没有；
   需要框架钉住的参考候选，或反汇编定位。

---

## 0.32 eMMC 驱动读到并验证了真实的 SHNGROOT manifest（2026-11-04 22:4x）

### 结果

刷入完整 8.74 GiB userdata 镜像后，`native-msdc-persist=1` 的内核日志给出：

```
NativeMsdc: controller 0x11230000 inherited CFG=0x02210499 STS=0x00000000
NativeMsdc: EXT_CSD rev 8, 122142720 sectors, cache 0, partition access 0
NativeMsdc: userdata sectors 13385728..122021854
NativeMsdc: userdata head 53484e47        ← "SHNG"
NativeMsdc: manifest status 0             ← kManifestOk
```

`NativeMsdc.h:239` 的枚举是 `kManifestOk = 0, kManifestNoMagic = 1, …`，所以 **status 0 = 解析成功**。
驱动已经完成了：MSDC0 继承 → EXT_CSD → GPT → userdata 定位 → manifest 读取与 SHA-256 校验 → 窗口表解析。

### 之前为什么一直是 "no SHNGROOT manifest"

两个独立原因，都不在驱动里：

1. **64 MB 片段没落到分区起始**：`fastboot flash userdata` 对稀疏镜像按块写入，
   我们截取的前 64 MB 加上 AVB footer 处理后并未落在 offset 0；
2. **读取日志的 dd 一直失败**（`su` 被拒绝，因为 `fastboot -w` 清掉了 Magisk 的授权数据），
   所以那几次"拉到的日志"其实是更早的旧文件 —— 连我自己加的诊断行都看不到，就是这个原因。

### 关键工作流修正（以后不用拍照、不用每次重设）

本机是 **userdebug** 构建（`ro.build.type=userdebug`、`ro.debuggable=1`），所以：

```sh
adb root                     # adbd 以 root 重新启动，不需要 Magisk
adb shell "dd if=/dev/block/by-name/flashinfo of=/data/local/tmp/fi.bin bs=1M"
adb pull /data/local/tmp/fi.bin flashinfo.bin
```

日志通道（内核 `flashinfo@LBA 30000`、固件 `LBA 30512`）一直是好的，只是需要 root 才能读。

### 现在的完整状态

```
内核入口/MMU/CPU/GIC/AMFI            ✅
mockfs 根 + PID 1 在 EL0             ✅
APFS 根卷挂载（自造 4 MiB 容器）        ✅
eMMC：控制器/EXT_CSD/GPT              ✅
eMMC：userdata SHNGROOT manifest      ✅（本节）
块设备窗口发布（native-msdc-root=1）    ⏳ 需 NATIVE_MSDC_BLOCK_DEVICES=1 + 3 个符号
真实 root（userdata 里的 OS 窗口）      ⏳ 依赖上一条
```

### 唯一剩下的技术阻塞

`NATIVE_MSDC_BLOCK_DEVICES=1` 的构建（发布 OS/SystemOS 窗口）还需要三个符号地址：
`IOBlockStorageDevice` 的基类构造、基类析构、`gMetaClass`。
168 个 vtable 方法已经由 `scripts/bind-by-vtable.py` 绑定（基类 vtable 定位在 `0xfffffff005cb37a0`），
但这三个既不在预链接内核（kext 符号被剥离）也不在 IPSW kernelcache（只有 6,056 个内核导出）里。
两条路：① CI 跑框架原版流程产出钉住的参考候选；② 反汇编定位（构造/析构可通过 `bl IOService` 的调用点识别）。

---

## 0.33 storage bindings 打通 + 真实 OS 卷被 APFS 挂载（2026-11-04 23:0x）

### 1. storage bindings 在 Linux 上解决了

按 sirius 记录的算法（`docs/zh/devices/sirius-dddqmmx.md:246`）实现 `scripts/derive-storage-symbols.py`：

- 用**公开 IOStorageFamily 源码**编译的参考对象（`build/storage-abi/IOBlockStorageDevice-reference.o`）取出 190 个槽名；
- 在本内核 `IOStorageFamily __DATA_CONST` 里找**唯一**匹配全部 176 个内核导出名的表；
- 构造 = "存 table+16 并转调 `IOService` 构造"的那个函数；析构 = 指向 `IOService::~IOService` 的跳板；
  `gMetaClass` = slot 7 的 `adrp/add` 目标。

本内核实测结果：

```
PASS: table 0xfffffff005cb44c0, 176 kernel-bound slots
  constructor 0xfffffff00496ed98 (forwards to IOService)
  destructor  0xfffffff00496edb8 (resolves to IOService)
  metaclass   0xfffffff006add1d0
```

两个坑：① `.set` 只能给 **kext 自有**的符号，给内核导出名也写会造成 **162 duplicate symbols**；
② 内核链接取的是 `build/native-msdc/libNativeMsdc.a` 这个名字，绑定归档必须**覆盖**该路径。

### 2. 窗口发布成功，APFS 挂上了真实的 OS 卷

刷入 8.74 GiB userdata + `native-msdc-root=1` 的内核后：

```
NativeMsdc: SystemOS volume ready
NativeMsdc: published OS
dev_init:306: disk2s1 device_handle block size 4096 block count 1483776
nx_mount: disk2s1 checkpoint search: largest xid 418, best xid 417
nx_mount: disk2s1 reloading after unclean unmount, checkpoint xid 417, superblock xid 418
```

`1,483,776 × 4096 = 6.07 GB`，正是 manifest 里 OS 窗口的大小 ——
**userdata → GPT → SHNGROOT manifest → 窗口块设备 → APFS 挂载真实 OS 卷** 这条链路全线走通。

### 3. 日志暴露的两个待修问题

1. **读取超时**：`disk0 couldn't read checkpoint descriptor block 27 …: 60`（ETIMEDOUT）、
   `disk2s1 … block 2 …: 5`（EIO）。大偏移的多块读取不稳定，需要调 chunk（驱动默认 128 扇区/CMD18）
   或改用 `native-msdc-dma=1` 的 GPD/BD 路径；
2. **根卷选择**：`kernel_args` 仍带 `rd=md0`，内核继续从那个 4 MiB RAM 卷挂根，所以最后仍是
   `process 1 exec of /sbin/launchd failed, errno 2`。下一步要去掉 `rd=md0` 并设置
   `[payload.adt] root_match_volume_uuid`（OS 卷的 UUID），让内核从 `disk2s1` 挂根。

### 工程记录：本机读日志的方法（不再需要拍照/重设手机）

```sh
adb root            # userdebug + ro.debuggable=1，adbd 以 root 重启
adb shell "dd if=/dev/block/by-name/flashinfo of=/data/local/tmp/fi.bin bs=1M"
adb pull /data/local/tmp/fi.bin flashinfo.bin
```

内核控制台在 LBA 30000、固件日志在 LBA 30512。**注意**：每次 `fastboot -w` 会让 Android 需要重设，
而 `su`/root 也会随之丢失 —— 用 `adb root` 恢复，不需要 Magisk。

---

## 0.34 真实根卷挂载成功 —— 停在"根卷未认证"（2026-11-05 00:0x）

### 结果

去掉 `rd=md0`、把 `[payload.adt] root_match_volume_uuid` 设为 OS 卷的 UUID 之后：

```
rootvp not authenticated after mounting @bsd_init.c:1099
```

**这条 panic 本身就证明了成功**：内核按 UUID 选中了 eMMC 驱动发布出来的 OS 窗口，APFS 挂载了它，
`bsd_init` 只是在检查"根卷是否已认证"时停下。传输链路已经全部打通。

### OS 卷 UUID 的来源（本机可复现）

`fsapfsinfo`（`apt install libfsapfs-utils`）直接读 userdata 镜像里的 OS 窗口：

```
Identifier : 04769d26-2ca0-4825-b443-e2e5f88300f2
Name       : DawnUpdate21H461.J120J121J207J208OS
```

注意窗口里是**完整磁盘镜像**：`EFI PART` 在 0x200、APFS 容器从 0x4400 开始，
所以要先 carve 掉 17408 字节的 GPT 前缀再交给 fsapfsinfo。这个 UUID 与 UFS 文档示例里的值一致
（同一个 IPSW 家族）。

### 现在的完整链路

```
LK → BootShim → SILICIUM_UEFI → PayloadBootDxe → UniXNU 菜单
   → DarwinBoot（窗口/视频/参数、memory-map、ExitBootServices）
   → 内核：PAN(A53) → CPU/GIC → AMFI(PMGR 垫片) → md0/mockfs 或 APFS
   → NativeMsdc：MSDC0 → EXT_CSD → GPT → userdata → SHNGROOT manifest
   → 窗口块设备（storage bindings 由 dispatch table 推导）
   → APFS 挂载 OS 卷（UUID 匹配）→ **根卷已挂载** ✅
   → 停在 rootvp 认证检查
```

### 下一步：根卷认证（VerifiedRoot）

NativeMsdc README 与实测 panic 指向同一条路：

> iPadOS APFS requires the root volume to be authenticated (`rootvp not authenticated after
> mounting` otherwise). Built with `-DNATIVE_VERIFIED_BLOCK=1` and `kits/VerifiedRoot/include`,
> the windows are served only through owner-signed VerifiedRoot policies.

框架里 `VerifiedRoot` 是**组件（kit）**：`github.com/UniXNU/VerifiedRoot`，revision `974f043d…`，
我们这棵树**还没取**（`kits/` 下只有 NativePrelink / XnuPrelink / dandelion）。

要做的事：

1. `[components] include` 加 `"VerifiedRoot"`（组件会自动 fetch）；构建脚本加 `build-sheng-source-storage.py`/其归档；
2. 驱动用 `-DNATIVE_VERIFIED_BLOCK=1` + `kits/VerifiedRoot/include` 编译（README 给的命令：
   `build-arm-driver.py NativeMsdc --verified-root`）；
3. 生成 owner 签名的策略区（`kits/VerifiedRoot/tools/manifest.py`），按 README 的布局放到 userdata
   窗口之后（`A04RPOL1` 头 + 条目，4064 字节处是头部的 SHA-256）；
4. 链接需要 `verified_root_key`、`vr_open`、`vr_verify_block`、`vr_xnu_crypto`（来自 kit 的单独归档）。

### 本轮另外两处工程细节

- 驱动加了 `native_msdc_busy` 门（控制台在驱动传输期间不写 flashinfo），避免控制台与块驱动抢 MSDC0
  —— 之前 APFS 报的 ETIMEDOUT/EIO 很可能就是这个竞争；
- `board.toml` 里 `[payload.adt]` 不能声明两次（我一度写成两段，TOML 直接报错），
  `root_match_volume_uuid` 必须并进已有的 `[payload.adt]`。

---

## 0.35 VerifiedRoot 接入：策略区读通，只差条目尺寸（2026-11-05 01:0x）

### 已完成

1. **组件已取**：`[components] include` 加 `"VerifiedRoot"`（`github.com/UniXNU/VerifiedRoot`，
   rev `974f043d…`），框架自动 fetch 到 `tree/kits/VerifiedRoot`（真实路径
   `/root/work/UniXNU/kits/VerifiedRoot`，含 `include/ src/ tools/ adapters/`）。

2. **owner 密钥与签名**：`tools/manifest.py` 用 RSA-3072 私钥签整幅窗口的 4096 字节块哈希，
   产出 `VROOT01` 信封 + **395 字节**的 `trusted-key.bin`（`VR_KEY_BYTES`）。

3. **verifier 归档**：`scripts/build-verified-root.py` 编译 `src/verified_root.c` +
   `adapters/xnu_crypto.c` + owner key 符号 → `build/verified-root/libVerifiedRoot.a`。
   两个坑：① `.c` 必须用 `-x c` 放在**源文件之前**，否则按 C++ 编译后文件作用域 `const`
   （`verified_root_key`、`vr_xnu_crypto`）变成内部链接、根本不出符号；② 随之要过滤
   `-std=gnu++2b` 之类的 C++ 专用开关。

   另一个坑：defconfig 里**两个** `-force_load` 只有第一个生效，所以把 vr 的三个 `.o`
   直接并进 `libStorageBindings.a`（`derive-storage-symbols.py` 已经这么做）。

4. **策略区写入**：`scripts/write_policy_region.py`，位置必须等于驱动的
   `policyRegionOffset()` = **最远窗口末端按 1 MiB 向上对齐**
   （本次 = `0x22f400000` = 9,382,658,048，也正好等于原始镜像大小）；
   布局 `A04RPOL1` + version 1 + count + total_bytes + 条目表 + 4064 处 SHA-256。
   镜像尾部必须按 4096 对齐，否则 fastboot 稀疏写入报
   `don't care size … is not a multiple of the block size`。

5. **控制台门控**（关键修复）：`patches/xnu/device-emmc-console-gate.patch` 让 `emmc_flush()`
   在驱动传输期间直接返回（读 `native_msdc_busy`）。之前策略区 8 个扇区的读取被控制台写入
   打断，读回来的头部**没有 magic**（日志里那行乱码就是现场）。

### 本轮的实测日志

```
NativeMsdc: reading the signed root policy, 47485472 bytes at userdata +9382658048
NativeMsdc: signed root policy read in 147604 ms, 1 entries
NativeMsdc: OS not published: no policy entry has this window's size (vr_open -1)
```

即：策略区**读成功了**（1 个条目），只是条目与窗口尺寸不匹配。

### 唯一剩下的问题（已定位，未做完）

`selectPolicy()` 按**窗口字节数**精确匹配条目，而 `configureVerification()` 还会校验
`view.image_bytes == windowBytes`。我第一次 carve 时按整 MiB 取整，签名的 `image_bytes`
是 6,077,546,496，而窗口是 **6,077,580,800**（+34,304）；第二次按 4096 取整又少了 1,536 字节
（该窗口不是 4096 的整数倍：1,483,784.375 块）。

**正确做法**：按 **512 字节** carve（`6,077,580,800 / 512 = 11,870,275` 扇区），
让文件长度**正好**等于窗口大小，再签名；`manifest.py` 会按 `(size+4095)/4096` 处理最后一块。
SystemOS 是可选窗口，可后做（或同样精确 carve 后作为第二个条目）。

### 宿主侧故障（需要先修）

WSL 的 vhdx 挂载失败，已以只读回退：

```
wsl: An error occurred mounting the distribution disk, it was mounted read-only as a fallback.
```

处理顺序：`wsl --shutdown` → 重新进入；仍只读就 `e2fsck -f /dev/sdX`（用 `wsl --mount` 挂 vhdx，
或 `wsl --manage Ubuntu --move`）。空间占用主要来自本轮的大镜像：
`os-window.img` 6.07 GB、`os-apfs.img` 6.07 GB、`systemos.img` 3.30 GB、`os-exact.img` 6.08 GB、
`userdata-root-signed.img` 9.43 GB —— 除最后一项外都可随时重建，优先删它们。

### 已落在 D: 的产物（无需 WSL 即可复现刷机）

| 文件 | 说明 |
| --- | --- |
| `D:\aios\out3\boot-gate.img` | 带控制台门控 + 块设备 + verified root 的内核（`af030600…`）|
| `D:\aios\out3\userdata-signed.img` | 带 `A04RPOL1` 策略区的 9.43 GB 镜像（条目尺寸待修）|
| `D:\aios\artifacts\boot_backup.img` | Android 原厂 boot（恢复用）|

---

## 0.36 精确签名刷入完毕，结果待读（2026-11-05 02:2x）

### 这一轮做了什么

`0.35` 定位到"条目尺寸与窗口不匹配"后，按 **512 字节**精确 carve 并重新签名：

```
window: 6077580800 bytes = 11870275 sectors at offset 1048576
carved: 6077580800 bytes (want 6077580800)
signed image bytes: 6077580800 -> matches the window: True
```

信封 `manifest.bin` 47,481,664 字节，策略区 `47,485,760` 字节（头部 + 1 个条目），
写回镜像 `D:\aios\out3\userdata-signed.img` 的 `0x22f400000`，头部 SHA-256 已重算，
文件保持 4096 对齐（9,430,892,544 字节）。随后刷入 userdata + `boot-gate.img` 并启动。

**结果尚未读取**：手机停在 iOS 内核上，既不在 adb 也不在 fastboot（USB 设备都不出现），
需要物理按键恢复。日志仍在 `flashinfo@LBA 30000`，不会丢。

### 早上第一件事

```powershell
# 长按电源 10 秒 → 音量下+电源 进 fastboot，然后：
powershell -ExecutionPolicy Bypass -File D:\aios\_work\collect_after_boot.ps1
```

脚本会：刷回 Android boot → `fastboot -w` → 等 Android → `adb root` → 拉 `flashinfo`
→ 解码并打印 `NativeMsdc` 与根卷相关的行（**不用拍照、不用装 Magisk**）。

### 两种可能的结果与下一步

- **成功**：日志出现 `NativeMsdc: OS verified against its signed policy`，并且不再停在
  `rootvp not authenticated`。那就该继续看它卡在哪一步（launchd 的 exec、或下一个缺失的 kext 服务）。
- **失败**：日志会给出 `vrReason` 的具体值（`VR_KEY`/`VR_SIGNATURE`/`VR_HASH_TABLE`/`VR_BLOCK`），
  按值定位：`VR_KEY` = 内核里那个 395 字节 anchor 与签名用的公钥不一致（重新生成
  `verified_root_key.o` 后必须重连内核）；`VR_BLOCK` = 某个 4096 字节块的哈希不符
  （窗口 carve 的范围与驱动读的窗口范围不一致）；`VR_SIGNATURE` = 私钥/公钥不配对。

### 一个已知的性能问题（成功后再优化）

策略区读取花了 **147,604 ms**（47 MB）。PIO + 64 KiB 分块在这个控制器上大约 320 KB/s，
启动时读 47 MB 太慢。方向：`native-msdc-dma=0/1` 对比、加大 CMD18 块数、
或让 `native-msdc-vcache=1` 缓存已验证块（驱动已有该开关）。

### 宿主侧：C: 空间已彻底修好

C: 曾被 WSL 的虚拟磁盘（55 GB，位于 `%LOCALAPPDATA%\wsl\{guid}\ext4.vhdx`）塞满到 0 字节，
导致 WSL 只读挂载。处理：删 Docker 数据盘与各类缓存腾出 11 GB → `wsl --export`（25.9 GB tar，
5.8 分钟）→ `wsl --unregister` → `wsl --import Ubuntu D:\wsl\Ubuntu <tar>`（10.2 分钟）→ 删除 tar。

结果：**C: 66 GB 可用，D: 56 GB 可用**，WSL 现在位于 D:，以后膨胀不再吃 C:。
`diskpart`/`Optimize-VHD` 需要管理员权限，本会话无法提权，所以走了导出/导入这条路。
另外教训：**不要在 `/mnt/d` 上用 `bs=512` 读大文件**（1,187 万次小块写，20 分钟没完成）；
用 4 MiB 块 + `truncate -s` 到精确长度即可。

---

## 0.37 给 iOS 数据区独立分区（解决"每次刷 userdata + 重设 Android"）（2026-11-05 10:3x）

### 背景

`0.36` 的精确签名跑出了这条日志：

```
NativeMsdc: OS verified against its signed policy
NativeMsdc: OS: read of sectors 0+1 refused, a block failed its signed hash
```

即**签名链全部通过**（`vr_open` 校验了 anchor、签名、哈希表），但每个数据块都判哈希不符。
已在内核里加了诊断（`boot-verifydiag.img`）：失败时打印 `index`、校验长度、数据前 8 字节、
实算摘要与表中摘要，一次启动即可区分三种成因（长度不是整块 / 读到的数据不对 / 内核 SHA-256 适配器有问题）。

### 工作流问题与方案

iOS 镜像原来放在 userdata **偏移 0**（SHNGROOT manifest 的固定位置），而 Android 的 F2FS
超级块也在那里 —— 两者互斥，所以每次 iOS 测试都要 `fastboot flash userdata`（6.5 分钟），
之后 Android 必须 `fastboot -w` 重设（开发者选项、USB 调试授权全部丢失）。

**方案：给 iOS 单独一个 GPT 分区 `ios`**（按你的要求设为 **30 GiB**），userdata 相应缩小到 21.8 GiB：

| 收益 | |
| --- | --- |
| 不再刷 userdata | iOS 镜像只刷一次到 `ios`：`fastboot flash ios userdata-signed.img` |
| Android 一直可用 | `/data` 不再被覆盖，开发者选项与 USB 调试授权保留 |
| 每次测试 | `fastboot flash boot` + 重启 + `adb root` 读日志 |
| iOS 空间 | 30 GiB（当前镜像 9.4 GB，余量足够以后加 Data 卷） |

### 已完成的代码

1. **驱动**（`package/NativeMsdc.h` 的 `gptFindData`）：分区表里存在 `ios` 时优先用它，
   否则仍用 `userdata`；manifest、窗口、策略区全部相对该分区的首扇区计算。
   重复项检查按"同类"比较（`ios` 优先，userdata 被忽略）。
   `native-msdc-area` 的偏移方案已废弃（被分区方案取代）。
2. **`scripts/../make_ios_partition.py`**（`_work/make_ios_partition.py`）：从 GPT dump 里
   把 `userdata` 尾部切出 `ios`，重算 entries CRC 与 header CRC，同时生成 primary 与 backup 两份。
3. **`_work/make_ios_partition.ps1`**：一键流程 —— `adb root` → dump GPT → 改表 → 写回两份 →
   `fastboot -w`（格式化缩小后的 userdata）→ `fastboot flash ios` → 重启。

产物：`D:\aios\out3\boot-iospart.img`（sha256 `d6a5b055…`，含 `ios` 分区识别）。

### 下次手机可用时的执行顺序

1. 先跑 `collect_after_boot.ps1` 取 `0.36` 那次启动的诊断日志（区分 A/B/C 三种成因）；
2. 再跑 `make_ios_partition.ps1`（会先 dump GPT 到 `D:\aios\out3\gpt-dump.bin`，出问题可写回）；
3. 之后每次 iOS 测试只需 `fastboot flash boot boot-iospart.img` + 重启 + 读日志。

### 注意

- `fastboot flash ios` 依赖 bootloader 重新读 GPT；改表后先 `fastboot reboot bootloader` 或让
  `-w` 流程先重启一次（脚本里已这么做）。
- 分区表的唯一 GUID 用固定值（`0102…10`），文档记录，便于识别。
- 持久化 store（`native-msdc-persist`）现在落在 `ios` 分区内；等 Data 卷真正启用时再单独规划。

### GPT 编辑的离线自检（已通过）

用一块与真机同形的合成 GPT（122,142,720 扇区、userdata 13,385,728..122,021,854）跑
`make_ios_partition.py --ios-gib 30`，校验结果：

```
primary: header CRC ok True, entries at LBA 2, entries CRC 0x3458b95a
backup:  header CRC ok True, entries at LBA 122142688, entries CRC 0x3458b95a
primary entries CRC ok: True   backup entries CRC ok: True
primary and backup tables identical: True
  [1] userdata   13385728..59107294   21.8 GiB
  [2] ios        59107295..122021854  30.0 GiB
```

即两份 GPT 一致、CRC 全对、`ios` 精确占据 userdata 原尾部。真机上执行时脚本会先把
原 GPT dump 到 `D:\aios\out3\gpt-dump.bin`，出问题可以直接写回。

---

## 0.38 对照官方文档找到的两处缺失，owner-root 已按文档接通（2026-11-05 13:0x）

按用户要求重新通读 unixnu.com 后，与自身实现逐条对比，发现**两个**真实缺口。

### 缺口一：块哈希校验失败（未解决，需一次启动）

宿主侧用 kit 自己的 C provider 验证我们的信封，**完全通过**：

```
PASS: real RSA signature and all 1483785 original blocks verified by C; host only
image_bytes 6077580800, volume_uuid 04769D26-…, original_image_modified false
```

即签名、anchor、哈希表都对。所以内核侧 `a block failed its signed hash` 只能是
「内核读到的字节与 carve 不同」或「内核 SHA-256 适配器」。`boot-verifydiag.img` 会打印
`index / 校验长度 / 数据前 8 字节 / 实算摘要 / 表中摘要`，一次启动即可定案。

**方法记录**：`kits/VerifiedRoot/tests/openssl_provider.c` 可在 Linux 上编译成宿主库
（`cc -shared -fPIC -I include src/verified_root.c tests/openssl_provider.c -lcrypto`），
再用 `tools/verify_image.py` 校验信封 —— 这条路以后所有签名问题都能离线自证。

### 缺口二：`rootvp not authenticated`（已按文档解决）

对照 `docs/zh/concepts/security` 与 `docs/zh/xnu/patch-reference`：

- 框架**早已自动应用** `offline-owner-root-binding-audit.patch`（源码树里 panic 后面紧跟
  `#if defined(XNU_OWNER_ROOT_TRUST)`），但 `XNU_OWNER_ROOT_AUDIT` / `XNU_OWNER_ROOT_TRUST`
  两个开关没开，代码被 `#if` 整段剔除 —— 这就是一直 panic 的原因。
- 该路径要求一个"已验证块存储"提供 `native_verified_block_root_authorized(dev_t)`；
  框架的实现是 `VirtioMmioBlock` 组件里的 `verifiedVolumeQuery()`，它通过 weak 导入
  `native_ufs_block_authenticates()` 询问每个 provider —— **我们的驱动早就定义了这个符号**。

### 为此做的三处工程改动

1. **配置**：`CONFIG_XNU_OWNER_ROOT_AUDIT=y`、`CONFIG_XNU_OWNER_ROOT_TRUST=y`、
   `CONFIG_XNU_NATIVE_VIRTIO_BLOCK=y`、`CONFIG_XNU_NATIVE_MSDC_VERIFIED=y`。
   注意构建读的是 `out/<os>/<build>/.config`，**不是** `configs/*_defconfig` —— 改完 defconfig
   必须同步那份 `.config`，否则开关不生效（踩过）。
2. **virtio 传输**：`build-arm-driver.py NativeVirtioBlock --verified-root`（组件名必须是
   `NativeVirtioBlock`，写成 `VirtioMmioBlock` 会被拒绝），输出在
   `build/virtio-mmio-block-verified/`；`derive-storage-symbols.py` 把该归档**覆盖**到
   `build/virtio-mmio-block/libVirtioMmioBlock.a`（框架在 `NATIVE_VIRTIO_BLOCK=1` 时自动链接这个路径），
   **不能**再折叠进 `libStorageBindings.a`，否则 85 个重复符号。
3. **空策略桩**：virtio 的 verified 构建引用 `verified_root_manifest` /
   `verified_root_manifest_bytes`；它在本 SoC 上从不 probe（没有 virtio-mmio 节点），
   所以在 `build-verified-root.py` 里给 0 长度的桩，真实策略仍由 eMMC 窗口携带。

另外给框架的 `build-xnu-4k-baseline.sh` 两处门禁加了 `XNU_NATIVE_MSDC_VERIFIED` 分支
（原来只认 probe / source-storage）——**这是本地偏离，已记录**。

### 增量编译的坑（值得记）

XNU 的 make 不跟踪编译开关变化：打开 `XNU_OWNER_ROOT_*` 后 `bsd_init.o` 仍是旧的，
代码不会编进去。改开关后必须删 `build/xnu/ios-obj/**/bsd_init.o` 与 `mach.development*`。
验证方法：`llvm-nm bsd_init.o | grep -c owner_root`，本次为 9。

### 产物

`D:\aios\out3\boot-ownerroot.img`（sha256 `3f87b8e8…`）：块设备 + 精确签名 + 空桩 +
owner-root audit/trust 全部在内。

---

## 0.39 块哈希失败的真正原因：carve 偏移算错（2026-11-05 13:1x）

### 诊断日志给出了答案

```
NativeMsdc: verify 0 len 4096 data 00000000 00000000 got 9952a041 want bc908a6c
```

- `len 4096` → 长度正确（排除"校验长度不对"）；
- `data 00000000` → 窗口首块确实是零（OS 窗口开头是保护性 MBR，前 8 字节为零）；
- `got 9952a041` → **内核算出的哈希与宿主对真实块的哈希完全一致**；
- `want bc908a6c` → 信封里的表是**另一份 carve** 的哈希。

### 根因

签名脚本里写的是 `dd bs=4M skip=$((OFFSET / BLOCK))`，而 `OFFSET / BLOCK = 1048576 / 4194304 = 0`
（整数除法）—— 于是 carve 从**镜像开头**（SHNGROOT manifest）开始，而驱动服务的是 **+1 MiB** 处的窗口。
两边差 1 MiB，每块都不符；宿主侧验证之所以通过，是因为它验的也是同一份错误的 carve。

宿主上能直接看到这个错误：错误 carve 的首 8 字节是 `53484e47524f4f54` = `"SHNGROOT"`。

### 修复与固化

- 用 1 MiB 块（偏移正好一个块，不会再丢）carve：`bs=1M skip=1 count=5797` + `truncate` 到精确长度
  → 首 8 字节 `0000000000000000`，宿主验证 `PASS: all 1483785 blocks`；
- 新增 `scripts/sign-window.py`：显式做算术、**拒绝非 MiB 对齐的偏移**、carve 后立即用 kit 的
  OpenSSL provider 验证信封，不通过就退出 —— 以后不会再出现"签名与 carve 一致但都错"的情况。

### 另一个坑：boot 分区只有 64 MiB

把 `[payload] size_mib` 提到 72 后，boot 镜像 76 MB，`fastboot flash boot` 报
`FAILED (remote: 'size too large')`。内核加了 verifier + virtio helper 后文件约 53 MiB，
`size_mib = 60` 是可用的最大值（60 + FAT 开销 ≤ 64）。

### 当前待确认

镜像已按新信封重新刷入（`userdata-signed.img` sha256 `C7BD8B1C…`），下一次启动的日志里
`want` 应等于 `got`（`9952a041`）。若仍不相等，说明策略区在设备上的位置与驱动读取的位置不一致，
下一步给驱动加打印（region 起点 + 信封 magic + generation）定位。

---

## 0.40 到达用户态：launchd 执行，卡在 dyld 共享缓存的签名策略（2026-11-05 21:0x–22:3x）

### 整条链路现在通了

```
eMMC → GPT → ios 分区（30 GiB，独立于 userdata）
  → SHNGROOT manifest → 窗口块设备（OS + SystemOS 两个条目）
  → VerifiedRoot 逐块校验：OS verified / SystemOS verified
  → APFS 挂载真实 OS 卷 → owner-root 接受（不再 rootvp panic）
  → /sbin/launchd exec 成功（sheng-skip-cs=1 清掉了 AMFI 的 ip_mac_return）
  → dyld 找到共享缓存（SystemOS union / preboot 载体两条路都试过）
  → AMFI 拒绝缓存签名：'is adhoc signed' + 'unsuitable CT policy 0 for this platform/device'
```

launchd 因此以 `namespace 6 subcode 0x1`（LIBSYSTEM，`Library not loaded:
/usr/lib/libSystem.B.dylib`）退出。

### 已排除的方向（都有日志证据）

| 尝试 | 结果 |
| --- | --- |
| 只带 OS 的 Apple trust cache | AMFI 仍拒绝缓存 |
| **OS + SystemOS 两份合并**（`scripts/merge_trustcache.py`，bundle sha256 `7416ca4f…`，与 marble 记录里的 "two-module original trust bundle" 完全一致）| 两个模块都加载（`loaded external trust cache module: 0 / 1`），AMFI 仍拒绝 |
| `OWNER_CACHE_CARRIER`（`/private/preboot` 载体）代替 union | 同样拒绝，说明不是挂载路径的问题 |
| 从 carve 里直接取 dyld cache 算 cdhash | cache 不在 OS 卷（扫描 `dyld_v1` 无命中），在 SystemOS cryptex 的 APFS 里；`fsapfsmount` 这个包没编进 FUSE，挂载不了 |

### 当前的判断

`OWNER_ROOT_TRUST=1` 会让 AMFI 按 owner 根的策略检查缓存签名（缓存是 adhoc 的 IPSW 版本），
而参考设备 `marble-milacy` 的实现要点是**未验证根**：记录明确写 *"launchd started with the
SystemOS union (libSystem resolved)"*，即那种配置下 AMFI 接受缓存。

### 未验证根实验为何没做完

按 marble 的思路改成 `OWNER_ROOT_AUDIT/TRUST=n` + 自己的 `bsd_init` 补丁（`root-unverified=1`
放行）时，补丁无法稳定应用：同一份源码在两次构建中 `bsd_init.c` 一次带框架的
`offline-owner-root-binding-audit` 片段、一次不带，从一次捕获生成的补丁在下一次必然失败。
为不把时间耗在补丁机制上，配置已回退到"已验证根"（它已经能到 AMFI 缓存检查这一步）。

### 下一步（明天从这里开始）

1. 让补丁稳定：把 `root-unverified` 的 hunk 改成**对两种树都成立**的形式（例如只改
   `panic(...)` 这一行，`@@ -N,1 +N,1 @@` 无上下文），或用框架的 `text-fixup` 机制在
   payload 里做字节补丁；
2. 若未验证根确实让 AMFI 接受缓存，再逐步恢复 `OWNER_ROOT_TRUST=1` 找出真正影响 AMFI 的开关；
3. 兜底方案：给 AMFI 的 CT-policy 比较打补丁（`"unsuitable CT policy"` 字符串已在
   `kernel.macho` 的 0x1228ad7 处定位到，可用 `AppleMobileFileIntegrity` 的 `__TEXT_EXEC`
   范围 0xfffffff004aaa000+132584 反查引用）。

---

## 0.41 AMFI 缓存策略：四条路都试过，卡在这里（2026-11-05 22:0x–23:1x）

### 已完成的事实（都有日志）

```
NativeMsdc: OS verified against its signed policy
NativeMsdc: SystemOS verified against its signed policy
NativeMsdc: published SystemOS / published OS
apfs: mount-complete volume DawnUpdate21H461…（真实 OS 卷）
load_init_program: attempting to load /sbin/launchd     ← exec 通过
dyld cache 找到：/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64
AMFI: 该缓存 adhoc 签名 → 'unsuitable CT policy 0 for this platform/device'
dyld: code signature registration for shared cache failed
launchd 退出 namespace 6 subcode 0x1（LIBSYSTEM，libSystem 未加载）
```

### 试过并排除的

| 尝试 | 证据 |
| --- | --- |
| 只带 OS 的 Apple trust cache | AMFI 仍拒绝缓存 |
| OS + SystemOS 两份合并（bundle sha256 `7416ca4f…`，与 marble 记录的 two-module original bundle 一致）| `loaded external trust cache module: 0 / 1` 两个都加载，仍拒绝 |
| `OWNER_CACHE_CARRIER`（`/private/preboot`）代替 union | 同样拒绝 → 与挂载路径无关 |
| 把 **dyld cache 自己的 cdhash** 加进 trust cache（`fcdb9268e40dc7599ae3f26c4116d5ab0329ead2`，从 SystemOS carve 的 `cache+0x74000` SuperBlob 里的 CodeDirectory 解出，identifier `com.apple.dyld.cache.arm64.universal`）| 三个模块都加载，仍拒绝 → **CT policy 检查先于 trust cache 查找** |

### 逆向尝试（未果）

- `"unsuitable CT policy"` 字符串在 `kernel.macho` 文件偏移 `0x1228ad7`（VA `0xfffffff0042e0ad7`，在 AMFI `__TEXT` 范围 `0xfffffff0042db6c0+45395` 内）；
- 扫描全部可执行段的 `adrp+add/sub`（含 shift 0/12）与 `ldr literal` 形式：**0 处引用**；
- 扫描全镜像里指向该 VA 的 8 字节指针：**0 个槽位**。

结论：这条日志不是通过常规引用生成的（或 AMFI 的判定代码不在这份预链接像里），所以"给 AMFI 打字节补丁"目前没有落点。**不建议**继续盲目反汇编。

### 明天的方向（按可行性排序）

1. **未验证根**（`OWNER_ROOT_AUDIT/TRUST=n`）：marble 记录明确写这种配置下 *"launchd started with the SystemOS union (libSystem resolved)"*。
   阻塞点是 `bsd_init` 补丁的应用不稳定：同一份源码在两次构建中一次带框架的
   `offline-owner-root-binding-audit` 片段、一次不带。解决办法：
   - 用**无上下文的单行补丁**（`@@ -N,1 +N,1 @@`，只替换 `panic(...)` 那一行）；
   - 或者用框架的 **payload `text_fixups`**（`/chosen/text-fixups`，官方支持在签名后改字节）。
2. **查 AMFI 判定来源**：把 `AppleMobileFileIntegrity` kext 单独反汇编（它的 `__TEXT_EXEC`
   范围 `0xfffffff004aaa000 + 132584`），找 `ct_policy` 比较与返回路径；也可以对比
   `AppleImage4`/`AMFI` 在 kernelcache 原始镜像里的位置，确认预链接是否正确搬运了该段。
3. **社区确认**：框架的 Telegram/GitHub 讨论里应有"verified root + dyld shared cache"的结论，
   这是最省时间的路。

## 0.42 两条误导线索的记录（2026-11-05 23:0x）

1. **`amfi-only-platform-code` 不是我们的 ADT 属性**。我在 payload 里搜到这个字符串
   （0x1718d96），一度以为框架把 AMFI 钉在平台策略；实际上下文是
   `...amfi-only-platform-code\0"AMFI: no amfi-only-plat...` —— 它是 **AMFI 自己的日志文本**，
   随 `kernel.macho` 进入 payload。DTS 与 board.toml 都没有这个属性。
2. **无上下文单行补丁同样不稳定**。`dandelion-root-unverified.patch` 用
   `@@ -946,1 +946,1 @@` 只替换 `panic(...)` 一行，仍然报
   `patch does not apply` —— 说明两次构建的 `bsd_init.c` 里那一行**本身**就不同（不只是上下文）。
   因此"未验证根"这条路线在补丁机制上暂时不可行，除非换成 payload `text_fixups`
   或修改框架生成源码树的方式。

当前配置已恢复为能走到 AMFI 判定的那一版（`OWNER_ROOT_AUDIT/TRUST=y`），
series 只保留已验证可用的六个补丁。

## 0.43 AMFI 拒绝点已精确定位（2026-11-05 23:4x）

### 方法上的关键突破

之前扫描"没有引用"是**方法错误**：我用的是 substring
（`"unsuitable CT policy"`）的地址，而编译器物化的是**整个字面量的开头**：

```
b"AMFI: '%s': unsuitable CT policy %#llx for this platform/device, rejecting signature.\n"
b"AMFI: '%s' is adhoc signed.\n"
```

从 substring 往回找到前一个 NUL 得到字面量起点，再用 `adrp + add/sub` 扫描，两处引用立刻命中：

| 字面量 | VA | 引用它的代码 |
| --- | --- | --- |
| `AMFI: '%s': unsuitable CT policy %#llx ...` | `0xfffffff0042e0acb` | **`0xfffffff004ab634c`** |
| `AMFI: '%s' is adhoc signed.` | `0xfffffff0042e0ceb` | **`0xfffffff004ab6090`** |

两处都在 `AppleMobileFileIntegrity.__TEXT_EXEC`（`0xfffffff004aaa000 + 0x205e8`）内，
说明**判定代码确实在预链接镜像里**，只是字符串引用是"字面量起点 + adrp/add"，不是指针表。

### 扫描器的可靠性已验证

用同一扫描器找我们自己补丁的字符串
`"DANDELION-EXEC: load_machfile"`（VA `0xfffffff0070c6033`）→ 命中 1 处
（`0xfffffff00773d8ec`），证明算法正确，之前的 0 命中确实是 substring 偏移造成的。

### 下一步（有明确落点）

1. 反汇编 `0xfffffff004ab6000–0xfffffff004ab6500`：这一区段包含两条消息的设置处；
   找 `ct_policy` 的比较和通往 `0xfffffff004ab634c`（CT 消息）的条件分支，
   把该条件分支改成"直接走接受路径"即可（或改成读一个启动参数）；
2. 补丁方式：内核是我们自己构建的，预链接后对 `kernel.macho` 做字节改写即可
   （Apple kext 在预链接里没有运行时签名校验）；
3. 验证：改完启动，看是否还打印那两行 AMFI 消息、launchd 是否不再
   `no dyld cache`。

## 0.44 陈旧日志的坑：控制台门控必须去掉（2026-11-06 00:0x）

### 症状

连续几轮读到的日志都是同一份：里面写着
`attempting to load 2 external trust cache modules` 和 `data partition ... (24576 MiB)`，
而当时的镜像已经是 **3 个 trust cache 模块**、`ios` 分区也早已改过 —— 说明**新日志根本没写进去**。

### 原因

`device-emmc-console-gate.patch` 让控制台在驱动传输期间直接返回。当初加它是怀疑控制台和
MSDC0 抢控制器，但那个现象的真因是 **carve 偏移 bug**（§0.39），门控从来不是必需的。
开启 verified root 之后驱动几乎一直在传输，控制台被永久饿死。

### 处置

- 从 series 移除 `device-emmc-console-gate.patch`（保留文件以备查）；
- `collect_nowipe.ps1` 现在**每次读完日志就清零 flashinfo 块**，这样下一轮启动从空块开始：
  读到空日志本身就是"控制台没写"的信号，不会再被旧内容误导。

### 顺带确认的事实

- 带 CT 补丁的那一轮，日志停在 APFS 初始化（`install_apfs_vfs_fs ... telemetry ENABLED`），
  而没有补丁的版本能走到 launchd —— 说明**单指令补丁太激进**，把 AMFI 的正常流程也跳过了。
  `dandelion-skip-cs` 那种"清一个返回值"的做法安全，直接改控制流不安全。
- 设备上的 `ios` 分区实际是 **24 GiB**（第一次 GPT 写入的值；后来的 30 GiB 只改了脚本默认值，
  没有重新写 GPT）。镜像 9.4 GiB 装得下，暂不影响，后续要扩容再重写 GPT。

### 当前基线镜像

`D:\aios\out3\boot-baseline.img`（sha256 `9f49f9d6…`）：无门控、无 CT 补丁、其它配置不变
（verified root + 双窗口策略 + 三模块 trust cache）。这是下一步实验的起点。

---

## 0.45 控制台为什么静默：控制台自己的写路径失效，改用驱动写入（2026-11-06 02:3x）

### 已经排除的原因

| 怀疑 | 结论 |
| --- | --- |
| 分区/地址错 | ❌ 控制台 `log-lba = 0x747b50f = 122,139,919` = `flashinfo + 30,000`，与我读的位置一致；同一分区里**固件块（+30,512）有 3,639 字节**，说明分区可写、地址没错 |
| ADT 里丢了 `emmc-log` 节点 | ❌ payload 的 ADT 里 `emmc-log` / `log-lba` / `log-sectors` 都在 |
| 驱动改了 CFG 没恢复 | ❌ 驱动 `HostSession` 构造时存 CFG/INTEN、析构时原值写回，分频和总线宽度都恢复 |
| 门控把它饿死 | ❌ 去掉门控后依然静默 |
| 从驱动里调用 `emmc_flush()` | ❌ 与定时刷写并发，直接把块写成空 |

### 结论

控制台是"只写、依赖固件留下的卡状态、每次写前 `rearm`"的设计。它的 `rearm` 只在时钟稳定位
为 0 时才重写 CFG，而驱动**会**在传输期间切 PIO/DMA 并迅速恢复，卡本身进入过读状态 ——
之后控制台的 CMD24 序列就失败，重试三次即 `emmc_failed`，整轮启动从此静默。
寄存器是恢复的，**失效的是控制台自己的命令序列**。

### 修法：把失败的块交给驱动写

- 控制台补丁里新增弱符号 `native_msdc_console_sink(uint32_t lba, const void *data)`；
- `emmc_flush` 在自己三次尝试都失败后，把该块交给这个 sink；成功则照常写 header 并返回，
  不再置 `emmc_failed`；
- 驱动实现 `native_msdc_console_sink()`：`HostSession` + `transfer(lba, 1, data, true)`，
  也就是整个移植一直在用的单块写路径。

这样控制台在驱动启动前用自己的 PIO 写（已验证可用），驱动启动后自动切到驱动的传输路径，
不再有并发（控制台的刷写发生在它自己的上下文，不与驱动传输重入），也不会永久放弃。

### 产物

`boot-ctflags.img`（sha256 `f11f45e4…`）：控制台带回退写者 + AMFI 数据级补丁（`mov w8, #0`
在 `0xfffffff004ab6334`），已刷入设备。

### 早上怎么验

```powershell
powershell -ExecutionPolicy Bypass -File D:\aios\_work\morning-check.ps1
```

手机先按电源 10 秒 → 音量下+电源进 fastboot。脚本会：刷回 Android boot（不擦除）→ 等 adb →
`adb root` → 从**设备绝对地址 122,139,919** 读 512 个扇区 → 判定 `LOG IS FRESH / EMPTY` →
打印最后 40 行和关键标记（trust cache 模块数、`unsuitable CT policy`、`dyld cache`、
`Library not loaded`、`launchd`、`private/var`）。

---

## 0.46 日志可用、根卷认证通过；新卡点是共享区映射与读超时（2026-11-06 08:0x–09:0x）

### 今早修复的两件事

1. **固件 `Compromised Data`**：`merge_trustcache.py` 造的第三个模块头写成了 19 字节
   （`DANDELIONCACHE01` 16 + 多余的 3 个零字节），整个 bundle 错位 3 字节，DarwinBoot
   直接拒绝启动 —— 这是昨晚所有"日志空白"的真正原因（内核从未运行）。
   修复后加了 `scripts/verify_trustcache.py`：刷机前校验 count/offsets/每个模块的
   version/uuid/条目数/精确长度。
2. **日志窗口太小**：控制台只预留 512 扇区（256 KB），日志正好在 `load_init_program`
   之前填满。把窗口移到 flashinfo 分区开头（`0x7473FDF` = 122,109,919）并扩到
   28,672 扇区（14 MB），避开固件自己的 DDNLOG01 块（+30,512）。

### 现在能读到的完整一轮

```
attempting to load 3 external trust cache modules   ← 三模块 bundle 全部加载
loaded external trust cache module: 0 / 1 / 2
NativeMsdc: OS verified / SystemOS verified / published SystemOS / volume ready
BSD root: disk3s1
apfs: mount-complete volume DawnUpdate21H461.J120J121J207J208OS
is_root_hash_authentication_required: Root Volume, root hash authentication is required
VM Swap Subsystem is ON
disk3: I/O timeout.
nx_buf_bread:628: disk3 buf_biowait() failed, error = 60, b_error = 60
_vnode_dev_read:907: disk3 *** got err 60 reading blknum 90747
Failed to open swap file 60
vm_swap_create_file failed @ 261 secs
load_init_program: attempting to load /usr/appleinternal/sbin/launchd.development
load_init_program: attempting to load /sbin/launchd
DANDELION-EXEC: clearing policy answer 0 (pid 1)
shared_region: [1(launchd)] check_np(...) vm_shared_region_start_address() failed
```

### 结论

- **AMFI 的 CT policy 拒绝已消失**（数据级补丁 `0xfffffff004ab6334: cset → mov w8,#0` 生效）；
- **根卷认证通过**（owner-root + `sheng-skip-cs`）；
- `launchd` 已经 exec 到共享区建立阶段。

### 新卡点（都可修）

1. **读超时**：`error = 60`（ETIMEDOUT）读 blknum 90747；策略区 73 MB 读取实测耗时 227 秒
   （≈320 KB/s）。驱动的 3 秒超时在 APFS 高并发下偏紧 —— 先加大超时并考虑提高分块；
2. **共享区映射**：`vm_shared_region_start_address() failed` ×2 → dyld 无法建立共享缓存映射，
   launchd 随后 `EXEC_EXIT_REASON_BAD_MACHO` 退出。AMFI 已接受缓存，问题在 `shared_region`
   建立阶段（映射地址/大小，或缓存未被注册为该区域）。

### 产物

`boot-biglog.img`（sha256 `89882976…`）：合法三模块 bundle + CT 数据级补丁 +
14 MB 控制台日志窗口。日志从 flashinfo 分区起始 LBA（122,109,919）读取。

---

## 0.47 下午绕弯的原因：lret=4 是 LOAD_FAILURE，不是 LOAD_BADMACHO（2026-11-06 20:0x）

### 事实

`dandelion-exec-diag.patch` 的 panic 给出：

```
DANDELION-EXEC: load_machfile lret=4 errno=85 cputype=16777228
                flags=0 filetype=2 ncmds=22 @kern_exec.c:1351
```

`cputype=0x100000C`（ARM64 ✓）、`filetype=2`（MH_EXECUTE ✓）、`ncmds=22` ✓ 都正常。
但 `bsd/kern/mach_loader.h` 的枚举是：

```c
#define LOAD_SUCCESS    0
#define LOAD_BADARCH    1
#define LOAD_BADMACHO   2
#define LOAD_SHLIB      3
#define LOAD_FAILURE    4   /* lret=4 是这个 */
#define LOAD_NOSPACE    5
#define LOAD_PROTECT    6
#define LOAD_RESOURCE   7
#define LOAD_ENOENT     8
#define LOAD_IOERROR    9
#define LOAD_DECRYPTFAIL 10
```

**lret=4 = LOAD_FAILURE（Miscellaneous error）**，不是 BADMACHO ✗。之前把
`mach_loader.c` 里 14 处、44 处 `LOAD_BADMACHO` 返回改成 panic，一次都没触发 —— 因为它们
根本不在失败路径上。这也解释了为什么屏幕一直是同一条 `DANDELION-EXEC`（我们自己的
panic 在 `load_machfile` 失败时打印，而插桩点在更里面、且是另一种返回码 ✗）。

### 处置

`dandelion-4k-diag.patch` 改为对 `mach_loader.c` 中**全部 86 处非成功返回**插桩
（LOAD_FAILURE / NOSPACE / PROTECT / RESOURCE / ENOENT / IOERROR / DECRYPTFAIL /
BADARCH / BADMACHO / SHLIB），每处 panic 带行号与返回码：`D4K line NNN: LOAD_XXX`。
下一次启动的屏幕会直接指出失败点（提交 `0c4d216`，镜像 `137f9ecd…`）。

### 教训

**诊断前先核对常量的定义**，不要凭名字推断数值。一下午的插桩都建立在
"4 = BADMACHO" 这个未经验证的假设上。

### 仍未解决的环境问题（不影响上面这条）

内核 eMMC 控制台在 verified-root 启动后停止写入，日志稳定停在
`VM Swap Subsystem is ON`（约 172 行）✗。已排除：地址/分区错（固件块可写 ✓）、
ADT 节点丢失（节点在 ✓）、驱动没恢复 CFG（HostSession 会恢复 ✓）、门控饿死、
定时刷写并发、跨模块调用（pexpert 与 kext 符号不互通，链接失败 ✗）、
失败后永久静默（已改成不放弃 ✓）。目前改用 **panic 屏**作为可靠诊断通道 ✓。

---

## 0.48 exec 通过；根因是读超时，已修（2026-11-06 23:1x）

### 精确定位

`D4K namei: path=/usr/lib/dyld raw_errno=60 (ETIMEDOUT) @mach_loader.c:3794`

- 60 = ETIMEDOUT（已核对 `bsd/sys/errno.h`：ENOENT=2, EACCES=13, ENOTDIR=20,
  **EROFS=30**, **ETIMEDOUT=60**, ELOOP=62）；
- `get_macho_vnode()` 的 `namei()` 失败发生在解析 dyld 路径时 —— 不是 Mach-O 解析、
  不是路径不存在、不是权限，而是**读磁盘超时**；
- 与日志里 `disk3: I/O timeout` / `buf_biowait() failed, error = 60` /
  `got err 60 reading blknum 90747` / `vm_swap_create_file failed` 完全同源。

### 为什么下午绕了弯

- `lret=4` 是 `LOAD_FAILURE`（4），不是 `LOAD_BADMACHO`（2）—— 常量表见
  `bsd/kern/mach_loader.h`；
- `load_machfile`/`parse_machfile` 多用 `ret = LOAD_FAILURE; ... return ret;`（变量式返回），
  前几轮只插桩 `return LOAD_XXX;` 字面量形式，全部漏掉；
- 最终对 86 处字面量 + 15 处变量式返回插桩，命中 `D4K line 3798`。

### 修复

`hostReadPio` 原先必须同时看到 `kIntXferDone` 中断位且收齐数据才成功；该位被状态读取
消费后，即使缓冲已完整也会轮询到超时。现在：

1. `offset == total && !(SDC_STS & BUSY) && FIFO 空` 也算完成 ✓；
2. 命令超时 3 秒 → **10 秒**（64 KiB @ 320 KB/s ≈ 0.2 秒）✓；
3. **不重试**（半刷新缓冲会破坏 verified 块哈希，早上已证伪）✓。

提交 `36b6062`，镜像 `227f9ab0…`。

### 结果

- 屏幕上 `DANDELION-EXEC` / `D4K namei` 都消失 ✓ —— dyld 路径解析成功、exec 通过；
- 日志已能写到 `load_init_program` / `DANDELION-EXEC: clearing policy answer 0` /
  `shared_region ... check_np(...)` ✓；
- `Failed to open swap file 30` = **EROFS**（只读文件系统），不再是超时 ✓ —— 因为
  `native-msdc-persist=1` 语义是"证明只读"，`=2` 才开放写入；且 Data 卷尚未建立。
  这是预期内、非致命的。

### 下一步

1. **Data 卷 / `/private/var`**：把 `native-msdc-persist` 从 1 改为 2，并按
   `docs/zh/guide/userdata.md` 与 `skip-setup.md` 的 seed 流程建立可写 Data 卷；
   需要 `[payload.adt] root_match_volume_uuid` 指向 Data 卷（当前指向 OS 卷）。
2. **策略区读取 227 秒**：73 MB @ 320 KB/s，是启动慢/"黄条只走一点"的原因；
   优化分块或启用 DMA（`native-msdc-dma=1`，需先通过 PIO 比对自检）。
3. 控制台在驱动启动后仍会停止写入，日志稳定停在 `VM Swap Subsystem is ON` 附近；
   诊断改用 panic 屏。

### 固件日志（同分区 +30,512）证明链路完好

```
DARWINBOOT: inputs validated; exiting boot services
DARWINBOOT: hand-off to the kernel
kernel va/pa, entry, args, video, command line 均已打印并校验
Exiting boot services. A green bar ... marks the loader handoff before the kernel branch.
```

固件阶段没有失败；"黄条只走一点"多半是内核在读 73 MB 策略区（约 4 分钟）期间屏幕
几乎不动。

---

## 0.49 下一步的硬依赖：Data seed 需要 macOS（2026-11-06 23:3x）

### 今晚最后的状态

- 修好读超时后，`DANDELION-EXEC` / `D4K namei` 消失，日志走到
  `load_init_program` / `DANDELION-EXEC: clearing policy answer 0` / `shared_region ...` ✓；
- `Failed to open swap file 30` = **EROFS**（已核对 `errno.h`：`EROFS=30`、`ETIMEDOUT=60`），
  不再是超时；根因是**没有可写的 Data 卷**；
- 一次"黄条只走一点"的观察是**假象**：读回设备 boot 分区发现当时刷的是 Android 原生
  boot（`kernel_size=13,907,597`、含 `ANDROID!`、无 `UniXNU`），不是我们的镜像
  （`kernel_size=771,005`）。已在确认 fastboot 在线后重新刷入修复镜像 ✓。
  **教训：刷机前必须确认 `fastboot devices` 非空，不能只看 `Writing ... OKAY`。**

### Data 卷的官方流程（环境依赖，本机做不了）

`docs/zh/guide/userdata.md` §4 与 `skip-setup.md`：

```sh
./unixnu build payload --data-ramdisk data.dbr [--skip-setup]
```

- 可写 Data 卷来自 **payload 里的 Data ramdisk seed**（`Payloads/scratch-data.img`）；
- `scripts/seed-data-grow.py`、`scripts/seed-data-skip-setup.py` 以及
  `UniXNU-fork/scripts/make-data-seed.py` 都调用 macOS 的 **`hdiutil` / `diskutil`**
  创建和挂载 APFS（`hdiutil create -fs APFS`）；seed 还要从 IPSW 的 OS 挂载拷
  `/private/var` 原始目录；
- 本机是 Windows + WSL，没有 `hdiutil`/`diskutil`，也没有可用的 APFS 写工具
  （`fsapfsmount` 无 FUSE）。因此**真正的 Data seed 必须在 macOS 上生成**，或在别的
  已具备 APFS 工具的 Linux 环境生成后把 `.dbr` 拷过来。

### 澄清：`native-msdc-persist=2` 不是 Data 卷

`NativeMsdc.cpp`：`persist=1` 证明只读，`=2` 才把 `allowWrites` 传给窗口；但
`configure()` 里 `writable = allowWrites && !(entry.flags & kWindowReadOnly)`，我们
manifest 里 OS/SystemOS 两个窗口都是只读窗口。所以改 `persist=2` 只会开放**驱动自己的
persistent store**（userdata 末尾的保留区），**不会**凭空产生 `/private/var`。
`/private/var` 必须由 Data ramdisk seed 提供。

### 可选的后续路线（按代价排序）

1. **在 macOS 上生成 Data seed**：按 `userdata.md` §4 用 `hdiutil`/`diskutil` 建 APFS
   Data 卷，`seed-data-grow.py --gib N` 调整到目标大小，`--skip-setup` 标记 Setup 完成，
   再把 `.dbr` 拷到本机，`build payload --data-ramdisk <seed>`；
2. **无 Data 卷继续调试用户态**：把 swap 关掉（`vm_swap` 相关 boot arg / 忽略 EROFS），
   看 launchd 是否能继续到第一个用户态服务 —— 但 `/private/var` 迟早还是必需的；
3. **优化启动速度**：策略区 73 MB @ 320 KB/s ≈ 227 秒，是"长时间没日志/屏幕不动"的
   主要原因；可试 `native-msdc-dma=1`（驱动有 PIO 比对自检，通过才启用）。

### 仍未解决

- 内核 eMMC 控制台在驱动启动后停止写入（稳定停在 `VM Swap Subsystem is ON` 附近），
  panics 屏是可靠通道；
- 显示/输入/GPU/Wi-Fi/电池等板级服务均未开始。

### 今晚（整天）的最终进度

固件 `Compromised Data` ✅ → 内核日志 ✅ → AMFI CT policy ✅ → 根卷认证 ✅ →
`launchd` exec 通过 ✅ → **下一关：可写 Data 卷 / `/private/var`**（需要 macOS 生成 seed）。

---

## 0.50 收尾状态（2026-11-07 00:0x）

### 最后一轮的日志（跑满 6 分钟以上、完整读取）

```
attempting to load 3 external trust cache modules / loaded 0,1,2
OS verified / SystemOS verified / published SystemOS / volume ready / published OS
BSD root: disk3s1
mount-complete volume DawnUpdate21H461.J120J121J207J208OS
is_root_hash_authentication_required: Root Volume, root hash authentication is required
VM Swap Subsystem is ON
Failed to open swap file 30                       ← EROFS（只读文件系统）
vm_swap_create_file failed @ 267 secs
mount-complete volume DawnUpdate21H461.J120J121J207J208SystemCryptex
load_init_program: attempting to load /sbin/launchd
DANDELION-EXEC: clearing policy answer 0 (pid 1)
shared_region: [1(launchd)] check_np(...) vm_shared_region_start_address() failed   ← 共享区为空，正常
```

`D4K namei` / `DANDELION-EXEC: load_machfile ...` panic **不再出现** ✓ ——
读超时修复（§0.48）生效，dyld 路径解析成功、launchd exec 通过。

### 屏幕照片（IMG_5892.JPG）

顶部三条色带按 `guide/debugging.md` §颜色条：绿色（DarwinBoot 交接）、
蓝色（内核入口已执行）、品红（到达打开 MMU 前）—— 链路完好 ✓。

### 今天刷入的最后一个镜像

`boot-biglog.img`（sha256 `107f076f…`）：
- 两个诊断 panic 已从 series 移除（`0f0b888`），避免以后误报；
- 保留全部修复：trust cache 模块头、CT 补丁、14 MB 日志窗口、
  读完成判定 + 10 秒命令超时。
- 已确认 fastboot 在线后刷入并启动 ✓。

### 未解决 / 下一关

**可写 Data 卷**：`Failed to open swap file 30` = EROFS，根因是没有 `/private/var`。
Data seed 必须用 macOS 的 `hdiutil`/`diskutil` 建 APFS（`userdata.md` §4、
`seed-data-grow.py`、`seed-data-skip-setup.py`、`make-data-seed.py` 全部依赖），
本机 Windows+WSL 无此工具，无 Linux 替代路径（§0.49）。

**控制台**：驱动启动后日志稳定停在 `VM Swap Subsystem is ON` 附近，
panics 屏与固件日志（同分区 +30,512）是可靠通道。

**性能**：策略区 73 MB @ 320 KB/s ≈ 227 秒，`vm_swap_create_file failed @ 267 secs`
里的 267 秒正是它 + 启动的耗时；`native-msdc-dma=1` 可试（需通过 PIO 比对自检）。

### 明早的第一步

1. 确认这轮干净镜像的启动结果与上面一致（无诊断 panic）；
2. 准备 macOS 环境生成 Data seed（这是继续往下走的唯一硬依赖）；
3. Data 卷之后的顺序：swap/`/private/var` → 第一个用户态服务 → 显示 → 输入/GPU。

### 全部证据链已记录

§0.42–§0.50：两条误导线索、AMFI 判定定位、控制台静默根因、
trust cache 模块头 bug、`lret` 常量表、变量式返回插桩、
`namei` errno 60、读完成修复、EROFS 30、Data seed 的 macOS 依赖、
刷机前必须确认 fastboot 在线（曾因设备不在 USB 而刷成 Android 原生 boot 的假象）。
