#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${script_dir}/common.sh"

repo_root="$(ksj_repo_root)"
cd "${repo_root}"

ksj_run git config core.hooksPath .githooks
ksj_note "installed git hooks from .githooks"
ksj_note "pre-commit: staged format/local-Markdown-link/generated-file/CMake checks"
ksj_note "commit-msg: normalized Conventional Commit subject check"
ksj_note "pre-push: Git LFS upload check is active; KSpaceJet smoke is disabled by default"
ksj_note "pre-push smoke: run just pre-push manually for linux-release smoke"
