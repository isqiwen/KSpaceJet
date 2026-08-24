# kspacejet-noncartesian-recon

`ksj-noncartesian-recon` is an open, development-only Provider for the
non-Cartesian multi-coil image-domain boundary. It is not a clinical
reconstruction algorithm and must not be used to make diagnostic decisions.

It exposes two synchronous, serial Operators:

```text
noncartesian_adjoint_reconstruct
radial_gridding_reconstruct
```

Both Operators take two dynamic inputs for the same logical frame:

- port 0, `kspace`: `ksj.noncartesian-kspace-frame`, canonical
  `[channel, sample]` complex-float32 channel-major storage;
- port 1, `trajectory`: `ksj.trajectory-frame`, canonical
  `[sample, coordinate]` float32 row-major storage, where coordinate 0 is the
  image-row frequency (`ky`) and coordinate 1 is the image-column frequency
  (`kx`). The radial runtime route explicitly converts raw ISMRMRD `[kx, ky]`
  into this Provider payload order.

Each batch contains exactly one item. The two items must have equal
`semantic_key_hash` and `order_key`; otherwise the Provider rejects the firing
before acquiring an output grant. The output is one
`ksj.coil-image-frame` in canonical `[ky, kx, channel]` complex-float32
coil-last storage. It preserves the k-space item's metadata and identity keys;
the host owns any ordinal sidecar.

## `noncartesian_adjoint_reconstruct`

The direct NUDFT oracle config is exactly:

```json
{"channels":8,"image_cols":128,"image_rows":128,"sample_count":4096}
```

`channels` is in `[1, 64]`, `image_rows` and `image_cols` are in `[2, 512]`,
and `sample_count` is in `[1, 65536]`. The Provider additionally rejects a
configuration when
`channels * image_rows * image_cols * sample_count > 268435456`. This is a
finite reference-kernel bound on direct-adjoint complex accumulations, not a
clinical NUFFT performance claim.

For every channel, it uses only public `ksj::nufft::direct_nudft2_adjoint`
with image origin `((image_rows - 1) / 2, (image_cols - 1) / 2)`, writes the
result into a pre-granted contiguous scratch image plane, then scatters it into
coil-last output. It is deliberately unweighted: no density compensation,
grid interpolation, sensitivity map, SENSE solve, hidden calibration, private
allocation, input retention, threads, files, network I/O, or terminal output.

## `radial_gridding_reconstruct`

The explicit 2-D radial config is exactly:

```json
{"channels":8,"density_compensation":"radial_analytic_ramp","image_cols":128,"image_rows":128,"sample_count":4096,"trajectory_units":"radians_per_pixel"}
```

The `density_compensation` and `trajectory_units` fields are required literal
values. This Operator therefore does not infer a DCF or a trajectory unit from
the payload. It accepts only finite two-coordinate trajectory values in
`[-pi, pi]` radians per pixel, computes the unnormalised radial analytic-ramp
DCF once in caller-owned scratch, and invokes
`ksj::nufft::radial_linear_gridding2_adjoint` for each channel. The gridding
primitive applies a 2-by-2 periodic linear scatter and an unscaled,
allocation-free inverse transform; the supported power-of-two axes use the
radix-2 FFT path. The direct NUDFT implementation remains the separate
same-DCF oracle.

Its configured bounds are `channels` in `[1, 64]`, `image_rows` and
`image_cols` in `[2, 512]` **and powers of two**, and `sample_count` in
`[1, 65536]`. Its scratch requirement is exactly:

```text
(2 * image_rows * image_cols + 2 * max(image_rows, image_cols)) * sizeof(complex_float32)
+ sample_count * sizeof(float)
```

The slices are, in order, the image/grid plane, FFT intermediate plane, two
FFT line vectors, and the analytic DCF vector. They are all caller-owned,
bounded, non-overlapping storage. The coil-image output grant is separate.
No Provider-owned allocation, trajectory or phase correction, coil
compression, sensitivity estimation/SENSE, 3-D/cine/EPI/partial-Fourier/GRAPPA
path, performance guarantee, or clinical claim is implied.

For both Operators, configured input sizes must fit their exact payload shape:

- k-space input: `channels * sample_count * 8` bytes;
- trajectory input: `sample_count * 2 * 4` bytes;
- coil-image output: `image_rows * image_cols * channels * 8` bytes.

The generated TypeRegistry matchers establish all three payload identities;
the Provider never copies TypeRef identity digests or layouts into source or
contracts, and its descriptor does not duplicate Contract content as an
operator digest.

## Development bundle identity

Until a signed bundle manifest exists, `bundle_digest` is SHA-256 over
NUL-delimited UTF-8 fields: the bundle domain, Provider ID, and
descriptor-order operator IDs, including the final NUL.
`src/provider_api.cpp` contains the descriptor literal:

```sh
printf '%s\0' \
  'kspacejet.provider-bundle' \
  'org.kspacejet.noncartesian-recon' \
  'noncartesian_adjoint_reconstruct' \
  'radial_gridding_reconstruct' |
  sha256sum
```

Its result is `4297da20fe070aae1988f456967aeed82d5bae62f8aad41585e25fe767000ef6`.
