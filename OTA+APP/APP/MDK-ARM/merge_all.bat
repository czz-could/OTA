@echo off
setlocal enabledelayedexpansion
chcp 437 >nul

:: 获取当前bat脚本所在完整目录
set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%..\..\"
set "BOOT_SRC=%ROOT_DIR%OTA\MDK-ARM\BOOTLOADER\BOOTLOADER.bin"

set "BOOT_LOCAL=BOOTLOADER\boot_temp.bin"
set "APP_BIN=BOOTLOADER\BOOTLOADER.bin"
set "OUT_BIN=BOOTLOADER\BOOTLOADER_TOTAL.bin"
set "TMP_GAP=tmp_gap.bin"
set "TMP_BOOT=tmp_boot_align.bin"

set BOOT_ALIGN=24576
set GAP_SIZE=8192

:: 清理临时文件
if exist "%OUT_BIN%" del /f /q "%OUT_BIN%"
if exist "%TMP_GAP%" del /f /q "%TMP_GAP%"
if exist "%TMP_BOOT%" del /f /q "%TMP_BOOT%"
if exist "%BOOT_LOCAL%" del /f /q "%BOOT_LOCAL%"

:: 校验Boot源文件
if not exist "%BOOT_SRC%" (
    echo ======================================================
    echo 错误：未找到Boot固件 BOOTLOADER.bin
    echo 文件路径：%BOOT_SRC%
    echo 解决步骤：
    echo 1. 关闭当前APP工程
    echo 2. 打开 OTA\MDK-ARM 下的Boot工程
    echo 3. 执行Build/Rebuild编译，生成BOOTLOADER.bin
    echo 4. 重新打开APP工程编译即可合并固件
    echo ======================================================
    exit /b 1
)

:: 复制Boot到本地临时文件
copy "%BOOT_SRC%" "%BOOT_LOCAL%" >nul
if not exist "%BOOT_LOCAL%" (
    echo ERROR: Copy Boot file failed
    exit /b 1
)

:: 校验APP bin
if not exist "%APP_BIN%" (
    echo ERROR: Missing APP bin: %APP_BIN%
    exit /b 1
)

:: 生成8KB 0xFF间隔文件
powershell -Command "$gap = New-Object byte[] %GAP_SIZE%; $gap | %%{$_ = 0xFF}; [System.IO.File]::WriteAllBytes('%TMP_GAP%', $gap)"

:: Boot补齐至24KB
powershell -Command "$boot = [System.IO.File]::ReadAllBytes('%BOOT_LOCAL%'); $needPad = %BOOT_ALIGN% - $boot.Length; if($needPad -gt 0){$pad = New-Object byte[] $needPad; $pad | %%{$_ = 0xFF}; $full = $boot + $pad}else{$full = $boot}; [System.IO.File]::WriteAllBytes('%TMP_BOOT%', $full)"

:: 三段合并
copy /b "%TMP_BOOT%" + "%TMP_GAP%" + "%APP_BIN%" "%OUT_BIN%" >nul

:: 清理临时文件
del /f /q "%TMP_GAP%" "%TMP_BOOT%" "%BOOT_LOCAL%"

echo ======================================
echo Merge Success! Output: %OUT_BIN%
echo ======================================