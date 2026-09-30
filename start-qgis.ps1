param(
  [string]$BuildDirectory = '.deps\qgis-build',
  [switch]$Wait,
  [Parameter(ValueFromRemainingArguments = $true)]
  [string[]]$QgisArguments
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildPath = Join-Path $ProjectRoot $BuildDirectory
$outputRoot = Join-Path $buildPath 'output'
$outputBin = Join-Path $outputRoot 'bin'
$qgisExe = Join-Path $outputBin 'qgis.exe'
$profileRoot = Join-Path $ProjectRoot '.dev\qgis-profile'

if (-not (Test-Path $qgisExe)) {
  throw "QGIS executable does not exist. Run .\build-qgis.ps1 first: $qgisExe"
}

. (Join-Path $ProjectRoot 'qgis-dev-env.ps1')

New-Item -ItemType Directory -Path $profileRoot -Force | Out-Null
$env:QGIS_PREFIX_PATH = $outputRoot
$env:QGIS_PLUGINPATH = Join-Path $outputRoot 'plugins'
$env:QT_PLUGIN_PATH = Join-Path $QtRoot 'plugins'
$env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $QtRoot 'plugins\platforms'
$env:PYTHONPATH = ((Join-Path $QgisSource 'python'), (Join-Path $outputRoot 'python'), $env:PYTHONPATH) -join ';'
$runtimePaths = @(
  $outputBin
  $outputRoot
  (Join-Path $QtRoot 'bin')
  (Join-Path $OsgeoRoot 'bin')
  (Join-Path $GdalRoot 'bin')
  (Join-Path $PdalRoot 'bin')
  (Join-Path $PythonRoot 'Scripts')
  $PythonRoot
  (Join-Path $OrtRoot 'lib')
  (Join-Path $env:SystemRoot 'System32')
  $env:SystemRoot
) | Select-Object -Unique
$env:PATH = $runtimePaths -join ';'

$arguments = @(
  '--nologo'
  '--noversioncheck'
  '--profiles-path'
  $profileRoot
)

if ($QgisArguments) {
  $arguments += $QgisArguments
}

$process = Start-Process -FilePath $qgisExe -WorkingDirectory $outputBin -ArgumentList $arguments -PassThru
Write-Host "QGIS started (PID $($process.Id)): $qgisExe"

if ($Wait) {
  $process.WaitForExit()
  exit $process.ExitCode
}
