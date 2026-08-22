param(
  [Parameter(Mandatory = $true)]
  [string]$InstallBinDirectory
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$binDirectory = (Resolve-Path -LiteralPath $InstallBinDirectory).Path
$applications = @("ksj.exe", "ksj-gateway.exe", "ksj-recon.exe", "ksj-research.exe")
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

$protocolTest = Join-Path $repoRoot "tests\apps\application_json_protocol_tests.py"
$runner = Join-Path $repoRoot "tools\devenv\windows\run.ps1"
& $runner python $protocolTest `
    $applicationPaths["ksj.exe"] `
    $applicationPaths["ksj-gateway.exe"] `
    $applicationPaths["ksj-recon.exe"] `
    $applicationPaths["ksj-research.exe"]
if ($LASTEXITCODE -ne 0) {
  throw "[kspacejet-check] installed JSON protocol test failed with exit code ${LASTEXITCODE}"
}
