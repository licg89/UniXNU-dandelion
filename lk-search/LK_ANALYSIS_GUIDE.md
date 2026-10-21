# LK Bootloader Analysis Guide

## Step 1: Find LK Source

### Option A: Xiaomi Open Source
```bash
git clone https://github.com/MiCode/Xiaomi_Kernel_OpenSource.git -b dandelion-q-oss
cd Xiaomi_Kernel_OpenSource
find . -name "lk" -o -name "bootloader" -o -name "*littlekernel*"
```

### Option B: Extract from Device
```bash
# Requires root
adb shell su
dd if=/dev/block/by-name/boot of=/sdcard/boot.img
adb pull /sdcard/boot.img

# Analyze
unpackbootimg -i boot.img
binwalk boot.img
```

### Option C: Use Generic LK
```bash
git clone https://github.com/littlekernel/lk.git
# Add MTK-specific code
```

## Step 2: Analyze LK Structure

Typical LK structure:
```
lk/
├── app/
│   ├── aboot/       # Android boot
│   ├── fastboot/    # Fastboot mode
│   └── shell/       # Shell
├── arch/
│   └── arm/
│       └── arm64/
├── dev/             # Device drivers
├── platform/
│   └── mt6765/      # MT6765 specific
├── kernel/
└── lib/
```

## Step 3: Find Kernel Loading Code

Look for:
- `app/aboot/aboot.c` - Android boot loader
- `boot_linux()` function - Kernel loading
- `boot_img_hdr` - Boot image header

Key functions to modify:
```c
void boot_linux(void *kernel, unsigned tags, ...)
{
    // This loads Android kernel
    // Modify to load XNU instead
}
```

## Step 4: Modify LK for XNU

### Changes needed:
1. Change kernel loading to load XNU
2. Set up XNU boot parameters
3. Pass device tree
4. Jump to XNU entry point

### Example modification:
```c
void boot_xnu(void *kernel, unsigned dtb_addr, ...)
{
    // Set up for XNU
    // Load XNU kernel
    // Jump to XNU entry
}
```

## Step 5: Compile LK

```bash
cd lk
make PLATFORM=mt6765
```

## Step 6: Test

```bash
# Flash modified LK
fastboot flash lk lk.bin

# Or chain-load
fastboot boot boot_xnu.img
```

## Resources

- [LK Official](https://github.com/littlekernel/lk)
- [MTK Open Source](https://github.com/MediaTekMTK/mtk-open-source)
- [Xiaomi Open Source](https://github.com/MiCode/Xiaomi_Kernel_OpenSource)
- [XDA Developers](https://forum.xda-developers.com/)

## Next Steps

1. Find LK source (Xiaomi/MTK/extract)
2. Analyze boot loading code
3. Modify for XNU
4. Compile and test
5. Debug with UART
