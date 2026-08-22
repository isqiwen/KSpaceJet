# Shared KSpaceJet development commands.
#
# Bootstrap ensures `just` is available as a host tool. This file deliberately keeps the
# recipe names and platform-specific commands in one place.

set default-list

[linux]
set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

[windows]
set shell := ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command"]

# Re-provision the developer environment after the initial bootstrap.
[linux]
bootstrap:
    bash tools/devenv/linux/bootstrap.sh

[windows]
bootstrap:
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1

# Prepare the platform-appropriate debug build directory.
[linux]
prepare-debug:
    bash tools/devenv/linux/bootstrap.sh --prepare linux-debug

[windows]
prepare-debug:
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1 -Prepare windows-vs2022-debug

# Prepare the platform-appropriate release build directory.
[linux]
prepare-release:
    bash tools/devenv/linux/bootstrap.sh --prepare linux-release

[windows]
prepare-release:
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1 -Prepare windows-vs2022-release

# Prepare the Linux-only application-test build tree.
[linux]
prepare-app-tests:
    bash tools/devenv/linux/bootstrap.sh --prepare linux-release-app-tests

# Incrementally build all installed applications in debug configuration.
[linux]
build-debug-applications:
    tools/devenv/linux/run.sh cmake --build --preset linux-debug --target ksj_cli ksj_gateway ksj_recon ksj_research

[windows]
build-debug-applications:
    .\tools\devenv\windows\run.ps1 cmake --build --preset windows-vs2022-debug --target ksj_cli ksj_gateway ksj_recon ksj_research

# Incrementally build all installed applications in release configuration.
[linux]
build-release-applications:
    tools/devenv/linux/run.sh cmake --build --preset linux-release --target ksj_cli ksj_gateway ksj_recon ksj_research

[windows]
build-release-applications:
    .\tools\devenv\windows\run.ps1 cmake --build --preset windows-vs2022-release --target ksj_cli ksj_gateway ksj_recon ksj_research

# Install the already built debug applications without preparing a build tree.
[linux]
install-debug-applications:
    tools/devenv/linux/run.sh cmake --build --preset linux-debug-install

[windows]
install-debug-applications:
    .\tools\devenv\windows\run.ps1 cmake --build --preset windows-vs2022-debug-install

# Install the already built release applications without preparing a build tree.
[linux]
install-release-applications:
    tools/devenv/linux/run.sh cmake --build --preset linux-release-install

[windows]
install-release-applications:
    .\tools\devenv\windows\run.ps1 cmake --build --preset windows-vs2022-release-install

# Verify the installed Windows Release applications without reconfiguring or rebuilding them.
[windows]
smoke-release-install:
    .\tools\checks\windows\smoke_release_install.ps1 -InstallBinDirectory .\out\install\windows-vs2022-release\bin

# Run staged-file formatting checks.
[linux]
format-staged:
    bash tools/checks/linux/format_check.sh --staged

[windows]
format-staged:
    .\tools\checks\windows\format_check.ps1 --staged

# Run full-repository formatting checks.
[linux]
format-all:
    bash tools/checks/linux/format_check.sh --all

[windows]
format-all:
    .\tools\checks\windows\format_check.ps1 --all

# Run formatting checks against the current branch's changed files.
[linux]
format-changed:
    bash tools/checks/linux/format_check.sh --changed

[windows]
format-changed:
    .\tools\checks\windows\format_check.ps1 --changed

# Verify local Markdown links without network access.
[linux]
link-check:
    tools/devenv/linux/run.sh python tools/checks/check_markdown_links.py --project-root .

[windows]
link-check:
    .\tools\devenv\windows\run.ps1 python .\tools\checks\check_markdown_links.py --project-root .

# Run the hermetic regression tests for the local Markdown link checker.
[linux]
link-self-test:
    tools/devenv/linux/run.sh python tools/checks/check_markdown_links.py --self-test

[windows]
link-self-test:
    .\tools\devenv\windows\run.ps1 python .\tools\checks\check_markdown_links.py --self-test

# Verify the generated execution-plan dashboard against its canonical ledger.
[linux]
plan-check:
    tools/devenv/linux/run.sh python tools/checks/check_execution_plan.py --project-root . --check

[windows]
plan-check:
    .\tools\devenv\windows\run.ps1 python .\tools\checks\check_execution_plan.py --project-root . --check

# Regenerate the derived execution-plan dashboard after a ledger update.
[linux]
plan-write:
    tools/devenv/linux/run.sh python tools/checks/check_execution_plan.py --project-root . --write

[windows]
plan-write:
    .\tools\devenv\windows\run.ps1 python .\tools\checks\check_execution_plan.py --project-root . --write

# Verify the required sibling KSpaceJet data workspace without reading datasets.
[linux]
workspace-check:
    tools/devenv/linux/run.sh python tools/checks/check_workspace_layout.py --project-root .

[windows]
workspace-check:
    .\tools\devenv\windows\run.ps1 python .\tools\checks\check_workspace_layout.py --project-root .

# Run the hermetic regression tests for the paired-workspace checker.
[linux]
workspace-self-test:
    tools/devenv/linux/run.sh python tools/checks/check_workspace_layout.py --self-test

[windows]
workspace-self-test:
    .\tools\devenv\windows\run.ps1 python .\tools\checks\check_workspace_layout.py --self-test

# Verify that the generated TypeRegistry stays synchronized with its source.
[linux]
type-check:
    tools/devenv/linux/run.sh python tools/type_registry/generate.py --project-root . --check

[windows]
type-check:
    .\tools\devenv\windows\run.ps1 python .\tools\type_registry\generate.py --project-root . --check

# Run the normal platform development check; workspace-check stays explicit for CI.
[linux]
check:
    bash tools/checks/linux/ci_check.sh

[windows]
check:
    .\tools\checks\windows\format_check.ps1 --all
    .\tools\devenv\windows\run.ps1 python .\tools\checks\check_markdown_links.py --project-root .
    .\tools\devenv\windows\run.ps1 python .\tools\checks\check_execution_plan.py --project-root . --check
    .\tools\devenv\windows\run.ps1 cmake --preset windows-vs2022-release

# Run the installed Git-hook checks on demand.
[linux]
pre-commit:
    bash tools/checks/linux/pre_commit.sh

[windows]
pre-commit:
    .\tools\checks\windows\pre_commit.ps1

# Run the optional local push smoke before pushing.
[linux]
pre-push:
    bash tools/checks/linux/pre_push.sh

[windows]
pre-push:
    .\tools\checks\windows\pre_push.ps1

# Linux-only test and benchmark entry points; P0-002 owns Windows validation.
[linux]
prepare-unit-tests:
    bash tools/devenv/linux/bootstrap.sh --prepare linux-release-unit-tests

[linux]
unit:
    bash tools/checks/linux/ci_unit.sh

# Build and execute the isolated Linux application-test suite.
[linux]
app-tests:
    tools/devenv/linux/run.sh cmake --build --preset linux-release-app-tests
    tools/devenv/linux/run.sh ctest --preset linux-release-app-tests --output-on-failure

# Run the complete local Linux quality gate.
[linux]
full:
    bash tools/checks/linux/ci_full.sh

# Run the Linux numerical benchmark smoke suite.
[linux]
benchmark-smoke:
    bash tools/checks/linux/benchmark_smoke.sh
