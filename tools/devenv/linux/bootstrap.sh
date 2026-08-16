#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../../.." && pwd)"
# shellcheck source=tools/devenv/tool-versions.env
source "${script_dir}/../tool-versions.env"

usage() {
  cat <<'EOF'
usage: tools/devenv/linux/bootstrap.sh [options]

Provision the reproducible KSpaceJet Linux developer environment.

Options:
  --prepare [PRESET]  Export local Conan recipes, install dependencies, and
                      configure PRESET (default: linux-release).
  --pull-lfs          Fetch the Intel Git-LFS payload without preparing.
  --offline           Do not access the network; verify/use only cached assets.
  --verify            Verify the existing environment without modifying it.
  --no-hooks          Do not configure repository-local Git hooks.
  --smoke             Run the lightweight check scripts after provisioning.
  --help              Show this help.

The script never invokes sudo or a system package manager.  Git, Git LFS and
the default GCC/G++ 14 toolchain are host prerequisites.  It installs a pinned
uv bootstrap binary under .kspacejet/, then uses uv to create .venv/ and sync
the locked developer-tool set.
EOF
}

prepare_preset=""
pull_lfs=0
offline=0
verify_only=0
install_hooks=1
run_smoke=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prepare)
      if [[ $# -ge 2 && "${2}" != --* ]]; then
        prepare_preset="$2"
        shift 2
      else
        prepare_preset="linux-release"
        shift
      fi
      ;;
    --pull-lfs)
      pull_lfs=1
      shift
      ;;
    --offline)
      offline=1
      shift
      ;;
    --verify)
      verify_only=1
      shift
      ;;
    --no-hooks)
      install_hooks=0
      shift
      ;;
    --smoke)
      run_smoke=1
      shift
      ;;
    --help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      exit 2
      ;;
  esac
done

note() {
  printf '[kspacejet-devenv] %s\n' "$*"
}

die() {
  printf '[kspacejet-devenv] error: %s\n' "$*" >&2
  exit 1
}

if [[ "${offline}" -eq 1 && "${pull_lfs}" -eq 1 ]]; then
  die "--offline cannot be combined with --pull-lfs"
fi

require_command() {
  local command_name="$1"
  command -v "${command_name}" >/dev/null 2>&1 || die "host prerequisite is missing: ${command_name}"
}

sha256_file() {
  local path="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "${path}" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "${path}" | awk '{print $1}'
  else
    die "need sha256sum or shasum to verify the uv bootstrap artifact"
  fi
}

check_host_prerequisites() {
  [[ "$(uname -s)" == "Linux" ]] || die "this entry point supports Linux only"
  [[ "$(uname -m)" == "x86_64" ]] || die "only Linux x86_64 is currently supported"

  require_command git
  require_command git-lfs
  require_command gcc
  require_command g++
  if ! command -v sha256sum >/dev/null 2>&1 && ! command -v shasum >/dev/null 2>&1; then
    die "need sha256sum or shasum to verify the uv bootstrap artifact"
  fi

  local gcc_major
  gcc_major="$(gcc -dumpfullversion -dumpversion | cut -d. -f1)"
  [[ "${gcc_major}" == "14" ]] || die "the default gcc must be major version 14, found: ${gcc_major}"

  local gxx_major
  gxx_major="$(g++ -dumpfullversion -dumpversion | cut -d. -f1)"
  [[ "${gxx_major}" == "14" ]] || die "the default g++ must be major version 14, found: ${gxx_major}"
}

uv_root="${repo_root}/.kspacejet/bootstrap/uv/${KSJ_UV_VERSION}/linux-x86_64"
uv_binary="${uv_root}/uv"

uv_is_valid() {
  [[ -x "${uv_binary}" ]] || return 1
  [[ "$("${uv_binary}" --version 2>/dev/null | awk '{print $2}')" == "${KSJ_UV_VERSION}" ]] || return 1
  [[ "$(sha256_file "${uv_binary}")" == "${KSJ_UV_LINUX_X86_64_BINARY_SHA256}" ]]
}

