#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${script_dir}/common.sh"

repo_root="$(ksj_repo_root)"
cd "${repo_root}"

mapfile -t staged_files < <(git diff --cached --name-only --diff-filter=ACMR | sed '/^$/d')

if [[ ${#staged_files[@]} -eq 0 ]]; then
  ksj_note "no staged files; pre-commit gate passed"
  exit 0
fi

reject_generated=0
for path in "${staged_files[@]}"; do
  case "${path}" in
    out/*|out-*/*|build/*|build-*/*|cmake-build-*/*|CMakeFiles/*|Testing/*|__pycache__/*|*.pyc|*.pyo|*.o|*.obj|*.a|*.lib|*.so|*.so.*|*.dylib|*.dll|*.exe|*.pdb|*.log|*.tmp|*.temp|.DS_Store)
      printf '[kspacejet-checks] generated or temporary file must not be committed: %s\n' "${path}" >&2
      reject_generated=1
      ;;
  esac
done

if [[ "${reject_generated}" -ne 0 ]]; then
  exit 1
fi

"${script_dir}/format_check.sh" --staged

ksj_note "checking local Markdown links"
ksj_run python3 tools/checks/check_markdown_links.py --project-root "${repo_root}"

ksj_note "checking canonical execution-plan dashboard"
ksj_run python3 tools/checks/check_execution_plan.py --project-root "${repo_root}" --check

cmake_files=()
for path in "${staged_files[@]}"; do
  case "${path}" in
    CMakeLists.txt|*/CMakeLists.txt|*.cmake)
      cmake_files+=("${path}")
      ;;
  esac
done

if [[ ${#cmake_files[@]} -gt 0 ]]; then
  ksj_has_command cmake || ksj_die "cmake is required for CMake checks"
  ksj_note "checking CMake presets"
  ksj_run cmake --list-presets=all

  if [[ "${KSJ_PRE_COMMIT_CONFIGURE:-ON}" != "OFF" ]]; then
    toolchain_file="${KSJ_PRE_COMMIT_TOOLCHAIN_FILE:-${repo_root}/out/build/linux-release/conan_toolchain.cmake}"
    if [[ -f "${toolchain_file}" ]]; then
      configure_dir="${repo_root}/out/checks/pre-commit-cmake-conan"
      generator_args=()
      if ksj_has_command ninja; then
        generator_args=(-G Ninja)
      fi

      ksj_note "running basic CMake configure for staged CMake changes"
      ksj_run cmake -S "${repo_root}" -B "${configure_dir}" "${generator_args[@]}" \
        -DCMAKE_TOOLCHAIN_FILE="${toolchain_file}" \
        -DBUILD_TESTING=OFF \
        -DKSJ_BUILD_UNIT_TESTS=OFF \
        -DKSJ_BUILD_BENCHMARKS=OFF \
        -DKSJ_BUILD_RESEARCH=OFF
    else
      ksj_note "skipping basic CMake configure: no generated Conan toolchain (set KSJ_PRE_COMMIT_TOOLCHAIN_FILE to enable it)"
    fi
  fi
fi

ksj_note "pre-commit gate passed"
