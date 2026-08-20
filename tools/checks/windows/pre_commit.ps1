. "$PSScriptRoot/common.ps1"

$repoRoot = Get-KSpaceJetRepoRoot
Set-Location $repoRoot

$stagedFiles = @(& git diff --cached --name-only --diff-filter=ACMR | Where-Object { $_ })
if ($stagedFiles.Count -eq 0) {
  Write-KSpaceJetNote "no staged files; pre-commit gate passed"
  exit 0
}

$rejectGenerated = $false
foreach ($path in $stagedFiles) {
  $isGeneratedPath = $path -match "^(out/|out-[^/]+/|build/|build-[^/]+/|cmake-build-[^/]+/|CMakeFiles/|Testing/|__pycache__/)"
  $isGeneratedFile = $path -match "(\.pyc|\.pyo|\.o|\.obj|\.a|\.lib|\.so|\.so\..*|\.dylib|\.dll|\.exe|\.pdb|\.log|\.tmp|\.temp)$"
  if ($isGeneratedPath -or $isGeneratedFile -or $path -eq ".DS_Store") {
    Write-Error "[kspacejet-checks] generated or temporary file must not be committed: $path"
    $rejectGenerated = $true
  }
}

if ($rejectGenerated) {
  exit 1
}

& "$PSScriptRoot/format_check.ps1" --staged
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

Write-KSpaceJetNote "checking local Markdown links"
Invoke-KSpaceJetCommand python tools/checks/check_markdown_links.py --project-root $repoRoot

Write-KSpaceJetNote "checking canonical execution-plan dashboard"
Invoke-KSpaceJetCommand python tools/checks/check_execution_plan.py --project-root $repoRoot --check

$cmakeFiles = @()
foreach ($path in $stagedFiles) {
  $fileName = [System.IO.Path]::GetFileName($path)
  $extension = [System.IO.Path]::GetExtension($path)
  if ($fileName -eq "CMakeLists.txt" -or $extension -eq ".cmake") {
    $cmakeFiles += $path
  }
}

if ($cmakeFiles.Count -gt 0) {
  if (-not (Test-KSpaceJetCommand cmake)) {
    Stop-KSpaceJetCheck "cmake is required for CMake checks"
  }

  Write-KSpaceJetNote "checking CMake presets"
  Invoke-KSpaceJetCommand cmake --list-presets=all

  if ($env:KSJ_PRE_COMMIT_CONFIGURE -ne "OFF") {
    Write-KSpaceJetNote "running basic CMake configure for staged CMake changes"
    if (Test-KSpaceJetWindows) {
      Invoke-KSpaceJetCommand cmake --preset windows-vs2022-release
    } else {
      $configureDir = Join-Path $repoRoot "out/checks/pre-commit-cmake"
      $generatorArgs = @()
      if (Test-KSpaceJetCommand ninja) {
        $generatorArgs = @("-G", "Ninja")
      }
      Invoke-KSpaceJetCommand cmake -S $repoRoot -B $configureDir @generatorArgs `
        -DBUILD_TESTING=OFF `
        -DKSJ_BUILD_UNIT_TESTS=OFF `
        -DKSJ_BUILD_BENCHMARKS=OFF `
        -DKSJ_BUILD_RESEARCH=OFF `
    }
  }
}

Write-KSpaceJetNote "pre-commit gate passed"
