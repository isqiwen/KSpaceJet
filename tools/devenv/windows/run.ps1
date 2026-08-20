param(
  [Parameter(Mandatory = $true, Position = 0, ValueFromRemainingArguments = $true)]
  [string[]]$Command
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$venvScripts = Join-Path $repoRoot ".venv\Scripts"
$venvPython = Join-Path $venvScripts "python.exe"
$manifestPath = Join-Path $PSScriptRoot "..\tool-versions.env"
$versions = @{}
foreach ($line in Get-Content -LiteralPath $manifestPath) {
  $trimmed = $line.Trim()
  if ($trimmed.Length -eq 0 -or $trimmed.StartsWith("#")) {
    continue
  }
  if ($trimmed -notmatch "^([A-Z0-9_]+)=(.+)$") {
    throw "[kspacejet-devenv] invalid tool version manifest line: $line"
  }
  $versions[$matches[1]] = $matches[2]
}

$justVersion = $versions["KSJ_JUST_VERSION"]
if ([string]::IsNullOrWhiteSpace($justVersion)) {
  throw "[kspacejet-devenv] tool version manifest does not define KSJ_JUST_VERSION"
}
$justRoot = Join-Path $repoRoot ".kspacejet\bootstrap\just\$justVersion\windows-x86_64"
$justPath = Join-Path $justRoot "just.exe"

if (-not (Test-Path -LiteralPath $venvPython -PathType Leaf)) {
  throw "[kspacejet-devenv] .venv is unavailable; run tools/devenv/windows/bootstrap.ps1 first"
}

if ($Command.Count -eq 0) {
  throw "[kspacejet-devenv] expected a command to run"
}

if ($Command[0] -in @("just", "just.exe") -and -not (Test-Path -LiteralPath $justPath -PathType Leaf)) {
  throw "[kspacejet-devenv] project-local just is unavailable; run tools/devenv/windows/bootstrap.ps1 first"
}

$pathEntries = @($venvScripts)
if (Test-Path -LiteralPath $justPath -PathType Leaf) {
  $pathEntries = @($justRoot) + $pathEntries
}
$pathSeparator = [IO.Path]::PathSeparator
$env:PATH = "$($pathEntries -join $pathSeparator)$pathSeparator$env:PATH"
$executable = $Command[0]
$arguments = @()
if ($Command.Count -gt 1) {
  $arguments = $Command[1..($Command.Count - 1)]
}

& $executable @arguments
exit $LASTEXITCODE
