# KSpaceJet reconstruction contracts

`ksj_recon_contracts` is the vendor-free value-model boundary for scan admission
and execution planning.  It provides:

- bounded canonical quantities and checked resource arithmetic;
- ISMRMRD XML to immutable `ScanDescriptor` parsing, with `ismrmrd/xml.h`
  confined to the implementation;
- `TargetEnvelope` and `MachinePolicy` deployment contracts;
- Provider-owned `OperatorContract` declarations for ports, granularity,
  partition/order, batch/rate/output bounds, resources, calibration, and
  normal/cancel terminal behavior, plus validated join/reorder/channel-group
  bounds and online join-progress proofs;
- exact `TypeDescriptor` and multi-domain `ResourceVector` models shared by
  planning, verification, and admission;
- immutable `ExecutionPlan`, independent `VerificationRecord`, and
  `AdmissionRecord` models;
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
lineage.  A schema-valid document is therefore necessary but not sufficient
for a valid KSpaceJet contract artifact.
