#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${script_dir}/common.sh"

repo_root="$(ksj_repo_root)"
cd "${repo_root}"

preset="${KSJ_CI_CHECK_PRESET:-linux-release}"

if [[ "${KSJ_FORMAT_SCOPE:-changed}" == "all" ]]; then
  bash "${script_dir}/format_check.sh" --all
elif [[ -n "${KSJ_FORMAT_BASE:-}" ]]; then
  bash "${script_dir}/format_check.sh" --changed "${KSJ_FORMAT_BASE}"
elif [[ -n "${GITHUB_BASE_REF:-}" ]]; then
  bash "${script_dir}/format_check.sh" --changed "origin/${GITHUB_BASE_REF}"
elif git rev-parse --verify HEAD^ >/dev/null 2>&1; then
  bash "${script_dir}/format_check.sh" --changed HEAD^
else
  bash "${script_dir}/format_check.sh" --all
fi

ksj_note "configuring ${preset}"
ksj_run cmake --preset "${preset}"
