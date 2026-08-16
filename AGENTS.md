# KSpaceJet agent guide

KSpaceJet is a C++20 open-source framework for high-throughput online streaming MRI
reconstruction. ISMRMRD is its only raw-acquisition data semantics: offline replay uses
standard ISMRMRD HDF5, while online acquisition and image delivery use a frozen public
MRD/ISMRMRD streaming-session binding. Do not define a KSpaceJet-private wire protocol.

## Repository map

- `libs/core/`: logging, threading, memory, platform and process primitives.
- `libs/numerics/`: general numerical APIs and backend implementations.
- `libs/io/kspacejet-ismrmrd/`: public ISMRMRD streaming reader.
- `libs/io/kspacejet-matio/`: MAT I/O support for diagnostics and interoperability.
- `libs/mri/kspacejet-mri-debug/`: reusable MRI inspection utilities.
- `libs/recon/kspacejet-recon-model/`: immutable reconstruction artifacts,
  planning inputs, execution records, resource model, and Provider-owned
  `OperatorContract` declarations.
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

## Pre-release evolution

KSpaceJet is actively under development, incomplete, and unreleased. Treat
every project-owned ABI, API, artifact, schema, CMake target, installation
path, and source-layout decision as mutable.

**Default rule for every future change:** replace the old shape with the one
current shape everywhere in the repository. Do not implement forward
compatibility or backward compatibility. Do not add migration shims,
deprecated spellings, aliases, adapters, version negotiation, dual
parsers/serializers, or parallel formats unless the user explicitly requests
that compatibility in the current task.

- Prefer one accurate name over aliases, deprecated spellings, compatibility
  headers, dual JSON fields, or transitional adapters.
- When a term is wrong or overly broad, rename it consistently across source
  directories, CMake targets, public headers, schemas, canonical JSON,
  compiler/verifier logic, fixtures, tests, documentation, and diagnostic
  text in the same change.
- Do not preserve an old name merely because it appears in the working tree.
  Current project-owned APIs, schemas, artifacts, TypeRefs, contract files,
  and generated symbols are intentionally unversioned. Do not introduce
  numbered release suffixes, ABI-major fields, compatibility aliases, or
  parallel versioned files while the project remains unreleased.

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

- Every application executable parses its command line with `CLI11::CLI11`.
  Define each supported command, subcommand, option, positional argument, and
  validation rule declaratively in CLI11; do not add hand-written `argv`
  scanning, token loops, fallback parsers, or undocumented command aliases.
  `--help`/`-h` and `--version` are the standard global surface. A command that
  needs machine-readable results exposes its documented `--format text|json`
  option; JSON command output is a protocol for callers, not a log format.
  Keep command-specific validation in the application or its owning library
  rather than duplicating a parser in a separate utility target.
- `ksj provider init <provider-slug> <operator-id> --output <directory>` is
  the canonical way to start a Provider. It copies the installed Provider
  template into a new, non-existing directory, validates names, and never
  overwrites or silently adds a project to the catalog. Complete the explicit
  typed ports, configuration grammar, CMake registration, and tests after
  scaffolding; do not turn the generated skeleton into a compatibility
  template.
- Put project-owned cross-platform behavior in `libs/core/kspacejet-platform`.
  Applications, Providers, and domain libraries call a semantic platform API;
  they do not include OS headers or carry `#ifdef _WIN32`/`__linux__` branches
  for filesystem, process, dynamic-library, socket, or system operations.
  Keep the operating-system headers and conditional implementation private to
  the platform layer, with focused platform tests for its shared behavior.
