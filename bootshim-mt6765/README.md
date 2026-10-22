# BootShim for MT6765 (Redmi 9A / dandelion)

基于 [mouseos/xnu_bootshim_benesse_a04br3](https://github.com/mouseos/xnu_bootshim_benesse_a04br3) 项目适配的MT6765版本。

## 关键修改

- **UART地址**: 0x11002000 (MT6765 UART0)
- **CPU**: Cortex-A53 (8核心)
- **Framebuffer**: 0x48000000 (待验证)
- **分辨率**: 720x1600
- **Magic**: DANIEL01 (Dandelion iOS Loader)

## 启动流程

```
MTK Preloader → LK → BootShim → XNU
```

BootShim是一个位置无关的ARM64 payload，由MTK LK启动，负责：
1. 输出诊断信息到UART
2. 解析LK提供的FDT（设备树）
3. 创建Apple boot arguments和ADT
4. 跳转到XNU内核入口点（EL1）

## 编译

```bash
# 需要aarch64-linux-gnu工具链
./build.sh
```

输出文件在 `../out/bootshim/`：
- `Image` - 编译后的bootshim二进制
- `build.json` - 构建信息
- `symbols.txt` - 符号表

## 打包boot.img

使用device仓库的打包工具（需要创建）：

```bash
python3 tools/build.py --bootshim ../out/bootshim --out out/diagnostic
```

## MTK Boot格式

- Android boot header v0
- page size: 2048
- bootopt=64S3,32N2,64N2 (offset 0x40)
- 512字节section header (magic: 0x58881688)
- Kernel payload: gzip(shim + DTB)

## UART验证

成功启动应该看到以下诊断消息：

1. `XNU_DANDELION_SHIM_DIAGNOSTIC_V1`
2. CPU寄存器值（CurrentEL, MIDR_EL1等）
3. `SHIM_DIAGNOSTIC_DONE`
4. `XNU_DANDELION_EL1_DIAGNOSTIC_V2`
5. FDT信息（memory, reserved ranges）
6. `SHIM_EL1_DIAGNOSTIC_DONE`

## 状态

- ✅ 代码已适配MT6765
- ⏳ 需要编译测试
- ⏳ 需要UART验证
- ⏳ Framebuffer地址待验证

## 参考

- [mouseos/xnu_bootshim_benesse_a04br3](https://github.com/mouseos/xnu_bootshim_benesse_a04br3)
- [mouseos/xnu_device_benesse_a04br3](https://github.com/mouseos/xnu_device_benesse_a04br3)
- [UniXNU Porting Guide](https://unixnu.com/porting/)
