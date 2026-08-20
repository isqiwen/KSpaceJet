# Build convention

## Developer environment

Before a build, first provision the checked-in developer-tool environment:

```bash
bash tools/devenv/linux/bootstrap.sh
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1
```

Linux hosts must provide Git, Git LFS, and default GCC/G++ 14. Windows hosts must provide
Git, Git LFS, Visual Studio 2022 v143 C++ Build Tools, and a Windows SDK. Those host tools
are deliberately not installed into a Python virtual environment. The pinned `uv` binary,
managed Python, and the project tooling (Conan, CMake, Ninja, `clang-format`, and
`cmake-format`) are instead kept under the repository's ignored `.kspacejet/` and `.venv/`
directories. The bootstrap does not use `sudo`, a platform package manager, or global Python
packages. See [Developer environment](../../tools/devenv/README.md) for the full ownership,
offline, and update rules.

A minimal Linux host also needs `curl` or `wget`, `tar`, and `sha256sum` or `shasum` for the
initial bootstrap download. Host `gdb` is required only for Linux VS Code debugging, while host
`clang++` is required only by the optional `linux-release-static-analysis` preset.

After bootstrap, use `tools/devenv/linux/run.sh` or `tools/devenv/windows/run.ps1` for every
direct terminal invocation of a managed tool. Do not use a system `conan`, `cmake`, `ninja`,
or formatter by accident.

Export the local Conan recipes, then install dependencies into the output folder used by the
selected CMake preset. Use `linux-release` with
`conan/profiles/linux-gcc14-release` on Linux and `windows-vs2022-release` with
`conan/profiles/windows-msvc2022-release` on Windows.

```bash
tools/devenv/linux/run.sh conan export conan/recipes/ismrmrd --user=kspacejet --channel=stable
tools/devenv/linux/run.sh conan export third_party/intel --user=kspacejet --channel=stable
tools/devenv/linux/run.sh conan install . --output-folder=out/build/linux-release \
  --profile:host=conan/profiles/linux-gcc14-release --build=missing
tools/devenv/linux/run.sh cmake --preset linux-release
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 conan export conan/recipes/ismrmrd --user=kspacejet --channel=stable
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 conan export third_party/intel --user=kspacejet --channel=stable
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 conan install . --output-folder=out/build/windows-vs2022-release `
  --profile:host=conan/profiles/windows-msvc2022-release --build=missing
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 cmake --preset windows-vs2022-release
```

Build the smallest changed target. `ksj_ismrmrd`, `ksj_array` and
`ksj_mri_debug` are common targets. Intel payload files are obtained through
Git LFS; a host oneAPI installation is not required.

The repository profiles require dynamic variants for all Conan dependencies
that provide a `shared` option (`*:shared=True`).  Linux also enables PIC for
those dependencies.  Do not override this to static linking in a KSpaceJet
build.

## VS Code workflow

The VS Code workflow deliberately separates environment preparation from ordinary
incremental compilation. First run the visible `KSJ: bootstrap developer environment` task,
then run the matching preparation task before the first build for each platform/configuration:

| Platform and configuration | Prepare environment | Incremental application build | Application install | CMake install preset | Install prefix |
| --- | --- | --- | --- | --- | --- |
| Linux Debug | `KSJ: prepare Linux Debug environment` | `KSJ: build Linux Debug applications` | `KSJ: install Linux Debug applications` | `linux-debug-install` | `out/install/linux-debug` |
| Linux Release | `KSJ: prepare Linux Release environment` | `KSJ: build Linux Release applications` | `KSJ: install Linux Release applications` | `linux-release-install` | `out/install/linux-release` |
| Windows Debug | `KSJ: prepare Windows Debug environment` | `KSJ: build Windows Debug applications` | `KSJ: install Windows Debug applications` | `windows-vs2022-debug-install` | `out/install/windows-vs2022-debug` |
| Windows Release | `KSJ: prepare Windows Release environment` | `KSJ: build Windows Release applications` | `KSJ: install Windows Release applications` | `windows-vs2022-release-install` | `out/install/windows-vs2022-release` |

Each `prepare` task calls the same platform bootstrap used at the command line. It verifies the
complete Intel payload manifest, exports the local ISMRMRD and Intel Conan recipes, runs `conan
install`, and configures CMake for its dedicated output directory. It produces the Conan toolchain
and CMake build files required by the matching `build` task.

Each `build … applications` task performs only `cmake --build` against that prepared
directory. It does not export recipes, run Conan, or configure CMake, and it must not
depend on a preparation task. The F5 debug configurations use this same incremental
build path; they do not prepare an environment automatically.

Each visible `install … applications` task depends only on its matching `build …
applications` task. It does not prepare the environment, export recipes, run Conan, or
configure CMake; after the incremental build it runs the corresponding CMake install
preset. The install prefix is set by the matching configure preset's
`CMAKE_INSTALL_PREFIX`, as shown in the table.

Re-run the matching `prepare` task only when first using a configuration, after deleting
its `out/build/` directory, or after changing dependencies, local Conan recipes or Intel
payloads, CMake configuration/presets, or Conan profiles. For ordinary `.cpp`/`.hpp`
changes, run only the incremental `build` task. If a build or F5 reports a missing
toolchain or build system, prepare that platform/configuration first.

## Applications

The normal application presets build and install all four executables: `ksj`,
`ksj-gateway`, `ksj-recon`, and `ksj-research`. Their CMake targets are respectively
`ksj_cli`, `ksj_gateway`, `ksj_recon`, and `ksj_research`.

`ksj-research` is an installed research-application scaffold; its experiment operations are not
implemented. It must not become a runtime or data-plane dependency of the other applications.
The VS Code application build tasks each build all four executables:

- `KSJ: build Linux Debug applications`
- `KSJ: build Linux Release applications`
- `KSJ: build Windows Debug applications`
- `KSJ: build Windows Release applications`

The corresponding visible install tasks install those same four executables without
preparing an environment:

- `KSJ: install Linux Debug applications`
- `KSJ: install Linux Release applications`
- `KSJ: install Windows Debug applications`
- `KSJ: install Windows Release applications`

Unit-test, benchmark, and `tests/research` presets intentionally set
`KSJ_BUILD_APPLICATIONS=OFF`. They use a dedicated target graph and must be
configured in a separate build tree from application builds. In particular,
`KSJ_BUILD_RESEARCH` controls only the `tests/research` targets; it does not control
`ksj_research`.
