param(
  [string]$BuildDirectory = '.deps\qgis-build',
  [string]$OutputDirectory = '.dist\jianghe',
  [ValidateRange(1, 64)]
  [int]$Parallel = 8,
  [switch]$Clean,
  [switch]$SkipBuild,
  [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

function Resolve-ProjectPath {
  param([Parameter(Mandatory = $true)][string]$Path)

  if ([System.IO.Path]::IsPathRooted($Path)) {
    return [System.IO.Path]::GetFullPath($Path)
  }

  return [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot $Path))
}

function Assert-Exists {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Description
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    throw "$Description does not exist: $Path"
  }
}

function Assert-NotDangerousDirectory {
  param([Parameter(Mandatory = $true)][string]$Path)

  $full = [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
  $root = [System.IO.Path]::GetPathRoot($full).TrimEnd('\')
  $project = [System.IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')

  if ([string]::IsNullOrWhiteSpace($full)) {
    throw 'Refusing to use an empty package directory.'
  }

  if ($full.Equals($root, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use a drive root as the package directory: $full"
  }

  if ($full.Equals($project, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use the project root as the package directory: $full"
  }

  if ($full.Length -lt 12) {
    throw "Refusing to use a very broad package directory: $full"
  }
}

function Assert-InsidePackage {
  param(
    [Parameter(Mandatory = $true)][string]$PackageRoot,
    [Parameter(Mandatory = $true)][string]$Destination
  )

  $root = [System.IO.Path]::GetFullPath($PackageRoot).TrimEnd('\') + '\'
  $dest = [System.IO.Path]::GetFullPath($Destination).TrimEnd('\') + '\'

  if (-not $dest.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to mirror files outside package root. Destination: $Destination"
  }
}

function Invoke-RobocopyMirror {
  param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Destination,
    [Parameter(Mandatory = $true)][string]$PackageRoot,
    [string[]]$ExtraArgs = @()
  )

  Assert-Exists $Source "Copy source"
  Assert-InsidePackage -PackageRoot $PackageRoot -Destination $Destination
  New-Item -ItemType Directory -Path $Destination -Force | Out-Null

  $arguments = @(
    $Source,
    $Destination,
    '/MIR',
    '/R:2',
    '/W:2',
    '/MT:16',
    '/NP',
    '/NFL',
    '/NDL'
  ) + $ExtraArgs

  Write-Host "Copying: $Source -> $Destination"
  # 使用完整路径调用 robocopy，避免 PATH 问题
  $robocopyExe = Join-Path $env:SystemRoot 'System32\robocopy.exe'
  & $robocopyExe @arguments
  $code = $LASTEXITCODE

  if ($code -gt 7) {
    throw "Robocopy failed with exit code $code while copying $Source"
  }

  $global:LASTEXITCODE = 0
}

function Write-Utf8NoBom {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Content
  )

  [System.IO.File]::WriteAllText($Path, $Content, [System.Text.UTF8Encoding]::new($false))
}

$buildPath = Resolve-ProjectPath $BuildDirectory
$outputRoot = Join-Path $buildPath 'output'
$outputBin = Join-Path $outputRoot 'bin'
$qgisExe = Join-Path $outputBin 'qgis.exe'
$qgisAppDll = Join-Path $outputBin 'qgis_app.dll'
$osgeoRoot = Join-Path $ProjectRoot '.deps\OSGeo4W'
$ortRoot = Join-Path $ProjectRoot '.deps\onnxruntime'
$pythonSupport = Join-Path $ProjectRoot '.dev\python'
$qgisPythonSource = Join-Path $ProjectRoot 'QGIS\python'
$consoleSource = Join-Path $qgisPythonSource 'console'
$packageRoot = Resolve-ProjectPath $OutputDirectory
$packageJianghe = Join-Path $packageRoot 'jianghe'
$packageOsgeo = Join-Path $packageRoot 'osgeo4w'
$packageOrt = Join-Path $packageRoot 'onnxruntime'
$packagePythonSupport = Join-Path $packageRoot 'python-support'
$largeSam2ModelName = 'sam2.1-hiera-large'
$oldSam2ModelName = ('sam2.1-hiera-t' + 'iny')

Assert-NotDangerousDirectory $packageRoot

if (-not $SkipBuild) {
  $buildScript = Join-Path $ProjectRoot 'build-qgis.ps1'
  Assert-Exists $buildScript 'Build script'
  Write-Host "Building QGIS before packaging..."
  & $buildScript -BuildDirectory $BuildDirectory -Parallel $Parallel
  if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
  }
}

Assert-Exists $qgisExe 'QGIS executable'
Assert-Exists $qgisAppDll 'QGIS application library'
Assert-Exists (Join-Path $outputRoot 'plugins') 'QGIS plugin directory'
Assert-Exists (Join-Path $outputRoot 'python') 'QGIS Python directory'
Assert-Exists $consoleSource 'QGIS Python console package'
Assert-Exists $osgeoRoot 'OSGeo4W runtime'
Assert-Exists (Join-Path $osgeoRoot 'apps\Qt6') 'Qt6 runtime'
Assert-Exists (Join-Path $osgeoRoot 'apps\Python312') 'Python 3.12 runtime'
Assert-Exists (Join-Path $outputBin (Join-Path "models\sam2-onnx\$largeSam2ModelName" 'vision_encoder.onnx')) 'SAM2 large vision encoder'
Assert-Exists (Join-Path $outputBin (Join-Path "models\sam2-onnx\$largeSam2ModelName" 'prompt_encoder_mask_decoder.onnx')) 'SAM2 large prompt encoder/mask decoder'

if (Test-Path -LiteralPath (Join-Path $outputBin "models\sam2-onnx\$oldSam2ModelName")) {
  throw 'The runtime output still contains an old SAM2 model folder. Rebuild before packaging.'
}

if ($ValidateOnly) {
  Write-Host 'Package validation OK. Output directory was not created because -ValidateOnly was used.'
  return
}

if ($Clean -and (Test-Path -LiteralPath $packageRoot)) {
  Write-Host "Cleaning package directory: $packageRoot"
  Remove-Item -LiteralPath $packageRoot -Recurse -Force
}

New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null

Invoke-RobocopyMirror -Source $outputRoot -Destination $packageJianghe -PackageRoot $packageRoot
Invoke-RobocopyMirror -Source $consoleSource -Destination (Join-Path $packageJianghe 'python\console') -PackageRoot $packageRoot -ExtraArgs @('/XD', '__pycache__', '/XF', 'CMakeLists.txt')
Invoke-RobocopyMirror -Source $osgeoRoot -Destination $packageOsgeo -PackageRoot $packageRoot
# QGIS is built with QGIS_DATA_SUBDIR=".". The build tree still stages install
# data under output\data, but an installed/portable application looks for
# runtime resources directly below its prefix (jianghe\resources,
# jianghe\images and jianghe\svg). Mirror those directories explicitly so
# direct double-clicks and the launcher resolve the same resource paths.
$outputData = Join-Path $outputRoot 'data'
foreach ($runtimeDataDirectory in @('resources', 'images', 'svg')) {
  $sourceDirectory = Join-Path $outputData $runtimeDataDirectory
  $destinationDirectory = Join-Path $packageJianghe $runtimeDataDirectory
  Invoke-RobocopyMirror -Source $sourceDirectory -Destination $destinationDirectory -PackageRoot $packageRoot
}

# srs.db is generated in the build tree rather than output\data\resources on
# this QGIS configuration. It is nevertheless required by the installed
# runtime at jianghe\resources\srs.db.
$srsDatabaseSource = Join-Path $buildPath 'resources\srs.db'
$srsDatabaseDestination = Join-Path $packageJianghe 'resources\srs.db'
if (Test-Path -LiteralPath $srsDatabaseSource) {
  Copy-Item -LiteralPath $srsDatabaseSource -Destination $srsDatabaseDestination -Force
} else {
  throw "Required QGIS CRS database does not exist: $srsDatabaseSource"
}

if (Test-Path -LiteralPath $ortRoot) {
  Invoke-RobocopyMirror -Source $ortRoot -Destination $packageOrt -PackageRoot $packageRoot
} else {
  Write-Warning "ONNX Runtime source directory was not found: $ortRoot. The package will rely on DLLs copied into jianghe\bin."
}

if (Test-Path -LiteralPath $pythonSupport) {
  Invoke-RobocopyMirror -Source $pythonSupport -Destination $packagePythonSupport -PackageRoot $packageRoot
} else {
  New-Item -ItemType Directory -Path $packagePythonSupport -Force | Out-Null
}

# Brand the deployment-facing executable without renaming QGIS ABI DLLs.
# qgis.exe reads qgis.env by basename, so the package uses a matching empty jianghe.env;
# the launcher supplies all portable paths explicitly.
$packagedExe = Join-Path $packageJianghe 'bin\qgis.exe'
$jiangheExe = Join-Path $packageJianghe 'bin\jianghe.exe'
if (Test-Path -LiteralPath $packagedExe) {
  Copy-Item -LiteralPath $packagedExe -Destination $jiangheExe -Force
  Remove-Item -LiteralPath $packagedExe -Force
}
$packagedEnv = Join-Path $packageJianghe 'bin\qgis.env'
if (Test-Path -LiteralPath $packagedEnv) {
  Remove-Item -LiteralPath $packagedEnv -Force
}
Write-Utf8NoBom -Path (Join-Path $packageJianghe 'bin\jianghe.env') -Content ''
# Build-only markers and debug symbols are not required on a target machine.
# Removing them also prevents development paths and QGIS build artifact names
# from leaking into the portable deployment package.
$packageBin = Join-Path $packageJianghe 'bin'
# Keep GEOS beside the executable. qgis_core.dll imports geos_c.dll by name,
# and a direct double-click can otherwise pick an older GEOS from another DLL
# search directory before the packaged OSGeo4W runtime.
foreach ($geosRuntime in @('geos.dll', 'geos_c.dll')) {
  $geosSource = Join-Path $osgeoRoot (Join-Path 'bin' $geosRuntime)
  Assert-Exists $geosSource "GEOS runtime $geosRuntime"
  Copy-Item -LiteralPath $geosSource -Destination (Join-Path $packageBin $geosRuntime) -Force
}

# Keep the Qt/GDAL dependency layout identical to the known-good OSGeo4W
# development runtime. In particular, do not rename icuuc67.dll to icuuc.dll:
# the bundled Qt6 build uses the Windows unversioned ICU runtime, and that alias
# causes an ABI mismatch (missing UCNV_FROM_U_CALLBACK_SUBSTITUTE).
$buildMarker = Join-Path $packageBin 'qgisbuildpath.txt'
if (Test-Path -LiteralPath $buildMarker) {
  Remove-Item -LiteralPath $buildMarker -Force
}
Get-ChildItem -LiteralPath $packageBin -File | Where-Object { $_.Extension -in @('.ilk', '.pdb') } | ForEach-Object {
  Remove-Item -LiteralPath $_.FullName -Force
}
$launcher = @"
@echo off
setlocal EnableExtensions

set "PACKAGE_ROOT=%~dp0"
if "%PACKAGE_ROOT:~-1%"=="\" set "PACKAGE_ROOT=%PACKAGE_ROOT:~0,-1%"
set "PACKAGE_ROOT=%PACKAGE_ROOT:\=/%"

set "QGIS_ROOT=%PACKAGE_ROOT%/jianghe"
set "OSGEO4W_ROOT=%PACKAGE_ROOT%/osgeo4w"
set "QT_ROOT=%OSGEO4W_ROOT%/apps/Qt6"
set "GDAL_ROOT=%OSGEO4W_ROOT%/apps/gdal-dev"
set "PDAL_ROOT=%OSGEO4W_ROOT%/apps/pdal-dev"
set "MSYS_ROOT=%OSGEO4W_ROOT%/apps/msys"
set "PYTHON_ROOT=%OSGEO4W_ROOT%/apps/Python312"
set "ONNX_ROOT=%PACKAGE_ROOT%/onnxruntime"
set "PROFILE_ROOT=%PACKAGE_ROOT%/profile"

rem Do not inherit Conda/Python variables from the caller.
set "PYTHONHOME="
set "PYTHONPATH="
set "PYTHONUSERBASE="
set "PYTHONSTARTUP="
set "CONDA_PREFIX="
set "CONDA_DEFAULT_ENV="
set "CONDA_EXE="
set "CONDA_PROMPT_MODIFIER="
set "CONDA_PYTHON_EXE="
set "CONDA_SHLVL="

if not exist "%PROFILE_ROOT%" mkdir "%PROFILE_ROOT%"

set "QGIS_PREFIX_PATH=%QGIS_ROOT%"
set "QGIS_DATA_SUBDIR=."
set "QGIS_CUSTOM_CONFIG_PATH=%PROFILE_ROOT%"
set "QGIS_PLUGINPATH=%QGIS_ROOT%/plugins;%PROFILE_ROOT%/profiles/default/python/plugins"
set "QT_PLUGIN_PATH=%QT_ROOT%/plugins;%QGIS_ROOT%/bin/qtplugins"
set "QT_QPA_PLATFORM_PLUGIN_PATH=%QT_ROOT%/plugins/platforms"
set "PYTHONHOME=%PYTHON_ROOT%"
set "PYTHONNOUSERSITE=1"
set "PYTHONPATH=%QGIS_ROOT%/python;%QGIS_ROOT%/python/plugins;%PYTHON_ROOT%/Lib;%PYTHON_ROOT%/DLLs;%PYTHON_ROOT%/Lib/site-packages;%PACKAGE_ROOT%/python-support;%PROFILE_ROOT%/profiles/default/python"
set "GDAL_DATA=%OSGEO4W_ROOT%/share/gdal"
set "PROJ_DATA=%OSGEO4W_ROOT%/share/proj"
set "PROJ_LIB=%OSGEO4W_ROOT%/share/proj"
set "PATH=%QGIS_ROOT%/bin;%QGIS_ROOT%;%OSGEO4W_ROOT%/bin;%QT_ROOT%/bin;%GDAL_ROOT%/bin;%PDAL_ROOT%/bin;%MSYS_ROOT%/bin;%PYTHON_ROOT%;%PYTHON_ROOT%/Scripts;%PYTHON_ROOT%/DLLs;%ONNX_ROOT%/lib;%SystemRoot%/System32;%SystemRoot%"

start "" "%QGIS_ROOT%/bin/jianghe.exe" --noversioncheck --profiles-path "%PROFILE_ROOT%" %*
"@

Write-Utf8NoBom -Path (Join-Path $packageRoot 'Start-Jianghe.cmd') -Content $launcher
$cnLauncherName = [string]::Concat([char]0x542F, [char]0x52A8, [char]0x6C5F, [char]0x6CB3, [char]0x7CFB, [char]0x7EDF, '.cmd')
$cnLauncher = "@echo off`r`ncall `"%~dp0Start-Jianghe.cmd`" %*`r`n"
Write-Utf8NoBom -Path (Join-Path $packageRoot $cnLauncherName) -Content $cnLauncher

$readme = @"
Jianghe Windows portable package
================================

Start:
  Double-click Start-Jianghe.cmd.
  A Chinese alias launcher is also generated in the same directory.

Deployment:
  Copy this whole directory to another Windows x64 computer. Do not copy jianghe.exe alone.

Included runtime folders:
  jianghe     - application runtime
  console     - QGIS Python Console support (under jianghe\python)
  osgeo4w     - Qt / GDAL / PROJ / Python runtime
  onnxruntime - ONNX Runtime libraries, when present
  profile     - local user profile created by the launcher

Model note:
  The package keeps SAM2 large under jianghe\bin\models\sam2-onnx\sam2.1-hiera-large.

If the target computer reports missing VC runtime DLLs, install Microsoft Visual C++ Redistributable 2015-2022 x64.
"@
Write-Utf8NoBom -Path (Join-Path $packageRoot 'README-deploy.txt') -Content $readme

Assert-Exists (Join-Path $packageJianghe 'bin\jianghe.exe') 'Packaged Jianghe executable'
Assert-Exists (Join-Path $packageJianghe 'python\console\__init__.py') 'Packaged QGIS Python console'
Assert-Exists (Join-Path $packageJianghe 'bin\qgis_app.dll') 'Packaged QGIS application library'
Assert-Exists (Join-Path $packageJianghe (Join-Path "bin\models\sam2-onnx\$largeSam2ModelName" 'vision_encoder.onnx')) 'Packaged SAM2 large vision encoder'
Assert-Exists (Join-Path $packageJianghe (Join-Path "bin\models\sam2-onnx\$largeSam2ModelName" 'prompt_encoder_mask_decoder.onnx')) 'Packaged SAM2 large prompt encoder/mask decoder'

if (Test-Path -LiteralPath (Join-Path $packageJianghe "bin\models\sam2-onnx\$oldSam2ModelName")) {
  throw 'Packaged output unexpectedly contains an old SAM2 model folder.'
}

Write-Host ''
Write-Host "Package created: $packageRoot"
Write-Host 'Copy this whole folder to the target Windows computer and run Start-Jianghe.cmd.'
