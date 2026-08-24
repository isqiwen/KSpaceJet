# KSpaceJet developer environment

This directory provisions the repository's development tools reproducibly on Linux x86_64 and
Windows x86_64. Linux bootstrap uses `sudo apt-get` to ensure `just` is installed and, during a
`--prepare`, the project-curated Qt/X11 development prerequisites; Windows bootstrap may use
`winget` when `just` is absent. It does not use a machine-wide Python package manager.

## Required paired data workspace

KSpaceJet does not store original MRI reconstruction data. Before development, clone
[KSpaceJet-ismrmrd-data](https://github.com/isqiwen/KSpaceJet-ismrmrd-data) beside this
repository, never inside it:

```text
<workspace>/
  KSpaceJet/
  KSpaceJet-ismrmrd-data/
```

The sibling data repository owns raw `.mrd`, `.h5`, `.hdf5`, and `.ismrmrd` payloads,
provenance, licenses, checksums, and Git-LFS policy. After bootstrap, check the layout with:

```bash
just workspace-check
```

The Linux and Windows pre-commit hooks run the same offline check. It validates only the
paired workspace and does not download or hash datasets; run
`../KSpaceJet-ismrmrd-data/tools/verify-data.sh` from the sibling repository when a full data
integrity check is required.

## Entry points

Linux:

```bash
bash tools/devenv/linux/bootstrap.sh
```

Windows PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1
```

Linux bootstrap uses apt to ensure `just` is installed; each Linux `prepare` also installs the
project-curated Qt/X11 development prerequisites. Windows bootstrap uses winget when `just` is
absent. Afterward, invoke shared recipes directly. The recipe
name is identical on Linux and Windows; `justfile` owns the platform-specific bootstrap,
Conan-profile and CMake-preset mapping:

After Winget installs or discovers `just`, bootstrap writes its package directory to the user PATH
and broadcasts the Windows environment change. Open a new terminal before invoking `just`
directly so it receives the updated PATH. The Windows runner is only for focused diagnostics with
managed project tools; it does not provision `just` or modify the user's PATH.

```bash
just prepare-release
just build-release-applications
just install-release-applications
just check
```

```powershell
just prepare-release
just build-release-applications
just install-release-applications
just check
```

If the Intel Git-LFS payload is absent, the prepare recipe obtains it before the Conan install.
`prepare-debug`, `format-staged`, `format-all`, `link-check`, `plan-check`, `workspace-check`,
`pre-commit` and `pre-push` are also shared names. Run
`just --list` for the supported command list.
`prepare-app-tests` and `app-tests` are Linux-only because there is no corresponding Windows
application-test preset yet.

The normal VS Code workflow remains: bootstrap once, run the matching `KSJ: prepare …
environment` task once per build configuration, then use the incremental build and install tasks.

## Tool ownership

Not every executable belongs in a Python virtual environment.

| Layer | Location | Contents |
| --- | --- | --- |
| Host prerequisite | OS / SDK installation | Git, Git LFS and Linux default GCC/G++ 14; or Git, Git LFS, Visual Studio 2022 14.4x/v143 C++ Build Tools, Windows SDK and winget on Windows. Linux bootstrap uses apt to ensure `just` is installed and every Linux `prepare` installs curated Qt/X11 development prerequisites; Windows uses winget only when `just` is absent. |
| Bootstrap runtime | `.kspacejet/bootstrap/uv/` | Checksum-verified, pinned `uv`; it does not alter the user's PATH or shell profile |
| Managed Python and cache | `.kspacejet/python/`, `.kspacejet/uv-cache/` | CPython acquired by uv and its disposable package cache |
| Project tool environment | `.venv/` | Conan, cmake-format, and the locked CMake, Ninja, and clang-format binary wheels |

The latter three paths are ignored by Git. `cmake`, `ninja`, and `clang-format` are native
executables, but their selected distributions provide cross-platform binary wheels. Keeping them in
the locked `.venv` makes the command versions used by hooks and VS Code deterministic. A future
native tool without a suitable audited wheel must be added as a separately checksum-verified
project-local artifact; it must not be silently taken from the system PATH.

The compiler toolchain, Git, and Git LFS remain host prerequisites because they integrate with the
operating system and SDK. The scripts validate them rather than attempting privileged or
distribution-specific installation. `just` is the deliberate exception: Linux bootstrap delegates
idempotent installation to apt, and Linux `prepare` uses the same explicit policy for the curated
Qt/X11 development package set; Windows uses winget only when `just` is missing. On Linux the default `gcc` and `g++` must both be major version
14, matching the Conan profiles. On Windows the scripts require Visual Studio 2022 with the v143 C++
tools (MSVC 19.4x / toolset 14.4x); install a Windows SDK with that workload.

On a minimal Linux host, the first bootstrap additionally needs ordinary host download and archive
utilities: `curl` or `wget`, `tar`, and `sha256sum` or `shasum`. It also needs `apt-get` and `sudo`.
Linux VS Code debugging
also needs host `gdb`, and the optional `linux-release-static-analysis` preset additionally needs host
`clang++`. Neither is needed for ordinary builds.

## Reproducibility and maintenance

`tool-versions.env` pins the bootstrap `uv` version plus Linux/Windows archive and installed-binary
checksums. The bootstrap verifies its cached executable before it is used.
`pyproject.toml`, `.python-version`, and `uv.lock` pin the project tool environment. The scripts run
`uv sync --locked`, so a stale or manually edited lock file is rejected instead of being resolved on
each developer machine.

Do not add a floating `latest` dependency. A maintainer updates a tool deliberately, regenerates
`uv.lock` when applicable (and every changed bootstrap checksum), and verifies the result on both
supported platforms before committing it.

Useful non-mutating checks are:

```bash
bash tools/devenv/linux/bootstrap.sh --verify
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1 -Verify
```

`--offline` prevents network access and succeeds only when the pinned `uv`, managed Python, and
locked packages are already cached locally. With `--prepare` / `-Prepare`, it additionally
requires a fully hydrated and verified Intel payload, the curated Linux Qt/X11 packages already on
the host, and every required Conan package in the local cache. In that mode the bootstrap passes
Conan `--no-remote`; `--offline` cannot be combined with an explicit Git-LFS pull.

Before a prepare, the bootstrap hashes every entry in the selected Intel payload manifest. This
distinguishes real Git-LFS content from a checkout containing only LFS pointers. The quick sentinel
check remains available for a fast manual health check; to audit every payload file explicitly, run:

```bash
tools/devenv/linux/run.sh python tools/devenv/verify_intel_payload.py --platform linux-x86_64 --full
```

```powershell
.\tools\devenv\windows\run.ps1 python .\tools\devenv\verify_intel_payload.py --platform windows-x86_64 --full
```

## Running shared development commands

`justfile` selects the correct platform command. Use the host `just` directly for every standard
development operation:

```bash
just prepare-release
just workspace-check
just plan-check
```

```powershell
just prepare-release
just workspace-check
just plan-check
```

For a focused diagnostic that has no recipe, the same platform wrappers can execute a locked
managed tool directly:

```bash
tools/devenv/linux/run.sh conan --version
tools/devenv/linux/run.sh clang-format --version
```

```powershell
.\tools\devenv\windows\run.ps1 conan --version
.\tools\devenv\windows\run.ps1 clang-format --version
```

Do not activate `.venv` or install managed project tools into the environment manually. Install
`just` through the bootstrap: Linux uses apt, while Windows uses winget only when it is absent. A
Linux `prepare` likewise uses apt for the explicit Qt/X11 development package set before Conan
checks the system dependencies.
