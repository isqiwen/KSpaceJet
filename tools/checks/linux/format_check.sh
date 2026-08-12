#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${script_dir}/common.sh"

usage() {
  cat <<'EOF'
usage: tools/checks/linux/format_check.sh [--staged|--all|--changed [BASE]]

Checks C/C++ files with clang-format and CMake files with cmake-format when
cmake-format is installed. Set KSJ_REQUIRE_CMAKE_FORMAT=1 to fail when
cmake-format is missing and CMake files are in scope.
EOF
}

mode="--staged"
base_ref=""
if [[ $# -gt 0 ]]; then
  mode="$1"
  shift
fi

if [[ "${mode}" == "--changed" ]]; then
  base_ref="${1:-}"
elif [[ "${mode}" != "--staged" && "${mode}" != "--all" ]]; then
  usage
  exit 2
fi

repo_root="$(ksj_repo_root)"
cd "${repo_root}"

files=()
if [[ "${mode}" == "--staged" ]]; then
  mapfile -t files < <(git diff --cached --name-only --diff-filter=ACMR | sed '/^$/d')
elif [[ "${mode}" == "--all" ]]; then
  mapfile -t files < <(git ls-files | sed '/^$/d')
else
  if [[ -z "${base_ref}" ]]; then
    if [[ -n "${GITHUB_BASE_REF:-}" ]]; then
      base_ref="origin/${GITHUB_BASE_REF}"
    elif git rev-parse --verify HEAD^ >/dev/null 2>&1; then
      base_ref="HEAD^"
    else
      base_ref="HEAD"
    fi
  fi

  if ! git rev-parse --verify "${base_ref}" >/dev/null 2>&1; then
    ksj_die "format check base ref does not exist: ${base_ref}"
  fi

  if git merge-base "${base_ref}" HEAD >/dev/null 2>&1; then
    diff_files="$(git diff --name-only --diff-filter=ACMR "${base_ref}...HEAD")" ||
      ksj_die "failed to compute changed files from ${base_ref} to HEAD"
    mapfile -t files < <(printf '%s\n' "${diff_files}" | sed '/^$/d')
  elif git rev-parse --verify HEAD^ >/dev/null 2>&1; then
    ksj_note "no merge base between ${base_ref} and HEAD; checking changes introduced by HEAD"
    mapfile -t files < <(git diff --name-only --diff-filter=ACMR HEAD^ HEAD | sed '/^$/d')
  else
    ksj_note "no merge base between ${base_ref} and HEAD; checking all tracked files"
    mapfile -t files < <(git ls-files | sed '/^$/d')
  fi
fi

cpp_files=()
cmake_files=()
for path in "${files[@]}"; do
  [[ -f "${path}" ]] || continue
  case "${path}" in
    *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.hxx|*.ipp)
      cpp_files+=("${path}")
      ;;
    CMakeLists.txt|*/CMakeLists.txt|*.cmake)
      cmake_files+=("${path}")
      ;;
  esac
done

if [[ ${#cpp_files[@]} -gt 0 ]]; then
  ksj_has_command clang-format || ksj_die "clang-format is required for C/C++ format checks"
  ksj_note "checking ${#cpp_files[@]} C/C++ file(s) with clang-format"
  ksj_run clang-format --dry-run --Werror "${cpp_files[@]}"
fi

if [[ ${#cmake_files[@]} -gt 0 ]]; then
  if ksj_has_command cmake-format; then
    ksj_note "checking ${#cmake_files[@]} CMake file(s) with cmake-format"
    ksj_run cmake-format --check "${cmake_files[@]}"
  elif [[ "${KSJ_REQUIRE_CMAKE_FORMAT:-0}" == "1" ]]; then
    ksj_die "cmake-format is required for CMake format checks"
  else
    ksj_note "cmake-format is not installed; skipped CMake format check"
  fi
fi

if [[ ${#cpp_files[@]} -eq 0 && ${#cmake_files[@]} -eq 0 ]]; then
  ksj_note "no format-checked files in scope"
fi
