# GitHub Actions 构建工作流

本项目使用 GitHub Actions 自动化构建 UEFI 固件和 XNU 内核。

## 📋 工作流列表

### 1. Build UEFI (build-uefi.yml)

**用途**: 构建 MT6765 UEFI 固件

**运行环境**: Ubuntu 22.04

**触发条件**:
- 推送到 `main` 或 `develop` 分支
- 修改了 `Mu-Silicium-ref/` 目录
- 手动触发 (workflow_dispatch)

**构建步骤**:
1. 安装系统依赖（nasm, iasl, gcc-aarch64-linux-gnu 等）
2. 安装 Python 依赖
3. 初始化 git 子模块
4. 运行 `build_uefi.py -d dandelion`
5. 上传构建产物

**输出**:
- `SILICIUM_UEFI.fd` - UEFI 固件
- `Mu-dandelion-0.img` - 完整的启动镜像
- 构建日志

### 2. Build XNU (build-xnu.yml)

**用途**: 构建 MT6765 的 XNU 内核

**运行环境**: macOS 14 (GitHub hosted runner)

**触发条件**:
- 推送到 `main` 或 `develop` 分支
- 修改了 `xnu/` 目录
- 手动触发

**状态**: ⚠️ 占位符 - 需要实现

**待完成**:
- [ ] 从 Apple 下载 XNU 源码
- [ ] 应用 MT6765 补丁
- [ ] 配置编译环境
- [ ] 生成 kernel.macho

**参考**:
- ios-sweet 项目使用类似的方法
- https://github.com/dipa-studio/ios-sweet

---

## 🚀 使用方法

### 手动触发构建

1. 进入 GitHub 仓库
2. 点击 "Actions" 标签
3. 选择要运行的工作流
4. 点击 "Run workflow"
5. 选择分支，点击 "Run workflow"

### 查看构建结果

1. 进入 "Actions" 标签
2. 点击对应的 workflow run
3. 查看构建日志
4. 下载构建产物 (Artifacts)

---

## 🔧 本地构建（备用方案）

如果 GitHub Actions 不可用，可以在本地构建：

### UEFI 构建

```bash
# 在 Linux 环境中
cd Mu-Silicium-ref
sudo ./setup_env.sh -p apt
pip3 install -r pip-requirements.txt
python3 build_uefi.py -d dandelion
```

### XNU 构建

```bash
# 在 macOS 环境中
# 参考 ios-sweet 的构建方法
cd xnu
# 具体步骤待实现
```

---

## 📦 构建产物

### UEFI 固件

位置: `Mu-Silicium-ref/Build/dandelionPkg/RELEASE_*/FV/SILICIUM_UEFI.fd`

用途:
- 刷入设备的 boot 分区
- 或打包成 boot.img 使用 `fastboot boot` 测试

### XNU 内核

位置: `xnu/out/kernel.macho`

用途:
- 与 ADT（Apple Device Tree）一起打包
- 由 DarwinBoot 加载

---

## 🐛 故障排查

### UEFI 构建失败

**常见问题**:

1. **依赖下载失败**
   - 检查网络连接
   - 尝试手动下载依赖
   - 或使用本地已构建的 BaseTools

2. **编译错误**
   - 检查 MT6765Pkg 配置
   - 验证硬件地址是否正确
   - 查看构建日志

3. **链接错误**
   - 检查库依赖
   - 验证符号定义

### XNU 构建失败

**常见问题**:

1. **Xcode 版本不兼容**
   - 使用 GitHub macOS runner
   - 或安装正确的 Xcode 版本

2. **源码下载失败**
   - 检查 Apple 开源网站
   - 验证版本号

---

## 📊 构建时间

| 工作流 | 预计时间 | 实际时间 |
|--------|----------|----------|
| UEFI | 10-20 分钟 | 待测试 |
| XNU | 30-60 分钟 | 待实现 |

---

## 🔗 相关资源

- **GitHub Actions 文档**: https://docs.github.com/en/actions
- **EDK2 构建指南**: https://github.com/tianocore/tianocore.github.io/wiki/Build-Instructions
- **ios-sweet 构建**: https://github.com/dipa-studio/ios-sweet
- **UniXNU 文档**: https://unixnu.com/guide/overview

---

**最后更新**: 2026-10-22  
**维护者**: UniXNU-dandelion 项目团队
