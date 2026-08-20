#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../../.." && pwd)"
venv_bin="${repo_root}/.venv/bin"
# shellcheck source=tools/devenv/tool-versions.env
source "${script_dir}/../tool-versions.env"
just_root="${repo_root}/.kspacejet/bootstrap/just/${KSJ_JUST_VERSION}/linux-x86_64"
just_binary="${just_root}/just"

if [[ ! -x "${venv_bin}/python" ]]; then
  printf '[kspacejet-devenv] error: .venv is unavailable; run tools/devenv/linux/bootstrap.sh first\n' >&2
  exit 1
fi

if [[ $# -eq 0 ]]; then
  printf '[kspacejet-devenv] error: expected a command to run\n' >&2
  exit 2
fi

if [[ "$1" == "just" ]] && [[ ! -x "${just_binary}" ]]; then
  printf '[kspacejet-devenv] error: project-local just is unavailable; run tools/devenv/linux/bootstrap.sh first\n' >&2
  exit 1
fi

if [[ -x "${just_binary}" ]]; then
  export PATH="${just_root}:${venv_bin}:${PATH}"
else
  export PATH="${venv_bin}:${PATH}"
fi
exec "$@"
