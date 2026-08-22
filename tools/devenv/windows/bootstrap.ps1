param(
  [string]$Prepare = "",
  [switch]$PullLfs,
  [switch]$Offline,
  [switch]$Verify,
  [switch]$NoHooks,
  [switch]$Smoke,
  [switch]$Help
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

function Show-Usage {
  @"
usage: tools/devenv/windows/bootstrap.ps1 [options]

Provision the reproducible KSpaceJet Windows developer environment.

Options:
  -Prepare PRESET  Export local Conan recipes, install dependencies, and
                    configure PRESET (windows-vs2022-debug or
                    windows-vs2022-release).
  -PullLfs         Fetch the Intel Git-LFS payload without preparing.
  -Offline         Do not access the network; verify/use only cached assets.
  -Verify          Verify the existing environment without modifying it.
  -NoHooks         Do not configure repository-local Git hooks.
  -Smoke           Run the lightweight check scripts after provisioning.
  -Help            Show this help.

The script uses winget to install just when it is absent. Git, Git LFS,
Visual Studio 2022 C++ Build Tools (v143), and a Windows SDK are host
prerequisites. It installs pinned uv under .kspacejet/, then uses uv to create
.venv/ and sync the locked developer-tool set. Use just <recipe> for the
shared development commands.
"@ | Write-Host
}

if ($Help) {
  Show-Usage
  exit 0
}

if ($Offline -and $PullLfs) {
  throw "[kspacejet-devenv] -Offline cannot be combined with -PullLfs"
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
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

$uvVersion = $versions["KSJ_UV_VERSION"]
$uvChecksum = $versions["KSJ_UV_WINDOWS_X86_64_SHA256"]
$uvBinaryChecksum = $versions["KSJ_UV_WINDOWS_X86_64_BINARY_SHA256"]
$pythonVersion = $versions["KSJ_PYTHON_VERSION"]
if ([string]::IsNullOrWhiteSpace($uvVersion) -or [string]::IsNullOrWhiteSpace($uvChecksum) -or [string]::IsNullOrWhiteSpace($uvBinaryChecksum) -or [string]::IsNullOrWhiteSpace($pythonVersion)) {
  throw "[kspacejet-devenv] tool version manifest is incomplete"
}

function Write-KSpaceJetDevNote {
  param([Parameter(Mandatory = $true)][string]$Message)
  Write-Host "[kspacejet-devenv] $Message"
}

function Stop-KSpaceJetDev {
  param([Parameter(Mandatory = $true)][string]$Message)
  throw "[kspacejet-devenv] $Message"
}

function Require-HostCommand {
  param([Parameter(Mandatory = $true)][string]$Name)
  if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
    Stop-KSpaceJetDev "host prerequisite is missing: $Name"
  }
}

function Invoke-KSpaceJetDevNative {
  param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string[]]$Arguments
  )

  & $Executable @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "[kspacejet-devenv] command failed with exit code ${LASTEXITCODE}: $Executable $($Arguments -join ' ')"
  }
}