- Treat `libs/core/` as the canonical home for project-wide foundation
  capabilities. Before adding a cross-cutting facility to an application,
  Provider, or domain library, first use the existing core component; if its
  semantic API is insufficient, extend that core component with a focused
  public API and tests instead of duplicating the facility at a leaf.
  In particular, all production diagnostic logging uses
  `<kspacejet/logging/logging.hpp>` and `KSJ_LOG_*`/`ksj::logging`; no
  application, Provider, or domain library may introduce a direct `spdlog`
  dependency or a private logger. Configure the process-global logger once at
  a process entrypoint; dynamically loaded Providers only emit through that
  already-configured logger. Core diagnostic records and configured log files
  use plain text; console diagnostics use `stderr`. A CLI running with
  `--format json` emits its structured success or failure report on `stdout`,
  leaving `stderr` available for core diagnostics; text-mode errors may use
  `stderr`. Neither stream is an internal logging substitute. The sole
  exception is core crash/signal handling: its
  async-signal-safe emergency write may use the minimal platform/stdio path
  and must never call the normal logger from a signal context.
- A Provider is one independently loadable dynamic-library, trust, and
  lifecycle boundary. An Operator is one single-purpose pipeline callable
  owned by that Provider. Group Operators only when they share a coherent
  domain, ABI, resource/lifecycle model, and release/trust boundary; do not
  turn a Provider into an unrelated algorithm grab bag.
- Every Provider uses the functional source layout: `src/provider_entry.cpp`
  contains only the exported `ksj_provider_query` ABI boundary;
  `src/provider_api.hpp` declares the Provider API boundary used by that
  entrypoint; `src/provider_api.cpp` owns Provider descriptor construction,
  Operator registration, and Provider-level dispatch; and
  `src/provider_state.hpp` contains only Provider-shared private state and the
  concrete definitions of ABI-opaque handles. Every Operator, including the
  first and only Operator in a Provider, has both
  `src/operators/<operator>.hpp` (its private descriptor/lifecycle
  declarations) and `src/operators/<operator>.cpp` (its implementation).
  A single-Operator Provider binds the API table directly to its Operator
  callbacks instead of adding forwarding wrappers. Reuse the SDK-private
  `<kspacejet/provider/detail/provider_support.hpp>` utilities for generic ABI
  validation and error handling. Code genuinely shared by several Operators
  lives under `src/support/<meaningful-domain>.{hpp,cpp}` and is named for its
  function; never use a catch-all `provider_internal.*` or
  `provider_common.*`. Namespaces follow the same functional boundary:
  `api`, `operators`, `state`, or a meaningful domain name—never
  `provider_internal`. No algorithm belongs in the ABI entrypoint or a
  catch-all Provider source file.
- `types/registry.json` is the single source of truth for every executable
  payload type. Provider-owned OperatorContract ports author only a readable
  `type_ref`; Provider C/C++ code obtains the complete ABI descriptor through
  the generated `<kspacejet/provider/type_registry.h>` factories and matchers.
  Never hand-copy a type layout, type identity digest, or schema digest into a
  Provider, contract, test, or runtime call site. A semantic type change
  requires a new distinct TypeRef and regenerated headers; run
  `python3 tools/type_registry/generate.py --project-root . --check` before
  handing off a type-registry change.
- Start new Providers from `sdk/templates/provider/` and, for every Operator
  intended for Pipeline resolution, keep one Provider-owned OperatorContract
  JSON file under `contracts/`. A pure ABI conformance fixture may omit a
  pipeline contract only when its README explicitly says it is not
  resolvable. Add focused tests for every Operator and update its Provider
  descriptor, CMake source list, contract installation list, and
  documentation in the same change.
- `providers/catalog.json` is the canonical product map of Provider and
  Operator identities. Use it before adding an Operator: group it with an
  existing Provider only when its data domain, ABI, lifecycle, resource model,
  and release/trust boundary are genuinely the same; otherwise reserve a new
  Provider. Host ingress, buffering, ordering, admission, egress, and sink
  delivery are runtime facilities, never Provider Operators.
- `providers/interfaces/` reserves planned Operator boundaries. An
  `OperatorInterface` is deliberately non-executable: it is not an
  `OperatorContract`, bundle manifest, loader descriptor, or Pipeline input.
  Never add a planned-only Operator to a compiled Provider descriptor, CMake
  module, ResolvedPipeline, or ExecutionPlan. Promote it atomically only when
  its exact contract, ABI implementation, resource/terminal semantics, tests,
  and catalog state are ready; then remove the planned interface entry rather
  than retaining an alias or duplicate source of truth.
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
