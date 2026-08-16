# KSpaceJet reconstruction model

`ksj_recon_model` is KSpaceJet's vendor-free reconstruction value-model
boundary. CMake consumers use `KSpaceJet::recon_model`, and C++ consumers
include `<kspacejet/recon/model.hpp>`.

It provides:

- bounded canonical quantities and checked resource arithmetic;
- ISMRMRD XML to immutable `ScanDescriptor` parsing, with `ismrmrd/xml.h`
  confined to the implementation;
- `TargetEnvelope` and `MachinePolicy` deployment constraints;
- Provider-owned `OperatorContract` declarations for stable operator identity
  and typed ports; per-node planning requirements belong to the graph layer's
  `PlanBuildRequest`, not the Provider contract;
- readable registry `TypeRef` values resolved into exact structural
  `TypeDescriptor` models with automatic identity digests, plus multi-domain
  `ResourceVector` models shared by planning, verification, and admission;
- immutable `ExecutionPlan` models, including one `OperatorPlanBinding` per
  node that freezes its canonical-config digest, plus independent
  `VerificationRecord` and `AdmissionRecord` models;
- strict, bounded public canonical JSON round trips for `AdmissionRecord` and
  terminal `RunRecord`, without self-digest fields or runtime persistence.

It deliberately does not compile PipelineDefinition graphs, load Providers, make
admission decisions, or schedule reconstruction work.  Those responsibilities
belong to the graph compiler and bounded runtime layers that depend on this
shared library.

The JSON schemas provide the portable structural form.  Parsing then invokes
the same value constructors as in-process callers for semantic constraints
that JSON Schema cannot express portably, such as unique device identities,
non-empty device reservations, artifact-chain ordering, and distinct replay
lineage. A schema-valid document is therefore necessary but not sufficient
for a valid KSpaceJet model artifact.
