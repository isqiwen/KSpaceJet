# KSpaceJet documentation

KSpaceJet is an ISMRMRD-only, streaming MRI reconstruction framework. The
repository contains framework infrastructure and numerical primitives; providers
own reconstruction algorithms and their deployment configuration.

## Executable topology

The project has four deliberately separate executable projects. All four belong to
the normal application build and install:

- `ksj`: the single user-facing CLI.
- `ksj-gateway`: site/external-system integration gateway service.
- `ksj-recon`: online reconstruction service and runtime owner.
- `ksj-research`: experiment orchestration, evidence freezing, statistics, and paper
  artifacts.

`ksj-research` is installed as an experiment tool, not a runtime or data-plane dependency
of the CLI, gateway, or reconstruction service. It remains separate from the existing
`KSJ_BUILD_RESEARCH` test/research switch. In VS Code, first run the visible
`KSJ: bootstrap developer environment` task, which calls the matching project-local
bootstrap; then, before the first build or F5 debug session for a platform/configuration,
run its matching `KSJ: prepare <platform> <config> environment` task. The application build
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
`ksj-gateway` and `ksj-recon` exchange only the frozen public MRD/ISMRMRD
streaming session; proprietary adaptation remains in a separately deployed
site Connector and there is no KSpaceJet-private wire protocol. The detailed
ownership and deployment rules are in the framework plan below.

- [Build](conventions/build.md)
- [Developer environment](../tools/devenv/README.md)
- [Testing](conventions/testing.md)
- [Numerics API](conventions/numerics_api.md)
- [Coding convention](conventions/coding.md)
- [Streaming reconstruction framework plan](architecture/streaming_reconstruction_framework_plan.md)
- [Optimized PipelineDefinition and reconstruction implementation baseline](architecture/KSpaceJet_pipeline_review_optimized_v1.md)
- [PipelineDefinition v1 historical design record](architecture/pipeline_definition_v1.md)
- [MRI streaming pipeline, parallelism and proof model](architecture/streaming_pipeline_parallelism_theory.md)
- [Paper drafts and reproducible research](papers/README.md)
- [KSpaceJet resource-contracted streaming paper draft](papers/kspacejet_resource_contract_streaming_paper_draft.md)
- [KSpaceJet multi-baseline comparison protocol](papers/kspacejet_gadgetron_comparison_protocol.md)

Documentation for obsolete private data formats and protocols was removed with
the corresponding product code.
