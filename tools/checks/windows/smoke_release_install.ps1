param(
  [Parameter(Mandatory = $true)]
  [string]$InstallBinDirectory,

  [string]$BuildBinDirectory
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path

if ($BuildBinDirectory) {
  $buildBinDirectory = (Resolve-Path -LiteralPath $BuildBinDirectory).Path
  $buildViewer = Join-Path $buildBinDirectory "ksj-viewer.exe"
  $buildQtPlatformPlugin = Join-Path $buildBinDirectory "platforms\qwindows.dll"

  if (-not (Test-Path -LiteralPath $buildViewer -PathType Leaf)) {
    throw "[kspacejet-check] build-tree viewer is missing: $buildViewer"
  }
  if (-not (Test-Path -LiteralPath $buildQtPlatformPlugin -PathType Leaf)) {
    throw "[kspacejet-check] build-tree Qt platform plugin is missing: $buildQtPlatformPlugin"
  }

  & $buildViewer "--ui-smoke" "--format" "json"
  if ($LASTEXITCODE -ne 0) {
    throw "[kspacejet-check] build-tree viewer UI smoke failed with exit code $LASTEXITCODE"
  }
  & $buildViewer "--export-smoke" "--format" "json"
  if ($LASTEXITCODE -ne 0) {
    throw "[kspacejet-check] build-tree viewer visualization export smoke failed with exit code $LASTEXITCODE"
  }
}

$binDirectory = (Resolve-Path -LiteralPath $InstallBinDirectory).Path
$applications = @("ksj.exe", "ksj-gateway.exe", "ksj-recon.exe", "ksj-research.exe", "ksj-viewer.exe")
$applicationPaths = @{}

foreach ($application in $applications) {
  $path = Join-Path $binDirectory $application
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "[kspacejet-check] installed application is missing: $path"
  }
  $applicationPaths[$application] = $path
  & $path "--help"
  if ($LASTEXITCODE -ne 0) {
    throw "[kspacejet-check] installed help failed with exit code ${LASTEXITCODE}: $application --help"
  }
}

& $applicationPaths["ksj.exe"] "--version"
if ($LASTEXITCODE -ne 0) {
  throw "[kspacejet-check] installed version failed with exit code ${LASTEXITCODE}: ksj --version"
}

& $applicationPaths["ksj-viewer.exe"] "--version"
if ($LASTEXITCODE -ne 0) {
  throw "[kspacejet-check] installed version failed with exit code ${LASTEXITCODE}: ksj-viewer --version"
}

$qtPlatformPlugin = Join-Path $binDirectory "platforms\qwindows.dll"
if (-not (Test-Path -LiteralPath $qtPlatformPlugin -PathType Leaf)) {
  throw "[kspacejet-check] installed Qt platform plugin is missing: $qtPlatformPlugin"
}

& $applicationPaths["ksj-viewer.exe"] "--ui-smoke" "--format" "json"
if ($LASTEXITCODE -ne 0) {
  throw "[kspacejet-check] installed viewer UI smoke failed with exit code $LASTEXITCODE"
}
& $applicationPaths["ksj-viewer.exe"] "--export-smoke" "--format" "json"
if ($LASTEXITCODE -ne 0) {
  throw "[kspacejet-check] installed viewer visualization export smoke failed with exit code $LASTEXITCODE"
}

$protocolTest = Join-Path $repoRoot "tests\apps\application_json_protocol_tests.py"
$runner = Join-Path $repoRoot "tools\devenv\windows\run.ps1"
& $runner python $protocolTest `
    $applicationPaths["ksj.exe"] `
    $applicationPaths["ksj-gateway.exe"] `
    $applicationPaths["ksj-recon.exe"] `
    $applicationPaths["ksj-research.exe"] `
    $applicationPaths["ksj-viewer.exe"]
if ($LASTEXITCODE -ne 0) {
  throw "[kspacejet-check] installed JSON protocol test failed with exit code ${LASTEXITCODE}"
}
