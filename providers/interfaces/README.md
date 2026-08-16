# Planned Provider interfaces

This directory reserves the product-level Operator surface before an
implementation exists. Each JSON file is an `OperatorInterface` with
`availability: "planned"`; it is intentionally **not** an `OperatorContract`,
a Provider module, a bundle manifest, or a runtime-discoverable descriptor.

The authoritative machine-readable index is
[`providers/catalog.json`](../catalog.json). The catalog links every
planned Operator to one file here and distinguishes it from development
Providers that already expose a loadable ABI descriptor and exact contract.

An `OperatorInterface` fixes only the reusable algorithm boundary: Provider
ownership, Operator identifier, coarse semantic ports, and configuration-key
names. It deliberately does not claim an exact `TypeDescriptor`, payload
layout, ABI digest, resource/rate bound, calibration lifecycle, cancellation
rule, or terminal behavior. When the Provider is implemented, its
`contracts/<operator>.json` declares the exact typed interface; the
node-owned `NodePlanningRequirements` in `PlanBuildRequest` declares the
selected finite scheduling, rate, resource, calibration, and terminal
requirements. The Provider ABI remains the executable capability upper bound.

Neither a planned interface nor an executable contract carries an
execution-profile-selection field. Profile selection and its deployment
requirements belong to PipelineDefinition, MachinePolicy, admission, and the
runtime.

## Explicit multi-port semantics

An Operator may declare any number of uniquely named input and output ports.
There is no port-multiplicity field: it was ambiguous about whether it meant
connections, items, channels, batches, or graph branches.

- Use separate **named input ports** for semantically distinct dependencies.
  For example, `noise_prewhiten` consumes `kspace` and an explicit
  `noise_model`; adaptive coil combination consumes `coil_images` and
  `sensitivity_maps`. A future executable plan must use its `NodeJoinSpec` or
  calibration binding to state how those inputs are keyed, synchronized,
  retained, and closed.
- Every declared input is an explicit dependency. A future executable
  pipeline must satisfy it through either one ordinary data edge or one
  calibration binding; omitting a port is the only way to express that an
  Operator has no such dependency.
- A coil/channel axis is ordinarily one dimension of a single typed k-space or
  coil-image payload, not one port per coil. Item quantity per firing belongs
  to `NodeRateSpec` and batch bounds in `NodePlanningRequirements`, not to the
  port declaration.
- Use separate **named output ports** only for distinct data semantics, such
  as a calibration `phase_model` and a conditioning
  `phase_corrected_kspace`, each with its own type, rate, resource and
  terminal rules. `phase_correct` itself takes the named `kspace` and
  `phase_model` inputs; it does not discover calibration state. One output
  port may emit a sequence of items; that too is a `NodeRateSpec` question,
  not a port-count question.
- A graph branch is fan-out of one output edge to multiple consumers, and a
  3-D slice/slab split or reassembly is an explicit partition/split/merge
  plan. Neither is represented by duplicating ports or by a boolean/string
  multiplicity flag.

This distinction follows the useful boundaries visible in Gadgetron-style
chains: conditioning and calibration inputs are explicit dependencies, while
coils and image-array slices normally remain dimensions or partitioned items
inside a typed stream. KSpaceJet must make each future join, fan-out, split,
and merge policy executable before it promotes the corresponding interface.

Calibration material is not implicit Provider-shared state. A planned
calibration artifact output reserves its semantic type and intended consumer
only; a future executable plan must declare the producer/consumer calibration
roles in `NodePlanningRequirements`, and a `PipelineDefinition` must connect
them with one explicit `bindings.calibration` entry. The runtime-owned adapter
then creates the readiness dependency. A calibration artifact interface does
not create an ordinary data edge, a cache visible to another Provider, or a
runtime binding.

These files are not CMake modules and are never searched by the loader,
resolver, compiler, or runtime. A planned Operator cannot appear in a
`ResolvedPipeline` or `ExecutionPlan` and cannot be selected by the runtime.
Implemented contracts linked below live in their Provider
directories instead of this reservation directory.

## Provider boundaries