function Add-WingetJustToPath {
  $wingetPackages = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages"
  $justExecutables = @(Get-ChildItem -LiteralPath $wingetPackages -Filter "just.exe" -File -Recurse -ErrorAction SilentlyContinue |
      Where-Object { $_.FullName -match "Casey[.]Just_" })
  if ($justExecutables.Count -ne 1) {
    return $false
  }
  $justDirectory = Split-Path -Parent $justExecutables[0].FullName
  $pathSeparator = [IO.Path]::PathSeparator
  $env:PATH = "$justDirectory$pathSeparator$env:PATH"

  $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
  $userPathEntries = @($userPath -split [regex]::Escape($pathSeparator) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
  if (-not ($userPathEntries | Where-Object { $_.TrimEnd('\\') -ieq $justDirectory.TrimEnd('\\') })) {
    [Environment]::SetEnvironmentVariable("Path", "$justDirectory$pathSeparator$userPath", "User")
  }

  Publish-UserEnvironmentChange

  return [bool](Get-Command "just" -ErrorAction SilentlyContinue)
}

function Publish-UserEnvironmentChange {
  if (-not ("KSpaceJet.EnvironmentChangeNotification" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

namespace KSpaceJet {
  public static class EnvironmentChangeNotification {
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr SendMessageTimeout(
      IntPtr hWnd,
      uint message,
      IntPtr wParam,
      string lParam,
      uint flags,
      uint timeout,
      out IntPtr result);
  }
}
"@
  }

  $result = [IntPtr]::Zero
  [void][KSpaceJet.EnvironmentChangeNotification]::SendMessageTimeout(
      [IntPtr]0xffff,
      0x001a,
      [IntPtr]::Zero,
      "Environment",
      0x0002,
      5000,
      [ref]$result)
}

function Ensure-HostJust {
  if (Get-Command "just" -ErrorAction SilentlyContinue) {
    return
  }
  if (Add-WingetJustToPath) {
    return
  }
  if ($Verify) {
    Stop-KSpaceJetDev "host prerequisite is missing: just; rerun bootstrap without -Verify to install it"
  }
  if ($Offline) {
    Stop-KSpaceJetDev "host prerequisite is missing: just while -Offline is set"
  }

  Require-HostCommand "winget"
  Write-KSpaceJetDevNote "installing host just with winget"
  Invoke-KSpaceJetDevNative -Executable "winget" -Arguments @(
    "install",
    "--id",
    "Casey.Just",
    "--exact",
    "--accept-package-agreements",
    "--accept-source-agreements")

  if (-not (Add-WingetJustToPath)) {
    Stop-KSpaceJetDev "winget installed just but its executable could not be located"
  }
}

function Test-HostPrerequisites {
  if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString() -ne "X64") {
    Stop-KSpaceJetDev "only Windows x86_64 is currently supported"
  }

  Require-HostCommand "git"
  Require-HostCommand "git-lfs"

  $programFilesX86 = ${env:ProgramFiles(x86)}
  if ([string]::IsNullOrWhiteSpace($programFilesX86)) {
    Stop-KSpaceJetDev "cannot locate Program Files (x86) to verify Visual Studio 2022"
  }
  $vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    Stop-KSpaceJetDev "Visual Studio Installer/vswhere is missing; install Visual Studio 2022 Build Tools with the C++ workload and Windows SDK"
  }

  $installationPath = & $vswhere -latest -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installationPath)) {
    Stop-KSpaceJetDev "Visual Studio 2022 v143 C++ Build Tools are missing; install the C++ workload and a Windows SDK"
  }

  $toolsetRoot = Join-Path $installationPath "VC\Tools\MSVC"
  $msvc194Toolsets = @(Get-ChildItem -LiteralPath $toolsetRoot -Directory -ErrorAction SilentlyContinue |
      Where-Object { $_.Name -match "^14\.4" })
  if ($msvc194Toolsets.Count -eq 0) {
    Stop-KSpaceJetDev "MSVC 19.4x (toolset 14.4x, Conan msvc/194) is required by the Windows profile"
  }

  $windowsSdkInclude = Join-Path $programFilesX86 "Windows Kits\10\Include"
  if (-not (Test-Path -LiteralPath $windowsSdkInclude -PathType Container)) {
    Stop-KSpaceJetDev "a Windows 10 or 11 SDK is required by the Windows profile"
  }
}

$uvRoot = Join-Path $repoRoot ".kspacejet\bootstrap\uv\$uvVersion\windows-x86_64"
$uvPath = Join-Path $uvRoot "uv.exe"

function Test-ProjectUv {
  if (-not (Test-Path -LiteralPath $uvPath -PathType Leaf)) {
    return $false
  }
  try {
    $reported = & $uvPath --version
    $uvExitCode = $LASTEXITCODE
    $actualChecksum = (Get-FileHash -LiteralPath $uvPath -Algorithm SHA256).Hash.ToLowerInvariant()
    return $uvExitCode -eq 0 -and (($reported -split "\s+")[1] -eq $uvVersion) -and $actualChecksum -eq $uvBinaryChecksum
  } catch {
    return $false
  }
}

