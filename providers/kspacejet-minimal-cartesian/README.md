# kspacejet-minimal-cartesian

`ksj-minimal-cartesian` is a deliberately narrow, image-producing Provider ABI
v1 fixture for the M3.7 bounded data-plane work.  It is not a clinical MRI
reconstruction algorithm and must not be used to make diagnostic decisions.

It exposes one synchronous, serial operator:

```text
cartesian_ifft2_single_coil
```

The operator accepts exactly one completed `ksj.kspace-frame` v1 in each
firing, but only when the frame has one channel, a canonical
`channel × ky × kx` complex-int16 payload, and dimensions that exactly match
its canonical config:

```json
{"cols":128,"rows":128}
```

`rows` and `cols` must be powers of two in `[2, 512]`.  The callback converts
the packed complex-int16 samples to scratch-resident complex float values,
performs an in-place normalized inverse 2D radix-2 FFT, and seals one
row-major float32 magnitude `ksj.image-frame` v1.  It has no coil combine,
calibration, noise prewhitening, trajectory correction, cropping, scaling,
ISMRMRD metadata creation, or public image egress.

The ABI boundary is deliberately exact, rather than a type-id convention. The
input must use the completed-FrameSlot descriptor identity
`sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc`;
the output uses the provider-local `ksj.image-frame/v1` descriptor identity
`sha256:bc161b76c25315236dd5d01fc766635200c1033b7b795bb629d625746f843cbe`.
For both ports the plugin compares type id, revision, ABI descriptor digest,
payload and metadata schema digests, element type, rank, dimension names,
layout/stride flags, memory domain, minimum alignment, and mutability. The
input is exactly `channel_major_contiguous` with canonical strides; the output
is exactly `row_major_contiguous` with canonical strides. A one-channel frame
is established by the configured dimensions and exact payload byte count, not
by silently accepting an extra channel dimension.

The full declared port/rate/resource contract is kept beside the plugin in
[cartesian_ifft2_single_coil-v1.json](contracts/cartesian_ifft2_single_coil-v1.json).
Its fixed upper bounds are one 1 MiB input frame, one 1 MiB output image, and
2 MiB of host scratch per firing.

## Bounded callback behavior

Before it begins the transform, the operator acquires and maps its one output
grant.  It uses only that mapped payload and the host-provided scratch buffer
while `operator_process_batch` is active.  It retains no input, starts no
thread, registers no asynchronous work, opens no file or network connection,
and performs no dynamic allocation during `operator_process_batch`.

On a normal `on_scan_end` it produces zero outputs.  `on_cancel` only releases
no provider work and has no ordinary data-publication capability.  The ABI
plugin does not itself own a `BufferPool`, `DataEdgePlan`, reorder queue, or
public ISMRMRD image adapter: M3.7 host integration must pre-grant the pool
slot and edge/reorder credits, validate the sealed handle, then transfer that
same handle through the plan-bound data path.

The Provider descriptor's bundle/interface/contract digests are frozen
development identities. They are not yet a signed or content-addressed
Provider bundle; production registration must bind this contract file to a
trusted bundle manifest.
