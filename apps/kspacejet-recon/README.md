# `ksj-recon`

`ksj-recon` is an offline HDF5 reconstruction reference executable, not an online service.
It admits a narrow, explicitly selected route, builds a normal `PipelineDefinition`, compiles
and verifies its execution plan, then runs the frozen graph through Provider node instances. It
never discovers a Provider from a directory or chooses an algorithm on the caller's behalf.

## Required reconstruction inputs

The intended public reconstruction contract is one ISMRMRD scan and one
user-authored, editable `PipelineDefinition`:

```text
ksj-recon --input <scan.mrd> --pipeline <pipeline.json> --output <image.mrd>
```

The scan is the acquisition data; the Pipeline is a separate required input
that selects the ISMRMRD input profile, Providers/Operators, graph, and
static algorithm parameters. A scan alone is therefore not a complete
reconstruction request. The Pipeline may be reused for semantically
compatible scans, but it must not contain per-run input/output paths, DLL/SO
or contract paths, scan-derived dimensions, or thread/queue/memory settings.
Those bindings remain runtime-owned.

Its `input_profile` expresses raw-container intent as either
`{"container":{"mode":"auto"}}` or
`{"container":{"mode":"explicit","path":"/absolute-hdf5-container"}}`.
The absolute path is an HDF5 container path inside the selected input file, not
a host filesystem path. The future P2-007 runtime source adapter, rather than
the authored parser, discovers standard raw-container candidates: `auto`
requires exactly one, and `explicit` binds the named standard raw container.
There is no fixed `/dataset` convention in `PipelineDefinition`.

This root-command interface is planned as `P2-007`; its required resolver,
scan-fact binding, verifier, and RunRecord prerequisites are not accepted yet.
Until then, the three commands below are development-only route facades: their
caller-supplied Provider/contract paths and their C++-assembled
PipelineDefinitions are transitional implementation details, not the future
`ksj-recon` interface.

For these current facades only, every Provider module and every
OperatorContract is supplied by the caller. The paths below are examples only;
use the modules and contracts that belong to the selected installation.

## `cartesian-rss`

The Cartesian route reconstructs one fully sampled 2-D Cartesian ISMRMRD/HDF5
frame and ends in `coil_combine_rss`.

```bash
ksj-recon cartesian-rss \
  --input scan.h5 \
  --output image.mrd \
  --cartesian-provider /absolute/path/to/libksj-cartesian-recon.so \
  --cartesian-contract /absolute/path/to/cartesian_ifft2_coil_images.json \
  --coil-combine-provider /absolute/path/to/libksj-coil-combine.so \
  --coil-combine-contract /absolute/path/to/coil_combine_rss.json
```

`--output` must name one `.mrd` file. The temporary route-local `--dataset`
option still defaults to `dataset`; it is not the authored
`input_profile.container` selector and will be replaced with the P2-007 source
adapter. `--format text|json` selects only the command result printed to
stdout; it does not select an image-file format.

### Optional conditioning branches

Each optional branch is all-or-nothing: provide the complete group of explicit
Provider and contract paths. A partial group is rejected before reconstruction.

```text
noise calibration    -> noise_model_estimate             -> noise_prewhiten
phase reference      -> phase_correction_estimate        -> phase_correct
parallel calibration -> coil_compression_basis_estimate  -> coil_compress

imaging acquisition -> [enabled conditioning operators]
                    -> [readout_oversampling_remove]
                    -> cartesian_ifft2_coil_images -> coil_combine_rss
```

Noise prewhitening:

```bash
  --noise-model-estimate-provider /path/libksj-calibration.so \
  --noise-model-estimate-contract /path/noise_model_estimate.json \
  --noise-prewhiten-provider /path/libksj-kspace-conditioning.so \
  --noise-prewhiten-contract /path/noise_prewhiten.json
```

Phase correction:

```bash
  --phase-correction-estimate-provider /path/libksj-calibration.so \
  --phase-correction-estimate-contract /path/phase_correction_estimate.json \
  --phase-correct-provider /path/libksj-kspace-conditioning.so \
  --phase-correct-contract /path/phase_correct.json
```

Coil compression requires the target number of virtual channels:

```bash
  --coil-compression-basis-estimate-provider /path/libksj-calibration.so \
  --coil-compression-basis-estimate-contract /path/coil_compression_basis_estimate.json \
  --coil-compress-provider /path/libksj-kspace-conditioning.so \
  --coil-compress-contract /path/coil_compress.json \
  --virtual-channel-count 8
```

Readout oversampling removal is likewise explicit:

```bash
  --readout-oversampling-remove-provider /path/libksj-kspace-conditioning.so \
  --readout-oversampling-remove-contract /path/readout_oversampling_remove.json \
  --readout-offset 64
```

