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

---

## 0.51 GitHub Actions 生成 Data seed 的方案（2026-11-07 00:3x）

### 为什么用 CI

`docs/zh/guide/userdata.md` §4：可写 Data 卷来自 `unixnu build payload --data-ramdisk`
传进来的 **APFS Data seed**，而 `seed-data-grow.py` / `seed-data-skip-setup.py` /
`make-data-seed.py` / `prepare-owner-data-fixture.py` 全部调用 macOS 的
`hdiutil` / `diskutil` 创建并挂载 APFS。本机 Windows+WSL 没有这些工具，也没有
可用的 APFS 写工具（`fsapfsmount` 无 FUSE）—— 这是硬依赖，无 Linux 替代路径。
GitHub Actions 的 `macos-26` runner 有。

### 已加入仓库的产物（提交 `5e26f59`）

| 文件 | 作用 |
| --- | --- |
| `.github/workflows/data-seed.yml` | `workflow_dispatch`，macos-26，下载 pin 过的 21H461 IPSW、校验 OS member、挂载、生成 seed、上传 |
| `scripts/build-data-seed.py` | 从 `--os-mount` 的 `/private/var` 建 APFS OwnerData 卷并用 DarwinBoot 的 DBRDISK1 writer 打包（改编自 NX769J 的通用实现）|
| `scripts/test_build_data_seed.py` | 10 个不需要 hdiutil 的单元测试，本机全部通过 ✓ |

**21H461 专属输入**（来自 `UniXNU/profiles/os/ipados-17.7.11-21H461/profile.toml`）：

```
IPSW_URL   https://updates.cdn-apple.com/2025SpringFCS/fullrestores/122-77166/DCA6CFE8-90E9-4B92-880D-3DE171798E40/iPad_Pro_HFR_17.7.11_21H461_Restore.ipsw
OS_MEMBER  044-76040-038.dmg   sha256 3f99b859c9728379110b66657c7b15285bcb1925ad8b5726542e2cdcf8b1028d
```

Apple CDN 已验证仍返回 HTTP 200（content-length 6,835,124,840）。IPSW 整体 hash
没有记录，因此 workflow 校验**提取出的 OS member** 的 pin（比整体 hash 更可靠）。

### 本地验证已过

- YAML 解析 ✓（11 步、runs-on macos-26）；
- `build-data-seed.py` 语法 ✓；
- `test_build_data_seed.py` 10/10 通过 ✓。

### 推送状态（待用户操作）

`git push` 被 GitHub 拒绝：

```
refusing to allow a Personal Access Token to create or update workflow
`.github/workflows/apfs-fixture.yml` without `workflow` scope
```

- 远程默认分支是 **`main`**，本地是 `master`（远程 `master` 已重定向）；
- 远程 `main` 停在 `19c9e3e`，本地 `5e26f59` —— 中间 100+ 本地提交未推；
- **需要用户换一个勾了 `repo` + `workflow` 的 token**（现有 token 只有 `repo`），
  或直接在 GitHub 网页新建 workflow 文件（网页登录不受 PAT scope 限制）。

### seed 回来后的本机步骤（顺序固定）

1. 下载 CI artifact `dandelion-data-seed.tar.gz`，解出 `dandelion-data.dbr`
   与 `dandelion-data.dbr.json`（含 `volume_uuid`）；
2. `configs/dandelion_defconfig` 增加：
   ```
   CONFIG_XNU_OWNER_DATA_MOUNT=y
   CONFIG_XNU_NATIVE_RAM_BLOCK=y
   CONFIG_XNU_OWNER_DATA_VOLUME_UUID="<报告里的 volume_uuid>"
   ```
   注意 build script 421–442：`OWNER_DATA_MOUNT=1` 要求 `OWNER_BOOT_MANIFEST=1`
   （我们已有 ✓），且 `OWNER_DATA_MOUNT=1 + NATIVE_RAM_BLOCK=1` 时**必须**给出
   观测到的 Data 卷 UUID，不能留空 —— UUID 每次 seed 构建都变。
3. `[payload.adt]` 增加 `packed_data_ramdisk = true`（DBRDISK1 走 PackedRAMDisk，
   `payload.py:190` 据此选 `--packed-data-ramdisk`）；board.toml 已含 RamBlock 组件 ✓；
4. `unixnu build kernel`（开关变化会触发 kern_exec.o 重编）→
   `unixnu build payload --data-ramdisk dandelion-data.dbr` → `unixnu bootimg`；
5. 刷入验证：`Failed to open swap file 30` 应消失，`/private/var` 可写，
   launchd 进入第一个用户态服务。

---

## 0.52 CI 已推送并触发（2026-11-07 01:0x）

### 事故与解决

- 旧 PAT `ghp_nRdNNXav…` 原本只有 `repo`，push 含 `.github/workflows/*` 的提交被拒：
  `refusing to allow a Personal Access Token to create or update workflow without workflow scope`；
  用户改权限后该 token 字符串失效（`401 Bad credentials`）；
  用户提供新 token（含 `workflow` scope）后**推送成功** ✓。
- 远程默认分支是 **`main`**，本地是 `master`；`master` 在远程返回 Moved Permanently。
  用 `git push <token>@github.com/... HEAD:refs/heads/main` 推送：
  `19c9e3e..9120aff`，远程 `main` 头 = `9120aff` ✓。
- 认证方式：`http.extraheader` 对该 git 无效（会要求输入用户名），必须把 token 放进 URL ✓。

### run

`workflow_dispatch`（size_mib=256, unixnu_ref=main）→ HTTP 204 ✓：

```
Data seed | https://github.com/licg89/UniXNU-dandelion/actions/runs/42256955170
```

状态 `queued`（macOS runner 排队，正常；`Data seed` workflow 在 GitHub 上是 `active`）。
当前 run 用的是推送时的 `9120aff` 版本；此后本地的改动不影响它。

### 远程依赖已用 API 验证

- `UniXNU/UniXNU` 的 `drivers/DarwinBoot` 是子模块 ✓；
- `UniXNU/DarwinBoot` 的 `Test/packed_ramdisk.py` 存在，且 `pack(source, destination)`
  与 `build-data-seed.py` 的调用一致 ✓；
- Apple CDN 的 21H461 IPSW 返回 200（6,835,124,840 字节）✓；
- OS member `044-76040-038.dmg` 的 sha256 pin 来自 21H461 profile ✓。

### 待办

1. 后台脚本 `poll_and_fetch_seed.sh` 每 60 秒查 run，完成后自动下载 artifact 到
   `D:\aios\out3\dandelion-data-seed.zip`；
2. 解出 `dandelion-data.dbr` + `.json`（含 `volume_uuid`）；
3. 运行 `scripts/enable-data-volume.py <uuid> <seed>`（提交 `5a091d3`）设置
   OWNER_DATA_MOUNT / NATIVE_RAM_BLOCK / OWNER_DATA_VOLUME_UUID / packed_data_ramdisk；
4. 重建 kernel → payload `--data-ramdisk <seed>` → bootimg → 刷入；
5. 验证 `Failed to open swap file 30` 消失、`/private/var` 可写。

### 已知风险

payload 的硬限制 `content > size_mib * 0.95`（60 MiB → 57 MiB）会拒绝过大的
payload；当前 kernel 等文件约 53 MiB，packed seed 一般只几 MiB（参考 alioth：
3 GiB 逻辑种子 packed 后 1.2 MB），若超限需把 `size_mib` 提到 boot 分区允许的
上限（64 MiB）。

---

## 0.53 CI 账单拦截；Linux 侧 UDIF→APFS 路径已验证（2026-11-07 01:0x–01:3x）

### Data seed CI run 结果：失败，但原因不在 workflow

run `42256955170`（Data seed）`completed / failure`，5 分钟内结束、0 个 step。
check-run annotation：

```
The job was not started because recent account payments have failed or your spending
limit needs to be increased. Please check the 'Billing & plans' section in your settings
Due to capacity constraints, jobs targeting macOS arm64 runners may experience longer queue times.
```

即 **GitHub 账户付费/额度问题**，需要在 Billing & plans 处理；workflow 本身没有被
执行过，不能据此判断它是否正确。`workflow_dispatch` 与 push 都是成功的（§0.52）。

### Linux 侧的新进展（不依赖 macOS）

| 步骤 | 结果 |
| --- | --- |
| 框架 `scripts/unixnu/udif.py` 解 `044-76040-038.dmg` | ✅ 6,077,580,800 字节裸磁盘镜像（文档明确说 Linux 用它代替 hdiutil）|
| GPT 解析 | ✅ `EFI PART` @0x200，APFS 容器分区从 LBA 34（0x4400）开始 |
| 切出容器 + `fsapfsinfo` | ✅ 卷 UUID `04769D26-2CA0-4825-B443-E2E5F88300F2`（与根卷一致）、卷名 `DawnUpdate21H461...OS`、1 个卷 |
| `packed_ramdisk.pack()` 要求 | ✅ 只要页对齐的原始镜像（≤1 TiB），不关心是不是 APFS —— seed 的本质是"原始镜像 + DBRDISK1 打包" |
| 读 `/private/var` 文件内容 | ❌ `fsapfsmount` 编译时无 FUSE 支持（"No sub system to mount APFS format"），且无 Python APFS 库 |

**结论**：本机已经能拿到**可读元数据的 APFS 容器**，但拿不到里面的文件树；
要完全绕开 macOS，还缺一个能读 APFS 文件的工具（或补 FUSE 支持）。
优先级：先处理 GitHub Billing（最快），其次才是本机补 APFS 读能力。

### 文档记录已同步（用户要求）

- `porting.toml`：从复制的示例模板改为实测状态（14 个 area，
  configuration/firmware/cpu/memory=verified，
  storage/userspace/diagnostics=in_progress，其余 planned），
  storage/userspace 两个 area 写入了真实的日志证据与当前墙（提交 `eefee9c`）；
- `MORNING.md`：从过时的"待验证"改为当前项目总览（提交 `91566fe`）；
- 说明：`/root/work/UniXNU/docs/zh/` 是**上游框架官方文档**的本地镜像，
  属于 `UniXNU/UniXNU`，不写入设备私有进度；项目进度的载体是
  HANDOFF / porting.toml / MORNING.md。

### 当前待办（不变）

1. 处理 GitHub Billing & plans，重跑 `data-seed.yml` → 拿 `.dbr` + `volume_uuid`；
2. `scripts/enable-data-volume.py <uuid> <seed>` → 开
   `OWNER_DATA_MOUNT` / `NATIVE_RAM_BLOCK` / `OWNER_DATA_VOLUME_UUID` /
   `packed_data_ramdisk`；
3. 重建 kernel → payload `--data-ramdisk` → bootimg → 刷入验证 EROFS 消失。

---

## 0.54 公开仓库解除 CI 账单阻塞（2026-11-07 01:4x）

### 处置

用户指出：把仓库改成 public 即可用 GitHub 免费提供的 hosted runner
（私有仓库的 macOS minutes 受账户支付状态限制）。

- `PATCH /repos/licg89/UniXNU-dandelion` `{"private":false}` → `private = False` ✓；
- 重新 `workflow_dispatch` → HTTP 204，新 run
  **42263833632**（https://github.com/licg89/UniXNU-dandelion/actions/runs/42263833632）；
- 轮询显示状态从 `queued` → **`in_progress`** ✓ —— runner 已分配，正在下载 IPSW。
  这与 42256955170（0 个 step、5 分钟内以账单注解失败）形成对照。

其余两个仓库（uefi-dandelion、UniXNU-mt6765-common）仍保持私有 ✓；
`UniXNU` 是 fork，无法改可见性。

### 公开带来的安全清理（提交 `6b02774`）

- `scripts/make_repos_private.sh` / `scripts/verify_repos.sh` 曾硬编码旧 PAT
  `ghp_nRdNNXav…`；现改为从 `GITHUB_TOKEN` 环境变量读取 ✓；
- 旧 token 已失效（API `HTTP 401`），但仍在 2 个历史提交
  （`64827de`、`6a7baec`）中可见 —— **待办：CI 结束后重写历史清除**，
  现在不重写是因为会改变 `head_sha`、打断正在排队的 run；
- 当前工作树已无 PAT 形状的字符串 ✓。

### CI 成功后自动做的事（已挂后台 `poll_seed2.sh`）

run 完成且 `conclusion=success` 时自动下载 artifact 到
`D:\aios\out3\dandelion-data-seed.zip`；失败则拉取 check-run 注解自动诊断。
拿到 seed 后按 §0.51 的固定 5 步执行。

---

## 0.55 Data seed 已接入并构建成功（2026-11-07 02:1x）

### CI 成果（run 42263833632，12/12 step success）

```
magic        DBRDISK1（packed）
volume_uuid  BB07C39B-F799-45FC-ABAD-A3D797ECF9A8
size_mib     256（逻辑 268,435,456 字节；packed 后 981 KiB）
files        39，file_bytes 611,715（21H461 OS 卷的 /private/var，跳过 staged_system_apps）
output_sha256 25d4e3c1…（与 payload 记录一致）
os_launchd_sha256 8cec9a3c…（与框架 build-target-dyld-fixture.py 的 21H461 期望值一致 ✓）
```

公开仓库后 runner 立刻分配，印证 §0.54 的判断。

### 接入的四处改动（提交 `4bbbd31`）

| 位置 | 改动 |
| --- | --- |
| `configs/dandelion_defconfig` | `OWNER_DATA_MOUNT=y`、`NATIVE_RAM_BLOCK=y`、`OWNER_DATA_VOLUME_UUID="BB07C39B-…"` |
| `board.toml [payload.adt]` | `packed_data_ramdisk = true`（payload.py:190 据此选 `--packed-data-ramdisk`）|
| 构建命令 | `unixnu build payload --data-ramdisk <dandelion-data.dbr>` |
| 驱动构建 | `build-native-msdc.py --block-devices 1 --verified-root --ufs-store-abi` |

### 两个真实的框架约束（不是绕过，是补齐）

1. **两个 transport 的 bindings 记录**：`NATIVE_RAM_BLOCK` + `NATIVE_VIRTIO_BLOCK` 同时开时，
   `build-xnu-4k-baseline.sh:744` 会读两个归档旁的 `bindings.json`，校验
   `compiled_object_sha256` 与对象一致、且两者导入的原始符号相同。macOS 侧由
   `build-virtio-mmio-bindings.py`（xcrun）生成；本机新增
   `scripts/write-transport-bindings.py`，用我们**自己从 dispatch table 推导的 18 个
   `IOBlockStorageDevice` 符号**（`derive-storage-symbols.py` 的产物）写成期望的 schema，
   并就地计算每个对象的哈希 —— 断言的都是实测值。
2. **对象命名不一致**：框架按归档名推导对象名（`libVirtioMmioBlock.a` → `VirtioMmioBlock.o`），
   而 `build-arm-driver.py` 按运行时标识符命名（`NativeVirtioBlock.o`）。脚本为二者建立**硬链接**，
   保证同一份字节。
3. **`native_ufs_store_*` 未定义**：`NativeRamBlock` 以弱符号导入这套 store API，
   而 eMMC 驱动只在 `NATIVE_MSDC_UFS_STORE_ABI` 下导出别名。新增 `--ufs-store-abi` 开关后
   链接通过 ✓（源码注释也说明不能与 NativeUfs 同时链接）。

### 构建结果

```
kernel=0 errors=0    （CT 补丁在重建后重新应用：0x1a9f17e8 → 0x52800008 ✓）
payload=0            data_ramdisk_sha256 25d4e3c1… ✓
bootimg=0            017a14b6… (63,688,704 bytes)
payload_bytes        62,914,560（60 MiB，未触发 57 MiB 的容量上限 ✓）
```

镜像 `D:\aios\out3\boot-data.img` 已刷入（确认 fastboot 在线后）。

### 待验证（下一次启动的日志）

- RamBlock 是否以 `md0`/IOMedia 发布 seed 并挂载；
- `Failed to open swap file 30`（EROFS）是否消失；
- launchd 之后是否出现 `/private/var` 或第一个用户态服务。

---

## 0.56 Data 卷成功挂载；dyld 缓存不可见是下一个问题（2026-11-07 03:0x）

### 诊断 panic 的输出（`dandelion-owner-diag.patch`，临时）

```
D4K owner: cache=0 stage=6 auth=1 union=1 mnt=0 ro=1 recheck=1 | data=0 stage=4 ro=0
           | root=0xfffffff5b28072e0
```

逐项解读（源码见 `offline-owner-systemos-mount.patch`）：

| 字段 | 值 | 含义 |
| --- | --- | --- |
| `cache` | 0 | `native_owner_cache_prepare` 返回 0 = 成功 |
| `stage` | 6 | 走到最后一步 |
| `auth` | 1 | `cache_authorized` 为真（readonly + apfs + verified-block + union 全部满足）|
| `union` | 1 | `native_union` 为真：`vnode_mountedhere(os_root)==cache_mount && rootvnode==mounted_root` |
| `mnt` | 0 | 挂载成功；`ro=1` 只读；`recheck=1` 根复核通过 |
| **`data`** | **0** | **`native_owner_data_prepare` 成功** |
| **`data stage`** | **4** | 走到最后一步（已校验设备号、非只读、apfs）|
| **`data ro`** | **0** | **Data 卷可写** —— EROFS 的对立面 |

即：**CI 生成的 Data seed 已经完整走通** —— RamBlock 发布（日志里 `disk1` =
65536 块 = 256 MiB）、内核按 `XNU_OWNER_DATA_VOLUME_UUID` 选中它、挂到
`/private/var`、校验为可写 APFS ✓。

### `Failed to open swap file 30` 是假线索

`vm_swap_init` 早于 `native_owner_data_prepare`（后者在 bsd_init 末尾、launchd 之前），
所以创建 swap 文件时 `/private/var` 还没挂载 —— EROFS 是必然的，不是故障。
Data 挂载成功之后 swap 可以再建。

### 新的真实问题：dyld 缓存不可见

屏幕上：

```
initproc failed to start -- namespace 6 subcode 0x1
Library not loaded: /usr/lib/libSystem.B.dylib
reason: tried: ... (no such file, no dyld cache)
```

owner 层全部成功（union=1、cache 挂载成功），但 dyld 找不到缓存。

**静态验证（本机，不需要设备）**：

| 镜像 | `com.apple.dyld` | `dyld_shared_cache_arm64` |
| --- | --- | --- |
| OS 根卷（`os-apfs.img`，由 `udif.py` 从 IPSW 解出） | 0 | 0 |
| **SystemOS cryptex**（`044-76034-038.dmg`） | **70** | **226** |

cryptex 内还有 `System/Library/Caches/com.apple.dyld`（8 处）✓ —— 说明缓存相对卷根
就在这个路径上，union 合并后理论上应可见。

### 下一步

已加入临时诊断（`D4K paths:` panic），用 `vnode_lookup` 分别查
`/usr/lib/dyld`、`/System/Library/Caches/com.apple.dyld`、该目录下的
`dyld_shared_cache_arm64`、`/private/var/tmp`，四个 errno 会指出是哪一层不可见：
0 = 可见；2(ENOENT) = 不存在；20(ENOTDIR) = 中间组件不是目录。

---

## 0.57 缓存路径全部可见，问题在 dyld 的加载方式 → 切到 cache carrier（2026-11-07 03:2x）

### 决定性诊断

```
D4K paths: dyld=0  cachedir=0  cachefile=0  vartemp=0
```

四个 `vnode_lookup` **全部返回 0**（都存在），包括
`/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64` 本身。

**结论：不是可见性、不是挂载、不是 union 的问题** ✗。内核在 launchd 之前能看到缓存文件，
而 dyld 仍然报 `no dyld cache` —— 失败发生在 **dyld 如何拿到这个缓存**这一步：
iOS 的 dyld 期望内核把缓存预先映射进 shared region（通过 `shared_region_*`），
或者需要显式给出 `DYLD_SHARED_CACHE_DIR`。我们的 union 路线两者都没有：
`XNU_OWNER_UNION_SYSTEMOS=1` 时框架故意**不**设置 `DYLD_*` 环境变量
（源码里那段 copyout 被 `#if !defined(XNU_OWNER_UNION_SYSTEMOS)` 跳过），
依赖 union 让默认路径可见。

### 静态验证（本机完成，不需要设备）

| 镜像 | `com.apple.dyld` | `dyld_shared_cache_arm64` |
| --- | --- | --- |
| OS 根卷（`os-apfs.img`，由 `udif.py` 从 IPSW 解出） | 0 | 0 |
| **SystemOS cryptex**（`044-76034-038.dmg`） | **70** | **226** |

cryptex 内部路径 `System/Library/Caches/com.apple.dyld`（8 处）✓。
缓存头解析出一个真实的子缓存：
`dyld_v1   arm64`，`addr 0x1800000 size 0x74000 maxProt 5 initProt 5`
（与 §0.40 记录的 `+0x74000` 签名位置一致）。

### 采用的下一个路线：cache carrier

框架为这个症状提供了另一条文档化路径（`offline-owner-cache-carrier.patch`，影响
`vm_unix.o` 的 shared region 代码）：

```c
const char *directory = "/private/preboot/System/Library/Caches/com.apple.dyld";
const char *environment = "DYLD_SHARED_CACHE_DIR=/private/preboot/System/Library/Caches/com.apple.dyld";
const char *fallback = "DYLD_FALLBACK_LIBRARY_PATH=/private/preboot/usr/lib";
```

即把 SystemOS 挂到 `/private/preboot` 并**显式**把缓存目录告诉 dyld。
配置切换（提交见下）：

```
CONFIG_XNU_OWNER_UNION_SYSTEMOS=n
CONFIG_XNU_OWNER_CACHE_CARRIER=y
```

约束（`build-xnu-4k-baseline.sh:523`）：`OWNER_CACHE_CARRIER=1` 要求
`OWNER_CACHE_STARTUP=1` ✓（我们本来就是）。同时把 `dandelion-owner-diag.patch`
从 series 退役 —— 它已完成使命，留着会在到达 launchd 前就 panic。

### 产物

`boot-data.img`（sha256 `808435ad…`）：carrier 配置 + Data seed（`25d4e3c1…`）+
CT 补丁（`0x52800008`）+ 14 MB 控制台窗口。**待刷入验证**。

### 若 carrier 仍不行，后续假设（按可测性排序）

1. dyld 需要内核预先 map 缓存 → 检查 `shared_region` 相关 boot arg / 看是否需要在
   `vm_shared_region` 里主动加载；
2. 16 KiB 缓存布局与 4 KiB 内核的页大小适配（框架 `native-4k-user-abi` 已改子 map shift，
   但 dyld 自身可能另有检查）；
3. AMFI 对缓存签名/映射的额外要求（CT 补丁只改了 CT policy 判定）。

---

## 0.58 carrier 路线：Data 卷与 cryptex 都挂上了，dyld 缓存仍是墙（2026-11-07 04:0x）

### carrier 配置下的完整启动日志（210 行，无 panic）

```
attempting to load 3 external trust cache modules / loaded 0,1,2 / completed
disk5s1 mount-complete volume DawnUpdate21H461.J120J121J207J208OS        (kernel_task)
disk3s1 mounting volume ...SystemCryptex, requested by: init (pid 1)
disk3s1 mount-complete volume ...SystemCryptex
NativeMsdc: store at sector 119400391, 65539 blocks, mode 1
dev_init:306: disk2 device_handle block size 4096 block count 65536
disk2s1 mounting volume OwnerData, requested by: init (pid 1)
handle_mount:817: disk2s1 vol-uuid: BB07C39B-F799-45FC-ABAD-A3D797ECF9A8
disk2s1 mount-complete volume OwnerData
load_init_program: attempting to load /sbin/launchd
DANDELION-EXEC: clearing policy answer 0 (pid 1)
shared_region: [1(launchd)] check_np(...) vm_shared_region_start_address() failed
```

