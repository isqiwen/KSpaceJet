#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../../.." && pwd)"
venv_bin="${repo_root}/.venv/bin"

if [[ ! -x "${venv_bin}/python" ]]; then
  printf '[kspacejet-devenv] error: .venv is unavailable; run tools/devenv/linux/bootstrap.sh first\n' >&2
  exit 1
fi

if [[ $# -eq 0 ]]; then
  printf '[kspacejet-devenv] error: expected a command to run\n' >&2
  exit 2
fi

if [[ "$1" == "just" ]] && ! command -v just >/dev/null 2>&1; then
  printf '[kspacejet-devenv] error: host prerequisite is missing: just; run tools/devenv/linux/bootstrap.sh to install it with apt\n' >&2
  exit 1
fi

export PATH="${venv_bin}:${PATH}"
exec "$@"
