# kspacejet-calibration

ksj-calibration is the executable Provider boundary for estimating explicit,
scan-bound conditioning artifacts. It does not apply those artifacts, perform
image reconstruction, combine coils, or own hidden cross-Provider state.

It exposes three serial, synchronous Operators:

~~~text
noise_model_estimate            {"channel_count":N}
phase_correction_estimate       {"channel_count":N,"readout_sample_count":K}
coil_compression_basis_estimate {"physical_channel_count":P,"virtual_channel_count":V}
~~~

All configurations are compact canonical JSON with the key order shown and
positive decimal integers only. N and P are at most 64, K is at most 4096,
and coil compression requires V <= P. The Provider ABI carries the TypeRef
and byte count but not dynamic axis extents, so these minimal static
configuration fields establish the non-derivable axes; remaining sample or
reference-line counts are derived from the exact payload byte count.

| Operator | Input to output | Numerical convention |
| --- | --- | --- |
| noise_model_estimate | ksj.noise-calibration-frame to ksj.noise-model | Treat input [channel,sample] as samples × channels, estimate mean-centred population covariance, then use caller-workspace Hermitian eigendecomposition and whitening with a fixed positive 1e-6 eigenvalue floor. Output is row-major W[output_channel,input_channel], applied as y = W x. |
| phase_correction_estimate | ksj.phase-reference-frame to ksj.phase-model | For every [channel,kx], sum its phase-reference lines and emit conj(sum / abs(sum)); an exactly zero sum emits 1 + 0i. The matching phase_correct Operator multiplies k-space by this phasor. |
| coil_compression_basis_estimate | ksj.kspace-frame to ksj.coil-compression-basis | Form uncentred Hermitian channel covariance R = Σ x xᴴ / (ky*kx), use caller-workspace Hermitian eigendecomposition, and emit the conjugate-transposed largest eigenvectors. Output is row-major B[virtual_channel,physical_channel], applied as y = B x. |

The payload types are obtained and checked solely through the generated
<kspacejet/provider/type_registry.h> registry helpers. All complex payloads
use their registered little-endian interleaved float32 real/imaginary
encoding; the Provider decodes and writes that wire representation explicitly.
Input and output metadata must be empty because each current TypeRef declares
no per-item metadata semantics.

Each firing consumes one input item, seals one output item, and copies its
semantic key hash and order key. The Provider supports a 64 MiB input cap, a
2 MiB output cap, zero retention, no asynchronous work, no private threads,
and no direct file or network I/O. Its nonzero temporary memory is an exact,
caller-owned firing scratch reservation: for N channels,
`noise_model_estimate` requires `24*N*N + 12*N` bytes and
`coil_compression_basis_estimate` requires `24*N*N + 4*N` bytes; phase
estimation requires none. This is part of the Provider's descriptor and
planning reservation. The callbacks use neither MemoryBroker nor hidden
Eigen/pool temporaries. Node-owned planning requirements still choose finite
scheduling, resource reservations, bindings, and terminal behavior for each
pipeline use.

The three contracts are
[noise_model_estimate.json](contracts/noise_model_estimate.json),
[phase_correction_estimate.json](contracts/phase_correction_estimate.json),
and
[coil_compression_basis_estimate.json](contracts/coil_compression_basis_estimate.json).
Each exact OperatorContract declares the typed ports; the Provider descriptor
does not duplicate Contract content as an operator digest.

## Development bundle identity

Until a signed content-addressed bundle manifest exists, bundle_digest is
SHA-256 over NUL-delimited UTF-8 fields including the final NUL: the domain
separator, Provider ID, then descriptor-order operator IDs. The current
descriptor order is noise model, phase correction, coil-compression basis.
This command produces the literal in
src/provider_api.cpp:

~~~sh
printf '%s\0' \
  'kspacejet.provider-bundle' \
  'org.kspacejet.calibration' \
  'noise_model_estimate' \
  'phase_correction_estimate' \
  'coil_compression_basis_estimate' |
  sha256sum
~~~

Its result is
`abd611a1428074691393e7057e21266246647acd9c8ec7d1d4ad65a971d40f25`.