The HDF5 preflight derives the encoded and reconstructed geometry from the
ISMRMRD XML and channel count from acquisition headers. It accepts normal
imaging plus only the calibration lanes for enabled branches: noise
measurements, phase-correction references, and parallel calibration. A
parallel-calibration-and-imaging acquisition supplies both relevant roles.
It rejects inconsistent geometry, trajectories, 3-D coordinates, discarded
samples, changing channel counts, non-finite values, partial or duplicate
imaging `ky` coverage, or calibration data for a disabled branch. This temporary reference
route currently supports dimensions in `[2, 512]` and `[1, 64]` physical channels; the channel
limit is not a KSpaceJet framework capacity claim.

## Standard image artifact

Every reconstruction route writes exactly one standard ISMRMRD HDF5 image
artifact (`.mrd`), not a raw `.f32` image or JSON sidecar. The file contains
the validated source XML at `/dataset/xml` and one magnitude `float32` image
at `/dataset/image_0`, including the standard ImageHeader, image data, and
MetaAttributes datasets. KSpaceJet maps its internal `[row=ky][column=kx]`
pixels to ISMRMRD `image(x=column, y=row)` without transposition. The
ImageHeader uses the reconstruction matrix/FOV and the first accepted imaging
acquisition's measurement, position, orientation, index, and timing fields.

The standard MetaAttributes include `DataRole=Image` and `ImageNumber=1`.
Namespaced `KSpaceJet.*` attributes record route, source-XML digest, plan and
verification digests, input counts, operators, and radial-only trajectory/DCF
provenance. They stay inside the standard ISMRMRD metadata container, so no
private companion file is required. The runtime-owned
`IsmrmrdImageArtifactSink` receives the terminal `ksj.image-frame` egress
lease, verifies the closed temporary HDF5 file through the ISMRMRD reader, and
atomically publishes it before acknowledging that egress. Atomic publication
is not a power-loss durability, retry, or exactly-once guarantee.

This is a bounded engineering reference route, not a clinical reconstruction
or service claim.

## `noncartesian-rss`

The non-Cartesian command runs the explicit direct-adjoint reconstruction
Provider followed by RSS coil combination:

```bash
ksj-recon noncartesian-rss \
  --input radial.h5 \
  --output image.mrd \
  --noncartesian-provider /absolute/path/to/libksj-noncartesian-recon.so \
  --noncartesian-contract /absolute/path/to/noncartesian_adjoint_reconstruct.json \
  --coil-combine-provider /absolute/path/to/libksj-coil-combine.so \
  --coil-combine-contract /absolute/path/to/coil_combine_rss.json
```

It accepts exactly one 2-D non-Cartesian XML encoding. Every acquisition must
carry a finite two-coordinate trajectory, have no flags or discarded samples,
and use the same active-channel count. The command aggregates that bounded
sequence into one frame; it does not infer trajectory units, density weights,
sensitivity maps, or a SENSE model. `--dataset` and `--format` have the same
meaning as for `cartesian-rss`.

## `radial-rss`

`radial-rss` is a separate 2-D radial linear-gridding reference route. It does
not alias or replace `noncartesian-rss`: the latter remains the direct-adjoint
oracle with no density compensation.

```bash
ksj-recon radial-rss \
  --input radial.h5 \
  --output image.mrd \
  --radial-provider /absolute/path/to/libksj-noncartesian-recon.so \
  --radial-contract /absolute/path/to/radial_gridding_reconstruct.json \
  --coil-combine-provider /absolute/path/to/libksj-coil-combine.so \
  --coil-combine-contract /absolute/path/to/coil_combine_rss.json \
  --trajectory-units cycles-per-fov
```

The command accepts only one declared 2-D `radial` ISMRMRD encoding with
power-of-two reconstructed axes. It requires
`--trajectory-units cycles-per-fov|radians-per-pixel|encoded-matrix-index`.
Raw ISMRMRD coordinates are `[kx, ky]`; the route checks each selected raw
unit interval, normalizes `kx` with the encoded width and `ky` with the
encoded height, then emits canonical `[row=ky, column=kx]`
`radians_per_pixel` coordinates before the Provider fires.
`encoded-matrix-index` means one raw coordinate unit is one index of the XML
encoded matrix. It exists because ISMRMRD carries trajectory numbers without
defining a universal normalization, and is never inferred from their
magnitudes. The Provider contract fixes
`density_compensation` to `radial_analytic_ramp` and uses caller-owned,
bounded linear-gridding/FFT workspace. This development-only route does not
perform trajectory or phase correction, SENSE, coil compression, 3-D/cine/EPI,
partial-Fourier, GRAPPA, performance qualification, or clinical reconstruction.
