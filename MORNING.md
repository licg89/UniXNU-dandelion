# 早上好 —— 昨晚（02:30–03:00）做完的事和早上要做的三件事

## 1. 昨天刷入的镜像（已在手机上）

`D:\aios\out3\boot-ctflags.img`，sha256 `f11f45e4…`，包含两处改动：

| 改动 | 作用 |
|---|---|
| **控制台回退写者** | 内核 eMMC 控制台自己的 PIO 写序列在驱动跑过之后会失效；现在三次失败后把该块交给**驱动的传输路径**写（`native_msdc_console_sink`），不再永久静默 |
| **AMFI 数据级补丁** | `0xfffffff004ab6334` 的 `cset w8, eq` 改为 `mov w8, #0`，强制"有平台标志"，让 dyld 共享缓存通过 CT-policy 判定 |

## 2. 早上第一件事：一键检查

手机先 **长按电源 10 秒 → 音量下+电源** 进 fastboot，然后：

```powershell
powershell -ExecutionPolicy Bypass -File D:\aios\_work\morning-check.ps1
```

它会：刷回 Android boot（**不擦除**）→ 等 adb → `adb root` → 从设备绝对扇区 **122,139,919** 读控制台块 → 判定 `LOG IS FRESH` / `EMPTY` → 打印最后 40 行与关键标记。

### 结果怎么解读

| 输出 | 含义 | 下一步 |
|---|---|---|
| `LOG IS FRESH` + 没有 `unsuitable CT policy` | ✅ 两个修复都成了 | 看 `launchd` / `libSystem` / 是否出现 `/private/var` 等新阶段 |
| `LOG IS FRESH` + 仍有 `unsuitable CT policy` | 控制台修好，CT 补丁无效 | 按日志里的新证据换判定点 |
| `LOG IS EMPTY/STALE` | 控制台回退写者也没生效 | 改用固件日志 + panic 屏（放弃内核控制台）|

## 3. GitHub 仓库私有化（已完成 3 个）

| 仓库 | 状态 |
|---|---|
| `licg89/UniXNU-dandelion` | ✅ **已私有** |
| `licg89/uefi-dandelion` | ✅ **已私有** |
| `licg89/UniXNU-mt6765-common` | ✅ **已私有** |
| `licg89/UniXNU` | ❌ 无法私有：它是 `UniXNU/UniXNU` 的 **fork**，GitHub 不允许 fork 改可见性；内容本来就是公开框架，无敏感信息。要彻底私有只能删掉 fork 或脱离 fork 关系（需 GitHub 支持）|

私有仓库的 Actions 仍可用（每月 2000 分钟免费额度），`.github/workflows` 里的 CI 不受影响。

## 4. 环境状态

- **C: 66 GB 可用**（WSL 磁盘已迁到 D:）
- **D: 39 GB 可用**（已清理 11.9 GB 实验产物）
- 仓库：`76786aa` 及之前的 100+ 次提交，HANDOFF 到 §0.45

## 5. 昨晚确认的技术结论（都已写进 HANDOFF）

1. 控制台静默的根因：不是地址/分区/ADT/寄存器恢复问题，而是**控制台自己的命令序列**在卡进入过读状态后失效，三次重试后永久放弃（§0.45）；
2. AMFI 的 CT 判定是**数据判断**（`tst x27, mask` → `cset` → `and` → `cmp` → 分支），改数据比改控制流安全（§0.44）；
3. 我昨晚两次控制台改动（门控、从驱动调用 flush）都会引入新问题，已全部回退，仓库现在只保留能工作的配置。
