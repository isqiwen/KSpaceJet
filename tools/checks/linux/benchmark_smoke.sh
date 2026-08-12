#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${script_dir}/common.sh"

usage() {
  cat <<'EOF'
usage: tools/checks/linux/benchmark_smoke.sh [--full] [--iterations N] [--trials N]
                                               [--module-sizes MODULE=A,B,C]
                                               [--out-dir DIR] [--baseline-dir DIR]
                                               [--save-baseline-dir DIR]

Runs the KSpaceJet numerics benchmark suite with the same Intel threading environment
used by production launch scripts. --full uses the production sweep defaults.
EOF
}

full=0
iterations=""
trials=""
module_sizes=()
out_dir=""
baseline_dir="${KSJ_NUMERICS_BENCHMARK_BASELINE:-}"
save_baseline_dir=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --full)
      full=1
      shift
      ;;
    --iterations)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      iterations="$2"
      shift 2
      ;;
    --trials)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      trials="$2"
      shift 2
      ;;
    --module-sizes)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      module_sizes+=("$2")
      shift 2
      ;;
    --out-dir)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      out_dir="$2"
      shift 2
      ;;
    --baseline-dir)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      baseline_dir="$2"
      shift 2
      ;;
    --save-baseline-dir)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      save_baseline_dir="$2"
      shift 2
      ;;
    --help)
      usage
      exit 0
      ;;
    *)
      usage
      exit 2
      ;;
  esac
done

if [[ "${full}" -eq 1 ]]; then
  iterations="${iterations:-50}"
  trials="${trials:-9}"
  gate_mode="policy"
else
  iterations="${iterations:-3}"
  trials="${trials:-5}"
  if [[ "${#module_sizes[@]}" -eq 0 ]]; then
    module_sizes=(
      "array=256,1024"
      "linalg=16,32"
      "fft=16,32"
      "signal=16,32"
      "image=16,32"
      "stats=16,32"
      "optimization=16,32"
      "sparse=16,32"
      "special=16,32"
    )
  fi
  gate_mode="smoke"
fi

repo_root="$(ksj_repo_root)"
cd "${repo_root}"

preset="${KSJ_BENCHMARK_PRESET:-linux-release-benchmark}"
benchmark_targets=(
  ksj_array_backend_benchmark
  ksj_linalg_backend_benchmark
  ksj_fft_backend_benchmark
  ksj_signal_backend_benchmark
  ksj_image_backend_benchmark
  ksj_stats_backend_benchmark
  ksj_optimization_backend_benchmark
  ksj_sparse_backend_benchmark
  ksj_special_backend_benchmark
)

ksj_note "configuring ${preset}"
ksj_run cmake --preset "${preset}"

ksj_note "building benchmark targets"
ksj_run cmake --build --preset "${preset}" --target "${benchmark_targets[@]}"

runner_args=(
  tools/ksj_numerics_benchmark/run.py
  --bin-dir "${repo_root}/out/build/${preset}/bin"
  --iterations "${iterations}"
  --trials "${trials}"
  --gate-mode "${gate_mode}"
)

for module_size in "${module_sizes[@]}"; do
  runner_args+=(--module-sizes "${module_size}")
done

if [[ -n "${out_dir}" ]]; then
  runner_args+=(--out-dir "${out_dir}")
fi
if [[ -n "${baseline_dir}" ]]; then
  runner_args+=(--baseline-dir "${baseline_dir}")
fi
if [[ -n "${save_baseline_dir}" ]]; then
  runner_args+=(--save-baseline-dir "${save_baseline_dir}")
fi

ksj_note "running numerics benchmark suite"
ksj_run python3 "${runner_args[@]}"
