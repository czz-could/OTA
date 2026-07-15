# OTA\-Boot\+APP 固件合并与烧录完整操作手册\.md

## 文档介绍

本文档为 **STM32 BootLoader \+ APP 双固件自动合并、编译、烧录、脚本自定义修改** 全套标准流程手册。

本方案特点：

- 纯 Windows 批处理，**无需 Python、无需额外软件**

- 全程 **相对路径**，换电脑、换盘符不报错

- 自动补齐 Boot 分区、自动预留 OTA 间隔区、自动拼接一体 bin

- 支持量产一键烧录一体固件，也支持日常分开调试烧录

---

## 一、固定工程目录结构（必须严格一致）

脚本强依赖目录结构，**文件夹层级不能乱改**

```Plain Text
OTA+APP 根目录
 ├─ OTA（BootLoader 工程）
 │   └─ MDK-ARM
 │       └─ BOOTLOADER.bin   【Boot 固件】
 │
 └─ APP（应用工程）
     └─ MDK-ARM
         ├─ merge_all.bat     【自动合并脚本】
         └─ BOOTLOADER
             ├─ BOOTLOADER.bin        【APP 固件】
             └─ BOOTLOADER_TOTAL.bin  【最终一体量产固件】

```

---

## 二、Keil 工程配置（唯一一次配置）

### 2\.1 APP 工程配置（必须配置）

打开 APP 工程 → 魔术棒 → User → After Build / Rebuild

填写并勾选：

- Run \#1：`fromelf --bin --output=$L@L.bin !L`

- Run \#2：`merge_all.bat`

作用说明：

- 第一行：Keil 编译完 **自动导出 APP\.bin**（无需手动操作）

- 第二行：导出完成后 **自动运行合并脚本**

### 2\.2 OTA Boot 工程（无需任何配置）

Boot 工程不需要配置 User 脚本，只需要正常编译即可自动生成 Boot\.bin。

---

## 三、双BIN合并原理（核心）

最终一体固件组成结构：

```Plain Text
[ 24KB 对齐Boot固件 ] + [ 8KB OTA空白分区 ] + [ APP应用固件 ]

```

- Boot 不足 24KB：自动补 0xFF 对齐分区

- 中间固定 8KB 空白区：用于存储 OTA 升级标记、版本号

- 最后拼接最新 APP 程序

---

## 四、完整编译流程（日常开发标准流程）

### 场景1：修改了 Boot 代码（必须两步）

1. 编译 OTA 工程 → 生成最新 Boot\.bin

2. 编译 APP 工程 → 自动合并生成**BOOTLOADER\_TOTAL\.bin**

### 场景2：只改 APP 代码（只需一步）

1. 直接编译 APP 工程

2. 脚本自动复用已有 Boot\.bin，直接合成新固件

---

## 五、merge\_all\.bat 脚本详解 \+ 自定义修改教程

### 5\.1 最终稳定版脚本（可直接使用）

