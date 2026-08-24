# KSpaceJet documentation

KSpaceJet is a pre-release, ISMRMRD-only MRI reconstruction framework. The
repository contains framework infrastructure and numerical primitives; providers
own reconstruction algorithms and their deployment configuration.

Current product boundaries, artifact authority, executable work status, and
acceptance evidence are defined only by the [KSpaceJet master implementation plan](architecture/KSpaceJet_project_plan_and_acceptance.md).
The architecture and paper links below are retained historical, non-normative
design and research records. They do not redefine the active external-integration
architecture, which is solely [KSpaceJet gateway architecture](architecture/KSpaceJet_gateway_architecture.md),
nor do they revive scanner/vendor acquisition, private protocol, structured core logging,
or a competing artifact authority.

## Executable topology

The project has five deliberately separate executable projects. All five belong to
the normal application build and install:

- `ksj`: the current pipeline-validation and Provider-scaffold CLI.
- `ksj-gateway`: the planned sole external-integration application; its current installed binary is still a scaffold, with no gateway operation implemented.
- `ksj-recon`: an offline HDF5 Cartesian/non-Cartesian RSS reference executable.
- `ksj-research`: an installed research application scaffold; its operational commands are
  unimplemented.
- `ksj-viewer`: a local Qt Widgets inspection application. It reads standard ISMRMRD
  metadata/acquisitions/images through the bounded inspection reader, displays a
  parse-only `PipelineDefinition`, and exports explicitly labelled display derivatives;
  it does not reconstruct or create a second MRI artifact.

`ksj-research` is installed as a research-tool scaffold, not a runtime or data-plane dependency
of the other applications. It remains separate from the existing `KSJ_BUILD_RESEARCH`
test/research switch. In VS Code, first run the visible
`KSJ: bootstrap developer environment` task; the matching platform bootstrap provisions repository-local
Python tools and uses apt on Linux to ensure `just` is installed; each Linux `prepare` also installs the project-curated Qt/X11 development prerequisites (Windows uses winget when `just` is absent). Then, before the first build or F5 debug session for a
configuration on the current platform, run `KSJ: prepare Debug environment` or
`KSJ: prepare Release environment`.
All post-bootstrap tasks invoke the shared root `justfile` recipes. The application build
tasks are
`KSJ: build Debug applications` and `KSJ: build Release applications`;
each builds all five executables incrementally and does not prepare the environment. See
[Build](conventions/build.md) for the preparation triggers and exact task mapping. The
matching visible install tasks are `KSJ: install Debug applications` and
`KSJ: install Release applications`. Each install task depends only on its
matching incremental application build, not on preparation; it uses that preset's
`CMAKE_INSTALL_PREFIX` under `out/install/`.
No external session, Connector, transport, or gateway service is a **current** KSpaceJet product
capability. The candidate-stable future design is [KSpaceJet gateway architecture](architecture/KSpaceJet_gateway_architecture.md); it preserves the ban on scanner/vendor acquisition and private wire protocols.

- [Build](conventions/build.md)
- [Developer environment](../tools/devenv/README.md)
- [Testing](conventions/testing.md)
- [Numerics API](conventions/numerics_api.md)
- [KSpaceJet gateway architecture (candidate-stable; not implemented)](architecture/KSpaceJet_gateway_architecture.md)
- [ISMRMRD inspection reader contract](architecture/KSpaceJet_ismrmrd_inspection_reader.md)
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
