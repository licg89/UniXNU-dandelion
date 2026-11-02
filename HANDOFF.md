# dandelion 移植交接（给另一台机器的 opencode）

最后更新：2026-11-02 14:30 (+08:00)。这份文件是**唯一的事实来源**，按它就能接着干。

---

## 0. 一句话现状

Redmi 9A（dandelion / MT6762G）已经**按 UniXNU 官方流程完整构建出可刷镜像并刷进手机跑起来**
（LK 接受 → 固件执行 → framebuffer 初始化），但**卡在 flashinfo 分区只有 16 MiB、新内核 51 MB 放不下**。
下一步固定为「先接日志通道，再缩内核」。

---

## 1. 仓库（全部在 GitHub，`licg89/` 名下）

| 仓库 | 作用 | 当前 HEAD |
|---|---|---|
| `UniXNU-dandelion` | 设备仓库（board.toml / dts / defconfig / CI） | 见远端 main |
| `UniXNU-mt6765-common` | SoC 仓库（soc.toml / dts/mt6765.dtsi / configs） | `97d9303c` |
| `uefi-dandelion` | 固件仓库（Mu-Silicium 基座 + 我们的 dandelionPkg/MT6765Pkg） | 见远端 main |
| `UniXNU`（fork） | 框架 fork：去掉了打不上的 `zzz-boot-progress.patch`；profile 去掉了 `early_security_init` | `1c436c6` |
| `dandelion-xnu` | **旧项目，仅参考**：硬件实测、DsI/触摸方案、原厂 boot 备份 | 私有 |

> 设备仓库现在是 **public**（GitHub Actions 私有仓库因计费被停，公开仓库免费）。

---

## 2. 构建环境（新机器照做）

### 需要
- WSL Ubuntu（**不要在 `/mnt/d` 上构建**，9p 太慢，慢 5–10 倍）
- 工具链：`/opt/unixnu-linux/llvm/bin/clang`（clang 24，由 `python3 scripts/linux/setup-toolchain.py` 编译，约 1–2 小时）
- `lzfse`（`/usr/local/bin/lzfse`，编译自 https://github.com/lzfse/lzfse）
- `dtc`

### 工作区布局（已就位在旧机器的 `~/work`）
```
~/work/UniXNU                  框架
~/work/UniXNU-dandelion        设备仓库
~/work/UniXNU-mt6765-common    SoC 仓库
~/work/uefi-dandelion          固件仓库
```

### 一条命令出镜像
```bash
cd ~/work/UniXNU-dandelion
export UNIXNU_UNIQUE_CHIP_ID=00002621
bash scripts/package.sh --os 17 --uefi ../uefi-dandelion
```
缓存好的情况下 **5–8 分钟**；产物在
`~/work/UniXNU/out/dandelion/ipados-17.7.11-21H461/dandelion/`：
- `kernel/kernel.macho`（51 MB）
- `payload/payload.img`（56 MiB）
- `boot/boot.img`（59.5 MB）

---

## 3. 已经跑通并验证的部分（不要重做）

| 步骤 | 证据 |
|---|---|
| 配置校验 | `== check done: 59 patches, configuration dandelion_defconfig` |
| 依赖 fetch | xnu `d8b80295118e`、libplatform、libdispatch、libpthread、corecrypto、IOStorageFamily 全自动 |
| IPSW | 框架自己从 Apple CDN 下 iPadOS 17.7.11 (21H461) 并解包 kernelcache/DeviceTree/trustcache |
| 内核编译 + 链接 | `mach.development.bcm2837` 产出（ArmCpu + ArmDTPlatform 组件） |
| prelink | `kernel.macho` 51 MB，sha256 `69c995b7…` |
| 固件 | `SILICIUM_UEFI.fd` 3 MiB（EDK2 全量编译通过） |
| payload / bootimg / verify | `boot.img` 59.5 MB，sha256 `34a29956…`，`step verify` 通过 |
| **真机执行** | 刷入后：厂商 logo → **灰屏**（我们的固件初始化了 framebuffer）→ 重启 |