| Provider ID | Single coherent boundary | Current surface |
| --- | --- | --- |
| `org.kspacejet.kspace-conditioning` | Frame-level coil/noise/phase/readout conditioning before a trajectory-specific reconstruction. Six Cartesian/non-Cartesian whitening, compression, and readout operators are executable development contracts with explicit calibration input batches where needed; non-Cartesian phase correction remains intentionally planned. | [`noise_prewhiten`](../kspacejet-kspace-conditioning/contracts/noise_prewhiten.json), [`phase_correct`](../kspacejet-kspace-conditioning/contracts/phase_correct.json), [`coil_compress`](../kspacejet-kspace-conditioning/contracts/coil_compress.json), [`readout_oversampling_remove`](../kspacejet-kspace-conditioning/contracts/readout_oversampling_remove.json), [`noncartesian_noise_prewhiten`](../kspacejet-kspace-conditioning/contracts/noncartesian_noise_prewhiten.json), [`noncartesian_coil_compress`](../kspacejet-kspace-conditioning/contracts/noncartesian_coil_compress.json), [`noncartesian_phase_correct`](org.kspacejet.kspace-conditioning/noncartesian_phase_correct.json) |
| `org.kspacejet.calibration` | Scan-bound estimation of explicit calibration artifacts. It does not apply those artifacts or create runtime readiness tokens. The noise, Cartesian phase, and coil-compression estimators are executable contracts; sequence-specific non-Cartesian phase estimation remains planned. | [`noncartesian_phase_correction_estimate`](org.kspacejet.calibration/noncartesian_phase_correction_estimate.json), [`cartesian_grappa_kernel_calibrate`](org.kspacejet.calibration/cartesian_grappa_kernel_calibrate.json), [`sensitivity_map_estimate`](org.kspacejet.calibration/sensitivity_map_estimate.json) |
| `org.kspacejet.cartesian-recon` | Cartesian reconstruction algorithms. `cartesian_ifft2_coil_images` is an executable contract in `kspacejet-cartesian-recon`; these are the remaining planned interfaces. | [`cartesian_partial_fourier`](org.kspacejet.cartesian-recon/cartesian_partial_fourier.json), [`cartesian_grappa_reconstruct`](org.kspacejet.cartesian-recon/cartesian_grappa_reconstruct.json), [`cartesian_sense_reconstruct`](org.kspacejet.cartesian-recon/cartesian_sense_reconstruct.json) |
| `org.kspacejet.noncartesian-recon` | Non-Cartesian trajectory correction and reconstruction. `noncartesian_adjoint_reconstruct` is an executable unweighted direct-adjoint contract in `kspacejet-noncartesian-recon`; these are the remaining planned interfaces. | [`trajectory_correction`](org.kspacejet.noncartesian-recon/trajectory_correction.json), [`noncartesian_sense_reconstruct`](org.kspacejet.noncartesian-recon/noncartesian_sense_reconstruct.json) |
| `org.kspacejet.coil-combine` | Conversion of multi-coil image data into one image. `coil_combine_rss` is an executable contract in `kspacejet-coil-combine`; adaptive combination remains planned. | [`coil_combine_adaptive`](org.kspacejet.coil-combine/coil_combine_adaptive.json) |
| `org.kspacejet.image-ops` | Stateless, per-image transforms. The implemented scale/offset/clamp contracts are catalogued separately. | [`image_magnitude_float32`](org.kspacejet.image-ops/image_magnitude_float32.json), [`image_phase_float32`](org.kspacejet.image-ops/image_phase_float32.json), [`image_crop_float32`](org.kspacejet.image-ops/image_crop_float32.json), [`image_window_float32`](org.kspacejet.image-ops/image_window_float32.json) |
| `org.kspacejet.image-normalization` | Normalization whose statistics policy merits a distinct lifecycle boundary. | [`image_normalize`](org.kspacejet.image-normalization/image_normalize.json) |
| `org.kspacejet.image-quality` | Image validation and quality-report generation. | [`image_nonfinite_check`](org.kspacejet.image-quality/image_nonfinite_check.json), [`image_range_check`](org.kspacejet.image-quality/image_range_check.json) |

## Calibration artifact bindings

