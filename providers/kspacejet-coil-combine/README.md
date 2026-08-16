# kspacejet-coil-combine

`ksj-coil-combine` is an open reference Provider for post-reconstruction coil
combination. It deliberately does not reconstruct k-space or estimate hidden
calibration state.

It exposes one synchronous, serial Operator:

```text
coil_combine_rss
```

The Operator accepts one `ksj.coil-image-frame` in canonical
`[ky, kx, channel]` complex-float32 storage and emits one `ksj.image-frame` in
canonical `[ky, kx]` float32 storage. Its config is exactly:

```json
{"channels":8,"cols":128,"rows":128}
```

`channels` is in `[1, 64]`; `rows` and `cols` are in `[2, 512]`. The Provider
validates the exact configured input byte count, wraps the coil-last payload as
a KSpaceJet cube view, and calls public
`ksj::stats::root_sum_of_squares_across(..., Dim::dim2)`. It retains no input,
requires no host scratch, starts no threads, and emits no terminal output.

The generated TypeRegistry matchers establish both input and output identity;
the Provider never duplicates TypeRef identity digests in code or contracts.

## Development bundle identity

Until a signed bundle manifest exists, `bundle_digest` is SHA-256 over
NUL-delimited UTF-8 fields: the bundle domain, Provider ID, and
descriptor-order operator IDs, including the final NUL.
`src/provider_api.cpp` contains the descriptor literal:

```sh
printf '%s\0' \
  'kspacejet.provider-bundle' \
  'org.kspacejet.coil-combine' \
  'coil_combine_rss' |
  sha256sum
```

Its result is `ba4f619047391419c8ffc42220ace7d888f047c9e2fd64aaeb78d9093191aed5`.
