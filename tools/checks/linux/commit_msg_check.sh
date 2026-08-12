#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${script_dir}/common.sh"

usage() {
  cat <<'EOF'
usage: tools/checks/linux/commit_msg_check.sh COMMIT_MSG_FILE

Checks the first non-empty, non-comment commit message line.

Accepted format:
  type(scope): subject
  type: subject

Accepted types:
  build, chore, ci, docs, feat, fix, perf, refactor, revert, style, test
EOF
}

if [[ $# -ne 1 ]]; then
  usage
  exit 2
fi

message_file="$1"
if [[ ! -f "${message_file}" ]]; then
  ksj_die "commit message file does not exist: ${message_file}"
fi

subject="$(
  sed -n \
    -e '/^[[:space:]]*#/d' \
    -e '/^[[:space:]]*$/d' \
    -e 'p;q' \
    "${message_file}"
)"

if [[ -z "${subject}" ]]; then
  ksj_die "commit message subject must not be empty"
fi

if [[ ${#subject} -gt 120 ]]; then
  ksj_die "commit message subject is too long (${#subject}/120): ${subject}"
fi

case "${subject}" in
  Merge\ * | Revert\ * | fixup!\ * | squash!\ *)
    ksj_note "commit message gate passed"
    exit 0
    ;;
esac

commit_pattern='^(build|chore|ci|docs|feat|fix|perf|refactor|revert|style|test)(\([A-Za-z0-9._/-]+\))?!?: .+'
if [[ ! "${subject}" =~ ${commit_pattern} ]]; then
  cat >&2 <<EOF
[kspacejet-checks] error: commit message subject is not normalized:
  ${subject}

Expected:
  type(scope): subject
  type: subject

Accepted types:
  build, chore, ci, docs, feat, fix, perf, refactor, revert, style, test

Examples:
  docs: align developer setup documentation
  fix(kspacejet-fe): handle missing runtime config
  build(cmake): update numerics benchmark target
EOF
  exit 1
fi

ksj_note "commit message gate passed"
