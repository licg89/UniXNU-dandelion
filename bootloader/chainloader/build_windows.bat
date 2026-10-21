@echo off
REM Build script for chainloader (Windows)

echo ==========================================
echo  Build Chainloader (Windows)
echo ==========================================
echo.

REM Check for cross-compiler
where aarch64-linux-gnu-gcc >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: aarch64-linux-gnu-gcc not found
    echo Please install ARM cross-compiler first
    pause
    exit /b 1
)

echo [1/4] Assembling chainloader_asm.s...
aarch64-linux-gnu-as -mcpu=cortex-a53 -o chainloader_asm.o chainloader_asm.s
if %ERRORLEVEL% NEQ 0 goto error

echo [2/4] Compiling chainloader.c...
aarch64-linux-gnu-gcc -Wall -O2 -nostdlib -nostdinc -ffreestanding -mcpu=cortex-a53 -mgeneral-regs-only -I. -c -o chainloader.o chainloader.c
if %ERRORLEVEL% NEQ 0 goto error

echo [3/4] Linking...
aarch64-linux-gnu-ld -T link.ld -nostdlib -o chainloader.elf chainloader_asm.o chainloader.o
if %ERRORLEVEL% NEQ 0 goto error

echo [4/4] Creating binary...
aarch64-linux-gnu-objcopy -O binary chainloader.elf chainloader.bin
if %ERRORLEVEL% NEQ 0 goto error

echo.
echo ==========================================
echo  Build Complete!
echo ==========================================
echo Output: chainloader.bin
dir chainloader.bin
goto end

:error
echo.
echo ERROR: Build failed
exit /b 1

:end
pause
