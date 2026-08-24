# KSpaceJet agent guide

KSpaceJet is a C++20 open-source MRI reconstruction framework. ISMRMRD is its only MRI data-exchange semantic and persistent image-artifact format: current reference routes use standard ISMRMRD HDF5 input and emit standard ISMRMRD HDF5 images, and any future incremental input or image delivery must preserve the same ISMRMRD semantics. CLI JSON stdout is a command-control result, not an image artifact. ksj-gateway is the only planned external-integration boundary; its candidate-stable architecture is docs/architecture/KSpaceJet_gateway_architecture.md, while its current executable remains a scaffold. KSpaceJet does not implement scanner/acquisition hardware, vendor Connector SDKs, device MRD sessions, private wire protocols, or vendor transport control.

This repository is pre-release. It has substantial offline/synchronous development foundations, but a directory, CMake target, schema, README claim, partial test, or scaffold does not by itself prove a feature is accepted, online-capable, isolated, or production-ready.

## Repository map

- libs/core: logging, threading, memory, platform, process and performance primitives.
- libs/numerics: general numerical APIs and backend implementations.
- libs/io/kspacejet-ismrmrd: public ISMRMRD reader and offline replay foundation.
- libs/io/kspacejet-matio: MAT I/O for diagnostics/interoperability.
- libs/mri/kspacejet-mri-debug: reusable MRI inspection utilities.
- libs/recon/kspacejet-recon-model: immutable reconstruction artifacts, planning inputs, execution records, resource model and Provider contracts.
- libs/recon/kspacejet-recon-graph: PipelineDefinition parsing, compiler and independent verifier.
- libs/recon/kspacejet-recon-runtime: bounded runtime primitives, FrameSlot, synchronous executor and reference data paths.
- libs/recon/kspacejet-provider-sdk: C ABI and generated type registry for Providers.
- libs/recon/kspacejet-provider-loader: explicit dynamic Provider loader; it is in-process unless a separately verified isolation mode is active.
- providers: independently shipped Provider plugins, catalog, contracts and planned interfaces.
- apps/kspacejet-cli: public ksj command-line interface.
- apps/kspacejet-gateway: planned sole external-integration application; the currently installed binary is a scaffold and is not an accepted service.
- apps/kspacejet-recon: offline reference reconstruction today; service/online claims require dedicated acceptance evidence.
- apps/kspacejet-research: installed research application scaffold; planned evidence-freezing and paper-artifact workflows are currently unimplemented. It must not become a runtime/data-plane dependency.
- sdk/templates/provider: starting point for a third-party Provider.
- schemas: structural artifact schemas only; resolver/compiler/verifier/runtime own semantic safety.
- types/registry.json: single source of truth for executable payload types.
- tests/unit: focused unit and component tests.
- tests/apps: application and CLI JSON protocol tests.
- tests/benchmarks and tests/research: Linux-only benchmark/research targets.
- third_party/intel: Git-LFS Intel IPP/MKL/OpenMP payload and local Conan recipe.

## Naming and product boundaries

- Project/display name: KSpaceJet.
- C++ namespace and internal CMake target prefix: ksj / KSJ_.
- Public CMake aliases: KSpaceJet::feature.
- Public include root: kspacejet/.
- Directories: kspacejet-*; executable names: ksj or ksj-*.

Do not reintroduce legacy DPC applications, proprietary queue tables, private protocols, BRF/ComQ compatibility, old replay formats, or proprietary reconstruction algorithms. Providers own substantive reconstruction algorithms. Gateway-owned external connection, authentication, protocol decoding and egress are separate from runtime-owned normalized ingress, ordering, admission and artifact execution; neither belongs in Provider Operators.

Do not claim the following without an ACCEPTED work item in the canonical plan: public online service, gateway relay, Provider isolation, strict-online, deadline-qualified behavior, GPU scheduling, 256-channel capacity, clinical/diagnostic use, or throughput/latency guarantees.

## External raw-data workspace

