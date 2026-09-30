@echo off
REM 关闭江河系统所有进程并重新打包

echo ========================================
echo 关闭江河系统并重新打包
echo ========================================
echo.

echo [1/3] 正在关闭江河系统进程...
taskkill /F /IM jianghe.exe 2>nul
taskkill /F /IM qgis.exe 2>nul
taskkill /F /IM qgis_desktop.exe 2>nul

echo 等待进程完全退出...
timeout /t 3 /nobreak >nul

echo [2/3] 清理临时文件...
if exist ".dist\jianghe\jianghe\bin\*.dll" (
    attrib -R ".dist\jianghe\jianghe\bin\*.dll" /S
)

echo [3/3] 开始打包...
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package-windows.ps1"

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ========================================
    echo 打包成功！
    echo ========================================
    echo.
    echo 新的部署包位置：.dist\jianghe\
    echo 现在可以运行：.dist\jianghe\启动江河系统.cmd
    echo.
) else (
    echo.
    echo ========================================
    echo 打包失败！错误代码: %ERRORLEVEL%
    echo ========================================
    echo.
)

pause
