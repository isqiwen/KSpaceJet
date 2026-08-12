#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${script_dir}/common.sh"

repo_root="$(ksj_repo_root)"
cd "${repo_root}"

preset="${KSJ_CI_PRESET:-linux-release-unit-tests}"

ksj_note "configuring ${preset}"
ksj_run cmake --preset "${preset}"

ksj_note "building ${preset}"
ksj_run cmake --build --preset "${preset}"

ksj_note "running full unit test suite"
ksj_run ctest --preset "${preset}" --output-on-failure
