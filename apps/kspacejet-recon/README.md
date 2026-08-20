# `ksj-recon`

`ksj-recon` is an offline HDF5 reconstruction reference executable, not an online service.
It admits a narrow, explicitly selected route, builds a normal `PipelineDefinition`, compiles
and verifies its execution plan, then runs the frozen graph through Provider node instances. It
never discovers a Provider from a directory or chooses an algorithm on the caller's behalf.

Every Provider module and every OperatorContract is supplied by the caller.
The paths below are examples only; use the modules and contracts that belong to
the selected installation.

## `cartesian-rss`

The Cartesian route reconstructs one fully sampled 2-D Cartesian ISMRMRD/HDF5
frame and ends in `coil_combine_rss`.

```bash
ksj-recon cartesian-rss \
  --input scan.h5 \
  --output image.f32 \
  --cartesian-provider /absolute/path/to/libksj-cartesian-recon.so \
  --cartesian-contract /absolute/path/to/cartesian_ifft2_coil_images.json \
  --coil-combine-provider /absolute/path/to/libksj-coil-combine.so \
  --coil-combine-contract /absolute/path/to/coil_combine_rss.json
```

`--metadata image.json` selects the metadata sidecar; otherwise it defaults to
`<output>.json`. `--dataset` defaults to `dataset`. `--format text|json`
selects the command result format.

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

The output is native-endian, row-major `float32` RSS data. This is a bounded engineering
reference route, not a clinical reconstruction or service claim.

## `noncartesian-rss`

The non-Cartesian command runs the explicit direct-adjoint reconstruction
Provider followed by RSS coil combination:

```bash
ksj-recon noncartesian-rss \
  --input radial.h5 \
  --output image.f32 \
  --noncartesian-provider /absolute/path/to/libksj-noncartesian-recon.so \
  --noncartesian-contract /absolute/path/to/noncartesian_adjoint_reconstruct.json \
  --coil-combine-provider /absolute/path/to/libksj-coil-combine.so \
  --coil-combine-contract /absolute/path/to/coil_combine_rss.json
```

It accepts exactly one 2-D non-Cartesian XML encoding. Every acquisition must
carry a finite two-coordinate trajectory, have no flags or discarded samples,
and use the same active-channel count. The command aggregates that bounded
sequence into one frame; it does not infer trajectory units, density weights,
sensitivity maps, or a SENSE model. `--metadata`, `--dataset`, and `--format`
have the same meaning as for `cartesian-rss`.