---

## 4. 卡点（唯一的）

```
flashinfo 分区 = 0x1000000 = 16 MiB
新 kernel.macho = 51,003,392 字节（含 31 个预链接 Apple kext）
旧 kernel.macho = 14.8 MB（无预链接）→ 刚好装得下
```
ESP（flashinfo）装不下新内核，UniXNU 启动管理器找不到内核 → 重启。
**另外**：新固件里没有旧项目的日志通道，所以过程完全不可见（黑屏/灰屏只能猜）。

---

## 5. 下一步（顺序固定）

### C. 先接日志通道（半天）
把旧项目 `dandelion-xnu` 里的 `Platforms/Xiaomi/dandelionPkg/Drivers/PayloadBootDxe/` 移植进
`uefi-dandelion` 的 `Platforms/Xiaomi/dandelionPkg`，加进 `dandelion.dsc [Components]` 与
`dandelion.fdf` 的 FV。它会把固件日志写进 flashinfo 的 `DDNLOG01` 块（偏移 30512 扇区，128 扇区），
之后每轮都能：
```bash
# 回 Android 读
adb shell "dd if=/dev/block/mmcblk0p43 bs=512 skip=30512 count=128" > log.bin
```
旧项目里现成的读取脚本：`dandelion-xnu/bootshim/read_log.py`、`tools/round.ps1 collect-log`。

### B. 再缩内核进 16 MiB（半天）
在 `UniXNU` fork 的 `profiles/os/ipados-17.7.11-21H461/profile.toml` 里把
`"roots" = [31 个 kext]` 裁到必需项（建议先留：
`com.apple.kec.Libm`、`com.apple.kec.corecrypto`、`com.apple.security.sandbox`、
`com.apple.security.AppleImage4`、`com.apple.driver.AppleMobileFileIntegrity`、
`com.apple.kec.AppleMatch`、`com.apple.iokit.IOStorageFamily`、`com.apple.filesystems.apfs`），
重建后内核应降到 ~15 MB 以内。

### A（备选，1–2 天）
把内核/payload 放到 `userdata`（mmcblk0p41，**52 GB**），改固件去找它。

---

## 6. 一路上踩过并已修掉的坑（别再踩）

| 现象 | 根因 | 修法 |
|---|---|---|
| `--os ios-16.5-…: expected one of …` | 框架只有 ipados profile | 用 `--os 17` |
| `feeds.toml: Invalid statement` | Windows 写文件带 **BOM** | Python 以 `encoding='utf-8'` 重写 |
| `is an EXAMPLE device repository` | `example = true` | 改 `false` |
| `scripts/package.sh: Permission denied` | Windows 丢可执行位 | `git update-index --chmod=+x` |
| `couldn't find remote ref 97d9303` | feeds 用了缩写 SHA | 用完整 40 位 |
| `is not a Mu-Silicium checkout` | 固件仓库不是 Mu 树 | 以 `mouseos/uefi-a04br3` 为基座 + 我们的包 |
| `PACKAGES_PATH doesn't exist: Common/Mu` | Mu 的 submodule 没有 gitlink | clone 三个子模块后 `git add` 记录 gitlink |
| `error 000E: MT6765Pkg/MT6765Pkg.dsc.inc not found` | DSC include 路径 | 改成 `Silicon/MediaTek/MT6765Pkg/...` 并在 `[firmware] packages` 列出每个包目录 |
| `#if ENABLE_SECUREBOOT == 1` invalid token | DSC 缺 `DEFINE ENABLE_SECUREBOOT` | `!ifndef` + `DEFINE = 0`，**必须在 `!include` 之前** |
| `Private set but not used [-Werror]` | clang 24 比厂商代码预期严 | `[BuildOptions] *_CLANGPDB_*_CC_FLAGS = -D CLANG -D ENABLE_SECUREBOOT=$(ENABLE_SECUREBOOT) -fshort-wchar -Wno-error -Wno-unused-but-set-variable`（用 `=` 替换，不要用 `==` 追加，会和 .inc 冲突） |
| `sizeof (L"A") == 8` static assert | 缺 `-fshort-wchar` | 同上 |
| `_mockfs_vfsops` undefined（间歇性） | OS 片段把 `CONFIG_XNU_SOURCE_MOCKFS` 设为 y，defconfig 的注释形式不算覆盖 | defconfig 里写 `CONFIG_XNU_SOURCE_MOCKFS=n`（**值不能带行尾注释**） |
| `mockfs fixture archive missing` | 同上 | 保留 `build-mockfs-fixture.py` 在 `[kernel] archives` |
| `CPU affinity must fit a u32` | `/cpus` 下有无 `reg` 的 Linux 节点（cpu-map、idle-states） | 给它们加 `status = "disabled";`（不要删，删了 dtc 会报语法错） |
| `timebase-frequency requires one cell` | DTS 没有该属性 | `/cpus` 里加 `timebase-frequency = <0xc65d40>;`（13 MHz，单格） |
| `chip_id = "TODO"` | 模板占位符 | `chip_id = "6765"`（16 进制字符串） |
| `DTB image must not be empty` | `header_version = 2` 要求独立 `--dtb`，框架是把树追加进内核 | 固件配置 `header_version = 0`，`append_dtb = false` |
| `the payload files take 48 MiB; raise size_mib` | 容量必须严格大于文件 | `[payload] size_mib = 56` |
| `boot.img` 超过 64 MiB 分区 | payload 64 MiB + FD + shim | `size_mib = 56` → boot.img 59.5 MB ✓ |
| CI 全部失败（`job was not started … payments failed`） | GitHub 私有仓库 Actions 计费 | 设备仓库设为 **public**（免费额度） |