The following table closes the current artifact chain. The first three
producers and their five conditioning consumers are executable development
contracts; the remaining producers and consumers stay planned. The exact
calibration key, epoch/artifact-identity compatibility, completion, bounded
retention, and EndOfInput behavior remain a pipeline/executor concern. Every
relationship shown below requires an explicit calibration binding; none
authorizes a consumer to estimate, discover, or retain calibration state
implicitly.

| Producer | Artifact | Consumer |
| --- | --- | --- |
| [`org.kspacejet.calibration/noise_model_estimate`](../kspacejet-calibration/contracts/noise_model_estimate.json) | `ksj.noise-model` | [`org.kspacejet.kspace-conditioning/noise_prewhiten`](../kspacejet-kspace-conditioning/contracts/noise_prewhiten.json) |
| [`org.kspacejet.calibration/noise_model_estimate`](../kspacejet-calibration/contracts/noise_model_estimate.json) | `ksj.noise-model` | [`org.kspacejet.kspace-conditioning/noncartesian_noise_prewhiten`](../kspacejet-kspace-conditioning/contracts/noncartesian_noise_prewhiten.json) |
| [`org.kspacejet.calibration/phase_correction_estimate`](../kspacejet-calibration/contracts/phase_correction_estimate.json) | `ksj.phase-model` | [`org.kspacejet.kspace-conditioning/phase_correct`](../kspacejet-kspace-conditioning/contracts/phase_correct.json) |
| [`org.kspacejet.calibration/coil_compression_basis_estimate`](../kspacejet-calibration/contracts/coil_compression_basis_estimate.json) | `ksj.coil-compression-basis` | [`org.kspacejet.kspace-conditioning/coil_compress`](../kspacejet-kspace-conditioning/contracts/coil_compress.json) |
| [`org.kspacejet.calibration/coil_compression_basis_estimate`](../kspacejet-calibration/contracts/coil_compression_basis_estimate.json) | `ksj.coil-compression-basis` | [`org.kspacejet.kspace-conditioning/noncartesian_coil_compress`](../kspacejet-kspace-conditioning/contracts/noncartesian_coil_compress.json) |
| [`org.kspacejet.calibration/noncartesian_phase_correction_estimate`](org.kspacejet.calibration/noncartesian_phase_correction_estimate.json) | `ksj.noncartesian-phase-model` | [`org.kspacejet.kspace-conditioning/noncartesian_phase_correct`](org.kspacejet.kspace-conditioning/noncartesian_phase_correct.json) |
| [`org.kspacejet.calibration/cartesian_grappa_kernel_calibrate`](org.kspacejet.calibration/cartesian_grappa_kernel_calibrate.json) | `ksj.grappa-kernel` | [`org.kspacejet.cartesian-recon/cartesian_grappa_reconstruct`](org.kspacejet.cartesian-recon/cartesian_grappa_reconstruct.json) |
| [`org.kspacejet.calibration/sensitivity_map_estimate`](org.kspacejet.calibration/sensitivity_map_estimate.json) | `ksj.sensitivity-map` | [`org.kspacejet.cartesian-recon/cartesian_sense_reconstruct`](org.kspacejet.cartesian-recon/cartesian_sense_reconstruct.json), [`org.kspacejet.noncartesian-recon/noncartesian_sense_reconstruct`](org.kspacejet.noncartesian-recon/noncartesian_sense_reconstruct.json), [`org.kspacejet.coil-combine/coil_combine_adaptive`](org.kspacejet.coil-combine/coil_combine_adaptive.json) |

Ingress, HostFrameAssembler, bounded edges, BufferPool, reorder, egress and
sink delivery are host/runtime facilities—not Providers and not pipeline
Operators. They therefore have no entries here.

## Promotion rule

When an interface is implemented, replace its planned catalog entry with the
one canonical Provider module and its exact `OperatorContract`; do not keep a
second compatibility interface, module name, or alias. The contract is then
the only source of truth for operator identity and typed-port resolution;
node-owned planning requirements remain a separate PlanBuildRequest input.

Runtime support is defined only by executable Provider contracts and generic
ExecutionPlan validation. A planned interface remains unavailable until its
contract, implementation, tests, and catalog promotion are all present.

Run the repository cross-reference check after changing the catalog or an
interface:

```bash
tools/devenv/linux/run.sh python tests/unit/providers/provider_catalog_validation.py --project-root .
```
