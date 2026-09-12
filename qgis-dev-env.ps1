# Initialize the native QGIS development environment for this checkout.
# Use from PowerShell: . .\qgis-dev-env.ps1

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$DepsRoot = Join-Path $ProjectRoot '.deps'
$PythonSupport = Join-Path $ProjectRoot '.dev\python'
$QgisSource = Join-Path $ProjectRoot 'QGIS'
$OsgeoRoot = Join-Path $DepsRoot 'OSGeo4W'
$QtRoot = Join-Path $OsgeoRoot 'apps\Qt6'
$GdalRoot = Join-Path $OsgeoRoot 'apps\gdal-dev'
$PdalRoot = Join-Path $OsgeoRoot 'apps\pdal-dev'
$PythonRoot = Join-Path $OsgeoRoot 'apps\Python312'
$OrtRoot = Join-Path $DepsRoot 'onnxruntime'
$WinFlexBisonRoot = Join-Path $DepsRoot 'winflexbison'
$VsInstallPath = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
$DevShellModule = Join-Path $VsInstallPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
$WinFlex = Join-Path $WinFlexBisonRoot 'win_flex.exe'
$WinBison = Join-Path $WinFlexBisonRoot 'win_bison.exe'

foreach ($requiredPath in @($QgisSource, $OsgeoRoot, $QtRoot, $OrtRoot, $WinFlexBisonRoot, $WinFlex, $WinBison)) {
  if (-not (Test-Path $requiredPath)) {
    throw "Required development path does not exist: $requiredPath"
  }
}

if (Test-Path $DevShellModule) {
  Import-Module $DevShellModule
  Enter-VsDevShell -VsInstallPath $VsInstallPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64'
}

$env:QGIS_SOURCE_DIR = $QgisSource
$env:OSGEO4W_ROOT = $OsgeoRoot
$env:QGIS_PREFIX_PATH = $OsgeoRoot
$env:Qt6_DIR = Join-Path $QtRoot 'lib\cmake\Qt6'
$env:GDAL_ROOT = $GdalRoot
$env:PDAL_ROOT = $PdalRoot
$env:PDAL_DIR = Join-Path $PdalRoot 'lib\cmake\PDAL'
$env:PYTHONHOME = $PythonRoot
$env:PYTHONPATH = $PythonSupport
$env:Python_ROOT_DIR = $PythonRoot
# Use the real OSGeo4W interpreter rather than the launcher in bin/.  The
# launcher depends on the OSGeo4W shell PATH and cannot be executed when Ninja
# regenerates CMake from an IDE or a plain terminal.
$env:Python_EXECUTABLE = Join-Path $PythonRoot 'python3.exe'
$env:ONNXRUNTIME_ROOT = $OrtRoot
$env:FLEX_EXECUTABLE = $WinFlex
$env:BISON_EXECUTABLE = $WinBison
$env:GDAL_DATA = Join-Path $GdalRoot 'share\gdal'
$env:PROJ_DATA = Join-Path $OsgeoRoot 'share\proj'
$env:CMAKE_PREFIX_PATH = ($QtRoot, $GdalRoot, $PdalRoot, $OsgeoRoot) -join ';'
$env:PATH = @(
  (Join-Path $QtRoot 'bin'),
  (Join-Path $OsgeoRoot 'bin'),
  (Join-Path $GdalRoot 'bin'),
  (Join-Path $PdalRoot 'bin'),
  (Join-Path $PythonRoot 'Scripts'),
  $PythonRoot,
  (Join-Path $OrtRoot 'lib'),
  $WinFlexBisonRoot,
  $env:PATH
) -join ';'

Write-Host "QGIS source: $QgisSource"
Write-Host "OSGeo4W: $OsgeoRoot"
Write-Host "Qt: $(& (Join-Path $QtRoot 'bin\qtpaths.exe') --qt-version)"
Write-Host "ONNX Runtime: $((Get-Content (Join-Path $OrtRoot 'VERSION_NUMBER')).Trim())"
Write-Host "Flex: $WinFlex"
Write-Host "Bison: $WinBison"
