# kspacejet-noncartesian-recon

`ksj-noncartesian-recon` is an open reference Provider for the non-Cartesian
multi-coil image-domain boundary. It is not a clinical reconstruction
algorithm and must not be used to make diagnostic decisions.

It exposes one synchronous, serial Operator:

```text
noncartesian_adjoint_reconstruct
```

The Operator takes two dynamic inputs for the same logical frame:

- port 0, `kspace`: `ksj.noncartesian-kspace-frame`, canonical
  `[channel, sample]` complex-float32 channel-major storage;
- port 1, `trajectory`: `ksj.trajectory-frame`, canonical
  `[sample, coordinate]` float32 row-major storage, where coordinate 0 is the
  image-row frequency and coordinate 1 is the image-column frequency.

Each batch contains exactly one item. The two items must have equal
`semantic_key_hash` and `order_key`; otherwise the Provider rejects the firing
before acquiring an output grant. The output is one
`ksj.coil-image-frame` in canonical `[ky, kx, channel]` complex-float32
coil-last storage. It preserves the k-space item's metadata and identity keys;
the host owns any ordinal sidecar.

Its config is exactly:

```json
{"channels":8,"image_cols":128,"image_rows":128,"sample_count":4096}
```

`channels` is in `[1, 64]`, `image_rows` and `image_cols` are in `[2, 512]`,
and `sample_count` is in `[1, 65536]`. The Provider additionally rejects a
configuration when
`channels * image_rows * image_cols * sample_count > 268435456`. This is a
deliberately finite reference-kernel bound on direct-adjoint complex
accumulations, not a clinical NUFFT performance claim.

For configured `C`, `R`, `Q`, and `S`, it enforces these exact payload sizes:

- k-space input: `C * S * 8` bytes;
- trajectory input: `S * 2 * 4` bytes;
- total input envelope: `C * S * 8 + S * 2 * 4` bytes;
- coil-image output: `R * Q * C * 8` bytes;
- request scratch: exactly one `R * Q * 8` byte complex image plane.

The descriptor permits exactly two input items, one output item, at most
128 MiB of output, and at most 2 MiB of scratch. The configured inputs are
also checked against the finite raw maxima of 32 MiB k-space plus 512 KiB
trajectory. The direct-adjoint work cap can make the effective accepted
envelope smaller.

For every channel, the Provider uses only public
`ksj::nufft::direct_nudft2_adjoint` with image origin
`((image_rows - 1) / 2, (image_cols - 1) / 2)`, writes its result into the
pre-granted contiguous scratch image plane, then scatters it into coil-last
output. It performs an unweighted direct adjoint: no density compensation,
grid interpolation, sensitivity map, SENSE solve, hidden calibration, private
allocation, input retention, threads, files, network I/O, or terminal output.

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
  'noncartesian_adjoint_reconstruct' |
  sha256sum
```

Its result is `6ca4098a56512026f49ee9c023d92e20b063369dcfccb610e12f5a1132db94a0`.
