# KSpaceJet documentation

KSpaceJet is a pre-release, ISMRMRD-only MRI reconstruction framework. The
repository contains framework infrastructure and numerical primitives; providers
own reconstruction algorithms and their deployment configuration.

Current product boundaries, artifact authority, executable work status, and
acceptance evidence are defined only by the [KSpaceJet master implementation plan](architecture/KSpaceJet_project_plan_and_acceptance.md).
The architecture and paper links below are retained historical, non-normative
design and research records. In particular, they do not revive proposals for
external MRD sessions, `ksj-gateway`, Connector, scanner or acquisition integration,
network transport/relay, structured core logging, or a competing artifact authority.

## Executable topology

The project has four deliberately separate executable projects. All four belong to
the normal application build and install:

- `ksj`: the current pipeline-validation and Provider-scaffold CLI.
- `ksj-gateway`: an installed application scaffold; no gateway operation is implemented.
- `ksj-recon`: an offline HDF5 Cartesian/non-Cartesian RSS reference executable.
- `ksj-research`: an installed research application scaffold; its operational commands are
  unimplemented.

`ksj-research` is installed as a research-tool scaffold, not a runtime or data-plane dependency
of the other applications. It remains separate from the existing `KSJ_BUILD_RESEARCH`
test/research switch. In VS Code, first run the visible
`KSJ: bootstrap developer environment` task; the matching platform bootstrap provisions repository-local
Python tools and uses apt on Linux to ensure `just` is installed (Windows uses winget when it is absent). Then, before the first build or F5 debug session for a
platform/configuration, run its matching `KSJ: prepare <platform> <config> environment` task.
All post-bootstrap tasks invoke the shared root `justfile` recipes. The application build
tasks are
`KSJ: build Linux Debug applications`, `KSJ: build Linux Release applications`,
`KSJ: build Windows Debug applications`, and `KSJ: build Windows Release applications`;
each builds all four executables incrementally and does not prepare the environment. See
[Build](conventions/build.md) for the preparation triggers and exact task mapping. The
matching visible install tasks are `KSJ: install Linux Debug applications`,
`KSJ: install Linux Release applications`, `KSJ: install Windows Debug applications`, and
`KSJ: install Windows Release applications`. Each install task depends only on its
matching incremental application build, not on preparation; it uses that preset's
`CMAKE_INSTALL_PREFIX` under `out/install/`.
No external session, Connector, transport, gateway service, or scanner integration is a current
KSpaceJet product capability. KSpaceJet does not define a private wire protocol.

- [Build](conventions/build.md)
- [Developer environment](../tools/devenv/README.md)
- [Testing](conventions/testing.md)
- [Numerics API](conventions/numerics_api.md)
- [Coding convention](conventions/coding.md)
- [Historical streaming reconstruction framework plan (non-normative)](architecture/streaming_reconstruction_framework_plan.md)
- [Historical PipelineDefinition and reconstruction design review (non-normative)](architecture/KSpaceJet_pipeline_review_optimized.md)
- [Historical PipelineDefinition design record (non-normative)](architecture/pipeline_definition.md)
- [Historical MRI pipeline, parallelism and proof model (non-normative)](architecture/streaming_pipeline_parallelism_theory.md)
- [Historical paper drafts and research material (non-normative)](papers/README.md)
- [Historical resource-contracted streaming paper draft (non-normative)](papers/kspacejet_resource_contract_streaming_paper_draft.md)
- [Historical multi-baseline comparison protocol (non-normative)](papers/kspacejet_gadgetron_comparison_protocol.md)

Documentation for obsolete private data formats and protocols was removed with
the corresponding product code.
