param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)

. "$PSScriptRoot/common.ps1"

function Show-Usage {
  @"
usage: tools/checks/windows/format_check.ps1 [--staged|--all|--changed [BASE]]

Checks C/C++ files with clang-format and CMake files with cmake-format when
cmake-format is installed. Set KSJ_REQUIRE_CMAKE_FORMAT=1 to fail when
cmake-format is missing and CMake files are in scope.
"@ | Write-Host
}

$mode = "--staged"
$baseRef = ""
if ($Arguments.Count -gt 0) {
  $mode = $Arguments[0]
}

if ($mode -eq "--changed") {
  if ($Arguments.Count -gt 1) {
    $baseRef = $Arguments[1]
  }
} elseif ($mode -ne "--staged" -and $mode -ne "--all") {
  Show-Usage
  exit 2
}

$repoRoot = Get-KSpaceJetRepoRoot
Set-Location $repoRoot

$files = @()
if ($mode -eq "--staged") {
  $files = @(& git diff --cached --name-only --diff-filter=ACMR | Where-Object { $_ })
} elseif ($mode -eq "--all") {
  $files = @(& git ls-files | Where-Object { $_ })
} else {
  if ([string]::IsNullOrWhiteSpace($baseRef)) {
    if ($env:GITHUB_BASE_REF) {
      $baseRef = "origin/$($env:GITHUB_BASE_REF)"
    } else {
      & git rev-parse --verify "HEAD^" *> $null
      $baseRef = if ($LASTEXITCODE -eq 0) { "HEAD^" } else { "HEAD" }
    }
  }

  & git rev-parse --verify $baseRef *> $null
  if ($LASTEXITCODE -ne 0) {
    Stop-KSpaceJetCheck "format check base ref does not exist: $baseRef"
  }

  $files = @(& git diff --name-only --diff-filter=ACMR "$baseRef...HEAD" | Where-Object { $_ })
}

$cppFiles = @()
$cmakeFiles = @()
foreach ($path in $files) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    continue
  }

  $fileName = [System.IO.Path]::GetFileName($path)
  $extension = [System.IO.Path]::GetExtension($path)
  if ($extension -in @(".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".ipp")) {
    $cppFiles += $path
  } elseif ($fileName -eq "CMakeLists.txt" -or $extension -eq ".cmake") {
    $cmakeFiles += $path
  }
}

if ($cppFiles.Count -gt 0) {
  if (-not (Test-KSpaceJetCommand clang-format)) {
    Stop-KSpaceJetCheck "clang-format is required for C/C++ format checks"
  }
  Write-KSpaceJetNote "checking $($cppFiles.Count) C/C++ file(s) with clang-format"
  Invoke-KSpaceJetCommand clang-format --dry-run --Werror @cppFiles
}

if ($cmakeFiles.Count -gt 0) {
  if (Test-KSpaceJetCommand cmake-format) {
    Write-KSpaceJetNote "checking $($cmakeFiles.Count) CMake file(s) with cmake-format"
    Invoke-KSpaceJetCommand cmake-format --check @cmakeFiles
  } elseif ($env:KSJ_REQUIRE_CMAKE_FORMAT -eq "1") {
    Stop-KSpaceJetCheck "cmake-format is required for CMake format checks"
  } else {
    Write-KSpaceJetNote "cmake-format is not installed; skipped CMake format check"
  }
}

if ($cppFiles.Count -eq 0 -and $cmakeFiles.Count -eq 0) {
  Write-KSpaceJetNote "no format-checked files in scope"
}