KSpaceJet contains framework source, contracts, small synthetic fixtures, and generated development output only. It must not contain original MRI reconstruction payloads. Raw ISMRMRD/HDF5 data belongs exclusively to the sibling [KSpaceJet-ismrmrd-data](https://github.com/isqiwen/KSpaceJet-ismrmrd-data) repository, including its provenance, licence, checksum, and Git-LFS policy.

Every development checkout must use this exact sibling layout (the parent directory name itself is arbitrary):

```text
<workspace>/
  KSpaceJet/
  KSpaceJet-ismrmrd-data/
```

Do not copy, symlink, vendor, Git-LFS-track, or create a project-internal raw-data directory in KSpaceJet. Data-dependent development must pass an explicit path under the sibling data repository. Before committing, the platform pre-commit hook runs `tools/checks/check_workspace_layout.py`; it verifies the sibling repository identity and rejects `.mrd`, `.h5`, `.hdf5`, and `.ismrmrd` payloads anywhere in the KSpaceJet working tree. The checker does not fetch data or substitute for `KSpaceJet-ismrmrd-data/tools/verify-data.sh`.

## Pre-release evolution

KSpaceJet is incomplete and unreleased. Treat every project-owned ABI, API, artifact, schema, CMake target, installation path and source-layout decision as mutable.

Default rule: replace the old shape with the one current shape everywhere. Do not add forward/backward compatibility, migration shims, deprecated spellings, aliases, adapters, version negotiation, dual parsers/serializers, dual JSON fields or parallel formats unless the user explicitly asks for compatibility in the current task.

- Prefer one accurate name over aliases.
- Rename consistently across source, CMake targets, headers, schemas, canonical JSON, compiler/verifier logic, fixtures, tests, docs and diagnostics.
- Do not introduce numbered ABI/API generations, compatibility headers or transitional formats simply because old code exists.

## Autonomous delivery and progress tracking

docs/architecture/KSpaceJet_project_plan_and_acceptance.md is the canonical execution ledger for this repository. It owns the active roadmap, stable work-item IDs, dependencies, scope, acceptance criteria, required validation, evidence, handoff state and completion status. Existing architecture documents remain technical references; they do not independently change work-item status.

Read this AGENTS.md and the canonical plan at the start of every non-trivial task. Resume from its IN_PROGRESS, BLOCKED and READY entries; do not rely on conversation history and do not create a competing TODO list, status file or unchecked implementation plan.

### State machine

Use only PLANNED, READY, IN_PROGRESS, VERIFYING, ACCEPTED, BLOCKED, NOT_APPLICABLE, SUPERSEDED and REOPENED.

- Start only a READY item whose dependencies are ACCEPTED, or whose documented activation predicate has made a dependency NOT_APPLICABLE.
- Change an item to IN_PROGRESS before its first source edit and record base commit/tree plus the next exact action.
- Use VERIFYING only after implementation, focused tests, fixtures, registration and documentation are present.
- Move to ACCEPTED only after every stated acceptance criterion and required validation command passes and exact evidence is in the ledger. Compilation, schema validation, a source directory or a partial test is never enough.
- If a required platform, CI, fixture, device, decision, credential or public binding is absent, use BLOCKED. Record the exact command/output, missing input, impact and what unblocks it.
- NOT_APPLICABLE requires its predeclared deterministic activation predicate and evidence. SUPERSEDED requires successor ID and rationale.
- A regression or invalidated evidence record changes an accepted item to REOPENED. Never edit history or weaken an acceptance rule just to retain ACCEPTED.

### Autonomous work loop

1. Inspect the canonical ledger, current worktree and relevant source/tests. Preserve unrelated user changes.
2. Select the highest-priority READY item in dependency order. There may be exactly one active IN_PROGRESS item in a worktree.
3. Confirm scope and contract impact, then mark the item IN_PROGRESS in the ledger.
4. Implement only that item, including focused tests, fixtures, CMake/registry/catalog registration and docs.
5. Run the smallest meaningful validation first, then every task-mandated gate. Fix failures within scope; do not weaken tests or criteria.
6. Review diff, diff check, generated files, schema/type/ABI consistency and allowed-path compliance.
7. Record exact commands, platform, results, artifacts, limitations and next action; then set ACCEPTED or BLOCKED.
8. After an accepted item, autonomously select the next READY item. Ask the user only for a material product decision, missing authority, unsafe expansion, unavailable required evidence or conflicting normative requirements.

Never reset, revert, overwrite or discard unrelated user work. Do not commit, push, create a pull request, mutate remote issues, alter repository settings, contact external systems, use credentials, connect a real scanner or change production configuration unless the current user explicitly authorizes that action.

## Build

Inspect CMakePresets.json before configuring. Bootstrap the repository-local developer environment before a VS Code task or a direct build:

    bash tools/devenv/linux/bootstrap.sh

    powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1

Hosts require Git, Git LFS and default GCC/G++ 14 on Linux, or Git, Git LFS, Visual Studio 2022 v143 C++ Build Tools and a Windows SDK on Windows. Linux bootstrap uses apt to ensure host `just` is installed and, during `--prepare`, the project-curated Qt/X11 development prerequisites; Windows bootstrap installs `just` with winget when absent. UV, managed Python and project tools are repository-local. Compilers, Git and Git LFS are host tools.

On minimal Linux, bootstrap also needs curl or wget, tar and sha256sum or shasum. Linux VS Code debugging needs host gdb. The optional linux-release-static-analysis preset needs host clang++.

Bootstrap provisions the repository-local Python tool environment. Afterwards invoke the root `justfile` directly with the host `just`; do not duplicate the platform/preset mapping outside that file:

    just prepare-release
    just build-release-applications
    just check

    just prepare-release
    just build-release-applications
    just check

`prepare-debug` / `prepare-release`, `build-*-applications`, `install-*-applications`, `format-*`, `check`, `pre-commit`, `pre-push`, `workspace-check` and `plan-check` have the same names on Linux and Windows. The recipes select the platform-specific bootstrap script, Conan profile and CMake preset. For focused diagnostics that have no recipe, call the platform runner explicitly with a locked managed tool; do not select a system Conan/CMake/Ninja/formatter by PATH order.

Product application builds and unit/benchmark/research test builds are separate CMake trees. Never configure KSJ_BUILD_APPLICATIONS together with KSJ_BUILD_UNIT_TESTS, KSJ_BUILD_BENCHMARKS or KSJ_BUILD_RESEARCH.

## VS Code workflow

First run KSJ: bootstrap developer environment. Before the first application build for a platform/configuration, run the matching preparation task:

- KSJ: prepare Linux Debug environment
- KSJ: prepare Linux Release environment
- KSJ: prepare Windows Debug environment
- KSJ: prepare Windows Release environment

The initial bootstrap task invokes the platform bootstrap directly; every post-bootstrap VS Code prepare/build/install task invokes the matching shared `just` recipe. Preparation verifies Intel payload, exports recipes, runs Conan install and configures CMake. Re-run it only after deleting its build directory or changing dependencies, recipes, payload, CMake/preset or profile.

Application build tasks build all four executables incrementally:

- KSJ: build Linux Debug applications
- KSJ: build Linux Release applications
- KSJ: build Windows Debug applications
- KSJ: build Windows Release applications

They must not implicitly run bootstrap, Conan export/install or configure. F5 uses the same incremental path. Application install tasks must depend only on their matching incremental application build task.

KSJ_BUILD_RESEARCH controls tests/research only; it does not control the installed ksj-research executable.

## Engineering rules

### CLI and output

- Every application parses command lines with CLI11. Define every command, subcommand, option, positional argument and validation rule declaratively. Do not hand-scan argv or add undocumented aliases.
- Help and version are global. Machine-readable results use documented format text or json options.
- CLI JSON success/failure output goes only to stdout. Core diagnostics go to stderr. Do not mix log records into JSON stdout.
- A command that is only a scaffold must state that in help and JSON; it must not return a success that callers could interpret as an implemented service.
- Keep command-specific semantics in its owning app/library, not in a duplicate utility runtime.

### Platform, core and logging

- Put project-owned cross-platform behavior in libs/core/kspacejet-platform. Apps, Providers and domain libraries call semantic platform APIs, not OS headers or local platform ifdefs.
- Reuse an existing core capability before adding a cross-cutting leaf implementation. If the core API is insufficient, extend core with tests.
- Production diagnostics use kspacejet/logging/logging.hpp and KSJ_LOG or ksj::logging. Do not introduce direct spdlog use outside the logging layer.
- Core diagnostics and configured log files are plain text. Console diagnostics use stderr. CLI JSON output is a caller protocol, not a log format.
- Machine-readable observability belongs in metrics, trace, audit artifacts, RunRecord and documented CLI JSON. Do not make a second conflicting structured-log policy.
- Crash/signal emergency writes may use only async-signal-safe minimal paths; they must not call the normal logger.

### Data, runtime and resource semantics

- Keep raw acquisition handling streaming. An AcquisitionView or span is valid only inside its callback; do not retain it across an asynchronous boundary.
- Runtime materializes host-owned data before an async Provider call. Providers must not open sources, retain borrowed reader views or create unaccounted pools.
- Standard ISMRMRD HDF5 input enters reference routes through the runtime-owned `IsmrmrdHdf5ReplaySource`; terminal `ksj.image-frame` output reaches disk through the runtime-owned `IsmrmrdImageArtifactSink`. Formal results use only standard ISMRMRD `image_x` series; do not add `/ksj_recon`, `/ksj_debug`, or `/ksj_meta` result groups. `PipelineDefinition` remains a separate required JSON input; a future optional `/ksj_pipeline` extension requires its own accepted work item and must never be required to read a standard file. Neither Source nor Sink is a Provider Operator or CLI-owned file implementation. The Sink acknowledges its graph egress only after its standard HDF5 temporary-file readback and atomic publication succeed; this does not claim power-loss durability, retry or exactly-once delivery.
- HDF5 replay and any future host-submitted normalized ISMRMRD input must be semantically equivalent at the runtime-frame boundary. Process-internal backpressure accounting must not create a private wire message.
- A future Gateway Profile decoder must complete size validation and host-owned materialization before GatewayRunHost/runtime handoff. Socket buffers, parser scratch and Connector-owned memory cannot cross that asynchronous boundary.
- Gateway connection, decoder, scan and egress resources require separate item/charged-byte bounds and an atomic admission relationship with the runtime ledger. A selected public profile may define standard flow behavior; KSpaceJet must not invent private ACK/credit/retry semantics.
- Every queue, pool, frame slot, batch, spool and device buffer needs item and charged-byte bounds. No unbounded queue, hidden buffer or silent raw acquisition drop.
- Maximum acquisition count is a resource bound, not a frame completion predicate. Completion must be scan/plan-specific and explicit.
- A Provider cannot bypass host output grants, resource ledger, plan or lifecycle. All fan-out, merge, reorder, partial output and terminal behavior must be explicit and verified.
- Do not claim GPU/NUMA/parallel behavior without the corresponding accepted DevicePlan, MachinePolicy, capability and fault/cancel evidence.

### Providers

- A Provider is one independently loadable dynamic-library, trust and lifecycle boundary. An Operator is one single-purpose pipeline callable.
- Group Operators only when data domain, ABI, lifecycle, resource model and release/trust boundary genuinely match.
- Provider entry layout is mandatory: src/provider_entry.cpp only exports ksj_provider_query; src/provider_api.hpp declares the API boundary; src/provider_api.cpp owns descriptors/registration/dispatch; src/provider_state.hpp contains Provider-shared private state and ABI-opaque handles.
- Every Operator, including a single-Operator Provider, has src/operators/name.hpp and src/operators/name.cpp. Shared code belongs in a meaningful src/support domain file; never use provider_internal or provider_common catch-alls.
- Reuse provider_support.hpp for generic ABI validation/error handling. No algorithm belongs in the ABI entrypoint.
- types/registry.json is the sole source of executable payload types. Provider contracts use readable type_ref; Provider C/C++ gets layouts and matchers from generated type registry factories. Never hand-copy type layout or digest.
- A type semantic change creates a distinct TypeRef and updates registry, generated headers, schema, contracts, fixtures, tests and callers together. Run:

    python3 tools/type_registry/generate.py --project-root . --check

- Start new Providers from sdk/templates/provider. Every resolvable Operator has one Provider-owned contract under contracts, focused tests, catalog entry, CMake source/install registration and documentation.
- providers/catalog.json is the canonical product map. Planned interfaces in providers/interfaces are non-executable reservations; do not put them in an executable descriptor, CMake module, ResolvedPipeline or ExecutionPlan until promotion is atomically complete.
- The current in-process loader is not a fault boundary. Do not claim that untrusted, crashing or hanging Providers have a qualified isolation boundary until worker/supervisor isolation has separately passed its plan task.

### Numerical and performance work

- Favor buffer reuse, contiguous traversal, bounded allocation and benchmark-backed backend selection in hot paths.
- Avoid unnecessary copies, allocations and thread oversubscription.
- Eigen is the public numerical baseline. Intel, FFTW, OpenCV, ITK and MATIO stay behind private implementation boundaries where possible; public headers expose KSpaceJet semantics, not vendor types.
- Correctness and resource bounds precede performance claims. A microbenchmark, an unrecorded run or a non-target machine is not a service latency/throughput proof.
- Any capacity claim, including 256 channels, must cite TargetEnvelope, MachinePolicy, input case, resource model, actual high-water and benchmark evidence.

### ABI, schema and change control

- Consider Linux shared-library and Windows DLL symbol boundaries for every CMake change.
- JSON Schema validates structure. Resolver/compiler/verifier/runtime tests prove semantic safety. Always provide both valid and schema-valid/semantic-invalid fixtures when relevant.
- Any public API, C ABI, schema, TypeRef, CMake install surface, CLI JSON or artifact digest change must be recorded as contract impact in the active work item, with all direct registrations, docs and tests updated in the same change.
- Do not add a compatibility shim to hide an incomplete migration. Replace the old shape throughout the repository.

## Validation matrix

Choose the smallest relevant test first and then the complete acceptance gates from the active work item.

| Change | Minimum validation |
| --- | --- |
| Markdown or plan document | git diff --check, path/link check and matching ledger update. |
| C++ unit implementation | relevant linux-release-unit-tests configure/build and ctest -R; full unit suite before phase acceptance. |
| Application or CLI | independent linux-release-app-tests tree, JSON protocol test and help/format contract. |
| Schema, pipeline or graph | valid/invalid fixtures, parser/graph tests and semantic verifier tests. Schema syntax alone is insufficient. |
| TypeRegistry | registry plus generated headers and generator check. |
| Provider | catalog/identity validation, contract, CMake registration and loader/provider focused tests. |
| CMake, Conan or install | formatter, relevant preset configure and Linux/Windows installation/dynamic-dependency evidence where applicable. |
| Runtime/concurrency | resource bounds, normal/cancel/error terminal tests, serial oracle and sanitizer/TSAN when required by the work item. |
| Host-submitted incremental input | normalized in-process ISMRMRD vectors, admission/cancel/callback and bounded-backpressure tests. No socket, session, relay or private wire message. |
| External gateway | P5-009 public-profile conformance, TLS/auth and route tests, bounded fake-peer/slow-peer/fuzz/disconnect tests, owned-buffer/runtime-equivalence evidence and clean-install qualification. No vendor raw protocol or private wire. |
| Performance | correctness first; record hardware, preset, threads, input, baseline, raw samples and statistics. |

Useful standard checks:

    git diff --check
    just format-all
    just unit
    just check
    just full

Do not commit out/, generated reports, cache files, benchmark output or reconstruction output.

## Final handoff discipline

At the end of a task, report the completed work-item ID, changed public behavior, exact validation performed, evidence location, remaining limitations and next READY item. Do not state that a phase, mode or project is complete unless the canonical ledger shows every required task ACCEPTED.

Use apply_patch for manual edits. Do not revert unrelated worktree changes.
