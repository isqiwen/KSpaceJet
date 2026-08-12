Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

function Get-KSpaceJetRepoRoot {
  $root = & git rev-parse --show-toplevel 2>$null
  if ($LASTEXITCODE -eq 0 -and $root) {
    return ($root | Select-Object -First 1)
  }

  return (Resolve-Path (Join-Path $PSScriptRoot ".." "..")).Path
}

function Write-KSpaceJetNote {
  param([Parameter(Mandatory = $true)][string]$Message)
  Write-Host "[kspacejet-checks] $Message"
}

function Stop-KSpaceJetCheck {
  param([Parameter(Mandatory = $true)][string]$Message)
  Write-Error "[kspacejet-checks] error: $Message"
  exit 1
}

function Test-KSpaceJetCommand {
  param([Parameter(Mandatory = $true)][string]$Name)
  return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function Invoke-KSpaceJetCommand {
  param([Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Command)

  if ($Command.Count -eq 0) {
    Stop-KSpaceJetCheck "empty command"
  }

  Write-Host ("+ " + ($Command -join " "))
  $exe = $Command[0]
  $exeArgs = @()
  if ($Command.Count -gt 1) {
    $exeArgs = $Command[1..($Command.Count - 1)]
  }

  & $exe @exeArgs
  if ($LASTEXITCODE -ne 0) {
    throw "command failed with exit code ${LASTEXITCODE}: $($Command -join ' ')"
  }
}

function Test-KSpaceJetWindows {
  return [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
    [System.Runtime.InteropServices.OSPlatform]::Windows)
}

function Split-KSpaceJetWords {
  param([string]$Value)
  if ([string]::IsNullOrWhiteSpace($Value)) {
    return @()
  }
  return @($Value -split "\s+" | Where-Object { $_ })
}

function Use-KSpaceJetDevelopmentTools {
  $repoRoot = Get-KSpaceJetRepoRoot
  $venvScripts = Join-Path $repoRoot ".venv\Scripts"
  if (Test-Path -LiteralPath $venvScripts -PathType Container) {
    $env:PATH = "$venvScripts$([IO.Path]::PathSeparator)$env:PATH"
  }
}

Use-KSpaceJetDevelopmentTools
