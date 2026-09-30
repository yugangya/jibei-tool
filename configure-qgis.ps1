param(
  [string]$BuildDirectory = '.deps\qgis-build',
  [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
  [string]$BuildType = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ProjectRoot 'qgis-dev-env.ps1')

$buildPath = Join-Path $ProjectRoot $BuildDirectory
$cmakeArgs = @(
  '-S', (Join-Path $ProjectRoot 'QGIS'),
  '-B', $buildPath,
  '-G', 'Ninja',
  "-DCMAKE_BUILD_TYPE=$BuildType",
  "-DCMAKE_PREFIX_PATH=$env:CMAKE_PREFIX_PATH",
  "-DQt6_DIR=$env:Qt6_DIR",
  "-DPDAL_DIR=$env:PDAL_DIR",
  "-DPython_ROOT_DIR=$env:Python_ROOT_DIR",
  "-DPython_EXECUTABLE=$env:Python_EXECUTABLE",
  "-DFLEX_EXECUTABLE=$env:FLEX_EXECUTABLE",
  "-DBISON_EXECUTABLE=$env:BISON_EXECUTABLE",
  "-DONNXRUNTIME_ROOT=$env:ONNXRUNTIME_ROOT"
)

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
  throw "QGIS CMake configuration failed with exit code $LASTEXITCODE"
}