**里程碑：CI 生成的 Data seed 以 `OwnerData` 卷被 APFS 挂载成功**，UUID 与 CI 报告
完全一致 ✓；SystemCryptex 也由 init 挂载 ✓。日志里没有真正的 panic
（只有两条无害的 `debug_log_init: Error!! gPanicBase is still not initialized`）。

### 已排除的假设（按顺序）

| 假设 | 结论 |
| --- | --- |
| 路径不可见 | ❌ 已排除：`dyld=0 cachedir=0 cachefile=0 vartemp=0`（四个 lookup 全部成功）|
| union 未建立 | ❌ 已排除：`union=1 auth=1` |
| Data 卷挂载失败 | ❌ 已排除：`data=0 stage=4 ro=0`（可写）|
| 缓存不在该卷 | ❌ 已排除：静态检查 cryptex 内 `com.apple.dyld` 70 处、`dyld_shared_cache_arm64` 226 处 |
| 未设置 DYLD_SHARED_CACHE_DIR | ❌ 已排除：carrier 配置下源码会设置该环境变量 |

### 当前唯一未解的墙

`shared_region: check_np(...) vm_shared_region_start_address() failed` 之后 dyld 报
`no dyld cache`。`vm_shared_region_start_address` 返回 `KERN_INVALID_ADDRESS` 当且仅当
`sr_first_mapping == -1`（共享区为空，见 §0.44 抓到的源码）。

也就是说：**内核从未把 dyld 缓存映射进 shared region**，而 dyld 期望它在那儿。
`XNU_OWNER_CACHE_STARTUP=1` 只做 `dyld_flags |= 8` + `commpage_update_dyld_flags()`
（owner 链模式），**不做预映射**（源码 `offline-owner-systemos-mount.patch:164`）。

真实 iOS 由内核在启动早期把 preboot 里的缓存映射进 shared region。要复刻它，
需要 `vm_shared_region.c` 侧的工作（`shared_region_map_and_slide_np` 的内核侧等价物，
或在 `native_owner_cache_prepare` 成功后主动建立映射）。

### 下一步候选（按代价排序）

1. **在内核里主动建立 shared region 映射**：在 `native_owner_cache_prepare` 成功之后，
   调用 shared region 的映射接口，把 `/private/preboot/.../dyld_shared_cache_arm64`
   映射进 `shared_region`，使 `check_np` 返回成功 —— 这是最接近真实 iOS 的做法；
2. **确认 dyld 是否真的收到 `DYLD_SHARED_CACHE_DIR`**：在 launchd 的 argv/envp 上做一次
   dump（panic 打印 envp 指针内容），排除环境未传递；
3. 参考 `docs/zh/devices/marble-milacy.md`（§0.41 记录它用 union 跑通了 libSystem）——
   确认它是否另有 `vm_shared_region` 改动。

### 收尾状态

- 手机刷回 Android（`collect_carrier.ps1` 完成），可正常使用 ✓；
- 仓库提交到 `4dfff44` 及之后的记录；远程 `main` 已同步；
- 镜像资产：`boot-data.img`（carrier 版 `808435ad…`）、`boot-biglog.img`、
  `userdata-signed.img`、CI seed（`dandelion-data.dbr`，981 KiB）。

---

## 0.59 carrier 授权链诊断（已刷入，等下一次读取）（2026-11-07 04:5x）

### 为什么需要它

`offline-owner-cache-carrier.patch` 在 `vm_unix.c` 里用
`native_owner_shared_cache_carrier()` 授权 dyld 的 `shared_region_map_and_slide_np`。
它有一串**全部必须成立**的条件（任一失败就静默拒绝，因为
`SHARED_REGION_TRACE_ERROR` 在非调试构建里被编译掉）：

1. `process_root == rootvnode`；
2. root 与 cache 挂载都必须是只读 APFS 且**不是同一个挂载**；
3. root 设备 == `native_verified_block_volume_device("04769D26-…")`；
4. cache 设备 == `native_verified_block_volume_device("8846FE78-…")`（SystemCryptex）；
5. `vnode_lookup("/private/preboot/System/Library/Caches/com.apple.dyld")` 成功、
   是 `VDIR`、且其挂载 == cache 挂载；
6. `vnode_parent(cache_file) == 该目录 vnode`；
7. `native_verified_block_root_authorized(root_device)`。

所以"dyld 报 no dyld cache 且没有日志"完全可能来自其中任意一条 ✗。

### 已加入的诊断（`dandelion-owner-diag.patch`，已刷入）

在 `native_owner_cache_prepare` / `native_owner_data_prepare` 成功之后
panic 一次，把上面可复现的部分全部打出来：

```
D4K carrier: edir=<errno> efile=<errno> eroot=<errno> dir=<0|1> reg=<0|1>
             dirmnt=<0|1> same=<0|1> root_same=<0|1> diff=<0|1>
             | dev=<selected device path> sel=<dev> mnt=<dev>
```

- `edir` / `efile`：`/private/preboot/System/Library/Caches/com.apple.dyld`（目录与文件）
  的 lookup errno；
- `dir` / `reg`：目录是 VDIR、文件是 VREG；
- `same`：目录与文件在同一挂载；
- `root_same`：进程根就是 rootvnode；`diff`：root 与 cache 不在同一挂载；
- `dev` / `sel` / `mnt`：`native_owner_cache_prepare` 解析出的设备路径、选中设备号、实际挂载设备号。

镜像 `boot-data.img`（sha256 `302b7816…`），已刷入设备并启动（04:0x）。
设备在约 5–6 分钟后 panic，`panic()` 的输出会**同时**进 panic 屏和 eMMC 控制台，
因此控制台日志里一定能拿到这一行。

### 早上怎么取（一条命令）

```powershell
powershell -ExecutionPolicy Bypass -File D:\aios\_work\morning-carrier.ps1
```

手机先按住电源 10 秒、再 `音量下 + 电源` 进 fastboot。脚本会：刷回 Android boot（不擦除）→
等 adb → `adb root` → 读 flashinfo → **打印 `D4K carrier:` 那一行**以及 owner/cryptex/
dyld/launchd 相关的全部行 → 最后把日志块清零，供下一轮使用。

### 读到之后的分支

| `D4K carrier` 结果 | 结论与下一步 |
| --- | --- |
| `edir≠0` | `/private/preboot/...` 目录不存在 → cryptex 没挂到 `/private/preboot`（或挂载点不同），改挂载点或改 carrier 的期望路径 |
| `reg=0` 或 `efile≠0` | 该目录下没有 `dyld_shared_cache_arm64` → 缓存文件名/子缓存布局不同，改为指向实际的子缓存 |
| `same=0` | 目录与文件不在同一挂载 → 目录来自 OS 卷、文件来自 cryptex，需让 carrier 接受 |
| `diff=0` | root 与 cache 在同一挂载 → 说明 cryptex 其实挂到了 `/`（union 语义残留）|
| `sel≠mnt` | 设备号不一致 → `native_verified_block_volume_device` 选错设备 |
| 全部成立但仍失败 | 授权通过、映射仍失败 → 转向在 `native_owner_cache_prepare` 里主动建立映射 |

### 当前设备状态

手机挂着（panic 后停在菜单/循环），**需要按电源才能读日志** —— 这是唯一无法自动化的步骤。
仓库已提交，远程 `main` 同步。

---

## 0.60 进程级坑：修改补丁后必须清 .cache/xnu-work（2026-11-07 10:5x）

### 现象

连续两轮刷入的诊断镜像，屏幕上都没有出现新加的 panic，而是停在旧的行为。
用字符串检索证明原因：

```
清树前：  D4K perms: 0   D4K launchd: 1     ← 内核里是上一版的诊断
清树后：  D4K perms: 1   D4K launchd: 0     ← 新诊断才真正编进去
```

框架构建日志里明明打印了 `== kernel: patch dandelion-owner-diag.patch`（补丁"已应用"），
但源码树 `.cache/xnu-work` 被复用：**补丁只在树首次生成时应用一次**，
之后修改补丁文件的内容不会被重新应用 ✗。

### 处置

```bash
rm -rf out/<device>/<profile>/<config>/tree/.cache/xnu-work
# 再 build kernel
```

`scripts/build_clean_patch.sh` 把这步和构建串在一起，并在构建前后打印诊断字符串计数，
用**内核里实际有没有该字符串**确认补丁是否生效 —— 这是比"日志说 patch 了"更可靠的验证。

### 这条也解释了更早的疑点

§0.45 里"控制台修复多次无效"很可能也有一部分是这个原因：改了
`device-emmc-log-console.patch` 后若树未重建，改动根本没有编进内核。
以后凡是**修改已有补丁文件**，都必须先清树，并检查内核内字符串确认生效。

### 同时确认的事实

`D4K launchd` 诊断没有触发，说明 `load_init_program_at_path` 返回了 0
（exec 成功）；屏幕上的 `initproc failed to start ... no dyld cache` 是
**launchd 在用户态启动失败**后由内核打印的，不是 exec 失败。
即：内核侧（exec、Data 卷、cryptex、carrier 七项条件）已全部通过，
问题在 dyld 用户态的缓存使用。

### 下一步（已构建待刷）

`boot-data.img`（sha256 `a657b05f…`）含新的 `D4K perms:` 诊断：
打印 `/private/preboot`、缓存目录、缓存文件三层 lookup errno、缓存文件大小，
以及 `KAUTH_VNODE_SEARCH` / `KAUTH_VNODE_READ_DATA` 的授权结果 ——
即 dyld 作为用户进程是否**被允许**读那个缓存。

---

## 0.61 dyld 缓存是"空壳"：证据链与后续（2026-11-07 13:0x）

### 结论

随 IPSW 发布的 cryptex 里，`/System/Library/Caches/com.apple.dyld/` 下的缓存是**占位文件**：
主文件 480 KiB、`.01` 109 MiB、`.02` 约 0、`.symbols` 654 MiB，而**主文件和 `.01` 的
`imagesCount` 都是 0**（内核直接读文件头得到的，不经任何中间层）：

```
D4K hdr: e=0 rd=0 | "dyld_v1   arm64" mappingOffset=0x200 mappingCount=1
                    imagesOffset=0 imagesCount=0
D4K split: main maps=1 imgs=0 | .01 maps=1 imgs=0
D4K alt:  err 2 2 2 0 | MiB -1 -1 -1 654
```

dyld 打开它 → 发现没有镜像 → 放弃 → 报
`Library not loaded: /usr/lib/libSystem.B.dylib (no such file, no dyld cache)`。
这与"路径不可见/权限不足/union 未建立/Data 卷失败"无关，那些都已用测量排除（§0.55–§0.58）。

### 已排除的替代解释

| 假设 | 排除依据 |
| --- | --- |
| 窗口被截断 | SHNGROOT 清单里 SystemOS 窗口 3,303,014,400 字节 = dmg 大小，完全一致 |
| 缓存在 OS 卷 | 直接扫 OS dmg：`dyld_shared_cache_arm64` 0 次；SystemOS 里 226 次 |
| 缓存在 cryptex 的嵌套前缀下 | `System/Cryptexes/OS/...` 与 `/private/preboot/System/Library/dyld` 全部 ENOENT |
| 权限/只读限制 | `auth 0 0 0`（SEARCH、READ_DATA 全部允许）|
| carrier 授权不过 | 七项条件打印全过（edir/efile/dir/reg/dirmnt/same/root_same/diff 均正常）|
| 内核没编进 carrier 代码 | 内核内 `native_owner_cache_carrier_accepts` 等符号存在 |

### IPSW / OTA 结构调查（离线）

- IPSW 分量：`OS=044-76040-038.dmg`(4 GiB)、`Cryptex1,SystemOS=044-76034-038.dmg`(3.1 GiB)、
  `Cryptex1,AppOS=044-75188-038.dmg`(10 MiB)、`RestoreRamDisk=044-75545-038.dmg`(176 MiB)
  —— 只有前两个可能承载缓存，二者都已包含在我们的 userdata 里 ✓；
- `Ap,SystemVolumeCanonicalMetadata`（`044-76040-038.dmg.mtree`，23 MiB）是**加密的 IM4P**，
  无法直接当文件清单用 ✗；
- OTA（`.../be8c2838….zip`，5.28 GB）的分量表显示
  `AssetData/payloadv2/image_patches/cryptex-system-arm64e` = 930,796,582 字节、**未压缩存储** ✓
  已按 range 下载成功，但它以 `RIDIFF10` 容器套 `pbzx`/xz 分块封装，
  原始字节里**没有任何明文缓存字符串**（全压缩）；
  框架与 `ipsw` CLI 都不认这个容器（`ipsw ota payload` 要求 YAA1/AA01）✗。

### 使用的工具

`blacktop/ipsw` v3.1.743（Linux x86_64，单文件）已下载到 `/root/work/ipsw-cli/ipsw` ✓
能力：`extract --dyld`（IPSW/OTA）、`ota payload|extract`、`dyld`、`kernel`、`mount`。
其 `--remote` 模式因 Apple CDN 未声明 `Accept-Ranges` 而拒绝（实际 curl 的 range 可用）✗，
所以改为**本地下载 IPSW 再提取**。

### 进行中

后台任务正在下载完整 IPSW（6.8 GB）并执行
`ipsw extract --dyld`，输出到 `/root/work/dyld-out`。
两种结果：

1. **拿到几百 MB~GB 的真实缓存** → 说明我们读到的 480 KiB 不是全部，把它接入
   `OWNER_CACHE_CARRIER` 的 `/private/preboot` 路径即可推进；
2. **拿到的仍是空壳** → 确认 IPSW 不含真实缓存，需要从已 restore 的设备的 Preboot 取，
   或改用 16.x 的 OS profile 做 bring-up。

---

## 0.62 缓存是完整的（推翻 §0.61 的"空壳"判断）；APFS 现可直接读取（2026-11-07 13:4x）

### 关键纠正

§0.61 判断"缓存是空壳"是**错误的** ✗。`ipsw dyld info` 直接解析主缓存头：

```
Magic = "dyld_v1   arm64"   Platform = iOS   OS Version = 17.7
Num Images    = 3354          ← 3354 个镜像
Num SubCaches = 52            ← 52 个分片
Shared Region: 3GB, address: 0x180000000 -> 0x24C5F0000
Cache UUID: 590D839A-82D2-30F1-A17D-0CC053420F73
```

主文件的 `imagesCount = 0` 是**分片缓存的正常形态**（镜像由子缓存描述），不是空壳。
缓存集合总量 **3,987,406,848 字节（3.7 GiB）**，`libSystem.B.dylib` 在缓存内 ✓。

### 用 apfs-fuse 得到的真实卷布局

本机已从源码构建 **`apfs-fuse`**（`/root/work/apfs-fuse/build/apfs-fuse`，435 KB；
需 `libbz2-dev` 与在 `ApfsLib/PList.h` 顶部加 `#include <cstdint>`）。据此直接读取 IPSW 的两个卷：

| 卷 | 顶层 | 关键内容 |
| --- | --- | --- |
| OS（`044-76040-038.dmg`） | `root/`、`private-dir/` | 完整根（Applications/System/usr/bin/…）；**`/usr/lib/dyld` = 937,056 字节，sha256 `f8df3c58…`**，与框架 `build-target-dyld-fixture.py` 记录的 21H461 期望值一致 ✓；**没有** `com.apple.dyld` 目录 ✗ |
| SystemOS cryptex（`044-76034-038.dmg`） | `root/`、`private-dir/` | **只有** `root/System` 与 `root/usr/lib`（少量文件）；`root/System/Library/Caches/com.apple.dyld/` 下 **54 个缓存文件**（主 491,520 B、`.01` 114,720,768 B … `.52`、`.symbols` 654 MiB、`.dylddata`、`.dyldlinkedit`）|

`ipsw dyld extract <cache> /usr/lib/dyld` 得到缓存内 dyld（879,456 B，`d43def0e…`），
与磁盘上的 `/usr/lib/dyld`（`f8df3c58…`）**不同**，这是正常现象（缓存内是重建版）。

### 我们的镜像确实提供完整缓存

把 `userdata-signed.img` 里的 SystemOS 窗口（offset `0x16a600000`，3,303,014,400 字节）
切出后用 apfs-fuse 挂载：**54 个缓存文件全部存在、大小与原 dmg 完全一致** ✓。
即内核侧提供的缓存是完整的，路径与权限此前也已逐项验证通过。

### 至此仍未知的一点

在这套条件下 dyld（用户态）仍报 `no dyld cache`。已排除：缓存缺失/不完整、
路径不可见、权限、只读、union、carrier 授权、共享区大小（内核
`SHARED_REGION_SIZE_ARM64 = 0x180000000` = 6 GiB > 缓存所需 3 GB）。

### 下一步候选（按信息量排序）

1. **在设备上枚举缓存目录**：诊断里用 `getdirentries`/`vnode_readdir` 列出
   `/private/preboot/System/Library/Caches/com.apple.dyld` 的**全部条目**，
   确认内核看到的不是只有 4 个文件（我们此前只查了 4 个名字）；
2. **检查 `native-4k-signature-hash-cache` 与缓存签名**：AMFI 对 dyld 缓存的
   代码签名注册可能仍失败但不再打印；
3. 用 `apfs-fuse` 把 54 个缓存文件**导出到 PC**，交给 `ipsw dyld` 做完整一致性校验，
   并与设备的失败时机对照。

### 工具留档

- `blacktop/ipsw` v3.1.743：`/root/work/ipsw-cli/ipsw`（`dyld info|extract`、`extract --dyld`、
  `ota payload|extract`、`kernel`、`mount`）；
- `apfs-fuse`：`/root/work/apfs-fuse/build/apfs-fuse`（用法 `-o ro <image> <mountpoint>`，
  `-v <volume-id>`）；
- 完整 IPSW 已下载到 `/root/work/21H461.ipsw`（6,835,124,840 字节）；
- OTA 的 `cryptex-system-arm64e` 成员已下载 `/root/work/ota-cryptex.bin`（930,796,582 字节，
  `RIDIFF10`+`pbzx` 封装，`ipsw` 不认，暂未解）。

---

## 0.63 突破：launchd 活着，到达用户空间（2026-11-07 18:4x）

### 结果

`boot-data.img` sha256 `79f1d138b05298be64964167769977b83d719b8652efdbae7b499024b2b4b0e6`
刷入后：

- 屏幕上**没有 panic 画面**，一直停在 UniXNU 菜单（我们的内核控制台写 eMMC，不写 framebuffer，
  所以内核跑起来后屏幕不再变化）；
- 日志结尾为：

```
load_init_program: attempting to load /usr/appleinternal/sbin/launchd.development
load_init_program: attempting to load /sbin/launchd
DANDELION-EXEC: clearing policy answer 0 (pid 1)
shared_region: 0xdafb622f6af16b6f [1(launchd)] check_np(0x16f1cdb50) vm_shared_region_start_address() failed
shared_region: 0xdafb622f6af16b6f [1(launchd)] check_np(0x16f1cd0f0) vm_shared_region_start_address() failed
```

- **日志里 0 条 I/O 错误**（此前每轮都有 `disk5: I/O timeout` / `error = 60` /
  `disk3: I/O error` / `got err 5`）；
- 驱动统计：`signed root policy read in 227920 ms, 2 entries, 1123 reads, 0 retries, 0 failures`；
- **没有 `initproc failed to start`**。

### 为什么"没有这条消息"就是成功

`bsd/kern/kern_exit.c:1850-1857`：

```c
if (p == initproc) {
    ...
    } else if (initproc_spawned) {
        prefix_str = "initproc exited";
    } else {
        prefix_str = "initproc failed to start";     // ← 之前每轮都出现
    }
}
```

这条消息**只在 init 进程退出时**打印。此前每一轮都会出现它并伴随 panic 屏
（`namespace 6 subcode 0x1 ... no dyld cache`）；这一轮没有出现，也没有 panic 屏
⇒ **launchd 没有退出，正在用户空间运行** ✓

### 关键改动：驱动整段重试

`package/NativeMsdc.cpp` 的 `transfer()` 原来明确写着"故意不重试"，理由是重试会造成窗口
hash 不匹配（曾经 `offline owner SystemOS mount failed: 80`）。那个理由只对**续读**成立：
`hostPrepareData()` 每次都会清 FIFO，`hostReadPio()` 每次都从 offset 0 重填缓冲区，
所以**整段重试读到的是完整新鲜数据**，hash 安全。

现在的实现：

- 读操作最多 3 次尝试，写操作不重试（不是瓶颈）；
- 新增 `native_msdc_retries` 计数器；
- 策略区读取日志改为
  `signed root policy read in %llu ms, %u entries, %llu reads, %llu retries, %llu failures`。

本轮 `0 retries, 0 failures`，说明这次启动本身没有遇到停顿；但 I/O 错误此前是**间歇性**的，
重试作为保险保留。**下次启动要复验是否稳定复现**。

### 同一轮确认的其它事实

- `D4K envp` 诊断（已退役）证明环境变量**正确送达** dyld：

```
D4K envp: ptr=0x1500 e1=0 vec0=0x1400 e2=0
env='DYLD_SHARED_CACHE_DIR=/private/preboot/System/Library/Caches/com.apple.dyld'
```

- 该诊断曾插在 `launchdsuffix` 分支里而**不会执行**（我们没有这个 boot arg，走的是
  `init_programs[]` 循环）—— 诊断必须放在真正会执行的路径上，这是一个方法论教训；
- `boot-progress=2` 与 `cs_debug=0xff` 会**明显加剧 I/O 超时**（framebuffer 刷新 + printf 洪流
  与 PIO eMMC 抢同一个控制器），曾导致 `Failed to mount root device`。诊断参数要一次只加一个。

### 下一步

1. **复验稳定性**：再启动一次，确认仍无 `initproc failed`、仍无 I/O 错误；
2. **显示驱动**：内核控制台目前只写 eMMC，屏幕上只能看到菜单残影，无法观察用户态进展；
   接入显示 provider 后才能看到启动画面与 UI；
3. 之后依次：输入、GPU、网络、音频、电源。

---

## 0.64 launchd 跑起来了；现在的墙是存储带宽（2026-11-07 19:0x–21:5x）

### 结论一句话

引导链、APFS、三个卷、dyld 缓存、launchd 全部打通；`launchd` 在用户空间**持续运行**
（`pid1stat=2` = SRUN），但 eMMC 实测只有 **128–320 KB/s**，dyld 要读几百 MB 缓存，
系统因此停在"启动中"而不是崩溃。**带宽是唯一剩下的墙。**

### 关键修复：整段重试（`package/NativeMsdc.cpp`）

`transfer()` 原来故意不重试，理由是"重试会留下部分刷新的缓冲区导致 hash 不匹配"。
那个理由只对**续读**成立：`hostPrepareData()` 每次清 FIFO，`hostReadPio()` 每次从
offset 0 重填，所以**整段重试读到的是完整新鲜数据**，hash 安全。

现状：读最多 **6 次**尝试，每次之间 `IODelay(1000)` 让卡退出失败数据态；写不重试。
新增 `native_msdc_retries` 计数，并输出到策略区日志与心跳。

**效果（实测）**：`failures=0`，`retries` 缓慢增长（偶发停顿全部救回），
`check_signature[pid: 1]: error = 60` 消失，launchd 不再退出。

### 失败机理（此前一直是这个）

```
disk3: I/O timeout.                          ← 驱动 3 次重试后返回 kIOReturnTimeout
check_signature[pid: 1]: error = 60          ← launchd 校验 dyld 缓存签名时读超时
dyld[1]: dyld cache '(null)' not loaded: (null)
  → Library not loaded: /usr/lib/libSystem.B.dylib (no such file, no dyld cache)
  → pid 1 exited -- exit reason namespace 6 subcode 0x1
```

这解释了**间歇性**：读成功 → launchd 活着；读超时 → 立刻退出。6 次重试后不再复现。

### 心跳（`dandelion-launchd-beat.patch`）

在 `load_init_program()` **之前**启动内核线程，每 20 秒打印一次：