---

## 7. 硬件实测值（旧项目测出来的，直接用）

| 项 | 值 |
|---|---|
| SoC | MT6762G/MT6765，8×A53，Mali-G72 MP1 |
| 分区 | `boot`=mmcblk0p33（64 MiB）、`flashinfo`=p43（**16 MiB**）、`userdata`=p41（**52 GB**）、`super`=p38、`metadata`=p14、`lk`=p31 |
| 原厂 boot 备份 | `D:\aios\artifacts\boot_backup.img`（67,108,864 字节）——**恢复 Android 就刷它** |
| 内核窗口 | DarwinBoot 上限 **256 MiB**（2G/1G/512M 都 `out of resources`） |
| 链接基址 | `0xFFFFFFF007004000` |
| GIC | GICv3，GICD `0x0c000000`，GICR `0x0c100000`，INTID 27/30 都可投递 |
| 定时器 | 13 MHz；**CNTV/PPI 27（上游路径）→ 100 Hz 正常**；自写 CNTP 会造成 5.8 kHz 风暴 |
| 显示 | 720×1600，density 320，DSI0 `0x14014000`，stride 2944，fb `0x7ec50000`，LCM 偏压 TPS65132 |
| 触摸 | Novatek **NT36525B**（HELITAI），**`novatek,irq-gpio=329`**，SPI3 `0x11013000` |
| 系统 | LineageOS 20 / Android 13，`adb root` 可用（未 root 时 dmesg、/proc/partitions 全部拒绝） |

---

## 8. 刷机与回退

```powershell
# 刷新镜像
adb reboot bootloader
fastboot flash boot  out\unixnu-build\boot-48.img
fastboot reboot

# 回退到原厂 Android（必须记住）
fastboot flash boot  artifacts\boot_backup.img
fastboot reboot
```

---

## 9. 给 opencode 的第一条指令建议

> 读 `HANDOFF.md`（本文件）。先做第 5 节 C：把 `dandelion-xnu` 里的 `PayloadBootDxe` 移植进
> `uefi-dandelion/Platforms/Xiaomi/dandelionPkg`（DSC 的 `[Components]` + FDF 的 FV），
> 重建固件与 boot.img，刷机后按第 5 节的 dd 命令读 flashinfo 日志。
> 然后再做 B（裁 `roots` 让内核 < 16 MB）。
> 每一步都要把日志/证据贴出来，不要凭猜。
