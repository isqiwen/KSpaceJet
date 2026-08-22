param(
  [Parameter(Mandatory = $true, Position = 0, ValueFromRemainingArguments = $true)]
  [string[]]$Command
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$venvScripts = Join-Path $repoRoot ".venv\Scripts"
$venvPython = Join-Path $venvScripts "python.exe"

if (-not (Test-Path -LiteralPath $venvPython -PathType Leaf)) {
  throw "[kspacejet-devenv] .venv is unavailable; run tools/devenv/windows/bootstrap.ps1 first"
}

if ($Command.Count -eq 0) {
  throw "[kspacejet-devenv] expected a command to run"
}

$pathSeparator = [IO.Path]::PathSeparator
$env:PATH = "$venvScripts$pathSeparator$env:PATH"
$executable = $Command[0]
$arguments = @()
if ($Command.Count -gt 1) {
  $arguments = $Command[1..($Command.Count - 1)]
}

& $executable @arguments
exit $LASTEXITCODE