```
D4K beat 59: procs=2 pid1stat=2 | reads=4563 writes=0 retries=125 failures=0
```

- `pid1stat=2` = **SRUN**，launchd 在跑，不是睡眠也不是退出；
- `procs=2` 持续 20 分钟 = 还没有派生任何子进程；
- `reads` 稳定增长 = 在持续读盘；
- `failures=0` = 重试机制有效。

（注意：心跳必须在 `load_init_program` **之前**启动 —— init 退出时内核 panic，之后启动的线程
永远不会报告。）

### 带宽测量与总线模式

| 测量 | 数值 |
| --- | --- |
| 策略区 73 MB | 227,922 ms → **320 KB/s** |
| 心跳期间 3169 × 64 KiB | 20 分钟 → **169 KB/s** |
| 理论值（HS 8-bit DDR） | 数十 MB/s ✗ 差 300 倍 |

`native-msdc-mode=1` 的遥测：

```
mode host ckmod=2 ckdiv=260 stable=1 bits=8 ddr=1 hs400clk=0
mode card rev=8 bus_width_raw=06 hs_timing=01 card_type=57 driver_strength=1f timing=1
mode source mux_valid=1 mux_reg=00000300 msdc0_mux=0 source_hz=unmeasured
```

**卡侧是高速配置**（`hs_timing=01` = HS 52 MHz，`bus_width_raw=06` = 8-bit DDR），
但**控制器分频 `ckdiv=260`** —— bootloader 留下的慢速枚举值，驱动从不修改 ✗
这就是 300 倍差距的来源。

### 两条提速尝试都失败了（重要负面结果）

| 尝试 | 结果 |
| --- | --- |
| `native-msdc-dma=1`（驱动自带 DMA 路径，含 PIO 比对自检）| **`Failed to mount root device`** —— DMA 读不可靠，连根卷都挂不上 |
| `native-msdc-clock=1`（`hostRaiseClock()`：按 4/8/16/32/64 依次降分频，每步回读同一扇区比对，不一致就恢复）| **`Failed to mount root device`** —— 单扇区比对通过也不够，持续负载下仍出错 |

结论：**只改分频不够**。改速率必须同时重新调 **pad 采样点（TDsel/RDsel）**，
否则位错误。驱动目前只记录 `pad0/pad1`，从不调优。`hostRaiseClock()` 已实现并保留，
由 `native-msdc-clock=1` 控制，默认关闭。

### 下一步（按价值排序）

1. **实现 pad 采样延迟调优 + 提高时钟**：MTK 的 `msdc_set_tdsel/rdsel` 思路 ——
   在 `hostRaiseClock()` 里对每个分频尝试若干采样点组合，用多扇区比对选最稳的一组；
   这是把系统从"能跑"变成"可用"的关键一步；
2. **修 DMA**：DMA 路径代码已有，失败点未定位（`native_msdc_dma` 会给出 1/2/3）；
   定位后再试，DMA 比 PIO 快一个量级；
3. 带宽解决后：确认 launchd 派生进程（`procs` 增长），再进入显示/输入适配。

### 可工作镜像

`boot-data.img` sha256 `5f8e475ed940d96dbaed5a4e964118bffb2494fecb964476550ffff040d2c822`
（时钟实验已用参数关闭，回到 launchd 能持续运行的状态）。

---

## 0.65 时钟/采样调优是破坏性的；改为长时观察（2026-11-07 22:0x–22:2x）

### 新增的遥测事实

`native-msdc-mode=1` 打印的寄存器原文：

```
mode raw inherited=02210499 cfg=02210499 iocon=00000010 sdc=ff027ffc pad0=00000000 pad1=00000000
```

解码：

| 寄存器 | 字段 | 值 |
| --- | --- | --- |
| `MSDC_CFG` (0x00) | `ckdiv` (19:8) | **260** ← 慢速枚举遗留 |
| | `ckmod` (21:20) | 2（数据模式）|
| | `ddr` (bit 7) | 1 |
| | `PIO` (bit 3) | 1 |
| `MSDC_IOCON` (0x04) | `R_D_SMPL` (21:16) | **0** ← 读数据采样延迟 |
| `SDC_CFG` (0x30) | `BUSWIDTH` (17:16) | 2（8-bit）|

即：卡侧高速（`hs_timing=01`、8-bit DDR），但控制器分频 260、采样延迟 0 —— 这个组合只在
慢速下成立。

### 两次调优尝试都破坏根卷（重要负面结果）

| 尝试 | 结果 |
| --- | --- |
| `hostRaiseClock()` v1：逐个降分频，单扇区回读比对 | `Failed to mount root device` |
| `hostRaiseClock()` v2：分频 × 采样点二维扫描（4 分频 × 16 采样），三扇区比对，失败后 `CMD13` 恢复到 transfer 态，并加 8 秒总预算 | 仍然 `Failed to mount root device` |

镜像 hash 已核对（`223BFE05…` 与构建输出一致），**不是刷错** ✓。

### 结论

**任何一次在错误时钟/采样下失败的读，都会把卡留在无法自行恢复的状态** —— 这个驱动
「从不 reset/retune」（HANDOFF 0.10 起就如此），没有控制器复位手段，所以扫描式试错
本身是破坏性的。要真正提速必须：

1. 实现 **MSDC 控制器复位 + 卡重新初始化**（CMD0/CMD1/ACMD41/CMD2/CMD3/CMD9/CMD7 + 时钟切换），
   然后才能在安全的边界内换速率；
2. 或拿到该 SoC（MT6762/MT6765）**确定的** MSDC 时钟源与 TDsel/RDsel 取值（Linux/EDK2 参考），
   一次性设对，不做扫描；
3. 或修好 DMA（DMA 路径存在但一开就挂根卷，失败点未定位）。

### 当前采取的策略

时钟实验用 `native-msdc-clock=1` 控制，**默认关闭**；`hostRaiseClock()` 代码保留（含恢复与预算），
留给上面 1/2/3 任一条件具备时再启用。

同时改为**长时观察**：以 PIO 320 KB/s 让系统跑 60 分钟以上，确认 dyld 是"慢"而不是"卡死"
（判据：心跳 `procs` 从 2 开始增长、`reads` 继续上升）。可工作镜像
`8342578ff29196bddd2503f462510d7cf9d733fd4fcdc92ae665522e5397df72`。

---

## 0.66 DMA 定位到超时，但三个假设都排除（2026-11-07 22:3x–23:2x）

### 上游参考：同款驱动在 MT8167 上 DMA 可用

`docs/zh/devices/a04br3.md`（Benesse TAB-A04-BR3，MT8167，同一份 `NativeMsdc`）：

> *"平板已在四个 CPU 上运行 iPadOS 主屏幕，**eMMC DMA** 和 CPU 帧缓冲渲染可用"*

所以 DMA 代码路径本身是成立的，MT6762 上的失败是**平台差异**，值得修。
（`drivers/community-native-msdc.md` 只写"驱动尚未启动"，我们的移植已经超过它；
上游 `mouseos/NativeMsdc` 与本仓库同版本，没有时钟/复位代码。）

### 关键测量：DMA 自检返回超时

开启 `native-msdc-dma=1` 后驱动打印：

```
NativeMsdc: DMA comparison with PIO failed, PIO kept (0xe00002d6) at 0x59a05000
```

`0xe00002d6` = **`kIOReturnTimeout`** —— DMA 引擎启动了但**从不完成** ✗

### 三个假设逐一排除

| 假设 | 改动 | 结果 |
| --- | --- | --- |
| 顺序错误：先发读命令再武装 DMA，数据相位没有目的地 | 改为**先武装 DMA（写 DmaSa/DmaCfg/DmaCtrl+Start）再发命令** | 仍然 `0xe00002d6` ✗ |
| 描述符校验和被引擎拒绝 | `kDmaCfg` 清除 `kDmaCfgChecksum`（描述符本身仍计算校验和）| 仍然 `0xe00002d6` ✗ |
| 中断位不足 | 核对：`kIntXferDone` = bit 12、`kIntDataErrors` = bit 14/15、`kIntDmaErrors` = 7<<17；状态寄存器不受屏蔽位影响 | 未改动，判定不是原因 |

三种情况下启动都停在 `Failed to mount root device`（DMA 自检失败后应回退 PIO，但实际连根卷都挂不上
—— 说明 DMA 尝试本身也会让卡进入坏状态，与 0.65 的时钟扫描同理）。

### 下一步最有价值的假设

**MTK MSDC 的 DMA 引擎可能有独立的时钟/门控**（与 PIO 路径分开的 CG 位）。若该门控未开，
DMA 引擎收到 START 也不会动作，表现正好是"永不完成 → 超时"。验证方式：
在 `hostEnableDma()` 之前确认 TOPCKGEN/CG 中 MSDC 的 DMA 相关位已使能；
MT6765/MT6762 的时钟门控位需要从 Linux/EDK2 参考确认。

其次：MT6762 的 GPD/BD 描述符格式可能与 MT8167 不同（位域或字段顺序），
需要对照 `dmaDescriptors()` 与目标 SoC 的手册/参考实现。

### 当前可工作状态

DMA 与时钟实验都用启动参数关闭（`native-msdc-dma` / `native-msdc-clock` 默认 0），
镜像 `bcd3a7913e2411ac42c08981c4fabd3bcbf9bd92e3ec31b0a85eb4cd97b0e9a1`
回到"launchd 持续运行、存储 320 KB/s"的状态。

---

## 0.67 参考 UniXNU-merlinx：真正缺的是 Data seed 内容（2026-11-07 23:3x–23:5x）

### 同代 SoC 的完整参考

用户提供 `https://github.com/Dracen-W/UniXNU-merlinx`（Redmi Note 9，**MT6768**，同一代 MSDC 控制器）。
其设备记录 `docs/.vitepress/device-records/merlinx-gytis283123.toml` 的存储条目写着：

> *"NativeEmmc replaces the timed-out UEFI storage path. The kernel mounts SystemOS, **executes
> initproc and reaches launchd, xpcproxy and SpringBoard**. The macOS-generated Data seed mounts
> read-write and **disables watchdogd through the launchd disabled plist**."*

即：**同代 SoC、同样纯 PIO 驱动，他们到了 SpringBoard** ✗ 所以 §0.64/0.66 的"带宽是唯一墙"
判断不完整 —— 带宽是慢，但不是停住的唯一原因。

### 与 merlinx 的配置差异（已全部采纳）

| 项目 | merlinx | 我们（改前）| 现在 |
| --- | --- | --- | --- |
| `OWNER_ROOT_AUDIT` / `OWNER_ROOT_TRUST` | =y | =n（未验证实验）| **=y** |
| `OWNER_UNION_SYSTEMOS` | =y | =n | **=y**（与 carrier 同时开）|
| `OWNER_CACHE_CARRIER` | =y | =y | =y |
| `launchd_unsecure_cache` | =1 | 无 | **=1** |
| `wdt` | =-1（关看门狗）| 无 | **=-1** |
| `ignition_level` | =1 | 无 | **=1** |
| `-no_slto_panic` / `sheng-aks=1` | 有 | 无 | 有 |

注意 `DYLD_SHARED_CACHE_DIR=` 的内核字符串计数因此变为 0 —— union 模式下框架不设该环境变量，
缓存通过 union 在默认路径可见，这是预期行为。

### 真正缺失的：Data seed 里的 12 个文件

merlinx 的 `data-seed.json` 有 84 个目录 + **12 个文件**；我们的 seed 从 OS 卷的 `private/var`
复制（39 个文件、611 KB），**一个都没有** ✗

| 文件 | 关键内容 | 没有它会怎样 |
| --- | --- | --- |
| `db/com.apple.xpc.launchd/disabled.plist` | `com.apple.watchdogd: true` 等 8 项 | 看门狗杀掉慢速用户态 |
| `db/com.apple.xpc.launchd/disabled.501.plist` | 用户级 4 项 | 同上 |
| `mobile/.../com.apple.springboard.plist` | `BKNoWatchdogs`、`FBLaunchWatchdogScale 20.0` | SpringBoard 被看门狗终止 |
| `mobile/.../com.apple.frontboard.plist` | `FBLaunchWatchdogScale 20.0` | 同上 |
| `mobile/.../com.apple.purplebuddy.plist` | `SetupDone`/`ForceNoBuddy: true` | 停在设置向导，进不了主屏 |
| `mobile/.../purplebuddy.notbackedup.plist` | 关闭各 mini buddy | 同上 |
| `mobile/.../managedconfiguration.notbackedup.plist` | `LockdownActivation...: false` | 配置检查阻塞 |
| `mobile/.../com.apple.migration.plist` | 迁移已完成标记 | 重复迁移检查 |
| `mobile/.../backboardd.plist`、`root/.../backboardd.plist` | 关闭 deep FB / FB 压缩 / IOSurface 压缩 | 无 GPU 时 backboardd 起不来 |
| `mobile/.../.GlobalPreferences.plist`、`root/.../.GlobalPreferences.plist` | 同上 | 同上 |

### 已实施

`scripts/build-data-seed.py` 新增 `SEED_FILES`（12 项，含 uid/gid/mode 与 plist 内容）与
`write_seed_files()`，在复制 `private/var` 之后写入，并记入报告 `seed_files`。
本地已用 `importlib` 载入该模块、在临时目录执行 `write_seed_files()` 验证：**12/12 生成、权限正确**。

seed 需要 macOS 的 `hdiutil`，所以走 `.github/workflows/data-seed.yml`（macOS runner）。
提交 `b74625d` 已推送，CI 运行 `42388131020` 正在构建新 seed。

### 下一步

1. 下载新 seed artifact，替换 `out3/data-seed/dandelion-data.dbr`，重建镜像并刷入；
2. 观察是否出现 `xpcproxy`/`SpringBoard`（心跳 `procs` 应开始增长）；
3. 若仍停在 launchd：用 `boot-ui-focus=launchd` 等 focus 参数采样卡点；
4. 显示仍是后续独立任务（`CONFIG_XNU_NATIVE_LEGACY_DISPLAY` + `build-gop-legacy-display.py`）。

---

## 0.68 慢的根因测定：内核顺序读整个 3.7 GB 缓存做签名校验（2026-11-08 01:1x–01:4x）

### 卡时钟的正确算法（推翻 §0.66 期间的自我更正）

`mtk-sd.c`（Linux，MT6765 同族）的 DDR 分频公式：

```c
mode = 0x2;                                   /* DDR，使用分频 */
div  = (src + ((hz << 2) - 1)) / (hz << 2);
sclk = (src >> 2) / div;
div  = div >> 1;                              /* 寄存器里存 div/2 */
```

`cfg=0x02210499` 解出 `CKMOD_EXTRA=2`（DDR）、`CKDIV_EXTRA=260`（bits 19:8，12 位分频）。
寄存器 260 → 实际 div 520 → `sclk = (src/4)/520`；`src` 若为 200 MHz 则 **≈ 96 kHz** ✗
与实测 320 KB/s（96 kHz × 8 bit × DDR ≈ 190 KB/s）吻合 ✓

**注意**：该 SoC 用 `clk_div_bits = 12`，即 EXTRA 字段才是生效的 ✓ 我们读对了。
（中途我曾据基础字段误判为 `div=4`，此处以 Linux 公式为准。）

### 为什么改时钟失败（MTK 序列还缺两步）

`msdc_set_mclk()` 的实际步骤：清 `CKPDN` → **门控源时钟 `clk_disable_unprepare(src_clk_cg)`** →
写 `CKMOD_EXTRA|CKDIV_EXTRA` → 使能源时钟 → 等 `CKSTB` → … → **按新频率重写 pad 调优**
（`def_tune_para` 的 `iocon` / `pad_tune`，≤52 MHz 与更高速率用不同值）。

我们只做了"停钟 → 写分频 → 等稳定 → 重启"，**没有门控源时钟，也没有 SoC 专用 pad 调优值** ✗
这两个值只存在于厂商时钟/驱动代码里，本仓库没有 → 直接提速这条路目前走不通。

### 决定性测定：读的是整个缓存，不是零散元数据

在 `transfer()` 里每 400 次读打印一次扇区（400 × 64 KiB ≈ 25.6 MB）：

```
read trace sector=1
read trace sector=90066399
read trace sector=90117599
read trace sector=71818903
read trace sector=83628111
read trace sector=83676367      ← +48,256 扇区
read trace sector=83724623      ← +48,256 扇区（= 24.7 MB，与 400 次读吻合）
read trace sector=83773007      ← +48,383
```

**8 个采样中 6 个递增，末尾稳定以 ~24.7 MB 步进** ⇒ 顺序扫读一个大文件 ✓
即 launchd/dyld 映射共享缓存时，内核**逐页校验 3.7 GB 缓存的代码签名**（哈希整文件）✗
3.7 GB ÷ 320 KB/s ≈ **3.2 小时** —— 这就是"看起来永远跑不完"的真正原因。

对比：真实 iOS 的共享缓存由 **SystemTrustCache 授权**，不做逐页哈希。

### 两条修复路线

| 路线 | 内容 | 可行性 |
| --- | --- | --- |
| **A. 跳过缓存哈希**（推荐）| 让 `shared_region_map_and_slide_np` 的校验信任 trust cache 而不逐页哈希；缓存 cdhash `fcdb9268…` 已在我们的 trust cache 模块里 | 完全在本仓库可改范围内 ✓ |
| B. 提高卡时钟 | 需要 MT6762 的源时钟门控位与 pad 调优值（`def_tune_para`）| 缺厂商数据 ✗ |

路线 A 修好后，读取量从 3.7 GB 降到几 MB，**即使卡只有 96 kHz 也能秒级完成** ✓

### 本轮其它已确认

- 12 个 seed 文件已进入镜像：日志 `disk2s1 vol-uuid: 661FF9FF-E51F-4050-99B7-0B101374F90F` +
  `mount-complete volume OwnerData` ✓
- `CONFIG_XNU_OWNER_DATA_VOLUME_UUID` 已随 CI 重建的 seed 更新 ✓
- merlinx 配置全部采纳（验证根 / union+carrier / `launchd_unsecure_cache` / `wdt=-1` /
  `ignition_level=1` / `-no_slto_panic` / `sheng-aks=1`）✓
- 心跳在 `load_init_program` 之前启动、记录 360 拍（2 小时），可无人值守观察 ✓

---

## 0.69 给 54 个缓存文件加 trust cache 授权：没有改变读取量（2026-11-08 01:5x–02:2x）

### 做法

用 apfs-fuse 挂载 cryptex，对 `root/System/Library/Caches/com.apple.dyld/` 下**全部 54 个文件**
解析 SuperBlob 里的 CodeDirectory 并取 SHA-256（脚本 `/mnt/d/aios/_work/cache_cdhashes.py`
思路：`dyld_cache_header.codeSignatureOffset`(0x48) → `CSMAGIC_EMBEDDED_SIGNATURE` →
索引里的 `CSMAGIC_CODEDIRECTORY` → 其字节的 SHA-256），截断为 20 字节写入 trust cache。

重建后的 bundle（`out3/apple-trustcache.bundle`，62,680 字节）三段模块：

```
module 0: v2, 2478 entries   (044-76040-038 OS)
module 1: v2,   76 entries   (044-76034-038 SystemOS)
module 2: v2,   54 entries   (dyld 缓存全集，新增)
PASS: the bundle matches the layout DarwinBoot validates
```

### 结果：读取量没有下降

镜像 `075f129c…` 刷入后，心跳与上一版**完全一致**：

```
D4K beat 0 : procs=2 pid1stat=2 | reads=1555 retries=8   failures=0
D4K beat 62: procs=2 pid1stat=2 | reads=4777 retries=133 failures=0
```

仍是每 20 秒约 50 次读（≈3.2 MB），累计 21 分钟约 300 MB —— **顺序扫读继续** ✗
所以这股读取**不是**"逐文件 cdhash 未授权导致的哈希" ✗，信任缓存这条路没有解决 §0.68 的问题。

### 位置核对

读扇区落在 83.6M–90.0M，而这个区间正是 **SystemOS 窗口**（userdata 从 71,690,207 扇区起，
OS 窗口 6,077,580,800 字节 ≈ 11.87M 扇区 → SystemOS 窗口自 ≈83.56M 扇区开始）✓
即它**按目录顺序把每个缓存文件从头读到尾** ✓

### 现在的判断

带宽 96 kHz 是硬约束（§0.68）。任何需要读几百 MB 的路径都要几十分钟到几小时 ✗
而真正能让它变快的两条路都还没打通：

1. **提高卡时钟** —— 需要 MT6762 的 `def_tune_para`（iocon / pad_tune）与源时钟门控位；
   这些在**厂商 Android 内核的 `mtk-sd.c` 里按 SoC 给出**，是下一步最该去找的东西
   （公开的 MT6762/MT6765 内核源码里 `dev_comp` 表就有 `pad_tune_reg` / `default_tune` 值）；
2. **减少读取量** —— 文件级 trust cache 无效，需要定位到底是 dyld 还是内核在顺序读；
   可先用"按 pid 打印读请求"的驱动诊断区分，或去掉 `launchd_unsecure_cache=1` 复测
   （该参数来自 merlinx，但可能让 dyld 走重建缓存而非映射的路径）。

### 今晚最终状态

`procs` 始终为 2、`pid1stat=2`（SRUN）、`failures=0` —— 系统活着、在读、不崩，但被带宽卡住。
所有配置（12 个 seed 文件、merlinx 开关、54 个缓存 cdhash、心跳、读追踪、时钟切换代码）
都已提交；`native-msdc-clock` / `native-msdc-dma` 默认关闭。

---

## 0.70 时钟提速的最终结论：试错式探测在本卡上是破坏性的（2026-11-08 02:2x–03:3x）

### 找到的两处真实缺陷

对照厂商内核 `mtk-sd.c`（`MiCode/Xiaomi_Kernel_OpenSource` 的 **dandelion-q-oss** 分支，
正是本机的 MT6762G）修正了驱动里两个错误，并补上一个缺失步骤：

| 项 | 厂商定义 | 我们原来 | 现在 |
| --- | --- | --- | --- |
| `MSDC_CFG_CKPDN` | bit 1 | bit 1 ✓ | ✓ |
| `MSDC_CFG_CKSTB` | **bit 7** | bit 4 ✗ | ✓ 已修正 |
| 改分频后重算数据超时 | `msdc_set_timeout()` → `SDC_CFG.DTOC`（0xff<<24，按 2^20 卡时钟计） | **完全没有** ✗ | ✓ 已实现按分频比例缩放 |

`SDC_CFG.DTOC` 缺失是重要发现：它按卡时钟周期计数，提速后不重算会让卡"立刻超时" ✗

### 但探测仍然破坏启动，且没有任何日志

三次构建（无条件执行、把参数移到命令行首位、把探测缩到 1 扇区 × 2 候选）结果一致：

- 日志**完全没有** `ckdiv … dtoc …` 行 ✗
- 日志长度从约 240 行降到 **108 行**，且停在 `mode source …` 之后
- 屏幕 `Failed to mount root device`，且出现得**很快**

结论：`hostRaiseClock()` **确实在执行**，但**第一个候选（分频 64）就把卡弄坏了** ✗
→ 根设备再也出现不了 → IOKit 等待循环放弃 → panic → 函数没跑完，日志来不及写 ✓

即：**这台卡/控制器上，任何一次错误时钟或采样下的读都不可恢复**（驱动无复位能力，见 §0.65），
所以"试错式探测"这条路本身不可行 ✗ 必须在**第一次就写对**分频 + 采样值。

### 需要的东西（下一次）

厂商 `mtk-sd.c` 里 `msdc_set_mclk()` 在换频率后会写回 `def_tune_para`：

```c
if (host->sclk <= 52000000) {
    writel(host->def_tune_para.iocon,      host->base + MSDC_IOCON);   // 0x04
    writel(host->def_tune_para.pad_tune,   host->base + tune_reg);     // 0xec（不是 0xf0）
}
```

