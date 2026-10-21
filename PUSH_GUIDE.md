# 推送指南

## 当前状态

所有代码已经提交到本地 git 仓库：
- ✅ 88 个文件已提交
- ✅ 12382 行新增代码
- ✅ 包括 GitHub Actions 工作流
- ✅ 包括 UEFI 移植框架
- ✅ 包括所有文档

## 手动推送到 GitHub

由于自动推送超时，请手动推送：

### 方法 1：使用 Git Bash
```bash
cd D:\UniXNU-dandelion
git push origin main
```

### 方法 2：使用 GitHub Desktop
1. 打开 GitHub Desktop
2. 选择 UniXNU-dandelion 仓库
3. 点击 "Push origin"

### 方法 3：使用 VS Code
1. 打开 VS Code
2. 进入源代码管理视图
3. 点击 "..." 菜单
4. 选择 "Push"

## 推送后的操作

推送成功后：

1. **查看 GitHub Actions**
   - 访问：https://github.com/licg89/UniXNU-dandelion/actions
   - 应该能看到 "Build UEFI for dandelion" 工作流开始运行
   - 等待构建完成（预计 10-20 分钟）

2. **查看构建结果**
   - 如果成功：下载构建产物（Artifacts）
   - 如果失败：查看构建日志，分析问题

3. **测试构建产物**
   - 下载 `dandelion-uefi-firmware` artifact
   - 包含 `SILICIUM_UEFI.fd` 和 `Mu-dandelion-0.img`
   - 可以尝试在设备上测试

## 可能的问题

### 认证问题
如果遇到认证错误：
```bash
# 使用 GitHub CLI
gh auth login

# 或使用 SSH
git remote set-url origin git@github.com:licg89/UniXNU-dandelion.git
```

### 大文件问题
如果推送很慢：
```bash
# 检查大文件
git rev-list --objects --all | git cat-file --batch-check='%(objecttype) %(objectname) %(objectsize) %(rest)' | sort --numeric-sort --key=3 | tail -n 10

# 考虑使用 Git LFS
git lfs install
```

### 网络问题
如果网络不稳定：
```bash
# 增加超时时间
git config http.lowSpeedLimit 0
git config http.lowSpeedTime 999999

# 或使用代理
git config http.proxy http://proxy.example.com:8080
```

## 下一步

推送成功后：

1. **等待 GitHub Actions 构建完成**
2. **下载并测试 UEFI 固件**
3. **如果有问题，查看构建日志并修复**
4. **继续开发 XNU 内核构建工作流**

---

**创建时间**: 2026-10-22  
**最后更新**: 2026-10-22
