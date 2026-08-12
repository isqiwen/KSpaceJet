param([Parameter(Mandatory = $true)][string]$MessageFile)

. "$PSScriptRoot/common.ps1"

if (-not (Test-Path -LiteralPath $MessageFile -PathType Leaf)) {
  Stop-KSpaceJetCheck "commit message file does not exist: $MessageFile"
}

$subject = Get-Content -LiteralPath $MessageFile |
  Where-Object { $_ -notmatch "^\s*#" -and $_ -notmatch "^\s*$" } |
  Select-Object -First 1

if ([string]::IsNullOrWhiteSpace($subject)) {
  Stop-KSpaceJetCheck "commit message subject must not be empty"
}

if ($subject.Length -gt 120) {
  Stop-KSpaceJetCheck "commit message subject is too long ($($subject.Length)/120): $subject"
}

if ($subject -match "^(Merge |Revert |fixup! |squash! )") {
  Write-KSpaceJetNote "commit message gate passed"
  exit 0
}

$commitPattern = "^(build|chore|ci|docs|feat|fix|perf|refactor|revert|style|test)(\([A-Za-z0-9._/-]+\))?!?: .+"
if ($subject -notmatch $commitPattern) {
  Write-Error @"
[kspacejet-checks] error: commit message subject is not normalized:
  $subject

Expected:
  type(scope): subject
  type: subject

Accepted types:
  build, chore, ci, docs, feat, fix, perf, refactor, revert, style, test

Examples:
  docs: align developer setup documentation
  fix(kspacejet-fe): handle missing runtime config
  build(cmake): update numerics benchmark target
"@
  exit 1
}

Write-KSpaceJetNote "commit message gate passed"
