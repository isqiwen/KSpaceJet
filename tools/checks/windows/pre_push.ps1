. "$PSScriptRoot/common.ps1"

$repoRoot = Get-KSpaceJetRepoRoot
Set-Location $repoRoot

if (Test-KSpaceJetWindows) {
  $preset = if ($env:KSJ_PRE_PUSH_PRESET) { $env:KSJ_PRE_PUSH_PRESET } else { "windows-vs2022-release" }
  $defaultTargets = "ksj_ismrmrd"
  $targets = Split-KSpaceJetWords $(if ($env:KSJ_PRE_PUSH_TARGETS) { $env:KSJ_PRE_PUSH_TARGETS } else { $defaultTargets })

  Write-KSpaceJetNote "configuring $preset"
  Invoke-KSpaceJetCommand cmake --preset $preset

  Write-KSpaceJetNote "building Windows pre-push targets"
  Invoke-KSpaceJetCommand cmake --build --preset $preset --target @targets

  Write-KSpaceJetNote "Windows pre-push smoke passed"
  exit 0
}

$preset = if ($env:KSJ_PRE_PUSH_PRESET) { $env:KSJ_PRE_PUSH_PRESET } else { "linux-release" }
$defaultTargets = "ksj_base_tests ksj_config_tests ksj_logging_tests ksj_threading_tests ksj_memory_tests ksj_array_eigen_tests ksj_linalg_tests ksj_fft_tests ksj_signal_tests ksj_image_tests ksj_stats_tests ksj_optimization_tests ksj_sparse_tests ksj_special_tests ksj_numerics_header_tests"
$targets = Split-KSpaceJetWords $(if ($env:KSJ_PRE_PUSH_TARGETS) { $env:KSJ_PRE_PUSH_TARGETS } else { $defaultTargets })
$ctestRegex = if ($env:KSJ_PRE_PUSH_CTEST_REGEX) { $env:KSJ_PRE_PUSH_CTEST_REGEX } else { "core|numerics" }

Write-KSpaceJetNote "configuring $preset"
Invoke-KSpaceJetCommand cmake --preset $preset

Write-KSpaceJetNote "building pre-push test targets"
Invoke-KSpaceJetCommand cmake --build --preset $preset --target @targets

Write-KSpaceJetNote "running CTest smoke: $ctestRegex"
Invoke-KSpaceJetCommand ctest --preset $preset -R $ctestRegex --output-on-failure

Write-KSpaceJetNote "pre-push gate passed"
