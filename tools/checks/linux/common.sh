#!/usr/bin/env bash

set -euo pipefail

ksj_repo_root() {
  local root
  if root="$(git rev-parse --show-toplevel 2>/dev/null)"; then
    printf '%s\n' "${root}"
    return 0
  fi

  cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd
}

ksj_run() {
  printf '+'
  printf ' %q' "$@"
  printf '\n'
  "$@"
}

ksj_note() {
  printf '[kspacejet-checks] %s\n' "$*"
}

ksj_die() {
  printf '[kspacejet-checks] error: %s\n' "$*" >&2
  exit 1
}

ksj_has_command() {
  command -v "$1" >/dev/null 2>&1
}

ksj_configure_tool_path() {
  local repo_root
  repo_root="$(ksj_repo_root)"
  local venv_bin="${repo_root}/.venv/bin"
  if [[ -d "${venv_bin}" ]]; then
    export PATH="${venv_bin}:${PATH}"
  fi
}

ksj_configure_tool_path
