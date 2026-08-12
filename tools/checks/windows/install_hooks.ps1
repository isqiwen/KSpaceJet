. "$PSScriptRoot/common.ps1"

$repoRoot = Get-KSpaceJetRepoRoot
Set-Location $repoRoot

Invoke-KSpaceJetCommand git config core.hooksPath .githooks
Write-KSpaceJetNote "installed git hooks from .githooks"
Write-KSpaceJetNote "pre-commit: staged format/generated-file/CMake checks"
Write-KSpaceJetNote "commit-msg: normalized Conventional Commit subject check"
Write-KSpaceJetNote "pre-push: Git LFS upload check is active; KSpaceJet smoke is disabled by default"
Write-KSpaceJetNote "pre-push smoke: run tools/checks/windows/pre_push.ps1 manually for local smoke"
