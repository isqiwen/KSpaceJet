# kspacejet-cartesian-recon

`ksj-cartesian-recon` is an open reference Provider for the Cartesian
multi-coil image-domain boundary. It is not a clinical reconstruction
algorithm and must not be used to make diagnostic decisions.

It exposes one synchronous, serial Operator:

```text
cartesian_ifft2_coil_images
```

The Operator accepts one complete `ksj.kspace-frame` with canonical
`[channel, ky, kx]` complex-float32 storage and emits one
`ksj.coil-image-frame` with canonical `[ky, kx, channel]` complex-float32
storage. Its config is exactly:

```json
{"channels":8,"cols":128,"rows":128}
```

`channels` is in `[1, 64]`; `rows` and `cols` are in `[2, 512]`. The
configured shape fixes the exact byte count of both payloads. For every input
channel, the Provider copies one contiguous k-space plane into bounded host
scratch, calls the public `ksj::fft::fft2_inplace_with_workspace` inverse transform with inverse normalization, then
explicitly transposes the result into coil-last image storage. It does not
compute magnitude, combine coils, estimate or apply calibration artifacts,
interpret ISMRMRD metadata, or perform image delivery.

The callback retains no input, starts no threads, opens no files or network
connections, and emits no terminal output. Its descriptor bounds one 128 MiB
complex input/output frame and exactly
`(2 * rows * cols + 2 * max(rows, cols)) * sizeof(complex_float32)` bytes of host scratch at the configured
maximum. The FFT temporary matrix and line workspaces are all borrowed from that host-owned scratch; the Provider
does not obtain pooled numerical storage during a firing.
The generated TypeRegistry matchers establish both input and output identity;
this Provider does not copy type digests or layouts into its contract.

## Development bundle identity

Until a signed bundle manifest exists, `bundle_digest` is SHA-256 over
NUL-delimited UTF-8 fields: the bundle domain, Provider ID, and
descriptor-order operator IDs, including the final NUL.
`src/provider_api.cpp` contains the descriptor literal:

```sh
printf '%s\0' \
  'kspacejet.provider-bundle' \
  'org.kspacejet.cartesian-recon' \
  'cartesian_ifft2_coil_images' |
  sha256sum
```

Its result is `b49ba77e1655bdbbc4839ab692eb8fb7d9551bcb7ba138c6274de836d4ab5006`.
