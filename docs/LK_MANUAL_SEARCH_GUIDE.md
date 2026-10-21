# LK Bootloader 源码手动搜索指南

## 🎯 快速搜索步骤

### 方法1: 浏览器搜索 (最快) ⭐⭐⭐⭐⭐

**直接在浏览器打开以下链接:**

1. **小米开源仓库**
   ```
   https://github.com/MiCode/Xiaomi_Kernel_OpenSource/tree/dandelion-q-oss
   ```
   - 查找: `bootloader/` 或 `lk/` 目录
   - 查找: `device/` 或 `platform/` 目录

2. **联发科开源**
   ```
   https://github.com/MediaTekMTK/mtk-open-source
   ```
   - 搜索 "MT6765" 或 "Helio G25"
   - 查找 "bootloader" 或 "lk"

3. **GitHub全局搜索**
   ```
   https://github.com/search?q=mt6765+lk+bootloader
   https://github.com/search?q=dandelion+littlekernel
   https://github.com/search?q=mediatek+lK+source
   ```

### 方法2: Git命令搜索 ⭐⭐⭐⭐

**在你的电脑上执行:**

```bash
# 创建搜索目录
mkdir ~/lk-search
cd ~/lk-search

# 克隆小米源码
git clone --depth 1 https://github.com/MiCode/Xiaomi_Kernel_OpenSource.git -b dandelion-q-oss

# 搜索LK
cd Xiaomi_Kernel_OpenSource
find . -type d -iname "*lk*" -o -type d -iname "*bootloader*"

# 如果找到LK目录
cd lk  # 或找到的目录
ls -la
```

### 方法3: 从设备提取 (需要root) ⭐⭐⭐⭐⭐

**如果你有root权限:**

```bash
# 1. 连接设备
adb devices

# 2. 提取boot分区
adb shell su -c "dd if=/dev/block/by-name/boot of=/sdcard/boot.img"
adb pull /sdcard/boot.img

# 3. 分析boot.img
# 在Linux上:
unpackbootimg -i boot.img
binwalk boot.img

# 在Windows上 (需要工具):
# 下载: https://github.com/osm0sis/android-bootimg-tools/releases
android-bootimg-tools boot.img
```

---

## 📋 关键搜索关键词

### GitHub搜索
```
mt6765 lk
mt6765 bootloader
dandelion lk
dandelion bootloader
mediatek littlekernel
redmi 9a lk source
mtk lk mt6765
helio g25 bootloader
```

### 文件搜索 (在源码中)
```bash
# 搜索LK相关文件
find . -name "*lk*" -type d
find . -name "*bootloader*" -type d
find . -name "*littlekernel*" -type d

# 搜索MT6765相关
find . -name "*mt6765*"
find . -name "*dandelion*"

# 搜索内核加载代码
grep -r "boot_linux" .
grep -r "load_kernel" .
```

---

## 🔍 找到LK后做什么?

### 1. 检查目录结构
```bash
cd lk  # 或找到的LK目录
tree -L 2  # 查看结构
```

**典型结构:**
```
lk/
├── app/          # 应用程序 (重要!)
│   └── aboot/    # Android boot (关键!)
├── arch/         # 架构代码
│   └── arm/
├── platform/     # 平台代码 (重要!)
│   └── mt6765/   # MT6765特定
├── dev/          # 设备驱动
└── kernel/       # 内核
```

### 2. 查找关键文件
```bash
# 内核加载代码
find . -name "aboot.c"
find . -name "*boot*.c"

# 平台初始化
find . -path "*/mt6765/*" -name "*.c"

# 启动汇编
find . -path "*/arm64/*" -name "*.s"
```

### 3. 分析代码
```bash
# 查看内核加载函数
grep -A 20 "void boot_linux" app/aboot/aboot.c

# 查看平台初始化
grep -A 20 "platform_init" platform/mt6765/platform.c
```

---

## 📊 搜索进度追踪

### 检查清单

- [ ] 搜索小米开源仓库
- [ ] 搜索联发科开源仓库
- [ ] GitHub全局搜索
- [ ] 搜索XDA论坛
- [ ] 搜索4PDA (俄语网站)
- [ ] 从设备提取boot.img (如有root)
- [ ] 分析找到的代码
- [ ] 确定修改方案

### 进度记录

**日期**: 2026-10-21

| 来源 | 状态 | 结果 | 备注 |
|------|------|------|------|
| 小米开源 | 🔍 搜索中 | 待确认 | dandelion-q-oss分支 |
| MTK开源 | 🔍 搜索中 | 待确认 | |
| GitHub搜索 | ⏳ 待执行 | - | |
| 设备提取 | ⏳ 待执行 | - | 需要root |

---

## 🛠️ 实用工具

### Linux工具
```bash
# 安装工具
sudo apt-get install git binwalk

# Python工具
pip install unpackbootimg

# 分析工具
sudo apt-get install ghidra  # 反编译器
```

### Windows工具
- **Git**: https://git-scm.com/download/win
- **Binwalk**: 需要WSL或Cygwin
- **Android Bootimg Tools**: https://github.com/osm0sis/android-bootimg-tools

### 在线工具
- **GitHub搜索**: https://github.com/search
- **Google搜索**: 搜索 "site:github.com mt6765 lk"
- **XDA搜索**: https://forum.xda-developers.com/search/

---

## 💡 搜索技巧

### 1. 使用Google高级搜索
```
site:github.com "mt6765" "lk" bootloader
site:github.com "dandelion" "littlekernel"
site:forum.xda-developers.com "redmi 9a" bootloader
```

### 2. 搜索变体
LK的别名:
- littlekernel
- little kernel
- lk bootloader
- mtk bootloader
- preloader (MTK早期bootloader)

### 3. 搜索相关设备
同SoC的其他设备可能也有开源:
```
mt6765 lk
mt6762 lk  # 类似SoC
helio g25 lk
helio g35 lk
```

### 4. 检查历史版本
```bash
# 查看所有分支
git branch -a

# 查看所有标签
git tag

# 搜索特定提交
git log --all --grep="lk"
git log --all --grep="bootloader"
```

---

## 📞 获取帮助

### 论坛
- **XDA Developers**: https://forum.xda-developers.com/
  - 搜索 "Redmi 9A bootloader"
  - 发帖询问

- **4PDA**: https://4pda.to/
  - 俄语,但有大量MTK资料
  - 使用Google翻译

- **MediaTek社区**: https://community.mediatek.com/
  - 官方论坛
  - 可以提问

### 社交媒体
- **Telegram**: 搜索 "Redmi 9A" 或 "MT6765" 群组
- **Discord**: 搜索 Android开发服务器

---

## ⚠️ 注意事项

### 法律
- 确保遵守开源协议
- 不要传播专有代码
- 尊重知识产权

### 安全
- 不要刷写未经验证的bootloader
- 备份原始boot.img
- 有变砖风险

### 技术
- LK可能不完整
- 需要ARM64交叉编译器
- 调试需要UART

---

## 📝 下一步

### 立即
1. ✅ 阅读本指南
2. ⏳ 执行搜索命令
3. ⏳ 记录搜索结果

### 找到LK后
1. 分析代码结构
2. 查找内核加载代码
3. 确定修改方案
4. 准备开发环境

### 如果找不到
1. 尝试从设备提取
2. 寻求社区帮助
3. 考虑替代方案 (链式加载)
4. 等待更多开源代码

---

**更新时间**: 2026-10-21  
**状态**: 📋 准备搜索  
**预计时间**: 1-2天
