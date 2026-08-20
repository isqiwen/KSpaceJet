# KSpaceJet developer environment

This directory provisions the repository's development tools reproducibly on Linux x86_64 and
Windows x86_64. It does not install anything with `sudo`, `apt`, `dnf`, `yum`, `winget`, or a
machine-wide Python package manager.

## Entry points

Linux:

```bash
bash tools/devenv/linux/bootstrap.sh
```

Windows PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1
```

To provision the tools and prepare a build tree in one invocation, add the platform preset.
If the Intel Git-LFS payload is absent, `--prepare` / `-Prepare` obtains it before the Conan
install step:

```bash
bash tools/devenv/linux/bootstrap.sh --prepare linux-release
```

Application-level CTests use the separate application build preset, so product
targets remain enabled while unit-test-only configuration stays isolated:

```bash
bash tools/devenv/linux/bootstrap.sh --prepare linux-release-app-tests
tools/devenv/linux/run.sh cmake --build --preset linux-release-app-tests
tools/devenv/linux/run.sh ctest --preset linux-release-app-tests
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1 `
  -Prepare windows-vs2022-release
```

The normal VS Code workflow remains: bootstrap once, run the matching `KSJ: prepare …
environment` task once per build configuration, then use the incremental build and install tasks.

## Tool ownership

Not every executable belongs in a Python virtual environment.

| Layer | Location | Contents |
| --- | --- | --- |
| Host prerequisite | OS / SDK installation | Git, Git LFS, Linux default GCC/G++ 14, or Visual Studio 2022 14.4x/v143 C++ Build Tools and a Windows SDK |
| Bootstrap runtime | `.kspacejet/bootstrap/uv/` | A checksum-verified, pinned `uv` executable; it does not alter the user's PATH or shell profile |
| Managed Python and cache | `.kspacejet/python/`, `.kspacejet/uv-cache/` | CPython acquired by uv and its disposable package cache |
| Project tool environment | `.venv/` | Conan, cmake-format, and the locked CMake, Ninja, and clang-format binary wheels |

The latter three paths are ignored by Git. `cmake`, `ninja`, and `clang-format` are native
executables, but their selected distributions provide cross-platform binary wheels. Keeping them in
the locked `.venv` makes the command versions used by hooks and VS Code deterministic. A future
native tool without a suitable audited wheel must be added as a separately checksum-verified
project-local artifact; it must not be silently taken from the system PATH.

The compiler toolchain, Git, and Git LFS remain host prerequisites because they integrate with the
operating system and SDK. The scripts validate them rather than attempting privileged or
distribution-specific installation. On Linux the default `gcc` and `g++` must both be major version
14, matching the Conan profiles. On Windows the scripts require Visual Studio 2022 with the v143 C++
tools (MSVC 19.4x / toolset 14.4x); install a Windows SDK with that workload.

On a minimal Linux host, the first bootstrap additionally needs ordinary host download and archive
utilities: `curl` or `wget`, `tar`, and `sha256sum` or `shasum`. They are checked with clear errors;
the bootstrap intentionally does not invoke a distribution package manager. Linux VS Code debugging
also needs host `gdb`, and the optional `linux-release-static-analysis` preset additionally needs host
`clang++`. Neither is needed for ordinary builds.

## Reproducibility and maintenance

`tool-versions.env` pins the bootstrap `uv` version plus Linux/Windows archive and installed-binary
checksums. The bootstrap verifies the cached executable before it is used.
`pyproject.toml`, `.python-version`, and `uv.lock` pin the project tool environment. The scripts run
`uv sync --locked`, so a stale or manually edited lock file is rejected instead of being resolved on
each developer machine.

Do not add a floating `latest` dependency. A maintainer updates a tool deliberately, regenerates
`uv.lock` (and any bootstrap checksum), and verifies the result on both supported platforms before
committing it.

Useful non-mutating checks are:

```bash
bash tools/devenv/linux/bootstrap.sh --verify
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1 -Verify
```

`--offline` prevents network access and succeeds only when the pinned uv, managed Python, and locked
packages are already cached locally. With `--prepare` / `-Prepare`, it additionally requires a fully
hydrated and verified Intel payload plus every required Conan package in the local cache. In that mode
the bootstrap passes Conan `--no-remote`; `--offline` cannot be combined with an explicit Git-LFS pull.

Before a prepare, the bootstrap hashes every entry in the selected Intel payload manifest. This
distinguishes real Git-LFS content from a checkout containing only LFS pointers. The quick sentinel
check remains available for a fast manual health check; to audit every payload file explicitly, run:

```bash
tools/devenv/linux/run.sh python tools/devenv/verify_intel_payload.py --platform linux-x86_64 --full
```

```powershell
.\tools\devenv\windows\run.ps1 python .\tools\devenv\verify_intel_payload.py --platform windows-x86_64 --full
```

## Calling a managed tool outside VS Code

The platform wrappers prepend the project tool environment to `PATH` for a single command:

```bash
tools/devenv/linux/run.sh conan --version
tools/devenv/linux/run.sh clang-format --version
```

```powershell
.\tools\devenv\windows\run.ps1 conan --version
.\tools\devenv\windows\run.ps1 clang-format --version
```

Do not activate `.venv` or install tools into it manually. Change the checked-in dependency
declarations and lock file, then rerun the bootstrap script.
