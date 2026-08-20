# KSpaceJet checks

The scripts in this directory validate the open KSpaceJet framework only. They
do not launch a private product runtime or process obsolete private formats. Development does require
the sibling `KSpaceJet-ismrmrd-data` checkout described below; no `common` or other third-party
toolchain checkout is required.

## First-time setup

Provision the project-local developer environment before running checks or VS Code build
tasks:

```bash
bash tools/devenv/linux/bootstrap.sh
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1
```

Linux hosts must already have Git, Git LFS, and default GCC/G++ 14. Windows hosts must
already have Git, Git LFS, Visual Studio 2022 v143 C++ Build Tools, and a Windows SDK.
These system-integrated tools stay on the host. The bootstrap creates a repository-local
`uv` environment for the locked Python and developer tools; it does not install every tool
into a virtual environment and does not alter a global Python environment. See
[the developer-environment guide](../devenv/README.md) for the complete policy.

Linux entry points:

```bash
tools/checks/linux/pre_commit.sh
tools/checks/linux/pre_push.sh
tools/checks/linux/ci_check.sh
tools/checks/linux/ci_unit.sh
tools/checks/linux/benchmark_smoke.sh
```

Windows entry points:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\checks\windows\pre_commit.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\checks\windows\pre_push.ps1
```

The pre-commit and Linux CI checks also run the offline Markdown link checker and the
canonical execution-plan checker. The link checker validates repository-local inline links
and Markdown heading fragments without fetching external URLs:

```bash
tools/devenv/linux/run.sh python tools/checks/check_markdown_links.py --project-root .
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 python .\tools\checks\check_markdown_links.py --project-root .
```

Use `--self-test` to run the hermetic checker regression tests, including the negative case
where a missing relative path must fail the normal command with a nonzero result. The check
excludes local environments, generated output, and the vendored Intel payload.

The execution-plan checker keeps the progress dashboard in the canonical plan as a generated,
read-only projection of section 12; it never creates a second status file. After changing a
work-item state, update section 12 and then regenerate and verify the dashboard:

```bash
tools/devenv/linux/run.sh python tools/checks/check_execution_plan.py --project-root . --write
tools/devenv/linux/run.sh python tools/checks/check_execution_plan.py --project-root . --check
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 python .\tools\checks\check_execution_plan.py --project-root . --write
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 python .\tools\checks\check_execution_plan.py --project-root . --check
```

`--self-test` exercises its parser, section-10/section-12 task-set equality, canonical-plan
entrypoint references, dashboard regeneration, stale-dashboard rejection, duplicate-ID and
status rejection, READY dependency validation, and the one-active/one-READY-work-item
invariants.

## Paired data-workspace check

Raw MRI payloads do not belong in KSpaceJet. Local development must place this repository and
the canonical data repository side by side:

```text
<workspace>/
  KSpaceJet/
  KSpaceJet-ismrmrd-data/
```

Both platform pre-commit hooks run this offline gate. It verifies the sibling repository's Git
origin and required data-contract paths, and rejects tracked or physical `.mrd`, `.h5`, `.hdf5`,
and `.ismrmrd` payloads in KSpaceJet. It intentionally is not part of self-contained CI, because
CI must explicitly check out the sibling repository before it can claim workspace verification.

```bash
tools/devenv/linux/run.sh python tools/checks/check_workspace_layout.py --project-root .
tools/devenv/linux/run.sh python tools/checks/check_workspace_layout.py --self-test
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 python .\tools\checks\check_workspace_layout.py --project-root .
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 python .\tools\checks\check_workspace_layout.py --self-test
```

The checker never fetches or hashes datasets. Run `tools/verify-data.sh` from inside the sibling
data repository for its manifest and checksum verification.

Before invoking a CMake-based check, export the local Conan recipes and run `conan install`
for the matching preset output directory. Use the platform runner for direct terminal
commands, for example `tools/devenv/linux/run.sh conan install ...` or
`powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 conan install ...`;
do not select a system Conan/CMake
by PATH order. See the root [README](../../README.md) for commands. `benchmark_smoke.sh`
tests only the numerical benchmark targets; it is Linux-only.

The locked development environment supplies `clang-format` and `cmake-format`. Set
`KSJ_REQUIRE_CMAKE_FORMAT=1` to make a missing formatter an error in environments that
intentionally do not use the bootstrap. Generated output, local caches and benchmark reports
must not be committed.
