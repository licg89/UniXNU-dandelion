# Windows构建环境设置指南

## 问题

你需要ARM64交叉编译器来构建内核。

## 快速解决方案

### 方案1: 安装ARM GNU工具链（推荐）

#### 方法A: 使用winget（最简单）

```powershell
# 以管理员身份运行PowerShell
winget install ArmGnuToolchain
```

#### 方法B: 手动下载安装

1. 访问: https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-a/downloads

2. 下载: `gcc-arm-10.3-2021.07-win64-aarch64-none-elf.exe`

3. 运行安装程序，安装到 `C:\Program Files\GNU Arm Embedded Toolchain`

4. 添加到PATH:
```powershell
# PowerShell（管理员）
$env:Path += ";C:\Program Files\GNU Arm Embedded Toolchain\10.3-2021.07\bin"
```

#### 方法C: 使用Chocolatey

```powershell
# 安装Chocolatey（如果没有）
Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

# 安装ARM工具链
choco install gcc-arm-embedded
```

### 方案2: 使用MSYS2

1. 下载并安装MSYS2: https://www.msys2.org/

2. 运行MSYS2终端，执行:
```bash
pacman -Syu
pacman -S mingw-w64-x86_64-arm-none-eabi-gcc mingw-w64-x86_64-make
```

3. 添加到PATH:
```powershell
$env:Path += ";C:\msys64\mingw64\bin"
```

### 方案3: 使用WSL2（最完整）

```powershell
# PowerShell（管理员）
wsl --install

# 重启后，在WSL中安装工具
wsl
sudo apt update
sudo apt install gcc-aarch64-linux-gnu make
```

## 验证安装

安装完成后，验证工具是否可用:

```bash
aarch64-linux-gnu-gcc --version
# 或
aarch64-none-elf-gcc --version
```

应该显示版本信息。

## 构建项目

安装工具后，在项目目录执行:

```bash
# 构建测试内核
cd bootloader/test_kernel
./build_windows.bat

# 构建链式加载器
cd ../chainloader
./build_windows.bat
```

## 故障排除

### 问题1: 找不到编译器

**解决:** 确保安装并添加到PATH

### 问题2: 权限错误

**解决:** 以管理员身份运行

### 问题3: 链接错误

**解决:** 检查link.ld文件路径

## 替代方案

如果安装工具太复杂，可以考虑:

1. **使用GitHub Actions** - 推送代码自动编译
2. **借用Mac电脑** - 编译XNU
3. **寻找预编译内核** - 从社区获取

## 下一步

安装工具后，继续执行:
1. 构建测试内核
2. 构建链式加载器
3. 打包boot.img
4. 刷入设备测试

---

*安装完成后告诉我，我帮你继续！*
