# KSpaceJet agent guide

KSpaceJet is a C++20 open-source framework for high-throughput online streaming MRI
reconstruction. ISMRMRD is its only raw-acquisition data semantics: offline replay uses
standard ISMRMRD HDF5, while online acquisition and image delivery use a frozen public
MRD/ISMRMRD streaming-session binding. Do not define a KSpaceJet-private wire protocol.

## Repository map

- `libs/core/`: logging, threading, memory, platform and process primitives.
- `libs/core/kspacejet-program/`: reusable internal executable-entry and program utilities.
- `libs/numerics/`: general numerical APIs and backend implementations.
- `libs/io/kspacejet-ismrmrd/`: public ISMRMRD streaming reader.
- `libs/io/kspacejet-matio/`: MAT I/O support for diagnostics and interoperability.
- `libs/mri/kspacejet-mri-debug/`: reusable MRI inspection utilities.
- `apps/kspacejet-cli/`: the public `ksj` command-line interface.
- `apps/kspacejet-gateway/`: the integration gateway service for external systems.
- `apps/kspacejet-recon/`: the bounded reconstruction service.
- `apps/kspacejet-research/`: the installed experiment-oriented research runner.
- `third_party/intel/`: Git-LFS Intel IPP/MKL/OpenMP payload and its local Conan recipe.
- `tests/unit/`: focused unit tests.

The current repository is the portable foundation; the approved implementation roadmap in
`docs/architecture/streaming_reconstruction_framework_plan.md` adds the four application
projects `ksj`, `ksj-gateway`, `ksj-recon`, and `ksj-research`, plus the
runtime, Provider SDK, and minimal open reference providers. Providers are independently
shipped dynamic-library plugins loaded by `ksj-recon`. Do not reintroduce legacy DPC
applications, obsolete private formats, proprietary queue tables,
private protocols, BRF/ComQ compatibility, or proprietary reconstruction algorithms.
Providers own substantive reconstruction algorithms. Reference providers and paper
matched kernels must remain minimal, open, independently verifiable, and outside the
private historical algorithm surface.

When `KSJ_BUILD_APPLICATIONS=ON`, all four application targets are built and installed.
`ksj-research` remains an experiment-oriented outer runner: it must not become a
runtime or data-plane dependency of `ksj`, `ksj-gateway`, or `ksj-recon`.
`KSJ_BUILD_RESEARCH` remains the independent switch for `tests/research` only.

## Naming

- Project/display name: `KSpaceJet`.
- C++ namespace and internal CMake target prefix: `ksj` / `KSJ_`.
- Public CMake aliases: `KSpaceJet::feature`.
- Public include root: `kspacejet/`.
- Directory names: `kspacejet-*`; executable output names: `ksj` or `ksj-*`.

## Build

Inspect `CMakePresets.json` before configuring. Bootstrap the repository-local developer
environment before running a VS Code task or any command-line build:

```bash
bash tools/devenv/linux/bootstrap.sh
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1
```

The host prerequisites are Git, Git LFS and default GCC/G++ 14 on Linux, or Git, Git LFS,
Visual Studio 2022 v143 C++ Build Tools and a Windows SDK on Windows. `uv`, managed Python,
and the project tools are repository-local; compilers, Git, and Git LFS are not virtual-
environment tools. See [tools/devenv/README.md](tools/devenv/README.md) for the ownership and
offline policy.

On a minimal Linux host, first bootstrap additionally needs `curl` or `wget`, `tar`, and
`sha256sum` or `shasum`. VS Code Linux debugging needs host `gdb`; only the optional
`linux-release-static-analysis` preset needs host `clang++`.

For direct terminal use, always call the platform runner instead of an arbitrary system
`conan` or `cmake`. Export local recipes first:

```bash
tools/devenv/linux/run.sh conan export conan/recipes/ismrmrd --user=kspacejet --channel=stable
tools/devenv/linux/run.sh conan export third_party/intel --user=kspacejet --channel=stable
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 conan export conan/recipes/ismrmrd --user=kspacejet --channel=stable
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 conan export third_party/intel --user=kspacejet --channel=stable
```

Linux uses `conan/profiles/linux-gcc14-release` and `linux-release`; Windows uses
`conan/profiles/windows-msvc2022-release` and `windows-vs2022-release`. Build the
smallest affected target through the same runner, for example `ksj_ismrmrd`, `ksj_array` or
`ksj_mri_debug`.

## VS Code build workflow

In VS Code, first run the visible `KSJ: bootstrap developer environment` task; then, before
the first application build for a platform/configuration, run its matching preparation task:

- `KSJ: prepare Linux Debug environment`
- `KSJ: prepare Linux Release environment`
- `KSJ: prepare Windows Debug environment`
- `KSJ: prepare Windows Release environment`

A preparation task calls the same project bootstrap used by the command line: it verifies the
Intel payload, exports local recipes, runs `conan install`, and configures CMake for that one build
directory. Re-run it only when that build directory is absent, or when
dependencies, local recipes or Intel payloads, CMake configuration/presets, or Conan
profiles change.

The VS Code application build tasks each build all four executables:
`KSJ: build Linux Debug applications`, `KSJ: build Linux Release applications`,
`KSJ: build Windows Debug applications`, and `KSJ: build Windows Release applications`.
They must remain incremental-build-only tasks: do not make them depend on preparation,
Conan export/install, or CMake configure. F5 uses the same incremental build path and
does not prepare an environment; prepare the selected configuration first when needed.

The visible VS Code application install tasks are `KSJ: install Linux Debug applications`,
`KSJ: install Linux Release applications`, `KSJ: install Windows Debug applications`, and
`KSJ: install Windows Release applications`. Each install task must depend only on its
matching incremental application-build task, never on preparation, Conan export/install,
or CMake configure. It runs the matching CMake install preset and installs to that
preset's `CMAKE_INSTALL_PREFIX`: respectively `out/install/linux-debug`,
`out/install/linux-release`, `out/install/windows-vs2022-debug`, and
`out/install/windows-vs2022-release`.

Do not use `KSJ_BUILD_RESEARCH` to enable `ksj-research`; that option controls only
`tests/research`.

## Engineering rules

- Keep raw acquisition handling streaming. `AcquisitionView` spans are valid only during
  their callback; do not retain them past that point.
- Keep HDF5 replay and the selected public MRD session semantically equivalent at the
  runtime-frame boundary. Backpressure/resource accounting is process-internal and must
  not create private wire messages.
- Favor buffer reuse, contiguous traversal and benchmark-backed backend selection in hot
  paths. Avoid avoidable allocations, copies and thread oversubscription.
- Eigen is the general numerical baseline. Intel, FFTW, OpenCV, ITK and MATIO stay behind
  private implementation boundaries when possible.
- Public headers expose mathematical semantics and KSpaceJet views/types, not vendor types.
- Consider Linux shared-library and Windows DLL symbol boundaries for every CMake change.
- Use `apply_patch` for manual edits; do not revert unrelated worktree changes.

## Validation

- Documentation: `git diff --check`.
- CMake: configure the relevant preset or a clean equivalent build directory.
- ISMRMRD reader: build/run `ksj_ismrmrd_tests` when dependencies are available.
- Intel recipe: create it with Linux and Windows Conan profiles; no system oneAPI install
  should be required.

Do not commit `out/`, generated reports, cache files or reconstruction outputs.