function Install-ProjectUv {
  if (Test-ProjectUv) {
    Write-KSpaceJetDevNote "using pinned project-local uv $uvVersion"
    return
  }
  if ($Verify) {
    Stop-KSpaceJetDev "project-local uv $uvVersion is absent or invalid; -Verify never downloads tools"
  }
  if ($Offline) {
    Stop-KSpaceJetDev "project-local uv $uvVersion is absent or invalid while -Offline is set"
  }

  $temporaryDirectory = Join-Path ([IO.Path]::GetTempPath()) ("kspacejet-uv-" + [Guid]::NewGuid().ToString("N"))
  New-Item -ItemType Directory -Path $temporaryDirectory -Force | Out-Null
  try {
    $archivePath = Join-Path $temporaryDirectory "uv.zip"
    $url = "https://github.com/astral-sh/uv/releases/download/$uvVersion/uv-x86_64-pc-windows-msvc.zip"
    Write-KSpaceJetDevNote "downloading pinned uv $uvVersion"
    Invoke-WebRequest -Uri $url -OutFile $archivePath -UseBasicParsing

    $actualChecksum = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualChecksum -ne $uvChecksum) {
      Stop-KSpaceJetDev "uv archive SHA-256 mismatch"
    }

    $extractDirectory = Join-Path $temporaryDirectory "extract"
    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractDirectory -Force
    $candidates = @(Get-ChildItem -LiteralPath $extractDirectory -Filter "uv.exe" -File -Recurse)
    if ($candidates.Count -ne 1) {
      Stop-KSpaceJetDev "unexpected uv archive layout"
    }

    $reported = & $candidates[0].FullName --version
    if ($LASTEXITCODE -ne 0 -or (($reported -split "\s+")[1] -ne $uvVersion)) {
      Stop-KSpaceJetDev "downloaded uv reports an unexpected version"
    }

    New-Item -ItemType Directory -Path $uvRoot -Force | Out-Null
    $temporaryTarget = "$uvPath.new"
    Copy-Item -LiteralPath $candidates[0].FullName -Destination $temporaryTarget -Force
    if (Test-Path -LiteralPath $uvPath -PathType Leaf) {
      Remove-Item -LiteralPath $uvPath -Force
    }
    Move-Item -LiteralPath $temporaryTarget -Destination $uvPath -Force
    if (-not (Test-ProjectUv)) {
      Stop-KSpaceJetDev "installed uv executable failed integrity verification"
    }
  } finally {
    if (Test-Path -LiteralPath $temporaryDirectory) {
      Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
    }
  }
}

function Set-UvProjectPaths {
  $env:UV_CACHE_DIR = Join-Path $repoRoot ".kspacejet\uv-cache"
  $env:UV_PYTHON_INSTALL_DIR = Join-Path $repoRoot ".kspacejet\python"
  $env:UV_MANAGED_PYTHON = "1"
  if ($Offline) {
    $env:UV_OFFLINE = "1"
  }
}

function Sync-PythonTools {
  Set-UvProjectPaths
  if ($Verify) {
    Invoke-KSpaceJetDevNative -Executable $uvPath -Arguments @("sync", "--locked", "--no-install-project", "--check")
    return
  }

  Write-KSpaceJetDevNote "installing managed CPython $pythonVersion and synchronizing .venv"
  Invoke-KSpaceJetDevNative -Executable $uvPath -Arguments @("python", "install", "--managed-python", "--no-bin", $pythonVersion)
  Invoke-KSpaceJetDevNative -Executable $uvPath -Arguments @("sync", "--locked", "--no-install-project")
}

function Configure-Repository {
  if ($Verify) {
    return
  }

  Write-KSpaceJetDevNote "configuring repository-local Git and Git-LFS settings"
  Invoke-KSpaceJetDevNative -Executable "git" -Arguments @("config", "--local", "core.autocrlf", "input")
  Invoke-KSpaceJetDevNative -Executable "git" -Arguments @("config", "--local", "core.eol", "lf")
  Invoke-KSpaceJetDevNative -Executable "git" -Arguments @("config", "--local", "fetch.prune", "true")
  Invoke-KSpaceJetDevNative -Executable "git" -Arguments @("config", "--local", "pull.ff", "only")
  # The project owns .githooks/pre-push and already delegates to `git lfs
  # pre-push`; configure local LFS filters without overwriting that hook.
  Invoke-KSpaceJetDevNative -Executable "git" -Arguments @("lfs", "install", "--local", "--skip-repo")

  if (-not $NoHooks) {
    & (Join-Path $repoRoot "tools\checks\windows\install_hooks.ps1")
    if ($LASTEXITCODE -ne 0) {
      throw "[kspacejet-devenv] failed to install Git hooks"
    }
  }

  if ($PullLfs) {
    Write-KSpaceJetDevNote "fetching Intel Git-LFS payload"
    Invoke-KSpaceJetDevNative -Executable "git" -Arguments @("lfs", "pull", "--include=third_party/intel/payload/**")
  }
}

