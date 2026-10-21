# GitHub Actions编译指南

## 概述

使用GitHub Actions在云端编译，不需要本地安装工具，不占本地空间。

## 使用方法

### 1. 推送代码到GitHub

```bash
# 添加所有更改
git add .

# 提交
git commit -m "Add test kernel and chainloader"

# 推送到GitHub
git push origin main
```

### 2. 查看编译进度

访问: https://github.com/YOUR_USERNAME/UniXNU-dandelion/actions

你会看到:
- ✅ Build Chainloader & Test Kernel
- ✅ Verify Scripts

### 3. 下载编译产物

编译完成后（约2-3分钟）：

1. 点击成功的workflow run
2. 滚动到页面底部 "Artifacts" 部分
3. 下载你需要的文件:
   - `test_kernel` - 测试内核
   - `chainloader` - 链式加载器
   - `boot_img` - boot.img（已打包）
   - `all_binaries` - 所有文件

### 4. 刷入设备

```bash
# 解压下载的文件
cd ~/Downloads
unzip all_binaries.zip

# 刷入设备
fastboot flash boot xnu_chainloader.img
fastboot reboot
```

## 自动编译触发

以下操作会自动触发编译：
- 推送到 `main` 分支
- 推送到 `master` 分支
- 推送到 `develop` 分支
- 创建Pull Request
- 手动触发（Actions页面）

## 编译产物

### test_kernel.bin
- 位置: `bootloader/test_kernel/test_kernel.bin`
- 大小: ~5-10KB
- 用途: 测试链式加载器
- 加载地址: 0x40100000

### chainloader.bin
- 位置: `bootloader/chainloader/chainloader.bin`
- 大小: ~10-20KB
- 用途: 链式加载器
- 加载地址: 0x40000000

### xnu_chainloader.img
- 位置: 项目根目录
- 大小: ~50KB（包含header）
- 用途: 完整的boot.img，可以直接刷入
- 格式: Android boot image

## 手动触发编译

如果不想推送代码，可以手动触发：

1. 访问: https://github.com/YOUR_USERNAME/UniXNU-dandelion/actions
2. 点击左侧 "Build UniXNU"
3. 点击右侧 "Run workflow"
4. 选择分支，点击 "Run workflow"

## 编译时间

- 首次编译: ~3-5分钟
- 后续编译: ~2-3分钟（有缓存）

## 存储空间

- GitHub免费账户: 2GB存储空间
- 每个artifacts保留30天
- 自动清理旧artifacts

## 故障排除

### 问题1: 编译失败

**解决:**
1. 查看Actions日志
2. 检查错误信息
3. 修复代码后重新推送

### 问题2: 找不到artifacts

**解决:**
1. 等待编译完成
2. 检查是否滚动到页面底部
3. artifacts只在编译成功后显示

### 问题3: artifacts过期

**解决:**
1. artifacts保留30天
2. 过期后需要重新编译
3. 可以手动触发workflow

## 下一步

下载artifacts后：

1. **测试链式加载器**
   ```bash
   fastboot flash boot xnu_chainloader.img
   fastboot reboot
   ```

2. **观察结果**
   - 连接UART查看日志
   - 或观察设备行为

3. **调试问题**（如果有）
   - 查看日志
   - 修复问题
   - 重新编译

## 优势

✅ 不占本地空间  
✅ 不需要安装工具  
✅ 自动编译  
✅ 多平台支持  
✅ 版本控制  
✅ 团队协作  

---

*推送代码后，等待2-3分钟即可下载编译好的文件！*
