# kspacejet-kspace-conditioning

`ksj-kspace-conditioning` is the executable Provider boundary for applying
explicit, scan-bound calibration artifacts to an assembled Cartesian or
non-Cartesian k-space frame before a reconstruction Provider consumes it. It does not estimate
calibration, assemble host acquisition data, reconstruct images, retain
frames, or own hidden cross-Provider state.

It exposes six serial, synchronous Operators:

~~~text
noise_prewhiten               {"channel_count":C,"cols":K,"rows":R}
phase_correct                 {"channel_count":C,"cols":K,"rows":R}
coil_compress                 {"cols":K,"physical_channel_count":P,"rows":R,"virtual_channel_count":V}
readout_oversampling_remove   {"channel_count":C,"input_cols":I,"output_cols":O,"readout_offset":D,"rows":R}
noncartesian_noise_prewhiten  {"channel_count":C,"sample_count":S}
noncartesian_coil_compress    {"physical_channel_count":P,"sample_count":S,"virtual_channel_count":V}
~~~

Configurations are compact canonical JSON with exactly the key order shown,
no whitespace, and decimal integer values. For the first two Operators,
`1 <= C <= 64` and `2 <= R,K <= 512`. Coil compression requires
`1 <= V <= P <= 64` and `2 <= R,K <= 512`. Readout cropping requires
`1 <= C <= 64`, `2 <= R,I,O <= 512`, and `D + O <= I`; its offset is
non-negative, so `0` is the canonical left-edge crop.
For each non-Cartesian Operator, `1 <= S <= 65536`; whitening also requires
`1 <= C <= 64`, while compression requires `1 <= V <= P <= 64`.

## Typed payload and numerical conventions

`ksj.kspace-frame` is contiguous channel-major complex float32 data with
layout `[channel, ky, kx]`. The corresponding MatrixView used by the linear
operators is `[channel, rows * cols]`; no transpose or implicit layout
conversion is performed. `ksj.noncartesian-kspace-frame` is a separate
contiguous channel-major complex float32 payload with layout `[channel,sample]`;
its MatrixView is `[channel,sample]`. Neither operator accepts the other
TypeRef.

| Operator | Inputs to output | Convention |
| --- | --- | --- |
| `noise_prewhiten` | `ksj.kspace-frame`, `ksj.noise-model` to `ksj.kspace-frame` | The model is row-major `W[C,C]` in `W[out,in]` order. The Provider computes `Y = W X` with public `KSpaceJet::linalg::matmul`, where `X[C,R*K]` is the channel-major input. |
| `phase_correct` | `ksj.kspace-frame`, `ksj.phase-model` to `ksj.kspace-frame` | The model is `[channel,kx]`. For every `ky`, output `[channel,ky,kx]` is input multiplied by the corresponding phasor `[channel,kx]`. |
| `coil_compress` | `ksj.kspace-frame`, `ksj.coil-compression-basis` to `ksj.kspace-frame` | The basis is row-major `B[V,P]` in `B[virtual,physical]` order. The Provider computes `Y = B X` with public `KSpaceJet::linalg::matmul`. |
| `readout_oversampling_remove` | `ksj.kspace-frame` to `ksj.kspace-frame` | For every `[channel,ky]`, copy exactly `input[kx=D..D+O)` into the `O`-wide output. There is no centered-crop assumption. |
| `noncartesian_noise_prewhiten` | `ksj.noncartesian-kspace-frame`, `ksj.noise-model` to `ksj.noncartesian-kspace-frame` | The same row-major `W[C,C]` convention applies directly to `X[C,S]`: `Y = W X` through public `KSpaceJet::linalg::matmul`. |
| `noncartesian_coil_compress` | `ksj.noncartesian-kspace-frame`, `ksj.coil-compression-basis` to `ksj.noncartesian-kspace-frame` | The same row-major `B[V,P]` convention applies directly to `X[P,S]`: `Y = B X` through public `KSpaceJet::linalg::matmul`. |

Payload sizes are exact: k-space is `channels * rows * cols * sizeof(cf32)`,
a noise model is `C*C*sizeof(cf32)`, a phase model is
`C*K*sizeof(cf32)`, and a compression basis is `V*P*sizeof(cf32)`. The
largest supported k-space input or output is `64*512*512*sizeof(cf32)`
(128 MiB); a non-Cartesian frame is at most `64*65536*sizeof(cf32)` (32 MiB).
Inputs and outputs are host-pageable, 64-byte aligned, and carry
empty metadata. TypeRefs are created and matched only through the generated
`<kspacejet/provider/type_registry.h>` helpers.

Each firing has exactly one dynamic k-space batch and, where required, one
ordinary named calibration input batch. Dynamic payloads use ABI port 0;
calibration inputs use ABI port 1; output uses ABI port 0. The executor owns
the explicit calibration binding and may source an immutable artifact from its
`CalibrationArtifactStore`. A calibration item with a nonzero semantic key
hash and/or order key must match the corresponding dynamic k-space key; a
zero field represents an unkeyed static artifact and is therefore accepted.
The output preserves the dynamic k-space semantic and order keys and has
empty metadata.

The Provider retains no input, requests no scratch space, starts no
asynchronous work or private threads, and produces no terminal output. It
rejects missing, duplicated, unknown, mistyped, mis-sized, or key-incompatible
input batches before applying an operation. Node-owned planning requirements
remain responsible for finite scheduling and resources in a pipeline.

The exact contracts are
[noise_prewhiten.json](contracts/noise_prewhiten.json),
[phase_correct.json](contracts/phase_correct.json),
[coil_compress.json](contracts/coil_compress.json), and
[readout_oversampling_remove.json](contracts/readout_oversampling_remove.json),
[noncartesian_noise_prewhiten.json](contracts/noncartesian_noise_prewhiten.json), and
[noncartesian_coil_compress.json](contracts/noncartesian_coil_compress.json).
Each contract declares only its typed ports; `src/provider_api.cpp` does not
duplicate Contract content as an operator digest.

Non-Cartesian phase correction is intentionally still planned. Its future
`noncartesian_phase_correct` interface consumes a distinct
`ksj.noncartesian-phase-model` estimated from a distinct
`ksj.noncartesian-phase-reference-frame`; it must not reuse the current
Cartesian `[channel,kx]` `ksj.phase-model`.

## Development bundle identity

Until a signed, content-addressed bundle manifest exists, `bundle_digest` is
SHA-256 over NUL-delimited UTF-8 fields including the final NUL: the domain
separator, Provider ID, then descriptor-order operator IDs. This command
reproduces the literal in `src/provider_api.cpp`:

~~~sh
printf '%s\0' \
  'kspacejet.provider-bundle' \
  'org.kspacejet.kspace-conditioning' \
  'noise_prewhiten' \
  'phase_correct' \
  'coil_compress' \
  'readout_oversampling_remove' \
  'noncartesian_noise_prewhiten' \
  'noncartesian_coil_compress' |
  sha256sum
~~~

Its result is `6ff48e549c4ff3ad41cfebb766e6d298360bf686dc7882f5df9e6401b7dc7325`.