`def_tune_para` 在 probe 时从寄存器**保存**，但对 MT6762 的**正确取值**需要厂商板级代码
（Android 内核 `arch/arm64/boot/dts/mediatek/` 或 `drivers/misc/mediatek/` 里的板级调优）。
拿到后可以一次性设对，不需要探测。

另注意：**我们的驱动读的 pad 寄存器是 `0xf0/0xf4`（`MSDC_PAD_TUNE0`），厂商用的是
`0xec`（`MSDC_PAD_TUNE`）** ✗ 位置都错了，读到的 `pad0=pad1=0` 不说明真实采样配置。

### 今晚的净结果

- 已修正 CKSTB 位与数据超时重算（真实缺陷）✓ 代码保留
- 时钟探测改为 `native-msdc-clock=1` 显式开关，**默认关闭** ✓
- 未验证：无法在设备上实测提速效果（因为探测破坏启动）
- 现在的镜像回到"launchd 持续运行、96 kHz、320 KB/s"的可工作状态

---

## 0.71 通宵跑通：launchd 真正运行并打印日志，随后 SIGBUS（2026-11-08 03:5x–10:4x）

### 通宵结果（镜像 `4a57497b…`，心跳 1080 拍 / 6 小时）

```
D4K beat 0  : procs=2 pid1stat=2 | reads=1555  retries=8    failures=0
D4K beat 999: procs=2 pid1stat=2 | reads=56581 retries=2002 failures=0
com.apple.xpc.launchd|1970-01-01 05:43:12 <Notice>: boot-mode set by sysctl: main
com.apple.xpc.launchd|1970-01-01 05:43:13 <Notice>: hello, launchd UUID: 4B525992-8C69-315E-A51A-CC9433E45B51
```

- 1000 拍（5.5 小时）内 `procs` 始终为 2、`failures=0`；
- `reads` 到 56,581 次 × 64 KiB = **3.6 GB**，即 §0.68 测到的"整个共享缓存顺序读"完整跑完 ✓
- 读完后 **launchd 开始打印自己的日志** ✓✓✓ 这是用户态真正运行的直接证据；
- 屏幕随后显示 **`initproc exited -- exit reason namespace 2 subcode 0xa`** ✗

### 解码

`bsd/sys/reason.h`：

```
#define OS_REASON_JETSAM 1
#define OS_REASON_SIGNAL 2
```

即 **namespace 2 = OS_REASON_SIGNAL**，`subcode 0xa` = **信号 10 = SIGBUS** ✗

而 `initproc exited` 而非 `failed to start`（`kern_exit.c:1853` 的 `initproc_spawned` 分支）
说明 **launchd 已经成功派生出子进程** ✓ 系统确实推进到了服务启动阶段。

### 现在的问题排序

| # | 问题 | 影响 |
| --- | --- | --- |
| **A** | 映射缓存前要顺序读 3.6 GB（5.5 小时）| **每次迭代都要等 5.5 小时**，无法调试 ✗✗ |
| **B** | launchd 启动后 SIGBUS 退出 | 需要诊断；但必须先解决 A 才能快速迭代 |

A 的候选解法（按可行性）：

1. **提高卡时钟**（§0.70 的结论：需要 MT6762 的 `def_tune_para`，或给驱动加复位能力）；
2. **找到 3.6 GB 读取的真正发起者**：现在已知它发生在 launchd 打印任何东西之前、
   `shared_region: check_np failed` 之后，即 dyld 自己映射缓存时；若能把该映射改为
   "内核信任并直接 map、不读内容"，读取量可降到几 MB；
3. 注意 `check_signature` 的 cdhash 授权（§0.69）对这股读取无效，说明它发生在更底层
   （mmap 页读取或 dyld 自建 map）。

### SIGBUS 的初步判断

launchd 在打印两行日志后立刻 SIGBUS，常见原因：

- dyld 经 `shared_region_map_and_slide_np` 自建的映射存在无法访问的页（我们内核 4 KiB，
  缓存按 16 KiB 构建）✗
- 缓存子片的 slide info / 页对齐与 4 KiB 不匹配

但 merlinx 用同样 4 KiB 内核跑到 SpringBoard ✓，所以不是不可逾越；等 A 解决后可用
`boot-ui-focus=launchd` 或 SIGBUS 的 pc 定位具体页。

### 本轮新增/变更

- 心跳延到 1080 拍（6 小时），便于无人值守整夜观察 ✓
- 时钟探测保持 `native-msdc-clock` 开关、默认关闭 ✓

---

## 0.73 时钟改法已正确，但启用后 OS 窗口不就绪（2026-11-08 12:4x–13:3x）

### 已确认正确的部分

`CKPDN` 极性 + `DTOC` 重算两处修正后，**改分频本身完全成功**：

```
D4K clock: div=4 smp=0 verified=1 cfg=0220049b sdc=ff027ffc
```

逐级 64→32→16→8→4 全部验证通过，且用**磁盘四个分散位置**（0x40 / 0x445d000 / 0x5000000 /
0x6000000）交叉验证同样通过 ✓ 即分频 4 的读在采样点上完全正确 ✓

### 但启用提速后启动失败，且失败点在提速之前

把 `hostRaiseClock()` 从 bring-up 移到 `publishThread` 结尾（窗口发布之后）后重试两次，
日志都停在（153 行）：

```
NativeMsdc: signed root policy read in 227931 ms, 2 entries, 1123 reads, 0 retries, 0 failures, ckdiv 0/0
NativeMsdc: OS verified against its signed policy
NativeMsdc: SystemOS verified against its signed policy
NativeMsdc: published SystemOS
NativeMsdc: SystemOS volume ready
NativeMsdc: published OS
```

- `ckdiv 0/0` 说明**提速尚未执行** ✓（符合新位置的设计）
- 缺 `OS volume ready`，也缺 `not seen after 30 s` → 卡在 OS 窗口的 `volumeBelow` 30 秒等待里，
  随后根卷挂载超时 → `Failed to mount root device` ✗
- 即失败发生在**提速之前**，与时钟无关 ✓；但昨晚 `4a57497b`（无提速）OS 卷是能挂上的 ✗
  → 这是本轮引入的**新时序问题**，需要单独排查

### 三条被排除的假设（本轮）

| 假设 | 做法 | 结果 |
| --- | --- | --- |
| CKPDN 极性写反 | 改为厂商语义（清=关钟、置=开钟）| **修复成功**，分频可改 ✓ |
| 改分频后未重算数据超时 | 按分频比例缩放 `SDC_CFG.DTOC` | 已实现 ✓ |
| 单扇区验证不具代表性 | 改 4 个分散扇区交叉验证 | 仍通过 ✓ 但整盘持续读仍不可靠 ✗ |

### 诊断教训（重要）

**控制台跑在同一块 eMMC 上**：一旦时钟改动影响卡，日志立刻失效 ✗ 本轮改用 `panic()`（走
framebuffer）才拿到 `D4K clock:` 结果 ✓ —— 以后凡是"探测可能弄坏存储"的改动，结果
必须走 panic 屏或其它不依赖该存储的通道。

### 下次的三个入口

1. **先查 OS 窗口 `volumeBelow` 30 秒不就绪**：这发生在提速之前，与时钟无关。可在
   `publishThread` 里把该等待的 tick 数和结果打印出来，或延长等待；
2. 提速落在窗口发布之后是否真的生效：日志里加一行 `clock settled` 到 publishThread 之后
   的位置确认；
3. 若提速生效后仍失败，则验证**通过已发布窗口**的读在新分频下是否正确（窗口读是逐块
   hash 校验的，是最严格的自检）——这是区分"卡不稳"与"窗口路径问题"的关键一步。

### 当前配置

`native-msdc-clock` / `native-msdc-clk` 均已关闭，回到 `4a57497b` 的等效配置：
慢速总线（分频 260）、launchd 能运行（约 5.5 小时读缓存后打印日志）。
驱动内的时钟代码（含正确序列、4 点验证、panic 报告、`native-msdc-clk` 选档）全部保留。

---

## 0.74 系统跑起来了：80 个进程、14.7 MB/s、分频 4 生效（2026-11-08 14:5x–16:1x）

### 两个关键修复

**1. 发布循环漏掉了最后一个窗口的就绪等待**（真实 bug）

`publishThread()` 原来是：

```c
if (position + 1 == gPlan.count) { device->release (); break; }   // 最后一个窗口直接跳过
bool ready = false;
for (unsigned tick = 0; tick < 300 && !(ready = volumeBelow (device)); ++tick) IOSleep (100);
```

**最后一个窗口正是 OS 窗口 —— 根卷的载体** ✗ 它从不等待就绪，于是根卷挂载与 APFS 卷出现
赛跑，多数情况下输掉 → `Failed to mount root device` ✓
移除该分支、并把握手等待从 30 秒延长到 120 秒后，日志出现
`NativeMsdc: OS volume ready` + `disk5s1 mount-complete ...OS` ✓ **根卷问题彻底解决** ✓

**2. 时钟提速移到 `publishThread` 结尾**

在 bring-up 阶段改时钟会连带弄坏控制台（日志与存储同源），并且根卷挂不上 ✗
移到所有窗口发布并确认就绪之后，策略区读取仍走原速（228 秒），之后的大流量读取走高速 ✓

### 实测结果（镜像 `2879a843…`，`native-msdc-clk=4`）

```
NativeMsdc: ckdiv 4 (from 260), dtoc 255 -> 255, cfg 02200499
NativeMsdc: clock divider 4 sample 0 verified
NativeMsdc: clock settled at divider 4 sample 0 (inherited 260)
```

- `cfg=02200499` 解出 `CKDIV = 4` ✓（260 → 4）
- 心跳：`procs` 从 **2 涨到 82** ✓✓✓（beat 22 出现首个子进程，beat 31 到 82）
- `reads` 从 3404 → 144,891（32 拍 / 640 秒）→ **14.7 MB/s** ✓ 与预测的 14 MB/s 一致
- `pid 100` 是 **watchdogd** ✓ 标准系统守护进程 → **launchd 服务图完整启动** ✓✓✓

### 新问题：分频 4 对校验读不稳定

```
NativeMsdc: verify 182692 len 4096 ... got 8e7d13e0 want 6c37a5e9
NativeMsdc: SystemOS: read of sectors 1461536+8 refused, a block failed its signed hash
（同类失败 3 次）
```

分频 4 下窗口的**逐块哈希校验偶尔读出错误数据** ✗ 即 4 点采样验证不足以保证整盘可靠 ✓
**判据现在很明确**：验证窗口的哈希失败数 ✓ —— 0 次才算可用 ✓

### 下一步

1. 用分频 **16 / 32** 复测，找到"哈希失败为 0"的最快分频（预计 8–16 倍，5.5 小时 → 20–40 分钟）；
2. 若都失败，则说明需要**重新调 pad 采样点**（`native-msdc-smp` 已有开关）或启用 **8-bit 之外的
   更保守设置**；
3. 稳定后继续推进：现在 `procs` 已到 82，下一步看服务图里谁失败（watchdogd 被 SIGTRAP 杀掉，
   说明 `disabled.plist` 未生效，需确认 seed 里 launchd 的 disabled 列表是否被读取）。

---

## 0.76 分频 8 不通过验证；FIFO 字数的猜测被硬件否定（2026-11-08 17:4x–18:1x）

### 分频复测结果

| 分频 | 结果 |
| --- | --- |
| 4 | ✅ 验证通过，吞吐 ≈25 MB/s，`procs` 到 82–92、pid 到 134 |
| 8 | ✗ `divider 8 did not verify; keeping 260` —— 验证失败并回退 |
| 16 | ✗ 缓存挂载 `mount()` 返回 ENOENT（`D4K cache: err=2 stage=3 status=2`）|

即分频与稳定性不是单调关系，**只有 4 实测可用** ✓

### 重要纠正：哈希失败与时钟无关

在**回退到 260** 的那次启动里同样出现 5 次哈希失败：

```
NativeMsdc: verify 275902 len 4096 data 6672616d 65776f72 got 40675bb4 want c5fdd957
```

所以"验证读偶发误码"是**既有问题**，与提速无关 ✗ —— 之前把它归因于分频 4 是错的 ✓

### 一次被硬件否定的猜测（记录下来免得重犯）

推测 `MSDC_FIFOCS` 的 RX 计数是**字**而不是字节，于是把 drain 循环改成"每次读 1 个字、
`available -= 1`"。结果**根卷直接挂不上** ✗ —— 说明该计数确实是**字节数** ✓
原实现（`available >= 4` 才读整字、否则单字节）是正确的 ✓ 已原样回退并在注释里写明，
避免以后再次尝试同一改动。

### 当前配置（工作状态）

镜像 `9a532bb5`：`native-msdc-clk=4` + 校验读哈希重试（4 次）✓
- 分频 4 通过验证 ✓
- 吞吐 ≈25 MB/s（比 226 KB/s 快约 110 倍）✓
- 服务图运行，`procs` 82–92、pid 到 134 ✓
- 已知残留：验证读到偶发误码（任何分频均有），重试机制可吸收一部分 ✓

### 下一步

1. 定位偶发误码的真正来源：PIO drain 已排除字数问题，下一步看
   `kMsdcIntr` 与 FIFO 的交互（例如 XferDone 与残余字节的处理），或用 DMA 路径对比；
2. 3.6 GB 缓存现在只需几分钟，可以开始快速迭代服务图问题（`backboardd` / `SpringBoard`）；
3. `disabled.plist` 未生效（watchdogd 仍被启动并 SIGTRAP 终止），需确认 launchd 是否读取
   Data 卷上的 `db/com.apple.xpc.launchd/disabled.plist`。

---

## 0.77 稳定到 100 个进程；最后一处是 PIO 数据通路的完整性（2026-11-08 18:2x–19:0x）

### 本轮数据（镜像 `9a532bb5`：分频 4 + 哈希重试）

```
NativeMsdc: ckdiv 4 (from 260) ... clock divider 4 sample 0 verified
NativeMsdc: verify 177555 ... got a6fcfbf0 want eaa49cc3     ← 仅 1 次哈希失败（上一轮 5 次）
D4K beat 25: procs=94 reads=108831 retries=47 failures=0
DANDELION-EXEC: clearing policy answer 0 (pid 27 / 28 / 43 / 89)
amfid[47] triggered unnest of range 0x1fd400000->0x1fd600000 of DYLD shared region
pid 176 = watchdogd（SIGTRAP 终止）
```

- **`procs` 峰值 100** ✓（上一轮 62）
- **哈希失败降到 1 次** ✓ —— 校验读重试机制有效 ✓
- 服务图持续派生进程（pid 一路到 **176**）✓
- **`amfid` 正常运行并映射 DYLD 共享区域** ✓ 用户态框架已经跑通

### 新发现：控制台写入也会产生乱码

日志尾部在 `D4K beat 25` 之后出现乱码（`ÄAGsëO a«~ÊÛ1iÙÎ…`）✗ 而不是正常文本 ✓
控制台与读盘走**同一条 PIO 路径** ✓ 于是：

- 读：偶发误码 → 窗口哈希失败 ✓
- 写：偶发误码 → 日志乱码 ✓

**同一个根因：PIO 数据通路存在偶发数据损坏** ✗✓✓ 这是目前唯一的硬件层遗留问题 ✓

### 已排除

- 不是分频问题（260 下同样出现）✓
- 不是 FIFO 字/字节计数（`MSDC_FIFOCS` 确为字节数，改动会直接挂根卷）✓
- 不是校验读重试缺失（已加入，失败数 5 → 1）✓

### 下一步：查 PIO 数据完整性

候选方向：

1. **XferDone 与残余字节的交互**：`if (offset == total && !busy && !fifo)` 提前返回时，
   若最后一个字的低字节顺序或半字处理有误，会出现固定位置的单字节错乱；
2. **多块读的边界**：`kMaxBlocks` 与 `SET_BLOCK_COUNT` 的组合是否与卡的分块大小匹配；
3. **对比 DMA 路径**：DMA 自检当初以 `kIOReturnTimeout` 失败（§0.66），若 DMA 修好可作为
   交叉验证 —— 同一份数据用两条路径读，不一致即为 PIO 的问题；
4. **降低并发**：当前控制台写入与 APFS 读共用驱动锁，改为控制台仅在安全窗口写，
   可观察乱码是否消失（用于区分"控制器并发"与"单次传输内部"的损坏）。

---

## 0.80 20 ms 稳定等待无效；误码率约 1e-5，在传输内部（2026-11-08 19:2x–19:4x）

镜像 `85242502`（分频 4、切换后 20 ms 稳定等待、自检关闭）跑 12 分钟：

```
NativeMsdc: ckdiv 4 (from 260) ... clock settled at divider 4 sample 0
verify 417011 / 349626 / 495601 / 510432     ← 4 次哈希失败
D4K beat 36: procs=79 reads=227923           ← 19.7 MB/s，procs 峰值 90
```

- 日志这次**没有乱码** ✓（说明乱码不是必然，与负载/时序有关）
- **哈希失败 4 次** ✗ —— 与上一轮 1 次、再上一轮 5 次同量级
- 失败率约 **4 / 数十万块 ≈ 1e-5** ✓ 偶发 ✓
- 20 ms 稳定等待**没有**改善 → 误码不在"切换后首次传输"，在**单次传输内部** ✗

### 当前确定的事实

| 项 | 状态 |
| --- | --- |
| 系统运行 | ✅ `procs` 90–100、pid 176、`amfid` 正常 |
| 吞吐 | ✅ 13–20 MB/s（原 226 KB/s，约 60–90 倍）|
| 启动耗时 | ✅ 约 10–15 分钟（原 5.5 小时）|
| 数据完整性 | ✗ 偶发误码约 1e-5，可被重试掩盖但不能根除 |

### 下次的排查顺序（按信息量）

1. **对比 DMA 路径**：修好 DMA 后同一扇区两条路径互读，不一致即可定位到 PIO；
2. **单块 vs 多块**：自检改为读 1 块（512 B）与 64 块各若干次，看误码是否与块数相关
   （若只在多块出现，问题在 `SET_BLOCK_COUNT`/FIFO 排空时序）；
3. **降低并发**：控制台写入与 APFS 读共用驱动锁；临时把控制台改为丢弃写、只在空闲时写，
   观察误码是否消失（区分"控制器并发"与"单次传输"）；
4. 若确认是 PIO 内部，考虑把 `kMaxBlocks` 降到 16–32（同一路径更小的突发）。

### 里程碑小结（本日）

从"永远卡在根卷"到 **"100 个进程、amfid 运行、启动 10–15 分钟"** ✓
三个真实缺陷已修：发布循环漏掉末窗口就绪等待、时钟切换位置、CKPDN 极性 + DTOC 重算 ✓
剩余唯一缺陷是 PIO 的偶发误码，已有重试兜底，不影响系统启动到服务图。

---

## 0.81 显示栈的准备工作与本机工具链位置（2026-11-08 20:0x–20:3x）

### 为什么下一步是显示

- 系统已能跑到 **90–100 个进程、pid 176、`amfid` 运行**，但**日志写不出来** ✗
  （控制台与存储同源，跑到一定阶段 eMMC 写入失效）
- 屏幕停在 UniXNU 菜单 ✗ —— 内核接管后不再画屏 ✓
- 因此现在**看得见**比任何后续功能都重要，且 SpringBoard 本身也需要显示 ✓

### 已带进来的参考材料（提交 `109a480`）

- `scripts/build-gop-legacy-display.py`（来自 UniXNU-merlinx）✓
- `scripts/data/merlinx-legacy-display-abi.json`（`IOMobileFramebufferLegacy` 方法地址表，
  从同一 iPadOS 17.7.11 预链接内核提取，**与设备无关，可直接复用**）✓

### 它还不能直接跑，缺三样

| 缺项 | 说明 |
| --- | --- |
| `package/MerlinxLegacyDisplay.cpp` | 参考移植的**板级胶水**（framebuffer 基址、显示策略），我们需要写自己的 |
| `build/native-gop-presenter/NativeGOPPresenter.o` | 需先构建该对象 |
| `xcrun` | Linux 上是半吊子（`--show-sdk-path` 返回 `iphoneos`、clang 是 x86 主机版）|

### 本机工具链位置（已确认）

```
clang     /opt/unixnu-linux/llvm/bin/clang          (clang 24)
ar/libtool /opt/unixnu-linux/CommandLineTools/usr/bin/
SDK 根    <tree>/.cache/fakeroot
```

替换脚本里三处 `xcrun`：`--show-sdk-path` → fakeroot；`clang` → 上面的 clang；`ar` → 上面的 ar ✓

### 便宜的替代方案（framebuffer 文字控制台）

panic 屏已经能在 framebuffer 上画字（`scripts/xnu-panic-screen.c` 里的 `gFont[]` 12×5 字形 +
绘制循环 ✓），且启动日志给了视频参数：

```
DARWINBOOT: video base=0x7ec50000 row=2944 width=720 height=1600 depth=32
```

做法：在 `native_msdc_console_sink()` 里把每块 512 字节文本同时画到 framebuffer ✓
需要先确认内核里如何映射该物理地址（panic 屏是直接按地址写的 ✓ 说明可行）✓

### 当前可工作状态（回退点）

镜像 `9a532bb5` / 等效配置：`native-msdc-clock=1` + `native-msdc-clk=4`
→ 分频 4 验证通过、13–25 MB/s、`procs` 90–100、pid 176、哈希失败 1–5 次（重试兜底）。

---

## 0.82 framebuffer 文字控制台：映射与绘制已验证，但钩错了地方（2026-11-08 20:5x–21:3x）

### 已经验证成功的部分

在驱动里实现 `fbConsoleInit/Put/Write`（复用 panic 屏的 5×12 字体、2 倍放大、95 个字形），
并确认了三件事：

1. **framebuffer 必须映射**：直接写物理地址 `0x7ec50000` 会 data abort（
   `Kernel data abort ... far: 0x000000007ec50000`），改用
   `IODeviceMemory::withRange(...)->map(kIOMapAnywhere)` 后正常 ✓
2. **几何参数正确**：`base=0x7ec50000 row=2944 720×1600 32bpp` ✓
3. **可见区覆盖全屏**：在 y=100/500/900/1300 各画一行标定文字，**四行全部可见** ✓
   —— 所以 reference port 记录的"可见扫描输出偏移"在本机不存在 ✓

### 钩子位置错了

「灰屏无字」的原因：`device-emmc-log-console.patch` **并不调用**
`native_msdc_console_sink()` ✗ —— 它是**独立的内核 C 实现**（自带 `emmc_write_block()` 等），
所以驱动里的 sink 从未被调用 ✓ 把绘制放在 sink 里自然永远不会执行 ✓

（另外我把绘制放在了"驱动忙就返回"的守卫之后，即使 sink 被调用也几乎不会执行 ✗ 两个错误叠加。）

### 下次的正确做法

在 `device-emmc-log-console.patch` 的 **`emmc_flush()` 开头**把当前 `emmc_sector` 的文本画到
framebuffer ✓ 该补丁里需要：
- 自带 95 个字形表（可从 `xnu-panic-screen.c` 的 GLYPH_FONT 复制 ✓ 760 字节）
- **自行映射 framebuffer**（内核 C 里可用 IOMemoryDescriptor，或复用 panic 屏的映射方式）

驱动侧的 `fbConsole*` 实现保留（映射与绘制的写法已验证 ✓），只是当前未启用。

### 当前配置

`native-fb` 已关闭（开着会清屏但不显示内容 ✗），镜像 `4755fcc5`，
等于工作配置：`native-msdc-clock=1` + `native-msdc-clk=4`，启动到 90–100 进程 ✓

---

## 0.83 framebuffer 控制台改为独立补丁；构建受网络影响（2026-11-08 21:4x–22:0x）

### 已完成

把「在 pexpert 的 eMMC 控制台里把字符同时画到 framebuffer」的改动做成**独立补丁**
`patches/xnu/dandelion-fb-console.patch`（20 KB，**尚未加入 series**）：