```Plain Text
@echo off
setlocal enabledelayedexpansion
chcp 437 >nul

:: ========== 用户可修改配置区 ==========
set BOOT_ALIGN=24576
set GAP_SIZE=8192
:: ====================================

:: 动态获取脚本真实路径（解决Keil路径错乱，禁止修改）
set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%..\..\"
set "BOOT_SRC=%ROOT_DIR%OTA\MDK-ARM\BOOTLOADER\BOOTLOADER.bin"

set "BOOT_LOCAL=BOOTLOADER\boot_temp.bin"
set "APP_BIN=BOOTLOADER\BOOTLOADER.bin"
set "OUT_BIN=BOOTLOADER\BOOTLOADER_TOTAL.bin"
set "TMP_GAP=tmp_gap.bin"
set "TMP_BOOT=tmp_boot_align.bin"

:: 清理旧文件
if exist "%OUT_BIN%" del /f /q "%OUT_BIN%"
if exist "%TMP_GAP%" del /f /q "%TMP_GAP%"
if exist "%TMP_BOOT%" del /f /q "%TMP_BOOT%"
if exist "%BOOT_LOCAL%" del /f /q "%BOOT_LOCAL%"

:: 检测Boot文件
if not exist "%BOOT_SRC%" (
    echo ======================================================
    echo 错误：未找到Boot固件 BOOTLOADER.bin
    echo 解决：先编译OTA Boot工程，再编译APP
    echo ======================================================
    exit /b 1
)

copy "%BOOT_SRC%" "%BOOT_LOCAL%" >nul
if not exist "%BOOT_LOCAL%" (
    echo ERROR: Copy Boot failed
    exit /b 1
)

if not exist "%APP_BIN%" (
    echo ERROR: APP bin missing
    exit /b 1
)

:: 生成8KB 0xFF间隔
powershell -Command "$gap = New-Object byte[] %GAP_SIZE%; $gap | %%{$_ = 0xFF}; [System.IO.File]::WriteAllBytes('%TMP_GAP%', $gap)"

:: Boot补齐对齐
powershell -Command "$boot = [System.IO.File]::ReadAllBytes('%BOOT_LOCAL%'); $needPad = %BOOT_ALIGN% - $boot.Length; if($needPad -gt 0){$pad = New-Object byte[] $needPad; $pad | %%{$_ = 0xFF}; $full = $boot + $pad}else{$full = $boot}; [System.IO.File]::WriteAllBytes('%TMP_BOOT%', $full)"

:: 三段合并
copy /b "%TMP_BOOT%" + "%TMP_GAP%" + "%APP_BIN%" "%OUT_BIN%" >nul

:: 清理临时文件
del /f /q "%TMP_GAP%" "%TMP_BOOT%" "%BOOT_LOCAL%"

echo ======================================
echo Merge Success! BOOTLOADER_TOTAL.bin
echo ======================================

```

### 5\.2 你可以自行修改的地方（重点）

1. **修改 Boot 分区大小**
`set BOOT_ALIGN=24576`

例：0x8000 = 32768

2. **修改 OTA 间隔区大小**
`set GAP_SIZE=8192`

3. **修改空白填充值**

两处 `0xFF` 可改为 `0x00`

4. **目录改动时修改Boot路径**
`BOOT_SRC=xxx`

### 5\.3 禁止修改

`%~dp0` 动态路径、文件检测、临时文件处理（改了必报错）

---

## 六、ST\-LINK Utility 烧录标准流程

### 6\.1 重要常识

ST\-LINK 会 **缓存旧bin**，每次更新固件必须重新加载！

### 6\.2 烧录步骤

1. 连接 ST\-LINK 与开发板

2. 打开 ST\-LINK Utility → Target → Connect

3. **File → Close File** 关闭旧缓存

4. File → Open file 选择新生成的 **BOOTLOADER\_TOTAL\.bin**

5. Target → Program \& Verify

6. 参数：


    - Start Address：`0x08000000`

    - 勾选 Verify、Reset after programming

    - Erase：Full chip erase（整片擦除）

7. 点击 Start 烧录完成自动运行

---

## 七、日常调试推荐方式（不合并BIN）

开发调试不需要合并脚本，分开烧录更稳：

1. Boot\.bin → 0x08000000

2. APP\.bin → 0x08008000

---

## 八、工程打包分发规范（团队标准）

### 8\.1 打包前必须操作

1. Keil 执行 Clean Targets

2. 删除所有编译产物 bin/axf/o/map

3. 只保留源码 \+ 工程文件 \+ bat脚本

### 8\.2 新人首次使用步骤

1. 解压工程

2. 先编译 OTA 工程生成 Boot\.bin

3. 再编译 APP 自动合成一体固件

---

## 九、常见报错汇总

- **找不到 Boot\.bin**：未先编译OTA工程

- **Copy Boot failed**：目录结构被改动

- **烧录还是旧程序**：ST\-LINK 未 Close File 刷新缓存

- **不跳转APP**：脚本分区大小与工程Flash地址不匹配
