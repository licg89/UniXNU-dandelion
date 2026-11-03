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