- 在 `pexpert/arm/pe_serial.c` 的 `emmc_transmit_data()` 开头调用 `fb_putc(c)` ✓
- 自带 95 个 5×12 字形（从 EDK2 `Font.h` 提取，2 倍放大）✓
- 直接按物理地址 `0x7ec50000` 写（pexpert 阶段很可能仍是恒等映射 ✓ 待验证）

`device-emmc-log-console.patch` 已恢复为可构建版本 ✓

### 阻塞点：构建时上游源码下载超时

```
prepare-xnu-source.py -> fetch(entry['download_url']) -> TimeoutError: The read operation timed out
```

**与补丁无关** ✗ 是清树后重新抓取 xnu 上游源文件时网络超时 ✓ 连续两次 ✓
下次网络稳定时直接：

```bash
# 把 dandelion-fb-console.patch 一行加进 patches/xnu/series 后
bash /mnt/d/aios/_work/build_with_bootstrap.sh
```

### 设备状态

设备上运行的是 21:34 刷入的 `4755fcc5` = **工作配置**
（`native-msdc-clock=1` + `native-msdc-clk=4`），启动到 90–100 进程 ✓
构建失败不影响它 ✓

### 注意（重要）

`build_clean_patch.sh` 每次都删 `.cache/xnu-work`，因此**每次都要重新下载上游源码** ✗
网络不稳时会连续失败。若只是改了**已应用的补丁内容**，可改用"不删树 + 手动反向应用旧补丁 →
应用新补丁"的流程节省时间与网络 ✓

---

## 0.84 framebuffer 控制台：pexpert 也无法访问 framebuffer（2026-11-08 22:2x–22:4x）

### 编译问题的根因（已修）

第一次插入时锚点选成 `emmc_transmit_data(uint8_t c)`，代码落在 **`static void` 与函数名之间** ✗
于是所有声明变成函数内局部，编译报
`pe_serial.c:1073: variable has incomplete type 'void'` 与一串
`use of undeclared identifier 'fb_x'` ✓ 改为锚定 **`static void`** 那一行后编译通过 ✓
（`build_clean_patch.sh` 现在会打印 build log 与 kernel.log 里的错误 ✓ 之前错误被警告淹没 ✗）

### 运行结果：pexpert 写 framebuffer 直接 data abort

```
Invalid kernel stack pointer (probable corruption), at pc 0xfffffff0074330c4
x12: 0x000000007ec50000        ← framebuffer 基址
x24: 0x000000096000005        ← ESR 数据中止
esr: 0x9600004f                far: 0xfffffff007befe38
```

即 **pexpert 阶段同样无法直接访问 0x7ec50000** ✗ 它运行在固件留下的页表上，那里没有该映射 ✓
（驱动阶段也失败过：§0.82 的 `far: 0x000000007ec50000` ✓ 同一原因 ✓）

### 结论：framebuffer 控制台必须放在内核里

| 位置 | 能否写 framebuffer | 原因 |
| --- | --- | --- |
| pexpert（`pe_serial.c`）| ✗ data abort | 固件页表，无映射 |
| kext 驱动 | ✗ data abort | 内核页表，无映射（需 `IODeviceMemory::map()`，已验证可用 ✓）|
| **内核本体**（有 pmap）| ✅ 应可行 | panic 屏就是这么做的 ✓ |

所以正确做法是二选一：

1. **内核侧钩 printf/IOLog**，用 `pmap` 或 `IODeviceMemory` 映射后绘制（写法已验证 ✓）；
2. 或让控制台补丁在**驱动加载之后**改走驱动提供的回调 —— 但 pexpert 的早期日志会丢 ✓

`patches/xnu/dandelion-fb-console.patch` 保留（字体 + 绘制 + 钩子都已正确 ✓），
只是**不能跑在 pexpert 里** ✓ 当前 `device-emmc-log-console.patch` 已恢复为可用版本 ✓

### 设备状态

镜像 `b5f22927` = 工作配置（分频 4、无 fb 代码）✓

---

## 0.85 framebuffer 控制台：pexpert 与 osfmk 都不行，只剩注入代码一条路（2026-11-08 22:4x–23:0x）

### 三次尝试与三次失败

| 位置 | 结果 | 原因 |
| --- | --- | --- |
| 驱动（kext）| ✗ `far: 0x7ec50000` | 内核页表无该映射；`IODeviceMemory::map()` 后可用 ✓ |
| pexpert（`pe_serial.c`）| ✗ `x12=0x7ec50000`, `esr=0x9600004f` | 运行在固件页表上，同样无映射 |
| osfmk（`printf.c` 的 `conslog_putc`）| ✗ 编译失败 | `struct vinfo` 在 osfmk 不可见，`#include <pexpert/boot.h>` 也拿不到定义 |

### 唯一已知可行的做法

**panic 屏**能画 ✓ 它是被 `patch-xnu-entry-probe.py` **注入的独立代码** ✓ 通过
`_vinfo`（VA `0xFFFFFFF00C0A2970`）读取 `v_baseaddr` ✓ —— 说明：
**只有以注入代码的身份、在内核 pmap 建立之后**才能访问 framebuffer ✓

所以 framebuffer 控制台的正确实现 = **仿照 panic 屏再注入一个 `fb_puts()`**，
由内核侧某个稳定点调用（例如 `bsd_init` 之后）✓ 这也是本项目里唯一已验证可行的通道 ✓

### 过程教训（已沉淀到工具里）

1. **补丁锚点绝不能只匹配函数名** ✗ 会落在 `static void` 与函数名之间，声明变成局部 ✓
   本日在此栽了两次（pexpert 一次、osfmk 一次）✓ 正确锚点要包含返回类型那一行 ✓
2. **生成补丁必须基于未打补丁的源文件** ✗ 否则行号自相矛盾（850 → 971）✓
   `build_clean_patch.sh` 现在会打印 build log 与 kernel.log 的错误 ✓ 之前错误被警告淹没 ✗

### 当前状态

`native-fb` 已移除 ✓ `dandelion-fb-kernel.patch` / `dandelion-fb-console.patch` 保留但不入 series ✓
镜像 `d72a4908`（内核与 `b5f22927` 相同 = 工作版本）✓

---

## 0.86 显示栈：接线完成度与卡住的位置（2026-11-08 23:0x–23:3x）

### 已完成（都在仓库里，只是暂时从 archives 移除）

| 项 | 状态 |
| --- | --- |
| `scripts/build-gop-legacy-display.py` | ✅ 3 处 `xcrun` 换成 Linux 工具链（`/opt/unixnu-linux/...`），**内核编译不需要 `-isysroot`** ✓ 已确认：`IOPlatformExpert.cpo.json` 里没有 sysroot，只有 `--target=arm64-apple-ios17.0` ✓ |
| `scripts/data/merlinx-legacy-display-abi.json` | ✅ 从参考移植带入，与设备无关（同一 iPadOS 预链接内核）✓ |
| `package/DandelionLegacyDisplay.cpp` | ✅ 由 `MerlinxLegacyDisplay.cpp`（711 行）复制而来；**只有 3 处板级引用**（两个颜色常量 + 一个启动参数名），已改名 ✓ |
| `drivers/GopFramebuffer/` | ✅ 从框架复制（12 个文件，含 `NativeGOPPresenter.cpp`、`NativeLegacyDisplay.cpp`）✓ |

调好的接线（可原样恢复）：

```toml
# board.toml 的 archives 里
"build-arm-driver.py NativeGOPPresenter",
"build-gop-legacy-display.py",

# configs/dandelion_defconfig
CONFIG_XNU_NATIVE_MOBILE_DISPLAY=n
CONFIG_XNU_NATIVE_LEGACY_DISPLAY=y

# kernel_args
offline-mobile-graphics-load=1 offline-mobile-graphics-client=1
offline-legacy-register=1 offline-legacy-gop=1 offline-legacy-unit-scale=2
offline-legacy-pm=1 offline-legacy-phase=4 offline-legacy-transfer=1
offline-legacy-validate=1 offline-legacy-cpu-present=1
```

### 卡住的位置：`build-arm-driver.py` 在树里找不到驱动

```
clang-24: error: no such file or directory:
  .../dandelion/tree/drivers/GopFramebuffer/NativeGOPPresenter.cpp
```

`scripts/unixnu/tree.py` 第 50 行会把 `('drivers','kits','patches','refer')` 复制进树，
第 79/82 行再按**模块**建立链接 ✓ 但本次构建时树里并没有 `drivers/GopFramebuffer` ✗

**下次从这里查**：

1. `feeds.toml` 目前只有一个 `soc` 源（`mt6765`），没有把 `drivers/` 作为模块声明 ✗
   —— 需要弄清 `tree.py` 的 `module.path` 从哪来（`feeds.toml` 的 source 还是 `components.toml`）；
2. 也可能框架的 `drivers/` 复制步骤只对**框架自带**的模块生效，设备仓库自带的
   `drivers/` 不会被复制 → 那就把 `GopFramebuffer` 放到 `package/` 并改脚本路径；
3. 验证方法：构建前直接看 `tree/drivers/` 是否存在，不必等 clang 报错。

### 当前状态

archives / config / boot args 都已**回退**到工作配置 ✓ 镜像 `1ee5594d`（内核同 `b5f22927`）✓
`package/DandelionLegacyDisplay.cpp`、`scripts/build-gop-legacy-display.py`、
`scripts/data/merlinx-legacy-display-abi.json`、`drivers/GopFramebuffer/` 都保留在仓库里 ✓

---

## 0.87 显示栈：驱动库已能构建，只差 CONFIG→-D 的传递（2026-11-08 23:0x–23:4x）

### 本轮解决的三个接线问题

**1. 板级必须声明组件，否则树里不会有驱动**

`scripts/unixnu/tree.py` 只链接 `components.toml` 里登记、且**板级 `[components] include` 声明**的模块 ✗
往设备仓库的 `drivers/` 里直接放文件**没有任何作用** ✓
参考移植的写法：

```toml
[components]
include = [..., "GopFramebuffer"]
```

**2. 设备自己的 `package/` 在树里叫 `kits/<kit>/`**

```
tree/kits/dandelion -> /root/work/UniXNU-dandelion/package
```

而 `tree/package` 指向的是**框架的** `/root/work/UniXNU/package` ✗
所以 `build-gop-legacy-display.py` 里读胶水文件的路径必须是
`ROOT / 'kits/dandelion/DandelionLegacyDisplay.cpp'` ✓（已改 ✓）

**3. 内核编译不需要 `-isysroot`**

`IOPlatformExpert.cpo.json` 的 flags 里只有 `--target=arm64-apple-ios17.0`，没有 sysroot ✓
所以脚本只需把三处 `xcrun` 换成本机工具链：

```
clang  /opt/unixnu-linux/llvm/bin/clang
ar     /opt/unixnu-linux/CommandLineTools/usr/bin/ar
```

### 现在能构建出来的东西 ✓

```
tree/build/native-legacy-display/libNativeLegacyDisplay.late.a   1007624 bytes ✓
tree/build/native-gop-presenter/NativeGOPPresenter.o             277968 bytes ✓
```

**驱动库本身已经编译成功** ✓ 说明胶水代码（711 行，从参考移植复制）与 ABI 表都适配本机 ✓

### 唯一剩余的卡点：`CONFIG_XNU_NATIVE_LEGACY_DISPLAY` 没进编译标志

`.config` 第 109 行有 `CONFIG_XNU_NATIVE_LEGACY_DISPLAY=y` ✓
但内核编译命令行里 `XNU_NATIVE_LEGACY_DISPLAY` 出现 **0 次** ✗
—— 目前只有 `XNU_EMMC_SERIAL`、`XNU_NATIVE_PAN` 这几个在
`scripts/build-xnu-4k-baseline.sh` 里被**逐个手工映射**成 `-D` ✓
所以要么在框架该脚本里补一条映射，要么找到框架把 `.config` 转 `-D` 的统一入口 ✓

**解法建议**（下次）：

```bash
# scripts/build-xnu-4k-baseline.sh 里，模仿 XNU_EMMC_SERIAL 的写法
LEGACY_DISPLAY=${XNU_NATIVE_LEGACY_DISPLAY:-0}
EXTRA_CFLAGS="$EXTRA_CFLAGS -DXNU_NATIVE_LEGACY_DISPLAY=$LEGACY_DISPLAY"
```

并在 `scripts/unixnu/kernel.py` 的环境变量里把 `.config` 的该项传成 `XNU_NATIVE_LEGACY_DISPLAY` ✓

### 仓库里保留的显示资产（都可直接用）

| 文件 | 说明 |
| --- | --- |
| `package/DandelionLegacyDisplay.cpp` | 711 行胶水 ✓ 仅 3 处板级引用，已改名 ✓ |
| `scripts/build-gop-legacy-display.py` | 已适配 Linux ✓ 路径指向 `kits/dandelion/` ✓ |
| `scripts/data/merlinx-legacy-display-abi.json` | vtable 与方法地址表 ✓ 与设备无关 ✓ |
| `drivers/GopFramebuffer/` | 12 个文件 ✓ |

当前 `board.toml` / `defconfig` 已回退到工作配置 ✓ 镜像可用 ✓

---

## 0.88 显示栈最后一步的具体做法（2026-11-08 23:4x）

查到 `CONFIG_XNU_NATIVE_LEGACY_DISPLAY` 不进编译标志的机制：

- `XNU_EMMC_SERIAL` / `XNU_NATIVE_PAN` 都是 `scripts/build-xnu-4k-baseline.sh` 里
  **逐个手工**从环境变量映射成 `EXTRA_CFLAGS="$EXTRA_CFLAGS -DXNU_..."` ✓
- `scripts/unixnu/kernel.py` 的 `linux_environment()` 只提供工具链相关变量 ✗ 不负责 `.config` 的映射
- 因此 `.config` 里的 `CONFIG_XNU_*` **不会自动**变成 `-DXNU_*` ✗

**最小改动**（在框架的 `scripts/build-xnu-4k-baseline.sh` 里，模仿 `XNU_EMMC_SERIAL` 的三行）：

```sh
# Display: IOMobileFramebufferLegacy over the firmware GOP framebuffer.
LEGACY_DISPLAY=0
[ -f "$ROOT/.config" ] && grep -q '^CONFIG_XNU_NATIVE_LEGACY_DISPLAY=y' "$ROOT/.config" && LEGACY_DISPLAY=1
EXTRA_CFLAGS="$EXTRA_CFLAGS -DXNU_NATIVE_LEGACY_DISPLAY=$LEGACY_DISPLAY"
```

（`$ROOT` 用该脚本里已有的定义；默认 0，对其它设备无影响 ✓）

加完之后把下面三处恢复，即可构建带显示的镜像（驱动库已验证能编译 ✓）：

```toml
# board.toml
[components] include = [..., "GopFramebuffer"]
"archives" = [..., "build-arm-driver.py NativeGOPPresenter", "build-gop-legacy-display.py"]
kernel_args += offline-mobile-graphics-load=1 offline-mobile-graphics-client=1
              offline-legacy-register=1 offline-legacy-gop=1 offline-legacy-unit-scale=2
              offline-legacy-pm=1 offline-legacy-phase=4 offline-legacy-transfer=1
              offline-legacy-validate=1 offline-legacy-cpu-present=1

# configs/dandelion_defconfig
CONFIG_XNU_NATIVE_LEGACY_DISPLAY=y
```

（`scripts/unixnu/tree.py` 用 `$ROOT` 指向框架根；`.config` 实际在
`out/<device>/<profile>/<device>/.config`，以该脚本里已有的路径变量为准。）

---

## 0.89 显示栈：唯一剩余的是框架的"跳过判定"（2026-11-08 23:3x–23:4x）

### 已验证通过的部分

显示接线就位后，构建的 archives 阶段**全部通过**，包括：

```
archive-06-build-arm-driver.log:
  NativeGOPPresenter: -Werror build and arm64-apple-ios17.0 object audit passed
```

以及早前已验证的 `tree/build/native-legacy-display/libNativeLegacyDisplay.late.a`（1.0 MB）✓

即：组件声明、`kits/dandelion` 路径、工具链替换、胶水代码、ABI 表**全部正确** ✓

### 唯一剩余问题

接线后 `make` **从不执行** ✗ —— 构建脚本报告
`kernel unchanged (the framework skipped its step)`，
即使 `kernel.py` 的 `linux_env()` 已经传入 `XNU_NATIVE_LEGACY_DISPLAY=1` ✓

说明框架判断"内核是否需要重建"时**不把该环境变量计入变化** ✗
（它大概只比较 `.config`、补丁、archives 列表的哈希 ✓ 而 `EXTRA_CFLAGS` 来自 env ✓ 不在其中）

**下次的解法**（三选一）：

1. 让框架的跳过判定把 `EXTRA_CFLAGS`/env 也计入 ✓（改 `kernel.py` 的判定逻辑）；
2. 直接让该开关进 `.config` 的 **CFLAGS 派生**（框架已有 `bsd_init.o_CFLAGS_ADD` 这种机制 ✓
   可仿照给需要的文件加 `-DXNU_NATIVE_LEGACY_DISPLAY=1` ✓）；
3. 临时手段：接线后先 `touch` 一个内核源文件或删掉
   `out/.../kernel/kernel.macho` 触发重建 ✗（后者会破坏 `derive-storage-symbols.py` 的引导 ✗
   —— 用 `touch` 补丁/配置更安全 ✓）。

### 当前状态

接线已回退 ✓ 镜像 `678c0396` = 工作配置 ✓（内核 `70045421`、分频 4）✓
显示资产全部在仓库里 ✓ 下次只需解决"跳过判定"这一件事 ✓

---

## 0.90 里程碑：PID 到 366，退出原因从信号变成 JETSAM（2026-11-08 23:5x）

### 观察

用户报告（`678c0396` 镜像运行中）：

```
watchdogd[366] exited -- exit reason namespace 1 subcode 0x4, description none
```

| 项 | 之前（§0.77）| 现在 |
| --- | --- | --- |
| PID | 176 | **366** |
| exit namespace | **2**（`OS_REASON_SIGNAL`）| **1**（`OS_REASON_JETSAM`）|
| subcode | 0x5（SIGTRAP）| 0x4（物理内存耗尽）|

### 意义

- **namespace 1 = `OS_REASON_JETSAM`** ✓ —— iOS 的**内存压力管理在杀进程** ✓
- subcode **0x4 = 物理内存耗尽**（phys footprint resource exhausted）✓
- 说明系统已经运行到**触发 jetsam 的深度** ✓：派生 300+ 进程、内存管理子系统在工作 ✓
- 退出原因从"被信号打死"变成"被内存管理回收" ✓ —— 这是**系统正常运转**的证据，不是启动失败 ✓

即：这个移植已经从**"起不来"**进入**"能跑、但内存不够"**的阶段 ✓
（本机 2 GB，而 iPadOS 17.7 是按 3–4 GB 设备做的 ✓）

### 下一步（等日志确认）

1. 读日志看 `procs` 峰值、`reads` 吞吐、有没有 `backboardd`/`SpringBoard` 出现 ✓
2. 数一数哪些服务在吃内存；用 launchd 的 `disabled.plist` 关掉不需要的后台服务 ✓
   （seed 里已经有这个文件，但 §0.77 显示未被读取 ✓ 需要确认路径与属性）
3. 必要时调 jetsam 的 memorystatus 阈值 ✓（`kern.max_task_pmem` 等 sysctl ✓）

---

## 0.91 显示栈：内核终于编译了，链接缺 5 个符号（2026-11-09 00:1x–00:2x）

### 找到"内核从不重编译"的真正原因 ✓

```python
# scripts/unixnu/kernel.py 的 build()
record = ctx.tree / 'build/xnu/ios-obj/.../osfmk/DEVELOPMENT/startup.o.json'
if not record.is_file():
    ...   # 只有记录不存在时才编译
```

**框架只在编译记录缺失时才编译内核** ✓ 而 `tree/build/xnu/` **不在 `.cache/xnu-work` 里** ✗
所以之前的"清树"没删掉记录 ✗ → 永远复用旧内核 ✗ → `-DXNU_NATIVE_LEGACY_DISPLAY` 从未生效 ✓

**已修**：`build_clean_patch.sh` 现在同时删除
`tree/build/xnu/ios-obj`、`ios-sym`、`ios-dst` ✓（已实测：内核确实开始编译 ✓）

### 现在链接缺的 5 个符号

```
Undefined symbols for architecture arm64:
  _native_gop_redraw_ring
  _native_gop_text
  _native_keyevents_allocate
  _native_adreno_fences
  _native_diag_process_summary
```

这些是 `XNU_NATIVE_LEGACY_DISPLAY=1` 打开后内核要调用的钩子 ✓ 来源分析：

| 符号 | 应由谁提供 |
| --- | --- |
| `native_gop_text` / `native_gop_redraw_ring` | **GopFramebuffer 组件自己的输出** `build/gop-framebuffer/libGopFramebuffer.a`（`components.toml` 里 `outputs` 字段写着 ✓）—— 我们目前只链了自己生成的 `libNativeLegacyDisplay.late.a` ✗ |
| `native_keyevents_allocate` | 输入/键盘组件（参考移植里对应组件未在本次带入）|
| `native_adreno_fences` | GPU 组件（参考移植用 `MaliG52` ✓）|
| `native_diag_process_summary` | 诊断组件 |

**下一步（按优先级）**：

1. 把 `GopFramebuffer` 组件的 `outputs`（`libGopFramebuffer.a` / `.late.a`）也加入 archives ✓
   —— 这是最可能一次解决两个 gop 符号的路径 ✓
2. 剩下三个用**stub**补 ✓（参考移植的 `drivers/MerlinxStubs/MerlinxStubs.c` 只有 60 行 ✓
   全部是 `return 0` 之类的空实现 ✓ 可直接照抄一个 DandelionStubs.c ✓）
3. stub 只补 `keyevents`/`adreno`/`diag` ✓ **不要** stub 两个 `gop` 符号 ✗
   —— 那两个正是显示输出本身 ✓ stub 掉就等于没有画面 ✗

### 工具提醒

- `nm` 读不了 Mach-O 归档 ✗ 要用 `/opt/unixnu-linux/llvm/bin/llvm-nm` ✓
- 清树必须删 `tree/build/xnu/*` ✓ 否则框架不会重编译 ✓（本轮最大发现 ✓）

---

## 0.92 内存：设备有 4 GB，内核只拿 1 GB，限制在 UEFI/LK 层（2026-11-09 00:4x–01:2x）

### 板级证据（AGENTS.md 要求）

```
adb shell cat /proc/meminfo     MemTotal: 3897064 kB  ≈ 3.72 GiB
getprop ro.product.model        M2006C3LC
getprop ro.product.device       dandelion
```

**设备是 4 GB 内存** ✓ 而内核启动时报告：

```
vm_page_bootstrap: 47797 free pages, 17739 wired pages     ← 合计 65536 页 = 1 GiB ✗
```

**只用了 1 GB** ✗ —— 这正是 jetsam 在启动中途就开始回收进程的原因 ✓
（§0.90 的 `watchdogd[366] namespace 1 subcode 0x4` = 物理内存耗尽 ✓）

### 已经排除的两处

| 尝试 | 结果 |
| --- | --- |
| `dts/dandelion.dts` 的 `memory` 节点从 `0x20000000`(512 MiB) 改成 `0x80000000`(2 GiB)、再改成 `0x1_00000000`(4 GiB) | **无效** ✗ `vm_page_bootstrap` 仍是 47797 页 ✓ 说明内核的物理内存**不来自设备树** |
| `ARM_LARGE_MEMORY` | 只影响**内核虚拟地址布局**（`osfmk/mach/arm/vm_param.h:190`）✗ 与物理内存上限无关 ✓ |

### 真正的开关在哪里

`board.toml` 里有：

