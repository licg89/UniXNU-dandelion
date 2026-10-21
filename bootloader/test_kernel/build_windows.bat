@echo off
REM Build script for Windows without make
REM Requires: aarch64-linux-gnu-gcc

echo ==========================================
echo  Build Test Kernel (Windows)
echo ==========================================
echo.

REM Check for cross-compiler
where aarch64-linux-gnu-gcc >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: aarch64-linux-gnu-gcc not found
    echo.
    echo Please install ARM cross-compiler:
    echo 1. Download from: https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-a/downloads
    echo 2. Or install via: winget install ArmGnuToolchain
    echo.
    pause
    exit /b 1
)

echo [1/4] Assembling start.s...
aarch64-linux-gnu-as -mcpu=cortex-a53 -o start.o start.s
if %ERRORLEVEL% NEQ 0 goto error

echo [2/4] Compiling kernel.c...
aarch64-linux-gnu-gcc -Wall -O2 -nostdlib -nostdinc -ffreestanding -mcpu=cortex-a53 -mgeneral-regs-only -c -o kernel.o kernel.c
if %ERRORLEVEL% NEQ 0 goto error

echo [3/4] Linking...
aarch64-linux-gnu-ld -T link.ld -nostdlib -o test_kernel.elf start.o kernel.o
if %ERRORLEVEL% NEQ 0 goto error

echo [4/4] Creating binary...
aarch64-linux-gnu-objcopy -O binary test_kernel.elf test_kernel.bin
if %ERRORLEVEL% NEQ 0 goto error

echo.
echo ==========================================
echo  Build Complete!
echo ==========================================
echo Output: test_kernel.bin
dir test_kernel.bin
goto end

:error
echo.
echo ERROR: Build failed
exit /b 1

:end
pause
