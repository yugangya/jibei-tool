@echo off
REM 修复 robocopy 路径问题后重新打包

echo ========================================
echo 重新打包江河系统
echo ========================================
echo.

echo [1/2] 关闭正在运行的程序...
taskkill /F /IM jianghe.exe 2>nul
taskkill /F /IM qgis.exe 2>nul
timeout /t 2 /nobreak >nul

echo [2/2] 开始打包...
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package-windows.ps1"

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ========================================
    echo 打包成功！
    echo ========================================
    echo.
    echo 修复内容：
    echo   ✓ 电压等级中文乱码（±800kV, ±500kV）
    echo   ✓ 日历控件箭头乱码（← →）
    echo   ✓ 照片编辑模式鼠标光标
    echo   ✓ 窗口切换后卡顿问题
    echo.
    echo 部署包位置：.dist\jianghe\
    echo.
) else (
    echo.
    echo ========================================
    echo 打包失败！错误代码: %ERRORLEVEL%
    echo ========================================
    echo.
)

pause