install_uv() {
  if uv_is_valid; then
    note "using pinned project-local uv ${KSJ_UV_VERSION}"
    return
  fi

  [[ "${verify_only}" -eq 0 ]] || die "project-local uv ${KSJ_UV_VERSION} is absent or invalid; --verify never downloads tools"
  [[ "${offline}" -eq 0 ]] || die "project-local uv ${KSJ_UV_VERSION} is absent or invalid while --offline is set"
  require_command tar

  local downloader=""
  if command -v curl >/dev/null 2>&1; then
    downloader="curl"
  elif command -v wget >/dev/null 2>&1; then
    downloader="wget"
  else
    die "need curl or wget to download the verified uv bootstrap artifact"
  fi

  local temporary_directory
  temporary_directory="$(mktemp -d)"
  trap 'rm -rf "${temporary_directory}"' RETURN

  local archive="${temporary_directory}/uv.tar.gz"
  local url="https://github.com/astral-sh/uv/releases/download/${KSJ_UV_VERSION}/uv-x86_64-unknown-linux-gnu.tar.gz"
  note "downloading pinned uv ${KSJ_UV_VERSION}"
  if [[ "${downloader}" == "curl" ]]; then
    curl --fail --location --proto '=https' --tlsv1.2 --silent --show-error --output "${archive}" "${url}"
  else
    wget --https-only --output-document="${archive}" "${url}"
  fi

  local actual_sha256
  actual_sha256="$(sha256_file "${archive}")"
  [[ "${actual_sha256}" == "${KSJ_UV_LINUX_X86_64_SHA256}" ]] || die "uv archive SHA-256 mismatch"

  tar --extract --gzip --file="${archive}" --directory="${temporary_directory}"
  mapfile -t candidates < <(find "${temporary_directory}" -type f -name uv -perm -u+x -print)
  [[ ${#candidates[@]} -eq 1 ]] || die "unexpected uv archive layout"
  [[ "$("${candidates[0]}" --version | awk '{print $2}')" == "${KSJ_UV_VERSION}" ]] || die "downloaded uv reports an unexpected version"

  mkdir -p "${uv_root}"
  install -m 0755 "${candidates[0]}" "${uv_binary}.new"
  mv -f "${uv_binary}.new" "${uv_binary}"
  uv_is_valid || die "installed uv executable failed integrity verification"
  trap - RETURN
  rm -rf "${temporary_directory}"
}

configure_uv_paths() {
  export UV_CACHE_DIR="${repo_root}/.kspacejet/uv-cache"
  export UV_PYTHON_INSTALL_DIR="${repo_root}/.kspacejet/python"
  export UV_MANAGED_PYTHON=1
  if [[ "${offline}" -eq 1 ]]; then
    export UV_OFFLINE=1
  fi
}

sync_python_tools() {
  configure_uv_paths
  if [[ "${verify_only}" -eq 1 ]]; then
    "${uv_binary}" sync --locked --no-install-project --check
    return
  fi

  note "installing managed CPython ${KSJ_PYTHON_VERSION} and synchronizing .venv"
  "${uv_binary}" python install --managed-python --no-bin "${KSJ_PYTHON_VERSION}"
  "${uv_binary}" sync --locked --no-install-project
}

configure_repository() {
  [[ "${verify_only}" -eq 0 ]] || return 0
  note "configuring repository-local Git and Git-LFS settings"
  git config --local core.autocrlf input
  git config --local core.eol lf
  git config --local fetch.prune true
  git config --local pull.ff only
  # The project owns .githooks/pre-push and already delegates to `git lfs
  # pre-push`; configure local LFS filters without overwriting that hook.
  git lfs install --local --skip-repo

  if [[ "${install_hooks}" -eq 1 ]]; then
    "${repo_root}/tools/checks/linux/install_hooks.sh"
  fi

  if [[ "${pull_lfs}" -eq 1 ]]; then
    note "fetching Intel Git-LFS payload"
    git lfs pull --include="third_party/intel/payload/**"
  fi
}

tool_runner="${repo_root}/tools/devenv/linux/run.sh"
intel_payload_verifier="${repo_root}/tools/devenv/verify_intel_payload.py"

intel_payload_is_complete() {
  "${tool_runner}" python "${intel_payload_verifier}" --platform linux-x86_64 --full --quiet >/dev/null 2>&1
}

ensure_intel_payload_for_prepare() {
  if intel_payload_is_complete; then
    return
  fi

  [[ "${offline}" -eq 0 ]] || die "Intel payload is absent or incomplete while --offline is set"
  note "Intel payload is incomplete; fetching the Git-LFS payload required by --prepare"
  git lfs pull --include="third_party/intel/payload/**"
  "${tool_runner}" python "${intel_payload_verifier}" --platform linux-x86_64 --full \
    || die "Git-LFS finished without a valid Intel payload"
}

prepare_build() {
  [[ -n "${prepare_preset}" ]] || return 0
  [[ "${verify_only}" -eq 0 ]] || die "--verify cannot be combined with --prepare"
  ensure_intel_payload_for_prepare

  local profile=""
  local output_folder=""
  case "${prepare_preset}" in
    linux-debug)
      profile="conan/profiles/linux-gcc14-debug"
      output_folder="out/build/linux-debug"
      ;;
    linux-release)
      profile="conan/profiles/linux-gcc14-release"
      output_folder="out/build/linux-release"
      ;;
    linux-release-unit-tests)
      profile="conan/profiles/linux-gcc14-release-unit-tests"
      output_folder="out/build/linux-release-unit-tests"
      ;;
    linux-release-app-tests)
      profile="conan/profiles/linux-gcc14-release"
      output_folder="out/build/linux-release-app-tests"
      ;;
    *)
      die "unsupported Linux prepare preset: ${prepare_preset}"
      ;;
  esac

  note "preparing ${prepare_preset}"
  "${tool_runner}" conan export conan/recipes/ismrmrd --user=kspacejet --channel=stable
  "${tool_runner}" conan export third_party/intel --user=kspacejet --channel=stable
  local -a conan_install_args=(
    --output-folder="${output_folder}"
    --profile:host="${profile}"
    --build=missing)
  if [[ "${offline}" -eq 1 ]]; then
    conan_install_args+=(--no-remote)
  fi
  "${tool_runner}" conan install . "${conan_install_args[@]}"
  "${tool_runner}" cmake --preset "${prepare_preset}"
}

show_versions() {
  note "tool versions"
  "${tool_runner}" python --version
  "${tool_runner}" conan --version
  "${tool_runner}" cmake --version
  "${tool_runner}" ninja --version
  "${tool_runner}" clang-format --version
  "${tool_runner}" cmake-format --version
}

run_smoke_checks() {
  [[ "${run_smoke}" -eq 1 ]] || return 0
  [[ "${verify_only}" -eq 0 ]] || die "--verify cannot be combined with --smoke"
  "${repo_root}/tools/checks/linux/pre_commit.sh"
  "${repo_root}/tools/checks/linux/format_check.sh" --changed HEAD
}

cd "${repo_root}"
check_host_prerequisites
install_uv
sync_python_tools
configure_repository
prepare_build
show_versions
run_smoke_checks
note "developer environment is ready"
