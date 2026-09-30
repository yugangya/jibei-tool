@echo off
REM 自动编译打包脚本（无交互模式）
setlocal

set "PROJECT_ROOT=%~dp0"
cd /d "%PROJECT_ROOT%"

echo ========================================
echo QGIS 江河系统 - 自动编译并打包
echo ========================================
echo 开始时间: %date% %time%
echo.

REM 步骤 1: 编译
echo [1/2] 编译 QGIS...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%build-qgis.ps1"
if %ERRORLEVEL% NEQ 0 (
    echo 编译失败！错误代码: %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)
echo 编译完成！
echo.

REM 步骤 2: 打包
echo [2/2] 打包部署版本...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%package-windows.ps1"
if %ERRORLEVEL% NEQ 0 (
    echo 打包失败！错误代码: %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)
echo 打包完成！
echo.

echo ========================================
echo 编译打包成功！
echo 结束时间: %date% %time%
echo 部署包位置: .dist\jianghe\
echo ========================================

exit /b 0