$toolRunner = Join-Path $repoRoot "tools\devenv\windows\run.ps1"
$intelPayloadVerifier = Join-Path $repoRoot "tools\devenv\verify_intel_payload.py"

function Invoke-ProjectTool {
  param([Parameter(Mandatory = $true)][string[]]$Arguments)
  & $toolRunner @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "[kspacejet-devenv] project tool failed with exit code ${LASTEXITCODE}: $($Arguments -join ' ')"
  }
}

function Test-IntelPayloadPresent {
  & $toolRunner python $intelPayloadVerifier --platform windows-x86_64 --full --quiet *> $null
  return ($LASTEXITCODE -eq 0)
}

function Ensure-IntelPayloadForPrepare {
  if (Test-IntelPayloadPresent) {
    return
  }
  if ($Offline) {
    Stop-KSpaceJetDev "Intel payload is absent or incomplete while -Offline is set"
  }
  Write-KSpaceJetDevNote "Intel payload is incomplete; fetching the Git-LFS payload required by -Prepare"
  Invoke-KSpaceJetDevNative -Executable "git" -Arguments @("lfs", "pull", "--include=third_party/intel/payload/**")
  & $toolRunner python $intelPayloadVerifier --platform windows-x86_64 --full
  if ($LASTEXITCODE -ne 0) {
    Stop-KSpaceJetDev "Git-LFS finished without the required Intel payload"
  }
}

function Prepare-Build {
  if ([string]::IsNullOrWhiteSpace($Prepare)) {
    return
  }
  if ($Verify) {
    Stop-KSpaceJetDev "-Verify cannot be combined with -Prepare"
  }
  Ensure-IntelPayloadForPrepare

  $profile = ""
  $outputFolder = ""
  switch ($Prepare) {
    "windows-vs2022-debug" {
      $profile = "conan/profiles/windows-msvc2022-debug"
      $outputFolder = "out/build/windows-vs2022-debug"
    }
    "windows-vs2022-release" {
      $profile = "conan/profiles/windows-msvc2022-release"
      $outputFolder = "out/build/windows-vs2022-release"
    }
    default {
      Stop-KSpaceJetDev "unsupported Windows prepare preset: $Prepare"
    }
  }

  Write-KSpaceJetDevNote "preparing $Prepare"
  Invoke-ProjectTool -Arguments @("conan", "export", "conan/recipes/ismrmrd", "--user=kspacejet", "--channel=stable")
  Invoke-ProjectTool -Arguments @("conan", "export", "third_party/intel", "--user=kspacejet", "--channel=stable")
  $conanInstallArguments = @("conan", "install", ".", "--output-folder=$outputFolder", "--profile:host=$profile", "--build=missing")
  if ($Offline) {
    $conanInstallArguments += "--no-remote"
  }
  Invoke-ProjectTool -Arguments $conanInstallArguments
  Invoke-ProjectTool -Arguments @("cmake", "--preset", $Prepare)
}

function Show-Versions {
  Write-KSpaceJetDevNote "tool versions"
  Invoke-KSpaceJetDevNative -Executable "just" -Arguments @("--version")
  Invoke-ProjectTool -Arguments @("python", "--version")
  Invoke-ProjectTool -Arguments @("conan", "--version")
  Invoke-ProjectTool -Arguments @("cmake", "--version")
  Invoke-ProjectTool -Arguments @("ninja", "--version")
  Invoke-ProjectTool -Arguments @("clang-format", "--version")
  Invoke-ProjectTool -Arguments @("cmake-format", "--version")
}

function Invoke-SmokeChecks {
  if (-not $Smoke) {
    return
  }
  if ($Verify) {
    Stop-KSpaceJetDev "-Verify cannot be combined with -Smoke"
  }
  Invoke-ProjectTool -Arguments @("just", "pre-commit")
  Invoke-ProjectTool -Arguments @("just", "format-changed")
}

Set-Location $repoRoot
Ensure-HostJust
Test-HostPrerequisites
Install-ProjectUv
Sync-PythonTools
Configure-Repository
Prepare-Build
Show-Versions
Invoke-SmokeChecks
Write-KSpaceJetDevNote "shared commands: just --list"
Write-KSpaceJetDevNote "developer environment is ready"
