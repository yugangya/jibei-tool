@echo off
REM ==========================================
REM QGIS 江河系统 - 重新编译并打包脚本
REM ==========================================
REM 说明：此脚本用于修复中文乱码后重新编译和打包系统
REM 作者：Claude (Opus 5)
REM 日期：2026-09-11
REM ==========================================

echo.
echo ========================================
echo  QGIS 江河系统 - 重新编译并打包
echo ========================================
echo.
echo 本次编译将修复以下中文乱码问题：
echo   1. 日历控件的箭头符号
echo   2. 电压等级下拉列表 (±800kV, ±500kV)
echo.
echo 预计编译时间：10-30 分钟（取决于机器性能）
echo.

set "PROJECT_ROOT=%~dp0"
cd /d "%PROJECT_ROOT%"

echo [步骤 1/3] 检查编译环境...
echo.

if not exist ".deps\qgis-build\build.ninja" (
    echo 错误：编译目录未配置！
    echo 请先运行：.\configure-qgis.ps1
    echo.
    pause
    exit /b 1
)

if not exist ".deps\OSGeo4W\bin\cmake.exe" (
    echo 错误：找不到 cmake.exe！
    echo 请确保 OSGeo4W 依赖已正确安装。
    echo.
    pause
    exit /b 1
)

echo 编译环境检查通过！
echo.

echo [步骤 2/3] 开始编译 QGIS...
echo.
echo 正在编译，请耐心等待...
echo （编译输出将显示在下方）
echo.

REM 使用 PowerShell 执行编译脚本
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& '%PROJECT_ROOT%build-qgis.ps1'"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ========================================
    echo  编译失败！错误代码：%ERRORLEVEL%
    echo ========================================
    echo.
    echo 可能的原因：
    echo   1. 源代码存在语法错误
    echo   2. 缺少依赖库
    echo   3. 磁盘空间不足
    echo.
    echo 请查看上方的错误信息。
    echo.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo ========================================
echo  编译成功！
echo ========================================
echo.

echo [步骤 3/3] 打包部署版本...
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& '%PROJECT_ROOT%package-windows.ps1'"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ========================================
    echo  打包失败！错误代码：%ERRORLEVEL%
    echo ========================================
    echo.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo ========================================
echo  全部完成！
echo ========================================
echo.
echo 中文乱码已修复！
echo 新的部署包位置：.dist\jianghe\
echo.
echo 修复内容：
echo   ✓ 日历控件箭头：← →
echo   ✓ 电压等级：±800kV, ±500kV
echo.
echo 现在可以运行：.dist\jianghe\启动江河系统.cmd
echo.
pause
