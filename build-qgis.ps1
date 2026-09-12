param(
  [string]$BuildDirectory = '.deps\qgis-build',
  [ValidateRange(1, 64)]
  [int]$Parallel = 8
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildPath = Join-Path $ProjectRoot $BuildDirectory

if (-not (Test-Path (Join-Path $buildPath 'build.ninja'))) {
  throw "QGIS is not configured. Run .\configure-qgis.ps1 first: $buildPath"
}

. (Join-Path $ProjectRoot 'qgis-dev-env.ps1')

& cmake --build $buildPath --target qgis_desktop --parallel $Parallel
if ($LASTEXITCODE -ne 0) {
  throw "QGIS build failed with exit code $LASTEXITCODE"
}

# Keep a standalone copy of the branded application icon beside qgis.exe for
# Windows shell integrations; layer-tree icons remain QGIS geometry icons.
$brandIcon = Join-Path $ProjectRoot 'app.ico'
$outputIcon = Join-Path $buildPath 'output\bin\app.ico'
if (Test-Path $brandIcon) {
  Copy-Item -LiteralPath $brandIcon -Destination $outputIcon -Force
}
