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
`uv` environment and installs a checksum-pinned project-local `just`; it does not install every
tool into a virtual environment or alter a global Python environment. See
[the developer-environment guide](../devenv/README.md) for the complete policy.

After the first bootstrap, run the shared `justfile` recipes through the platform runner. The
recipe names below are the same on Linux and Windows; only Linux-only recipes such as `unit`
and `benchmark-smoke` are intentionally absent on Windows.

Linux entry points:

```bash
tools/devenv/linux/run.sh just pre-commit
tools/devenv/linux/run.sh just pre-push
tools/devenv/linux/run.sh just check
tools/devenv/linux/run.sh just unit
tools/devenv/linux/run.sh just benchmark-smoke
```

Windows entry points:

```powershell
.\tools\devenv\windows\run.ps1 just pre-commit
.\tools\devenv\windows\run.ps1 just pre-push
.\tools\devenv\windows\run.ps1 just check
```

The pre-commit and Linux CI checks also run the offline Markdown link checker and the
canonical execution-plan checker. The link checker validates repository-local inline links
and Markdown heading fragments without fetching external URLs:

```bash
tools/devenv/linux/run.sh just link-check
```

```powershell
.\tools\devenv\windows\run.ps1 just link-check
```

Use `--self-test` to run the hermetic checker regression tests, including the negative case
where a missing relative path must fail the normal command with a nonzero result. The check
excludes local environments, generated output, and the vendored Intel payload.

The execution-plan checker keeps the progress dashboard in the canonical plan as a generated,
read-only projection of section 12; it never creates a second status file. After changing a
work-item state, update section 12 and then regenerate and verify the dashboard:

```bash
tools/devenv/linux/run.sh just plan-write
tools/devenv/linux/run.sh just plan-check
```

```powershell
.\tools\devenv\windows\run.ps1 just plan-write
.\tools\devenv\windows\run.ps1 just plan-check
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
tools/devenv/linux/run.sh just workspace-check
tools/devenv/linux/run.sh just workspace-self-test
```

```powershell
.\tools\devenv\windows\run.ps1 just workspace-check
.\tools\devenv\windows\run.ps1 just workspace-self-test
```

The checker never fetches or hashes datasets. Run `tools/verify-data.sh` from inside the sibling
data repository for its manifest and checksum verification.

Before invoking a CMake-based check, run the matching `just prepare-*` recipe so it exports local
Conan recipes and configures the matching output directory. Use the platform runner directly only
for a focused diagnostic with no recipe; never select a system `just`, Conan or CMake by PATH
order. See the root [README](../../README.md) for commands. `benchmark-smoke` tests only the
numerical benchmark targets; it is Linux-only.

The locked development environment supplies `clang-format` and `cmake-format`. Set
`KSJ_REQUIRE_CMAKE_FORMAT=1` to make a missing formatter an error in environments that
intentionally do not use the bootstrap. Generated output, local caches and benchmark reports
must not be committed.