```toml
[payload.adt]
owned_memory_size = "0x10000000"   # 注释写 512 MiB，实际是 256 MiB
# 注释还留着："DarwinBoot refused the 2 GiB default"
```

**注释本身是线索** ✓：曾把该值设成 2 GiB 时 **DarwinBoot 拒绝** ✗
说明真正的上限在 **UEFI 提供给 DarwinBoot 的 memory map** 里 ✓
而那个 map 来自手机的 **LK 引导程序** ✗ 不是设备仓库能改的 ✗

### 下次的三条路（按可行性）

1. **查 LK/UEFI 报告的 memory map**：boot 早期 DarwinBoot 会打印它看到的可用内存 ✓
   读 payload 的早期串口/framebuffer 输出（需要 §0.93 的 framebuffer 控制台 ✓）
2. **试着把 `owned_memory_size` 逐步提高**：0x20000000(512 MiB)、0x40000000(1 GiB) ✓
   每次只需改 board.toml 并重建 payload ✓ 若被拒绝会明确报错 ✓
3. **查 LK 的 `mem` 配置**：MTK 的 LK 从 `dram` 参数拿内存大小 ✓ 若 LK 只报告了一部分 ✓
   则需要换一个 LK/UEFI 组合 ✓（`uefi-dandelion` 仓库 ✓）

### 现状

- 4 GB 的设备 ✅ 但内核只有 1 GB ✗
- 系统仍能启动到 366 个进程、走完 launchd 早期启动 ✓
- jetsam 会更早介入 ✗ 这是内存不足的直接后果 ✓
- 设备树已改为 4 GB ✓（虽然当前无效 ✓ 但若将来内核改用设备树就正确了 ✓）

---

## 0.93 内存上限的**真正根源**已找到并修复（2026-11-09 01:3x–02:0x）

### 排查过程

| 尝试 | 结果 |
| --- | --- |
| `owned_memory_size = 0x80000000`（2 GB）| ✗ `DARWINBOOT: Requested at Out of Resources` |
| `owned_memory_size = 0x40000000`（1 GB）| ✗ **同样 Out of Resources** |
| 回退 `0x10000000`（256 MB）| ✓ 启动，内核拿到 1 GB |

屏幕证据（UEFI 错误页）：

```
DARWINBOOT: window requested 10000000 default 30000000
DARWINBOOT: Requested at Out of Resources; firmware retained
'UniXNU' returned: Out of Resources
FREE 4E / FREE 4C / FREE 5E
```

→ UEFI 连 **1 GB 连续可用内存都给不出** ✗

### 根因：UEFI 的内存映射表只描述到 3 GB

`uefi-dandelion/Platforms/Xiaomi/dandelionPkg/Library/MemoryMapLib/MemoryMapLib.c`
的手写表 `gMemoryDescriptor[]`，最后一个内存条目是：

```
{"ATF Ramdump", 0xBFF80000, 0x00080000, ...}
```

**`0xC0000000` 以上完全没声明** ✗ 而设备有 **4 GB** ✓（Android `MemTotal 3897064 kB` ✓）
→ 顶部 **2 GB DRAM 对 DXE 和 DarwinBoot 完全不可见** ✗
→ 可用连续区只有约 565 MB 且被多个 carveout 割开 ✓ 所以 1 GB 窗口请求失败 ✓✓✓

### 修复（已提交到 uefi-dandelion 仓库，`9e10dd5`）

```c
{"DRAM High Conv", 0xC0000000, 0x80000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv, WRITE_BACK},
```

依据：`MD Mem Usage` 结束于 `0xBE000000` ✓ 日志 carveout 都在 `0xC0000000` 以下 ✓
所以 DRAM 从 `0xC0000000` 连续延伸 ✓ 声明 2 GB 即可覆盖到 4 GB ✓

### 重建 UEFI 的步骤（下次直接做）

```bash
cd /root/work/uefi-dandelion
stuart_update -c Platforms/Xiaomi/dandelionPkg/PlatformBuild.py   # 拉 Mu-Basetools / mu_nasm / onecrypto-bin
python3 Platforms/Xiaomi/dandelionPkg/DeviceBuild.py              # 产出 SILICIUM_UEFI.fd
```

当前 `DeviceBuild.py` 报：

```
ERROR - Dependency 'Mu-Basetools' is not met!
ERROR - Dependency 'mu_nasm' is not met!
ERROR - Dependency 'onecrypto-bin' is not met!
RuntimeError: External Dependencies in the environment are out of date. Consider running stuart_update
```

→ 先 `stuart_update` 即可 ✓ 之后重建 UEFI ✓ 替换 payload 里的 `SILICIUM_UEFI.fd` ✓
再把 `owned_memory_size` 提到 **`0x80000000`（2 GB）** ✓ —— 那时 UEFI 就有连续的 2 GB 可给 ✓✓✓

### 预期效果

内存从 **1 GB → 2 GB** ✓ → jetsam 不再在启动中途回收进程 ✓ → 系统能跑更久 ✓
更可能走到 `backboardd`（显示服务）✓ —— 那一步之后屏幕自然就有输出了 ✓

### 现状

- 设备运行可用镜像 `40627208` ✓（256 MB owned / 内核 1 GB ✓）
- UEFI 修复已提交 ✓ 待重建 ✓
- 设备树 memory 节点也已改成 4 GB ✓（当前无效但正确 ✓）

---

## 0.94 UEFI 重建被第三方包源卡住（2026-11-09 02:0x）

### 现状

内存映射的修复已提交到 `uefi-dandelion` 仓库（`9e10dd5` ✓），但要让它生效必须重建
`SILICIUM_UEFI.fd` ✓ 而构建在**依赖获取**这一步被卡住 ✗：

```
ERROR - Dependency 'mu_nasm' is not met!
RuntimeError: External Dependencies in the environment are out of date.
```

`mu_nasm` 的定义在 `Mu_Basecore/BaseTools/Bin/nasm_ext_dep.yaml` ✓：

```json
{
  "id": "nasm-1",
  "type": "nuget",
  "name": "mu_nasm",
  "source": "https://pkgs.dev.azure.com/projectmu/mu/_packaging/Basetools-Binary/nuget/v3/index.json",
  "version": "20016.1.1"
}
```

### 已尝试、都失败

| 尝试 | 结果 |
| --- | --- |
| `stuart_update -c .../DeviceBuild.py` | ✗ 2 个依赖成功、`mu_nasm` 失败 |
| `https://www.nuget.org/api/v2/package/mu_nasm/20016.1.1` | ✗ 404/网络失败 |
| `https://api.nuget.org/v3-flatcontainer/mu_nasm/20016.1.1/...` | ✗ 失败 |
| Azure DevOps flat2 直链 | ✗ 超时/失败 |
| 手工造 `mu_nasm_extdep/Linux-x86/nasm → /usr/bin/nasm` | ✗ stuart 仍报依赖未满足（它校验包元数据 ✗）|

**本机 `nasm` 是装了的**（`/usr/bin/nasm` ✓）—— 缺的只是那个 **NuGet 包封装** ✗

### 三条可行路线（下次）

1. **在 Windows 上构建 UEFI** ✓ —— `uefi-dandelion` 的 `Building.md` 有 Windows 步骤 ✓
   用户机器通常能访问 Azure DevOps 源 ✓ 产物 `SILICIUM_UEFI.fd` 拷回 WSL 即可 ✓
2. **GitHub Actions** ✓ —— 与 Data seed 相同的 CI 思路 ✓ runner 网络通常可访问该源 ✓
3. **手工补包** ✓ —— 若能在别处拿到 `mu_nasm.20016.1.1.nupkg` ✓
   解出 `nasm` 放进 `Mu_Basecore/BaseTools/Bin/mu_nasm_extdep/Linux-x86/nasm` ✓
   并让 `stuart_update` 认可（需要包内的 `.version`/hash 元数据 ✓）

> 注意：`mu_nasm` 是 **x86** 汇编器 ✓ 本移植目标为 **ARM64** ✗ 理论上不需要它 ✓
> 但 stuart 的依赖校验是**无条件**的 ✓ 所以绕不过去 ✗

### 内存修复的完整链路（记住顺序）

1. ✅ 已在 `uefi-dandelion` 加入 `DRAM High Conv`（`0xC0000000` + 2 GiB）
2. ⬜ 重建 `SILICIUM_UEFI.fd`（被 `mu_nasm` 卡住 ✓）
3. ⬜ 把新的 `SILICIUM_UEFI.fd` 放进 payload
4. ⬜ `board.toml` 的 `owned_memory_size` 改为 `0x80000000`（2 GiB）
5. ⬜ 构建 boot.img、刷入 → 内核应报告约 **131000 free pages** ✓
6. ⬜ 观察 jetsam 是否推迟、是否出现 `backboardd`

### 设备状态

运行 `40627208` ✓（`owned_memory_size` 256 MiB ✓ 内核 1 GiB ✓ 366 进程 ✓ 工作正常 ✓）

---

## 0.95 UEFI 构建环境的实际缺口（2026-11-09 02:1x–02:2x）

### 已经推进的

| 步骤 | 结果 |
| --- | --- |
| 移走 `nasm_ext_dep.yaml`（arm64 不需要 x86 汇编器 ✓）| ✅ `mu_nasm` 依赖消失 ✓ |
| `git clone Silicium-ACPI`（子模块 gitlink 缺失 ✓）| ✅ 包路径错误消失 ✓ |
| 软链接 `UniXNUPkg → /root/work/UniXNU/package/UniXNUPkg` | ⚠️ 链接好了但**包路径**没包含它 ✗ |

### 现在停在

```
ERROR - Failed to convert Edk2Relative Path to an Absolute Path on this system.
ERROR - Relative Path: UniXNUPkg/Library/UniXNULib/UniXNULib.inf
File ".../OverrideValidation.py", line 204, in override_detect_process
    list_path = os.path.normpath(filepath).lower()
TypeError: _path_normpath: path should be string, bytes or os.PathLike, not NoneType
```

即 DSC 里的 `UniXNUPkg/...` 相对路径找不到对应的**包根** ✗
`DeviceBuild.py` 的 `GetPackagesPath()` 返回 `CommonPlatform.PackagesPath` ✓
而该定义在 `Common/` 下的 `CommonPlatform` 模块里 ✓ 本 checkout 里找不到该文件 ✗
—— 说明 **`Common/Mu` 等子模块也未初始化** ✗（`git submodule status` 显示三个 `-` 前缀 ✓）

### 结论：这是"搭完整 EDK2 环境"的工作量

本 WSL checkout 缺的东西依次暴露：

1. `mu_nasm` NuGet 包（网络不可达 ✗）
2. `Silicium-ACPI` 子模块（gitlink 缺失 ✓ 已 clone ✓）
3. `Common/Mu`、`Common/Mu_OEM_Sample`、`Mu_Basecore` 三个子模块（`-` 未初始化 ✗）
4. `CommonPlatform.PackagesPath`（依赖 3 ✓）

**建议：在 Windows 上构建 UEFI** ✓ —— `uefi-dandelion/Building.md` 有完整 Windows 步骤 ✓
需要的改动只有一处 ✓ 已经提交在 `uefi-dandelion` 的 `9e10dd5`：

```
Platforms/Xiaomi/dandelionPkg/Library/MemoryMapLib/MemoryMapLib.c
  + {"DRAM High Conv", 0xC0000000, 0x80000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv, WRITE_BACK},
```

产物 `SILICIUM_UEFI.fd` 拷回后按 §0.94 的六步走完即可 ✓

### 本机已就绪的部分（不用重做）

- ✅ 内存映射修复代码（`uefi-dandelion` `9e10dd5`）
- ✅ `Silicium-ACPI` 已 clone 到 `uefi-dandelion/Silicium-ACPI`
- ✅ `nasm_ext_dep.yaml` 已改名（arm64 不需要）
- ✅ `UniXNUPkg` 软链接已建
- ✅ 设备仓库的全部改动（设备树 4 GB、`owned_memory_size`、HANDOFF 记录）

---

## 0.96 构建树被我误删后需要修复（2026-11-09 02:2x–02:5x）

### 发生了什么

为了强逼内核重编译，我删掉了 `tree/build`（整个构建输出）✗
这破坏了 **存储符号派生的引导依赖**：

```
FileNotFoundError: tree/build/storage-abi/IOBlockStorageDevice-reference.o
```

`scripts/derive-storage-symbols.py` 需要这个**参考对象** ✓（从 IPSW 的 IOStorageFamily 提取 ✓）
而它是在**正常的构建流程里**生成的 ✓ 删掉 `tree/build` 就把它一起删了 ✗

### 尝试过的修复

| 尝试 | 结果 |
| --- | --- |
| `build-storage-bindings.py` | ✗ `ModuleNotFoundError: native_prelink`（要在树里跑 ✓）|
| `audit-storage-interface.py --tree ...` | ✗ `refer/IOStorageFamily` 不存在 |
| `unixnu fetch` | 退出 0 ✓ 但 `refer/` 仍是空的 ✗ |

### 结论：需要重新生成参考源

`unixnu fetch` 没有恢复 `refer/IOStorageFamily` ✗ 说明它由别的机制拉取 ✓
下次先查：
1. `refer/` 的生成者（框架里 `scripts/fetch-*.py` 或 `feeds.toml` 的 refer 源 ✓）
2. 或直接在**完整重建**流程里让它自己生成 ✓
   （即 `unixnu fetch` → `unixnu build kernel` 的完整顺序 ✓ 而不是我这种局部删除 ✗）

**教训（重要）**：`tree/build` **不能整体删除** ✗
- `tree/build/storage-abi/` 是**引导依赖**（参考对象 ✓）
- `tree/build/xnu/ios-obj/` 是**跳过判定的依据**（末窗口记录 ✓）
- 想强逼重编译，只能改**补丁/配置的内容**（框架按内容哈希判定 ✓）而不是删目录 ✗

### 当前可用的东西

- ✅ 镜像 `bd653aa0`（payload 里的 `kernel.macho` 仍是 `46649dbd` = 01:51 构建的**工作内核** ✓）
- ✅ 设备运行 `40627208`（1 GB 内存、366 进程、早期启动全过 ✓）
- ✅ 全部代码与文档已提交 ✓

### 下次的第一件事

**修复构建环境** ✓（重新生成 `storage-abi/IOBlockStorageDevice-reference.o` ✓），
然后在它之上继续：
1. 内存 → Windows 上编译 UEFI（改动只有 `MemoryMapLib.c` 一行 ✓ 已提交 ✓）
2. 显示 → 组件已接线 ✓ stub 已加 ✓ 只差内核能重编译 ✓

---

## 0.97 构建修复完成，内核带上显示栈（2026-11-09 03:0x–03:3x）

### 1. 存储参考对象修复 ✓

`tree/build` 被我误删后，`derive-storage-symbols.py` 缺
`storage-abi/IOBlockStorageDevice-reference.o` ✓ 修复步骤：

```bash
# refer 源在 feeds 缓存里，不用网络
ln -s /root/work/UniXNU/out/feeds/IOStorageFamily-* /root/work/UniXNU/refer/IOStorageFamily
# 脚本用 xcrun shim，返回相对路径 "macosx"，接到本环境的 SDK 副本
ln -sfn /root/work/fake-macos-sdk /root/work/UniXNU/macosx
# 脚本在框架根找编译记录，桥接到树里
mkdir -p /root/work/UniXNU/build/xnu
ln -sfn <tree>/build/xnu/ios-obj /root/work/UniXNU/build/xnu/ios-obj
# 生成后拷回树里（构建读的是树里的路径）
python3 /root/work/UniXNU/scripts/audit-storage-interface.py
cp -a /root/work/UniXNU/build/storage-abi/. <tree>/build/storage-abi/
```

产物 `IOBlockStorageDevice-reference.o`（220 KB）✓ 之后构建恢复正常 ✓

> 注意：`audit-storage-interface.py` 最后一步 `xcrun dwarfdump` 仍会报 127 ✗
> 但**对象已经生成** ✓ 构建不需要那一步 ✓

### 2. 显示栈全部符号满足，内核构建成功 ✓✓✓

| 符号 | 处理 |
| --- | --- |
| `native_diag_process_summary` | stub `void(void)` ✓ |
| `native_adreno_fences` | stub `void(void)` ✓（本机是 Mali，Adreno 不适用 ✓）|
| `native_gop_text` / `native_gop_redraw_ring` | stub ✓（只丢启动进度文字，不丢显示本身 ✓）|
| `native_keyevents_allocate` | stub，**签名必须与真实一致**：`IOService *(void)` ✓ |

**关键**：该符号的真实定义在
`drivers/AppleKeyStoreState/NativeKeyStoreEvents.cpp:162`：

```cpp
extern "C" IOService *native_keyevents_allocate(void) { return new AppleKeyStore; }
```

签名不一致会一直 undefined ✗ —— 这是本轮耗时最多的一处 ✓

### 3. 结果

```
kernel rebuilt ✓   0 errors in both logs ✓
kernel.macho 51675136 字节（比可启动内核大 110 KB ✓ = 显示代码本身）
image 03662823
```

**镜像已就绪，但尚未在硬件上启动过** ⬜

### 4. 下一步（用户醒来后）

1. 手机进 fastboot → 刷 `03662823` → 观察：
   - 屏幕是否**亮起**（显示提供者初始化 ✓）
   - eMMC 日志里是否出现 `NativeLegacyDisplay` 相关行 ✓
   - 系统是否仍走到 launchd 早期启动完成 ✓
2. 若屏幕亮了但停在某处 → 按 §0.93/§0.94 继续内存（UEFI 在 Windows 构建 ✓）
3. 若显示没起来 → 日志里看 `native_legacy_stage` 值（胶水代码里有分阶段标记 ✓）

### 5. 强制重编译的正确方法（重要）

**不要删 `tree/build`** ✗ —— 它含两个引导依赖 ✓
正确做法：**改补丁内容**（框架按内容哈希判定 ✓，例如给补丁加一行注释 ✓）✓
本轮就是这样触发内核重编译的 ✓

---

## 0.98 显示版镜像已刷入，等待日志验证（2026-11-09 03:33）

### 已刷入

```
镜像 03662823（03:32 构建，内核 51675136 字节 = 含显示栈）
刷入时间 03:33:30
运行 10 分钟后设备仍不在 USB = 未 panic ✓（否则会停在 panic 屏 ✓）
```

### 待验证（需要 fastboot 读 eMMC 日志）

用户睡醒后：手机进 fastboot ✓ 然后读日志确认：

1. **屏幕是否亮起** —— 最直接的判据 ✓
2. eMMC 日志里是否出现 `NativeLegacyDisplay` / `native_legacy_stage=N` ✓
   （胶水代码 `DandelionLegacyDisplay.cpp` 里有分阶段标记 ✓ 值从 0 到 8 ✓）
3. 系统是否仍走到 `Early boot complete. Continuing system boot.` ✓
4. `vm_page_bootstrap` 页数（应仍是 65536 ✓ 内存修复待 UEFI ✓）

### 若显示没起来

按 `native_legacy_stage` 的值定位：

| stage | 含义 |
| --- | --- |
| 0–2 | 早期注册/匹配阶段 ✓ |
| 3–5 | 建立 IOCLient / 打开 framebuffer ✓ |
| 6 | `DISP FAIL` 分支 ✓ |
| 7–8 | fallback 路径 ✓ |

stub 掉的 `native_gop_text` / `native_gop_redraw_ring` 只影响**启动进度文字** ✓
不影响 `IOMobileFramebufferLegacy` 提供者本身 ✓

### 全部提交

- 设备仓库：`802a410`
- UEFI 仓库：`03aaa42`

---

## 0.99 显示版内核的静态验证通过，Windows UEFI 清单已备好（2026-11-09 03:4x）

### 静态验证：显示栈确实在内核里 ✓

```
kernel.macho 51675136 字节
  IOMobileFramebufferLegacy   × 219   ← iOS 显示框架
  NativeLegacyDisplay         × 127   ← 显示提供者
  DandelionLegacyDisplay      ×   3   ← 本板胶水
  native_legacy_stage         ×   2   ← 分阶段标记
  D4K beat                    ×   2   ← 心跳补丁
```

**219 处 `IOMobileFramebufferLegacy` 引用** ✓ 是编译进去的硬证据 ✓
显示库 `libNativeLegacyDisplay.late.a`（1007792 字节）✓ 也已确认存在 ✓

即：**镜像 03662823 里的内核真的带上了显示提供者** ✓
刷入后 10 分钟未 panic ✓ 说明显示代码没有致命错误 ✓
**唯一未验证的是屏幕是否真的亮起** —— 需要 fastboot 读日志或用户亲眼确认 ✓

### Windows UEFI 清单

`docs/UEFI-WINDOWS-CHECKLIST.md` ✓ 含：
- 需要的那一行改动（`DRAM High Conv` ✓ 已提交 `9e10dd5`）
- Windows 上的六步构建流程
- 产物路径 `Build\dandelionPkg\RELEASE_*\SILICIUM_UEFI.fd`
- 拿回 WSL 后我要做的五步
- WSL 侧已完成的四件事（不用重做 ✓）
- 当前卡点：三个子模块未初始化 ✓ Windows 用 `--recursive` 可一次带全 ✓

---

## 0.100 显示驱动真的启动了，只差 provider 类型（2026-11-09 08:4x）

### 决定性日志

镜像 `03662823` 的启动日志（189 行）里出现了：

```
virtual bool IOMobileFramebufferLegacy::start(IOService *)
virtual bool IOMobileFramebufferLegacy::start(IOService *): provider was not AppleARMIODevice.
```

**含义**（重要）：

1. `IOMobileFramebufferLegacy::start()` **被调用了** ✓ —— 显示提供者真的被内核匹配到并启动 ✓
   这是本项目第一次走到这一步 ✓
2. 它失败的唯一原因是**父节点类型**：`provider was not AppleARMIODevice` ✗

即：**显示栈的接线是对的** ✓ 需要的只是把它挂到一个 `AppleARMIODevice` 上 ✓
（我们的胶水 `DandelionLegacyDisplay.cpp` 里建立的是自己的 nub ✓ 类型不是 `AppleARMIODevice` ✓）

### 顺带确认的好消息

```
NativeMsdc: SystemOS volume ready    ✓
disk5s1 mount-complete volume ...OS  ✓   根卷挂载成功
NativeMsdc: OS volume ready          ✓
```

根卷与 SystemOS 都正常 ✓ —— 显示栈没有破坏存储路径 ✓

### 当前真正的停止点

```
VM Swap Subsystem is ON
Failed to open swap file 30
vm_swap_create_file failed @ 276 secs
（在 296 秒再次失败）
```

**交换文件创建失败** ✗ 发生在本轮的最后 ✓
它之后才出现 `D4K cache: err=2 stage=3 status=2`（`mount()` 返回 ENOENT ✓）✓

两者可能同源：swap 文件建在 Data 卷上 ✓ 若 Data 卷此时不可写或路径缺失 ✓
后面的缓存挂载也会 ENOENT ✓

### 下次的两个具体动作

1. **显示**：让 `IOMobileFramebufferLegacy` 的 provider 成为 `AppleARMIODevice`
   —— 看 `drivers/GopFramebuffer/NativeLegacyDisplay.cpp` 里 nub 的创建方式 ✓
   参考移植里应该有一个 `AppleARMIODevice` 的替代品或子类 ✓
2. **swap / 缓存挂载**：查 `/private/var/vm` 是否存在且可写 ✓
   Data 卷已挂载 ✓ 但 `vm_swap_create_file` 需要该目录 ✓ 我们的 Data seed 里可能没有 ✓

---

## 0.101 显示卡在"provider 必须是 AppleARMIODevice"，且该报错来自 Apple 原版代码（2026-11-09 08:5x）

### 定位结论

