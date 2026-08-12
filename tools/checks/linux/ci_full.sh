#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${script_dir}/common.sh"

"${script_dir}/ci_check.sh"
"${script_dir}/ci_unit.sh"

if [[ "${KSJ_RELEASE_BENCHMARK_SWEEP:-0}" == "1" ]]; then
  "${script_dir}/benchmark_smoke.sh" --full
else
  "${script_dir}/benchmark_smoke.sh"
fi
