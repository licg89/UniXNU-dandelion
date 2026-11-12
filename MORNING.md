# 早上好 —— 2026-11-13 凌晨的工作结果

## 一、第一件事：请刷回可用镜像

手机现在跑着**会 panic 的内核** ✗（我凌晨试诊断时引入的），刷回即可：

1. **长按电源 10 秒**关机，再**电源 + 音量下**进 fastboot
2. 运行：
   ```powershell
   powershell -ExecutionPolicy Bypass -File D:\aios\_work\flash_ios.ps1 -Image D:\aios\out3\boot-good-0113.img
   ```
3. 约 10 分钟后屏幕上会有彩条 + RD 进度（内存 3.68 GB ✓ 存储 29 MB/s ✓）

详见 `D:\aios\_work\FLASH-ME.md` ✓

## 二、今晚拿到的最重要结论

### ✅ 你的判断完全正确：没有 GPU 驱动也能看到界面

框架自己的设备记录写得很明确（`docs/devices/oneplus13t.md`）：

> "The resource-only candidate **reached the system UI with normal basic touch**. Its GPU nodes remain
> disabled and were omitted from ADT, so this test provides no GPU power-up or rendering evidence."

> "board.toml selects no GPU or Metal component; **CPU framebuffer presentation stays active**."

**`GopFramebuffer` 就是"CPU 渲染进帧缓冲"那条路** ✓ 我们已经在用它 ✓ → **本机能出界面** ✓
我之前说"PowerVR 无驱动所以永远看不到"是**错的** ✗ 已收回 ✓

### ✅ SpringBoard 已能运行，死于一条 Swift 断言

`brk` 追踪（简单版 ✓ 有效 ✓）拿到的地址：

| PC | comment | 含义 |
| --- | --- | --- |
| `0x1c39224f8` | `0x1` | ★ **三个服务共同**：`brk #1` = Swift `assertionFailure` |
| `0x1a140b2ec` | `0x0` | `brk #0` = C 层 `__builtin_trap` |
| `0x100c58b0c` | `0x1` | SpringBoard **自身 text** 内的断言 |
| `0x1011a0b0c` | `0x1` | 同上 |

**三个不同服务死在同一个地址** ✓ → 同一条 Swift 前置条件 ✓

## 三、符号化的五次尝试（都记在 HANDOFF §0.130）

| 方法 | 结果 |
| --- | --- |
| `atos` | ❌ `cannot load symbols ... for architecture arm64` |
| grep `.symbols` | ❌ 那是**二进制表** ✗ |
| `nm -n <cache>` | ❌ `not recognized as a valid object file` |
| 自己解析 `dyld_cache_header` | ❌ `imagesCount = 0`（新版头部布局 ✗）|
| `dyld_info -segments` | ❌ `non-mach-o in fat file`（缓存分片 ✗）|
| dump 头部十六进制 | ⏳ 排队 15 分钟后失败 ✗（macOS runner 不稳 ✗）|

**唯一成功的是挂载** ✓（`hdiutil attach` → 53 个分片 ✓ `.symbols` 685 MB ✓）

## 四、GitHub Actions 已配置好

| 项 | 状态 |
| --- | --- |
| `gh` 认证 | ✅ `licg89` ✓ |
| 仓库 | `licg89/UniXNU-dandelion` ✓ 默认分支 `main` ✓ |
| workflow | `dyld-symbols.yml`（现名 "dyld cache header dump" ✓）|
| 辅助脚本 | `scripts/cache-images.py` ✓ `image-from-segments.py` ✓ `symbolize-addresses.py` ✓ |
| 最后一次 | `df9aaf1` ✓ 提交好了 ✓ 只是 runner 排队失败 ✗ |

**明天只需重跑一次** ✓：`gh workflow run dyld-symbols.yml` ✓ 它会 dump 缓存头部前 1 KB ✓
拿到布局后我就能给出正确的镜像表偏移 ✓ → **镜像名** ✓ → 断言位置 ✓

## 五、明天最有效率的一步

**恢复 `brk` 纯标量追踪 + CI dump 头部** ✓：

1. 重新应用 `dandelion-brk-trace.patch`（**只打 PC/LR/comment** ✓ 不读内存 ✓）
2. CI 重跑 dump ✓
3. 拿到镜像名 → 定位断言 → 修 → **SpringBoard 活下来 → CPU framebuffer 出界面** ✓

## 六、当前状态总览

| 项 | 值 |
| --- | --- |
| 仓库 | `bd93730` ✓ 已推送 ✓ 工作区干净 |
| HANDOFF | §0.130（约 7,000 行）✓ |
| porting.toml | 14 区域，memory/storage/display/diagnostics = verified ✓ |
| 内存 | 3.68 GB ✓ |
| 存储 | 29 MB/s ✓ |
| 启动深度 | SpringBoard 运行 ✓，死于 Swift 断言 ✗ |
| **UI 可行性** | ✅ **已确认可行**（CPU framebuffer ✓）|