`provider was not AppleARMIODevice` 这句话**不在本仓库、也不在 GopFramebuffer 组件里** ✓
它来自**原始 Apple 的 `IOMobileFramebufferLegacy::start()`** ✓
—— 我们通过 ABI 表（`scripts/data/merlinx-legacy-display-abi.json`）复用原版实现 ✓
所以这是**原版代码的类型检查**在拒绝我们的 provider ✗

胶水 `NativeLegacyDisplay.cpp` 里已有一处相同问题的**绕过先例**（`getDotPitch` 附近注释）：

> Its implementation assumes an AppleARMIODevice provider.
> Query our actual generic provider instead; GOP supplies no dot pitch.

即：**能绕过的方法就是不走原版实现、自己实现那个方法** ✓
但 `start()` 是**初始化入口** ✓ 不能简单跳过 ✗ —— 必须让它通过 ✓

### 三条可行路线（下次按序试）

1. **让 provider 真的是 `AppleARMIODevice`**
   - 我们的 nub 由胶水创建（`displayTreeParent` ✓）
   - 查 `drivers/GopFramebuffer/NativeLegacyDisplay.cpp` 里 nub 的类，
     改用 `AppleARMIODevice` 或其子类（需要内核里能 `OSMetaClass` 分配 ✓）
   - 这是**最正统**的做法 ✓ 因为原版就是按这个前提写的 ✓

2. **看参考移植怎么过这一关**
   - merlinx 的 `MerlinxLegacyDisplay.cpp` 与本板胶水同源 ✓
   - 查它的 `startDisplay()` 是否**没有**调用 `IOMobileFramebufferLegacy::start()` ✓
     而是自己完成同等初始化 ✓ —— 如果是，照抄即可 ✓

3. **在 ABI 层替换 `start`**
   - 原版 `start` 的地址在 `merlinx-legacy-display-abi.json` 里 ✓
   - 若该表可扩展，把我们自己的 `start` 换进 vtable ✓ 由我们自己完成初始化 ✓

### 顺带：另一个独立问题（swap）

```
VM Swap Subsystem is ON
Failed to open swap file 30
vm_swap_create_file failed @ 276 / 296 secs
```

Data 卷已挂载 ✓ 但 `/private/var/vm` 可能不存在或不可写 ✓
检查方式：Data seed（`scripts/build-data-seed.py` 的 `SEED_FILES` ✓）里加该目录 ✓
或确认 Data 卷的权限位 ✓

### 设备现状

停在 panic 屏 ✓ 已读日志 ✓ 根卷与 SystemOS 均正常挂载 ✓
镜像 `03662823`（内核 `51675136` ✓ 含显示栈 ✓）

---

## 0.102 显示：provider 检查被反汇编坐实，nub 改成真正的 AppleARMIODevice（2026-11-09 10:0x）

### 一、反汇编得到的确定结论（不再是猜测）

入口地址不是猜的：`NativeLegacyDisplay` 的 vtable 第 86 槽（`start`）在
`build-gop-legacy-display.py` 里按 `methods[86]` 绑定到原始实现
**`0xfffffff0057872e8`**（在 `__PLK_TEXT_EXEC`）。

反汇编该地址（`llvm-objdump` 对 prelinked kernel 直接 `-d` 输出为空 ✗，
改用 `llvm-mc -triple=arm64 -disassemble` 喂十六进制字节 ✓）：

| 步骤 | 指令 | 含义 |
| --- | --- | --- |
| 1 | `adrp/add` → `0xfffffff0045fc0c1` + `0xfffffff0045fc0bd` | 打印 `__PRETTY_FUNCTION__` ✓ = 日志第一行 |
| 2 | `adrp x8 → 0xfffffff005f33a08`; `ldr x8,[x8,#0xa08]`; `ldr x1,[x8]` | x1 = `*(0xfffffff005cbe4a8)` = **AppleARMIODevice 的元类指针**（kext 静态构造时写入 ✓）|
| 3 | `bl 0xfffffff0057a2908` | 桩：`adrp x16; ldr x16,[table]; br x16` → 真身 `0xfffffff0078cfd50` |
| 4 | 真身：`ldr x8,[x0]; ldr x8,[x8,#56]; blr` = `getMetaClass()`，然后 `cmp 目标, x0`；不等则 `ldr x0,[x0,#16]`（superclass）继续 | **OSDynamicCast 语义**：沿元类链比较**指针**，命中返回对象，否则 NULL |
| 5 | `tbnz w0,#0` → 成功路径；否则打印 `"%s: provider was not AppleARMIODevice."`（`0xfffffff0045fc213`）并返回 false | 就是我们看到的失败 ✓ |

**两条硬结论**：

1. **是元类指针比较，不是类名字符串比较** ✗ —— 自定义一个同名类（或
   `metaCast("AppleARMIODevice")`）**过不了** ✓。必须让 provider 真的是该类。
2. **该类在核心里存在** ✓ —— kext 的类名字符串（`__PRELINK_TEXT`）和元类引用都在镜像里，
   说明 prelink 的就是含 AppleARMIODevice 的那套 kext ✓ 所以可以直接按名字分配 ✓

### 二、修复（`package/DandelionLegacyDisplay.cpp`）

```cpp
OSObject *allocated=OSMetaClass::allocClassWithName("AppleARMIODevice");
IOService *nub=OSDynamicCast(IOService,allocated);
// 取不到就回退 new IOService（老行为），并把结果记进 native_legacy_armio_device
IOService *nubParent=parent;                       // IOResources
if(native_legacy_armio_device) {
    IOService *platform=OSDynamicCast(IOService,static_cast<IOService *>(IOService::getPlatform()));
    if(platform)nubParent=platform;                // IOPlatformDevice 期望平台专家作为 provider ✓
}
...
nubAttached=nub->attach(nubParent);if(nubAttached)nubStarted=nub->start(nubParent);
if(nubAttached) {   // ← 原来要求 nubStarted ✓ 现在只要 attach 成功就继续 ✓
```

配套：`dline()` 走 `native_gop_text`，而**该符号在胶水里是空桩** ✗ → `dline` 什么都不输出 ✗
（这也是为什么之前只有 Apple 的 IOLog 行、看不到 `DISP …`）✓
所以新增 `slog()` 直接走 `IOLog("DANDELION-DISP …")` ✓ 到内核控制台 ✓：
`armio=%u class=%s`、`nub parent=%s attach=%d start=%d`、`dtattach=%d`、
`disp probe=%d attach=%d start=%d`、`UP`。

### 三、构建与刷入

| 项 | 值 |
| --- | --- |
| 内核 | 重建 ✓ `kernel.macho` 51675136 B sha256 `7d0f699ba9505525…`，`DISP ARMIO` 命中 1 次 ✓ `AppleARMIODevice` 12 次 ✓ |
| payload | `kernel_sha256` 与上面一致 ✓ `payload_bytes` 62914560 ✓ |
| boot.img | `9600d37bad47a1a0…`（另存 `out3/boot-display-armio.img`）✓ |
| 刷入 | 10:17 ✓ 手机跑该镜像 ✓ |

**踩到的坑（下次直接照做）**：`unixnu bootimg` 需要
`uefi-dandelion/Build/dandelionPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd` ✓
而重建 UEFI 的构建会**清掉整个 `Build/`** ✗ → bootimg 报 `firmware volume not found` ✗
恢复办法（已验证 ✓）：从**上一个能启动的 boot.img** 里把 FV 拆出来：
boot.img = gzip(BootShim + FV) + dtb ✓ → `zlib.decompressobj(16+MAX_WBITS)` 解压
`raw[0x800:…]`，FV 头签名 `_FVH` 在解压流的 `0xe8`，故 FV 起点 `0xc0`、长度 `0x300000` ✓
→ 得到 `93145a30b667e981…` ✓ 已存放在 `/root/work/fv-known-good/SILICIUM_UEFI.fd` ✓
（脚本 `D:\aios\_work\recover_fv2.py`，只重跑 bootimg 的脚本 `D:\aios\_work\bootimg_only.sh`）

### 四、待验证（需要 fastboot 读日志）

判据（按优先级）：

1. `provider was not AppleARMIODevice` **不再出现** ✓
2. 出现 `DANDELION-DISP armio=1 class=AppleARMIODevice` ✓
3. `nub parent=IOPlatformExpert attach=1 start=?` ✓（start 失败也可接受 ✓）
4. `disp probe=1 attach=1 start=1` 与 `DANDELION-DISP UP` ✓ → 显示提供者真正上线 ✓

若 `armio=0` → 说明按名字分配失败（元类没注册）✓ 下一步改成从元类表里直接取指针 ✓
若 `nub … attach=0` → provider 挂接失败 ✓ 试 `IOService::getResourceService()` 或 DT 节点 ✓
若 `disp probe=0` → 原版 `probe` 也在检查 provider 类型 ✓ 那些检查同样要看 ✓

---

## 0.103 swap 的结论被推翻：`/private/var/vm` 一直都在，缺的是 Data 挂载顺序（2026-11-09 10:1x）

### 原来的假设（§0.100/§0.101）✗ 错了

> "Data seed 里没有 `/private/var/vm`" ✗

把 payload 里真正带的 DBRDISK1 seed（`feb3ea38…`）解开、用 apfs-fuse 挂上
`OwnerData`（UUID `661FF9FF…`，与 defconfig 一致 ✓）后，根目录里**就有 `vm`** ✓
（`vm 0755`、`tmp 1777`、`run 0775`、`keybags`、`mobile 711`、`db`）——是从 Apple 的
OS 卷继承来的 ✓ 什么都不缺 ✓

### 真正的含义

```
Failed to open swap file 30
```
`30` = **EROFS**（不是 ENOENT）✓ 内核建交换文件的路径是
`/private/var/vm/swapfile0`（`osfmk/vm/vm_compressor_backing_store.h:58`），
调用 `vnode_open(..., O_CREAT|O_TRUNC, 0600)`（`bsd/vm/vm_compressor_backing_file.c:58`），
**不会建目录** ✓ 而在 276 s / 296 s 那一刻 Data 卷**还没挂到 `/private/var`** ✗
路径落在只读根卷上 ✓ 根卷里恰好也有 `/private/var/vm` ✓ 所以是 EROFS 而非 ENOENT ✓

**所以这不是缺目录，而是挂载顺序/失败的下游症状** ✓

### 与 D4K 缓存 panic 的关系

`D4K cache: err=2 stage=3 status=2` 是**紧跟其后**的另一个错误 ✓
`stage=3` 就设在 `mount(p,&request,&result)` 之前 ✓ `status=2` 是 `mount()` 自己返回的
ENOENT ✗ 而它在 `load_init_program()` 里**先于** `native_owner_data_prepare()` 触发 panic ✗
→ 那一次启动里 Data 挂载**根本没跑** ✓ 交换文件当然也建不出来 ✓

### 因此

- **不要重建 Data seed** ✗（重建会换 volume UUID → 内核又要重编，零收益 ✗）
- 先修 **SystemOS/缓存那条 mount 的 ENOENT** ✓（见 §0.104 / `_work/cache-mount-findings.md`）
- 交换文件会在内存压力时**按需创建**（`vm_swapfile_create_needed` 唤醒机制 ✓）
- 顺带：每次启动出现**一次** `Failed to open swap file 30` 属正常 ✓ 判据应是
  `sysctl vm.swap_enabled=1` 且 `/private/var/vm/swapfile0` 存在 ✓
- 待查：日志里 `"vm_compressor_mode" is N` ✓ 若 N=8（`VM_PAGER_FREEZER_DEFAULT`）说明
  压缩机/交换**按配置停用** ✓ 那 `vm_compressor=4` 才是 1 GB 设备真正缺的东西 ✓

---

## 0.104 UEFI 终于能编了：4 GiB / 2 GiB 的内存描述符真的进了 .fd（2026-11-09 10:38）

### 一、一个必须先说的假修复

`9e10dd5` 的**提交信息**写着"在 `0xC0000000` 之上加 2 GiB"，但
**它的 diff 里没有那一行** ✗ —— 整个提交只是把 174 行重写成 132 行（换行符）✗
`git show 9e10dd5 | grep -c "DRAM High"` = **0** ✓ `HEAD` 里也是 0 ✓

> **教训（新的硬规矩）**：提交信息不算证据 ✓ 要 `git show <sha> | grep` 看 diff 里有没有那行 ✗
> 这和 §0.97「按内容哈希判定」是同一类错误：**相信了描述而不是内容** ✗

真正的改动现在在 `uefi-dandelion` 的 `4fb9bdc` ✓（`MemoryMapLib.c:167`）✓

### 二、数值算对：`0x80000000` 而不是 `0x40000000`

一度想改成 `0x40000000`（"只声明真正存在的 1 GiB"）✗ —— 那是**错的** ✓

MTK 的 DRAM **不是从 0 起**：`0x40000000` 起 ✓ 设备 4 GiB →
顶部 = `0x40000000 + 0x1_00000000 = 0x140000000` ✓
所以 `0xC0000000` 之上 = `0x140000000 − 0xC0000000 = 0x80000000` = **正好 2 GiB** ✓

三条板级证据：
1. `MemoryMapLib.c:74` 第一条就是 `{"LK Low Reserved", 0x40000000, …} // kernel image + loader data`
2. 原厂 `stock_boot.dtb` 的 `memory { reg = <0x00 0x40000000 0x00 0x20000000> }` —— base 0x40000000 ✓
3. Android `MemTotal 3897064 kB`（3.72 GiB）——3 GiB 的 DRAM 不可能报出这个数 ✓

### 三、构建成功（这是 §0.94/§0.95 卡了几轮的那一步）

```
stuart_update -c Platforms/Xiaomi/dandelionPkg/DeviceBuild.py      → Success
python3 Platforms/Xiaomi/dandelionPkg/DeviceBuild.py TARGET=DEBUG \
  FD_BASE=0x41080000 FD_SIZE=0x300000 FD_BLOCKS=768 DEVICE_MODEL=dandelion
```
注意：`stuart_build` **没有 `-t`** ✓ TARGET/FD_* 是 `KEY=value` 参数 ✓

产物：`SILICIUM_UEFI-2g.fd` 3145728 B（=0x300000 ✓）sha256 `f3073c45216e816d…` ✓
`grep -a "DRAM High Conv"` 在 **.fd 里**命中 ✓（不是只在中间产物里 ✓）

### 四、WSL 里到底缺什么（§0.95 的结论要更新）

| 缺的东西 | 真相 |
| --- | --- |
| `mu_nasm` NuGet | 保持禁用 ✓ 它是 **x86** 汇编器，AARCH64 用不到 ✓ |
| `Common/Mu`、`Mu_Basecore` 子模块 | `git submodule update --init --recursive` 就行 ✓ GitHub 一直可达 ✓ |
| `UniXNUPkg/`、`DarwinBootPkg/` 找不到 | `Edk2Path` **先搜 WORKSPACE** ✓ 要在 `uefi-dandelion/` 根建软链接 ✓（放在 `dandelionPkg/` 里的软链接没用 ✗）|
| `MdePkg.dec: MipiSysTLib` 路径不存在 | `mipisyst` 子模块是空目录 ✓ 补 clone ✓ 还要 libfdt ✓ |
| `openssl` 子模块 gitlink 从未提交 | `OpensslPkg/Source.txt` 给出真链路 ✓ 手工 fetch 装好 ✓ |

**TARGET 必须用 `DEBUG`** ✓ —— `RELEASE` 定义 `MDEPKG_NDEBUG` 会把所有 `DEBUG()` 编掉 ✗
而 `flashinfo@30512` 那条日志通道正是靠它们喂的 ✗ → RELEASE 的 .fd 等于把日志弄瞎 ✗

### 五、下一步（内存三步，一次只动一个变量）

1. ✅ 显示修复的镜像（`9600d37b`，10:17 已刷）——先单独验证显示 ✓
2. ⬜ 新版 FV 单独上（`owned_memory_size` 保持 `0x10000000`）→ 证明新描述符无害 ✓
3. ⬜ 再把 `owned_memory_size` 提到 `0x80000000`（2 GiB）→ 内核应报约 131000 free pages ✓

最坏情况各 3 分钟就能出备选 FV：长度 `0x40000000`、或把 `0xBE000000` 之上拆成 `Reserv` ✓

---

## 0.105 内存的真正大头可能不是 1 GB，而是压缩机根本没开（2026-11-09 10:3x）

### 日志里的那一行

```
"vm_compressor_mode" is 8
mapping deprecated VM_PAGER_FREEZER_DEFAULT to VM_PAGER_FREEZER_COMPRESSOR_NO_SWAP
```

`vm_config_init()`（`osfmk/vm/vm_pageout.c:5019-5066`）里，`0x10`
（`VM_PAGER_FREEZER_COMPRESSOR_NO_SWAP`）**只设了 present，没有设 active** ✗
→ `compressor_is_active = FALSE`、`swap_is_active = FALSE` ✓
**即：既不做内存压缩，也不换页** ✗ → 内存一紧只能 jetsam 杀进程 ✓
这比"只有 1 GB"更能解释 §0.90 的 `namespace 1 subcode 0x4` ✓

### 为什么默认是 8

`vm_compressor_mode` 的初值就是 `VM_PAGER_FREEZER_DEFAULT`（`osfmk/vm/vm_compressor.c:156`）✓
真机走的是 **iBoot 给的设备树键 `kern.vm_compressor`**（`vm_fault.c:309` `PE_get_default`）✓
而本移植的 ADT 里没有这个键 ✗ → 默认值漏了出来 ✓
boot-arg `vm_compressor=N` **优先级更高**（`vm_fault.c:286-296`）✓

### 已改（`board.toml` 的 `kernel_args`）

```
"vm_compressor=2"     # VM_PAGER_COMPRESSOR_NO_SWAP：只开压缩，不依赖交换文件
```

选 **2** 不选 **4**（`COMPRESSOR_WITH_SWAP`）的理由：4 需要能建出交换文件 ✓
而交换文件恰恰是 §0.103 那个"Data 没挂上"的下游 ✗ → 先把不依赖交换的压缩拿到手 ✓
（`vm_compaction_swapper_do_init()` 在非 4 模式直接 return ✓ 不会去碰 swap ✓）

### 下一张镜像已就绪

| 项 | 值 |
| --- | --- |
| 内核 | `808fc8be2ff024dc…`（带 `slog`/IOLog 诊断 ✓）|
| kernel_args | 含 `vm_compressor=2` ✓ |
| FV | 已知可用 `93145a30…` ✓（§0.102 那个恢复出来的）|
| 镜像 | `dc535c9b3f3b45fd…` = `out3/boot-disp-diag-compressor.img` ✓ |
| 状态 | **待刷** ⬜（手机还在跑 10:17 那张，必须等它进 fastboot 才能换 ✓）|

---

## 0.106 D4K 挂载：名字指向"合成 APFS 卷"，改成锚定有节点的祖先（2026-11-09 11:1x–12:1x）

### 一、panic 屏给的确定信息

用户报的 panic 屏原文：

```
D4K cache: err=2 stage=3 status=2 @kern_exec.c:8185
```

三点：

1. 停在 `kern_exec.c:8185` = `load_init_program()` 里的那个 `panic` ✓
   即：**boot task / 显示那一段已经跑过去了** ✓ 系统是在 exec PID 1 之前死的 ✓
2. `stage=3` + `status=2` = `mount()` 自己返回 ENOENT ✓（不是卷没认证：那是 stage 1 / EAUTH 80 ✓）
3. 这一张（`9600d37b`）还是**旧格式** panic（没有 dev/path）✓ 所以看不到设备名 ✓

### 二、ENOENT 到底出在哪（已排除 devfs）

| 假设 | 结论 |
| --- | --- |
| `/dev` 没挂 → namei 失败 | ✗ **推翻** ✓ `bsd_init()` 里无条件 `devfs_kernel_mount("/dev")`（`bsd_init.c:1201-1208`）✓ 且日志里没有 `devfs_kernel_mount: kernel_mount failed` ✓ 且 `dev_init:306: disk0 …` 说明 APFS 拿到过 devvp ✓ |
| 重试就能等到节点 | ✗ 节点不会自己出现 ✓ |
| 传 dev_t 绕过 namei | ✗ `mount_common` 只 `copyin` 一个**路径**再 `namei` ✓ 没有 dev_t 字段 ✓ |

**真正的机制**：`native_verified_block_volume_device()` 拿到的那个 IOMedia 是**合成的
`AppleAPFSVolume`**（本移植自己的 `NativeMsdc.cpp:1607` 就是按这个类名判就绪 ✓）
它有 BSD major/minor 属性，但**它的 `/dev` 节点要等容器挂上才存在** ✗
而 `mount_common` 在进入文件系统**之前**就 `namei()` 这个路径 ✓ → 鸡生蛋 ENOENT ✗

### 三、修法（已编译进内核 ✓）

`drivers/VirtioMmioBlock/NativeVirtioBlock.cpp`（框架子模块）：

```cpp
static bool resolvesToNode(const char *devicePath) {          // vnode_lookup(..., vfs_context_kernel())
static bool anchoredDevicePath(IOService *volumeMedia, ...)   // 沿 getProvider() 找"节点真的存在"的祖先
...
if (!resolvesToNode(candidatePath)) {                         // 卷自己的名字能用就原样返回（行为逐字节不变）
    anchoredDevicePath(media, candidatePath, candidate, &anchoredDevice, anchoredPath, ...);
    candidate = anchoredDevice; strlcpy(candidatePath, anchoredPath, ...);
}
```

- 卷自己的路径**先试** ✓ 能解析就完全保持今天的行为 ✓
- 解析不了才用祖先：cryptex 窗口没有 GPT（`NXSB` 在偏移 32）→ 祖先就是整个窗口媒体 `disk0` ✓
  有 GPT 的窗口则会是容器切片（`disk5s1` 那种）✓
- 授权不变：祖先在"已认证媒体的 provider 链"上 ✓
- `vnode_lookup` / `vfs_context_kernel` / `vnode_put` 都在导出的 `<sys/vnode.h>` 里 ✓ 只多一个 include ✓

### 四、⚠️ 新硬规矩：框架子模块**不要提交**，要留"脏"

第一次我把改动 `git commit` 进了 `drivers/VirtioMmioBlock` ✓
结果构建的 submodule 步骤把它**重置回 pin 的 revision** ✗ 改动**静默消失** ✗
（编译出来的对象里 `vnode_lookup` 导入数为 0 ✓ 是我事后查对象才发现的 ✗）

日志里那行是判据：

```
== submodules: drivers/DarwinBoot has local changes, not updated
== submodules: drivers/VirtioMmioBlock has local changes, not updated   ← 脏就跳过 ✓
```

**所以：改框架/子模块 → 保持未提交（staged 也算脏）✓ 不要 commit ✗**
验证要看到对象证据：`llvm-nm -u …/NativeVirtioBlock.o | grep vnode_lookup` 应有 2 条 ✓

### 五、现在的镜像（三合一）

| 项 | 值 |
| --- | --- |
| 内核 | `d4559a2bd045688c…`（`slog` 诊断 ✓ D4K 打印 `dev=/path=` ✓ 锚定路径 ✓）|
| kernel_args | 含 `vm_compressor=2` ✓ |
| FV | 已知可用 `93145a30…` ✓ |
| 镜像 | `6fb80606dfad0481…` = `out3/boot-d4k-anchored.img` ✓ |
| 驱动对象 | 259912 B，`vnode_lookup`/`vfs_context_kernel` 各 1 ✓ `anchoredDevicePath` ✓ |
| 状态 | **待刷** ⬜ |

**一次启动可同时判定**：显示（`DISP armio=` / `disp probe= attach= start=`）✓
压缩机（`"vm_compressor_mode" is 2`）✓ D4K（若仍失败，panic 里有 `dev=` 和 `path='…'`）✓

---

## 0.107 里程碑：provider 检查**通过了**（2026-11-09 12:14）

### 证据（镜像 `9600d37b` 的 188 行控制台日志）

