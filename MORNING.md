# 项目现状总览（更新于 2026-11-07 01:3x）

## 1. 启动链当前到达的位置

固件交接（绿/蓝/品红色条 ✓）→ XNU 内核 → eMMC 驱动 → OS/SystemOS 双窗口校验
→ 真实 APFS 根卷挂载 → 根哈希认证 → `launchd` exec 通过 → **停在缺可写 Data 卷**：

```
load_init_program: attempting to load /sbin/launchd
DANDELION-EXEC: clearing policy answer 0 (pid 1)
Failed to open swap file 30          ← EROFS（只读文件系统）
```

`30` = EROFS（已核对 errno 表）；`shared_region ... vm_shared_region_start_address() failed`
是共享区为空时的正常返回，不是故障。

## 2. 日志与诊断

- 控制台窗口：flashinfo 分区起始，28,672 扇区（14 MB）；
- `powershell -ExecutionPolicy Bypass -File D:\aios\_work\collect_nowipe.ps1`
  （刷回 Android、不擦除、拉取并清空日志块）；
- 控制台在驱动启动后停止写入，**panic 屏 + 固件日志（+30,512）是可靠通道**；
- 诊断 panic 已退役（`patches/xnu/series` 中注释），避免以后误报。

## 3. GitHub 仓库私有化（已完成 3 个）

| 仓库 | 状态 |
|---|---|
| `licg89/UniXNU-dandelion` | ✅ **已私有** |
| `licg89/uefi-dandelion` | ✅ **已私有** |
| `licg89/UniXNU-mt6765-common` | ✅ **已私有** |
| `licg89/UniXNU` | ❌ 无法私有：它是 `UniXNU/UniXNU` 的 **fork**，GitHub 不允许 fork 改可见性；内容本来就是公开框架，无敏感信息。要彻底私有只能删掉 fork 或脱离 fork 关系（需 GitHub 支持）|

仓库已推送到远程 `main`（`9120aff` 及之后）。macOS Actions 目前被账户账单/额度拦住：
run 42256955170 的注释为 "recent account payments have failed or your spending limit needs
to be increased"。需要在 GitHub 的 Billing & plans 里处理，或改用本地 Linux 路径。

## 4. 环境状态

- **C: 66 GB 可用**（WSL 磁盘已迁到 D:）
- **D: 39 GB 可用**（已清理 11.9 GB 实验产物）
- 仓库：`76786aa` 及之前的 100+ 次提交，HANDOFF 到 §0.45

## 5. 昨晚确认的技术结论（都已写进 HANDOFF）

1. 控制台静默的根因：不是地址/分区/ADT/寄存器恢复问题，而是**控制台自己的命令序列**在卡进入过读状态后失效，三次重试后永久放弃（§0.45）；
2. AMFI 的 CT 判定是**数据判断**（`tst x27, mask` → `cset` → `and` → `cmp` → 分支），改数据比改控制流安全（§0.44）；
3. 我昨晚两次控制台改动（门控、从驱动调用 flush）都会引入新问题，已全部回退，仓库现在只保留能工作的配置。


## 6. 本机已验证的 Linux 路径（绕开 macOS 的进展）

`udif.py`（框架自带，Linux 可用）把 IPSW 的 `044-76040-038.dmg` 解成裸磁盘镜像
（6,077,580,800 字节），GPT 显示 APFS 容器在 0x4400（LBA 34）；切出后
`fsapfsinfo` 成功读出卷信息，UUID 正是根卷
`04769D26-2CA0-4825-B443-E2E5F88300F2`，卷名 `DawnUpdate21H461...OS` ✓。

**限制**：本机 `fsapfsmount` 编译时没有 FUSE 挂载支持（"No sub system to mount APFS
format"），且无 Python APFS 读库，所以**读不出 `/private/var` 的文件内容** ——
Data seed 仍需要 macOS 侧生成，或补一个能读 APFS 文件的工具。

## 7. 真实状态以 porting.toml 为准

`porting.toml` 已从复制的示例模板改为实测状态（14 个 area）：
configuration/firmware/cpu/memory = verified；storage/userspace/diagnostics =
in_progress；display/input/gpu/network/audio/power/packaging = planned。
完整证据链见 HANDOFF §0.1–§0.52。
