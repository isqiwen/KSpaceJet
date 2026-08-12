#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${script_dir}/common.sh"

repo_root="$(ksj_repo_root)"
cd "${repo_root}"

preset="${KSJ_PRE_PUSH_PRESET:-linux-release}"
default_targets="ksj_base_tests ksj_config_tests ksj_logging_tests ksj_threading_tests ksj_memory_tests ksj_array_eigen_tests ksj_linalg_tests ksj_fft_tests ksj_signal_tests ksj_image_tests ksj_stats_tests ksj_optimization_tests ksj_sparse_tests ksj_special_tests ksj_numerics_header_tests"
read -r -a targets <<< "${KSJ_PRE_PUSH_TARGETS:-${default_targets}}"
ctest_regex="${KSJ_PRE_PUSH_CTEST_REGEX:-core|numerics}"

ksj_note "configuring ${preset}"
ksj_run cmake --preset "${preset}"

ksj_note "building pre-push test targets"
ksj_run cmake --build --preset "${preset}" --target "${targets[@]}"

ksj_note "running CTest smoke: ${ctest_regex}"
ksj_run ctest --preset "${preset}" -R "${ctest_regex}" --output-on-failure

ksj_note "pre-push gate passed"