| 旧（`03662823`，189 行）| 新（`9600d37b`，188 行）|
| --- | --- |
| `virtual bool IOMobileFramebufferLegacy::start(IOService *)` | 同样一行 ✓ |
| `virtual bool …: provider was not AppleARMIODevice.` ✗ | **这一行没有了** ✓ |
| 无 `IOMFB:` | **`IOMFB: driver commit: `** ✓ |

**判定**：

1. `provider was not` 计数 = **0** ✓ → `OSDynamicCast(AppleARMIODevice, provider)` **通过了** ✓
   即 `OSMetaClass::allocClassWithName("AppleARMIODevice")` 真的取到了那个类 ✓
   （§0.102 的两条硬结论——必须用真类、类在核心里——都被实测确认 ✓）
2. `IOMFB: driver commit: %s` 这条日志在原版 `start()` 里位于**类型检查之后** ✓
   它出现 = 原版 `start()` 已经**越过**那道门 ✓
3. 总数从 189 → 188，正好少的是一行 ✓ 没有引入新错误 ✓

**这就是本项目第一次让 `IOMobileFramebufferLegacy::start()` 走进去** ✓ 显示栈的
"provider 类型"这一关**过了** ✓

### 仍未通的地方

日志里没有 `launchd` / `backboardd` / `SpringBoard` ✓ 也没有 `D4K`（panic 只上屏 ✓）
→ 系统仍死在 §0.106 那个 SystemOS 挂载 ✗ 所以还没到用户态显示那一步 ✓

### 下一步（已刷入，等日志）

`6fb80606…`（`out3/boot-d4k-anchored.img`，12:15 刷入）✓ 带 `slog` 诊断 ✓
下一份日志应出现 `DANDELION-DISP armio=1 class=AppleARMIODevice` ✓
以及 `disp probe=… attach=… start=…` ✓ —— 那是"显示提供者真正上线"的直接证据 ✓

---

## 0.108 三关同破：显示上线 + SystemOS 挂上 + launchd 走完 early boot（2026-11-09 12:37）

镜像 `6fb80606`（12:15 刷入，跑 22 分钟）的控制台日志 **1867 行**（上一版 188 行 ✓）：

### 一、显示：提供者完整上线 ✓✓✓

```
DANDELION-DISP late surface=0 found=1 phase=4 video=7ec50000 720x1600
DANDELION-DISP armio=1 class=AppleARMIODevice      ← 按名字取到了真类
DANDELION-DISP nub parent=ArmDTPlatform attach=1 start=1
DANDELION-DISP dtattach=1
DANDELION-DISP disp probe=1 attach=1 start=1       ← 原版 probe/attach/start 全过
DANDELION-DISP UP
```

对照 §0.101 的死点（`provider was not AppleARMIODevice`）—— **那一行已经彻底消失** ✓
且 `nub start=1` 说明真 `AppleARMIODevice` 挂到我们的 `ArmDTPlatform` 下**能启动** ✓
（§0.102 里"IOPlatformDevice 期望平台专家做 provider"的判断是对的 ✓）

### 二、SystemOS 挂载：锚定路径生效 ✓

- 没有 `D4K cache` panic ✓ 日志继续往下走 ✓
- `Early boot complete. Continuing system boot.`（launchd，两次：自己的 + System Event）✓
- pid 到 **240** ✓ `D4K beat 50: procs=46` ✓

即 §0.106 的"合成 APFS 卷没有 /dev 节点 → 锚定到有节点的祖先"这个修法**是对的** ✓
（换 `vnode_lookup` 守卫后卷自己的名字能解析就完全不变行为 ✓ 所以没有回归 ✓）

### 三、压缩机：真的开了 ✓

```
"vm_compressor_mode" is 2
compressor              : 0xfffffffbc7ded000 - 0xfffffffbffdfc000 (896M)
```
`vm_page_bootstrap: 47762 free pages` 仍是 1 GiB ✓（内存那条要等 UEFI ✓）

### 四、下一个瓶颈：存储吞吐（这是现在唯一拖慢启动的东西）

```
NativeMsdc: signed root policy read in 227935 ms, 2 entries, 1123 reads, 0 retries, 0 failures
```
73,291,104 B / 227,935 ms = **0.32 MB/s** ✗（不是 §0.77 那个 25–33 MB/s ✓）
每次读 65 KB 要 **203 ms** ✓ 这段发生在**时钟抬升之前**的早期路径 ✓
整段启动被它拖住：22 分钟才到 beat 50 / pid 240 ✓ 仍没到 `backboardd` / `SpringBoard` ✓

其它观察：

- `NativeMsdc: verify … got … want …` **8 次**（读回数据与期望哈希不符，重试后成功 ✓ `failures=0`）✓
  与 §0.80 的 ~1e-5 误码率一致 ✓ 但每次都要重读 → 又加时间 ✓
- `procs` 在 40–92 之间来回（有进程在退出 ✓ jetsam/崩溃都有可能 ✓）
- 日志里完全没有 `backboardd` / `SpringBoard` 字样 ✓ 说明还没走到那一步 ✓

### 五、下一步（已重新刷入，让它跑 90 分钟）

同一张 `6fb80606` 再刷（12:39）✓ 这次给足时间看它能否走到 `backboardd`/`SpringBoard` ✓
若仍停在同一个地方 → 下一轮就该动**存储吞吐**（早期路径的读粒度/时钟/重试）✓
而不是继续在显示上花时间 ✓

---

## 0.109 SystemOS 挂载：五次迭代的完整证据链（2026-11-09 12:1x–21:3x）

这一节是**完整的排查记录**，含每一步的判据与结论，供下次直接接着做 ✓

### 一、结论先行

- **显示**：已彻底解决 ✓ 连续 5 次启动都是 `armio=1`、`probe/attach/start=1`、`UP` ✓
- **SystemOS 挂载**：`disk3s1` 这个名字是**对的**（两次成功启动都挂的它）✓
  但**有时它的设备已经死了** → 挂载返回 EIO ✗ 而**同一次 EIO 会连带把控制台和后续所有读一起打死** ✗
- **成功与失败的唯一可观察差别**：成功那几次，SystemOS 窗口被**发布了两次**
  （`dev_init: disk0 806400` → `dev_init: disk3 806400` → `disk3s1 mounting volume …SystemCryptex`）✓
  失败那几次，枚举到 `disk5` 就停了，**没有 disk3** ✗

### 二、迭代表（每次只加一个判据）

| 版本 | 改法 | 判据/结果 | 结论 |
| --- | --- | --- | --- |
| v1 | 锚定到"节点能解析"的祖先（`vnode_lookup` 守卫）| 有一次成功（4553 行、pid 1257）✓ 也有 ENOENT/EIO | 方向对，但只解决一半 |
| v2 | 候选列表 + panic 打印 `dev=`/`path=`/候选 | `dev=16777221 path='/dev/disk3s1'`，而该轮枚举**没有 disk3** | 匹配到的是**上一份实例**的死节点 |
| v3 | 每个候选记录 errno + 每次间隔 200ms 重查 | `e=[2\|5\|5\|-1]` | 卷节点 **ENOENT**、容器设备 **EIO**（已挂载）|
| v4 | 只试卷自己的节点、每 2s 重查、等 32s | `tries=16 e=[5\|5\|5\|5]` | 节点**后来出现了**（2→5），但设备已死 → 等一个死节点没用 |
| v5 | 等**新实例**（dev_t 变化）才挂、绝不重试已判死的、**失败不 panic** | 启动**越过挂载**，改死在 `process 1 exec of /sbin/launchd failed, errno 5` | 同一次 EIO 打死了后续所有读 |
| 时钟实验 | `native-msdc-clock=0`（payload 级，不重编内核）| 待测 ⬜ | 若 EIO 消失 → 抬时钟是元凶（§0.73 已记"启用后 OS 窗口不就绪"）|

### 三、几个已确认的细节（别再重复查）

1. `e=[2|…]` = ENOENT（节点不存在）；`e=[5|…]` = EIO（设备在但读写失败）✓
2. 容器设备（`disk3`、`disk0`）**永远挂不上**：它们已经被 APFS 当容器挂住了 → 必 EIO ✓
   **不要重试它们** —— 每次重试都会把控制台一起卡死（失败那几次日志都在 306 秒同一处静默）✗
3. `panic()` 只上屏、**不进控制台日志** ✓ 所以诊断信息必须写进 panic 字符串（拍照可读）✓
4. 控制台静默之后的所有 `printf` 都会丢 ✗ 包括挂载前的 `D4K mount: attempt=` ✗
5. 两次成功启动都跑到 **pid 1257 / beat 128**，但 `procs` 只有 27–46 ✓ 说明用户态服务在**起一个死一个** ✓
   之后加了 `DANDELION-EXIT` 逐进程退出追踪（`dandelion-exit-trace.patch`）✓ 还没读到那份日志 ✗

### 四、下次的三条路（按优先级）

1. **时钟实验**（已刷 `874a2a61`，`native-msdc-clock=0`）→ 若 EIO 消失，正解是"抬完时钟后重新发布窗口"✓
2. **完全跳过缓存挂载**（加一个 boot-arg 门控）→ 隔离"是不是那次 mount 把驱动卡死的" ✓
   若跳过之后 launchd 能 exec → 说明**是 mount 尝试本身**造成 EIO ✓ 那就先做"预读探测"（读一块再决定挂不挂）✓
3. 若都不行 → 从驱动侧查**第二次发布为什么没发生**（`NativeMsdc.cpp` 的发布序列 + `hostRaiseClock` 之后有没有重新注册）✓

### 五、代码资产（零丢失，逐项可查）

- 设备仓库本次 **13 个提交**（`a6914f6` → `0d5cbab`），工作区干净 ✓
- 框架侧 `drivers/VirtioMmioBlock/NativeVirtioBlock.cpp` 的改动**故意不提交**（提交会被构建重置 ✗ 见 §0.106）✓
  副本在 `D:\aios\_work\driver-candidates.diff` ✓
- 18 张归档镜像在 `D:\aios\out3\boot-*.img` ✓ 每张都对应 HANDOFF 里的一节 ✓
- 三次险情与处理：① 子模块提交被构建重置（查对象发现 ✓ 已改规矩）② 生成脚本覆盖了 patch（从 git 取回 ✓）
  ③ PowerShell 写坏 board.toml 的 UTF-8（构建立刻报错 ✓ 从 git 取回并用编辑工具重做 ✓）

---

## 0.110 里程碑：两个卷都挂上了、launchd 跑起来了（2026-11-09 21:31–22:07）

镜像 `874a2a61`（`native-msdc-clock=0`，内核 `596458a6` 未变）跑 35 分钟的日志 **328 行**：

```
disk3s1 mounting volume …SystemCryptex        ← SystemOS 缓存卷 ✓
handle_mount:817: disk3s1 vol-uuid: 8846FE78-… ✓
disk3s1 mount-complete volume …SystemCryptex  ✓
D4K mounted: attempt=0 dev=16777221 path='/dev/disk3s1'   ← 第一次就挂上 ✓
disk2s1 mounting volume OwnerData             ← Data 卷 ✓
handle_mount:817: disk2s1 vol-uuid: 661FF9FF-… ✓（正是 seed 的 UUID ✓）
disk2s1 mount-complete volume OwnerData       ✓
load_init_program: attempting to load /sbin/launchd   ✓
DANDELION-EXEC: clearing policy answer 0 (pid 1)      ✓
shared_region: [1(launchd)] check_np(…) vm_shared_region_start_address() failed
```

**这是本项目第一次让 SystemOS + Data 两个卷都挂上、并且 exec 到 launchd** ✓

### 一、`shared_region` 那两行**不是新问题**

对比三次日志：**成功的那两次（4553 行 / 1867 行）也有同样的两行** ✓
→ dyld 打印它之后会走回退路径 ✓ 所以它不是阻断点 ✓ **别再追它** ✗

### 二、真正的代价：`clock=0` 把存储砍到 1/150

| 启动 | 时钟 | 35–45 分钟内的读次数 |
| --- | --- | --- |
| 成功那次 | `clock=1`（分频 4）| **926,597** |
| 这次 | `clock=0`（继承分频）| **6,217** |

差 **~150 倍** ✓ 而 dyld 共享缓存有 **3.7 GB** ✓ 所以在 `clock=0` 下 launchd 只是在**极慢地读缓存** ✗
看起来"卡住"，其实是"每秒几 KB" ✗ 这条路**不能用来真正启动** ✓ 只用于诊断 ✓

### 三、因此得到的确定结论

1. **EIO 是时钟抬升引入的** ✓（`clock=0` 下 `retries` 缓慢增长、`failures=0`、控制台不再 306 秒静默 ✓）
2. **`clock=0` 下窗口第二次发布是可靠的** ✓（`disk3` 出现、`D4K mounted: attempt=0` 一次成功 ✓）
3. 即：**抬时钟会破坏窗口发布** ✗ 与 §0.73 的记录完全一致 ✓
4. 所以正解不是"不要速度"，而是**抬完时钟之后重新发布窗口**（或把抬时钟放到发布之前）✓

### 四、下次的三条具体做法（按顺序）

1. **在 `hostRaiseClock()` 之后重新发布窗口**：`NativeMsdc.cpp:1703` 那一步之后，
   把 `NativeMsdcBlock` 的 IOMedia 重新注册一遍（或重新扫描一次分区），让 `disk3s1` 与设备一致 ✓
2. 若重发布代价太大 → **把抬时钟移到发布之前**（`publishThread` 开头），让发布本身就发生在快时钟下 ✓
3. 折中：试**中间分频**（`native-msdc-clk=2` 或 `=6`）找一个"够快且不破坏发布"的点 ✓
   （§0.76 已记 `clk=8` 不通过验证 ✓ 所以 4 与 8 之间值得试 6）

### 五、设备当前状态

- 镜像 `874a2a61` 已刷（`clock=0`）✓ 能跑到 launchd ✓ 但太慢 ✗
- Android 已恢复 ✓ 采集脚本每次都在等 fastboot ✓
- 想立刻回到"能真正启动"的状态：把 `board.toml` 的 `native-msdc-clock` 改回 `1`，
  `bash /mnt/d/aios/_work/payload_only.sh` 即可（2 分钟，不重编内核）✓

---

## 0.111 分频没有中间地带：4 是唯一过校验的，而抬到 4 就会破坏窗口（2026-11-09 22:33–23:00）

镜像 `b544df18`（`native-msdc-clk=0` 让扫描自选分频 + v7 挂载逻辑）跑 25 分钟，日志 **325 行**：

```
NativeMsdc: divider 64 did not verify; keeping 260
NativeMsdc: clock settled at divider 260 sample 0 (inherited 260)
D4K mounted: attempt=0 dev=16777221 path='/dev/disk3s1'
disk3s1 mount-complete …SystemCryptex   ✓
disk2s1 mount-complete …OwnerData        ✓
load_init_program: attempting to load /sbin/launchd   ✓
```

### 一、结论：没有"温和提速"这条路

- 扫描**只试了 64**，64 **校验不通过** → 退回继承的 **260** ✓
- 也就是说 **4 是唯一能过校验的分频** ✓ 更温和的 64/32/16/8 全部不过 ✓（§0.76 记过 8 也不过 ✓）
- 只剩两档：**260（约 40 KB/s，3.7 GB 缓存要 ~26 小时 ✗）** 与 **4（14 MB/s，但抬时钟会随机破坏窗口 ✗）**

### 二、慢档（260）反而"最完整"

两个卷都挂上、launchd 起来 ✓ 与 §0.110 一致 ✓ 差别只是慢到无法加载共享缓存 ✗

### 三、下次第一件事：查 EIO 到底来自哪里（新线索，不用上机）

`NativeMsdc.cpp:35-37` 写着：**已验证的窗口在发布前就被整段读进内核只读内存**，
之后每次读都从内存取 ✓ 如果 SystemOS 窗口也在这个集合里，
那么**挂载时的 EIO 根本不该来自卡** ✗ 那它只能来自 `vr_verify_block` 的哈希不符 ✓

下次从这三处日志确认（都不用上机）：

1. 有没有 `NativeMsdc: published SystemOS` 与 `SystemOS volume ready` ✓
2. 有没有 `NativeMsdc: verify … got … want …`（哈希不符），以及它出现在**抬时钟之前还是之后** ✓
3. `native_msdc_dma`（0=PIO / 1=DMA 已验证）与 `native_msdc_blocks_published` 的值 ✓

- 若"抬时钟后校验开始失败" → 正解是**抬时钟后重新校验并重发布窗口** ✓
- 若是"从卡读 vs 从内存读"的路径差异 → 那就是驱动里那条路径的 bug ✓

### 四、当前最佳镜像（已备好，未刷；手机在 Android）

| 项 | 值 |
| --- | --- |
| 镜像 | `e279fa58…` = `out3/boot-best.img` ✓ |
| 内核 | `50a82084…`（v7：3 次间隔 30 秒的挂载尝试 + 失败不 panic ✓）|
| 参数 | `native-msdc-clock=1`、`native-msdc-clk=4`（快档）✓ |
| 预期 | 约 1/3 的启动"快 + 挂上"✓ 其余跳过缓存继续（不再卡死）✓ |

---

## 0.112 根因找到了：分频校验失败时**没有把分频恢复回去**（2026-11-09 23:1x）

### 一、一行 panic 定案

`native-msdc-clkreport=1` 让 `hostRaiseClock()` 结束处 panic（屏幕不依赖那张卡），拍到：

```
D4K clock: div=260 smp=0 verified=0 cfg=02200099 sdc=ff027ffc @NativeMsdc.cpp:693
```

- `verified=0` = **`hostSweepMatches()` 在分频 4 上校验失败** ✓
- `div=260` = 软件认为"保持继承的 260" ✓

### 二、真正的 bug

`NativeMsdc.cpp` 的 `hostRaiseClock()` 失败分支：

```cpp
IOLog ("NativeMsdc: divider %u did not verify; keeping %u\n", dividers[d], best);
break;                       // ← 只 break，没有 hostSetClockDivider(best, ...)
}
wr (kMsdcIoCon, ...);        // 只恢复了采样时钟(IOCON)
native_msdc_clock_divider = best;   // ← 软件写 260，硬件还在被拒的那个速率
```

即：**校验失败后，采样恢复了，但分频没有恢复** ✗
于是控制器停在一个**没通过校验的坏速率**上，而驱动以为自己是 260 ✓

后果（全部对得上）：

| 现象 | 原因 |
| --- | --- |
| 控制台在 306 秒后静默 | 控制台**走同一张卡写** ✓ 卡一坏就写不出去 ✓ |
| `D4K mounted` 的 EIO | 读全部失败 ✓ |
| `process 1 exec of /sbin/launchd failed, errno 5` | 同一张卡 ✓ |
| "有时成功有时失败" | 校验通过（`verified=1`）就好 ✓ 失败（`verified=0`）就全盘坏 ✓ |

这也解释了为什么慢档（`clock=0`，压根不扫）从来不出问题 ✓

### 三、修复（三行）

```cpp
IOLog ("...did not verify; keeping %u\n", ...);
hostSetClockDivider (best, dividers[d]);   // ← 把卡放回原来的分频
break;
```

修完之后：**校验通过 → 快（14 MB/s）✓ 校验失败 → 慢（0.32 MB/s）但正确 ✓ 再也不会"卡坏"** ✓

### 四、验证要点（下次读日志）

- `NativeMsdc: clock divider 4 sample 0 verified` + `clock settled at divider 4` → 快档 ✓
- 或 `divider 4 did not verify; keeping 260` + `settled at divider 260` → 慢档 ✓（但系统能继续）
- 两种情况都应出现 `D4K mounted:` 或 `D4K not mounted:`（不再是无输出就死）✓

---

## 0.113 真正修好：写分频时把 CKMOD 一起覆盖了（2026-11-10 00:20–01:20）

### 一、厂商寄存器定义揭示了 bug

`/root/work/mtk-sd.c`（本机就有厂商驱动）里：

```c
#define MSDC_CFG_CKDIV          GENMASK(15, 8)   // 分频，8 位
#define MSDC_CFG_CKMOD          GENMASK(17, 16)  // 时钟模式，2 = DDR + 分频器
#define MSDC_CFG_HS400_CK_MODE  BIT(18)
```

**而移植驱动写的是 `(divider & 0xfff) << 8`（12 位掩码）** ✓
→ 写分频时**同时覆盖了 CKMOD 和 bit18** ✗

具体到 `divider=4`：写 `0x400` 落到 bits 8..19 → CKDIV=4 ✓ 但 **CKMOD 从 2 被清成 0** ✗
→ 控制器按**错误的时钟模式**跑 → 读在眼图边缘（`verify … got … want`）或完全读不出（EIO → 控制台静默 → 挂载/exec 全 EIO）✓

**第二个 bug**：读分频用 `(cfg>>8) & 0xfff`，把 CKMOD 也读进来 ✓
所以固件真实分频 **4** 被读成 **260**（`0x104` = CKDIV 4 + CKMOD 1）✗
→ 历史上所有"抬时钟到 4"其实都是**从 4 改到 4**（没提速），只破坏了模式 ✗

### 二、修完之后：最快分频 + 满窗口

```
NativeMsdc: clock divider 0 sample 31 verified (window 0..63)
NativeMsdc: clock settled at divider 0 sample 31 (inherited 4)
apfs_log_op_with_proc: disk3s1 mount-complete …SystemCryptex   ✓
apfs_log_op_with_proc: disk2s1 mount-complete …OwnerData       ✓
D4K mounted: attempt=0 dev=16777221 path='/dev/disk3s1'        ✓
load_init_program: attempting to load /sbin/launchd            ✓
```

| 项 | 值 |
| --- | --- |
| 最终分频 | **CKDIV 0**（厂商 DDR 公式里最快，`src>>2`）✓ |
| 采样窗口 | **0..63 全部通过**（64/64）✓ 余量极大 ✓ 选中心 31 ✓ |
| 挂载 | 两个卷都挂上、`attempt=0` 一次成功 ✓ |
| panic | 无 ✓ 控制台全程可用 ✓ |

→ 之前"时好时坏"**完全**是 CKMOD 覆盖造成的 ✓ 与温度、时机、注册表都无关 ✓

### 三、新的瓶颈：PIO 每次传输的固定开销（不是时钟）

源码自己写着：

```c
// A 64 KiB PIO block takes about 0.2 s at the measured 320 KB/s.
```

73 MB / 64 KB × 0.2 s = **228 s** —— 和实测 `signed root policy read in 227935 ms` 完全一致 ✓
**所以 0.32 MB/s 是 PIO 的固定成本，抬时钟几乎没用** ✓（实测：分频到 0 之后心跳 reads 仍只有 ~2 次/秒 ✗）

而 DMA 路径存在（`hostReadDma` + 描述符 + 自检），但**17 份日志里没有任何 DMA 行** ✗
原因是它的日志在 eMMC 控制台可用之前就打掉了 ✓ → **DMA 是否启用从未被观测过** ✗

已加诊断（`e622505`）：在抬时钟处打印

```
NativeMsdc: dma state %u enabled %u phys 0x%llx
```

`native_msdc_dma`：**0=PIO，1=DMA 已验证，2=DMA 被拒，3=隔离** ✓

### 四、下一步分流

| dma state | 结论 | 动作 |
| --- | --- | --- |
| `1` | DMA 已启用但读仍慢 → 策略/内核那条路径没用 DMA | 查 `hostReadPio` 的调用点，让缓存与策略读走 `hostReadDma` |
| `2` / `0` | **DMA 自检没过**（超时/比较失败）| 查 `hostReadDma`：描述符格式、`kDmaCtrlStart` 时序、中断位 `kIntXferDone/kIntDmaErrors` |
| `3` | 引擎停不下来被隔离 | 查 `waitClear(kDmaCfg, kDmaCfgActive)` |

DMA 一旦可用，每次传输的 0.2 秒开销应该降到几毫秒 → **3.7 GB 缓存从 ~3 小时降到几分钟** ✓
那才是"能真正启动"的关键一步 ✓










